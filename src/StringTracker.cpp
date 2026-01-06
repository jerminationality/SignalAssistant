#include "StringTracker.h"
#include "SessionLogger.h"
#include "StringTrackerParams.h"
#include "util.h"
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <cstdio>
#include <mutex>

namespace {
constexpr float kMinPitchHz = 60.0f;
constexpr float kMaxPitchHz = 6000.0f;
constexpr float kMinOnsetSeparationSec = 0.060f;
constexpr int kPitchConfidenceFrames = 3;
constexpr float kPitchConfidenceMaxCents = 28.0f;
constexpr float kPitchConfidenceHzFloor = 0.8f;
constexpr int kPitchHoldFrames = 4;
constexpr int kPitchHoldReleaseFrames = 10;
constexpr float kEnvRiseAlpha = 0.15f;
constexpr float kEnvFallAlpha = 0.03f;
constexpr float kEnvMin = 1.0e-5f;
constexpr int kReleaseQuietFrameCount = 8;
constexpr float kOpenBiasMinHoldSec = 0.36f;
constexpr float kLowStringRetriggerGuardSec = 0.22f;
constexpr int kAubioDebugString = 0; // set to -1 to disable raw aubio logging
constexpr float kCalibrationBaseTargetRms = 0.0018f;
constexpr float kCalibrationMinTargetRms = 5.0e-5f;
constexpr float kCalibrationMaxTargetRms = 0.02f;
constexpr float kCalibrationGainMin = 0.2f;
constexpr float kCalibrationGainMax = 8.0f;

std::once_flag gLoggedTrackerSettings;

void logTrackerSettingsOnce(const Tuning& tuning, const TrackerConfig& cfg) {
  std::call_once(gLoggedTrackerSettings, [&]() {
    auto& logger = SessionLogger::instance();
    logger.logf("tracker-settings",
                "TrackerConfig onsetThreshold=%.5f minNoteDurSec=%.3f hopSec=%.3f slideDelta=%.1f bendDelta=%.1f",
                cfg.onsetThreshold,
                cfg.minNoteDurSec,
                cfg.hopSec,
                cfg.slideDeltaCents,
                cfg.bendDeltaCents);

    for (int s = 0; s < 6; ++s) {
      const int midi = tuning.stringMidi[static_cast<std::size_t>(s)];
      const float lowHz = std::max(20.f, trackerparams::lowCutMultiplier(s));
      const float highHz = std::min(6000.f, trackerparams::highCutMultiplier(s));
      logger.logf(
          "tracker-settings",
          "string%d midi=%d lowCut=%.2fHz highCut=%.2fHz baseline=%.6f gateRatio=%.5f envFloor=%.6f sustainScale=%.3f retriggerScale=%.3f pitchTol=%.3f onsetScale=%.3f",
          s + 1,
          midi,
          lowHz,
          highHz,
          trackerparams::baselineFloor(s),
          trackerparams::gateRatio(s),
          trackerparams::envelopeFloor(s),
          trackerparams::sustainFloorScale(s),
          trackerparams::retriggerGateScale(s),
          trackerparams::pitchTolerance(s),
          trackerparams::onsetThresholdScale(s, 1.0f));
    }
  });
}

float stringLowCutMultiplier(int s) {
  return trackerparams::lowCutMultiplier(s);
}

float stringHighCutMultiplier(int s) {
  return trackerparams::highCutMultiplier(s);
}

float stringOnsetThreshold(int s, float base) {
  return trackerparams::onsetThresholdScale(s, base);
}

float stringBaselineFloor(int s) {
  return trackerparams::baselineFloor(s);
}

float stringGateRatio(int s) {
  return trackerparams::gateRatio(s);
}

float stringEnvelopeFloor(int s) {
  return trackerparams::envelopeFloor(s);
}

float stringSustainFloorScale(int s) {
  return trackerparams::sustainFloorScale(s);
}

float stringRetriggerGateScale(int s) {
  return trackerparams::retriggerGateScale(s);
}

float stringAubioThresholdScale(int s) {
  return trackerparams::aubioThresholdScale(s);
}

float stringOnsetSilenceDb(int s) {
  return trackerparams::onsetSilenceDb(s);
}

float stringPitchSilenceDb(int s) {
  return trackerparams::pitchSilenceDb(s);
}

inline float energyToVelocity(float rmsVal) {
  return std::clamp(rmsVal * 12.0f, 0.0f, 1.0f);
}

int stringFftMultiple(int s) {
  return trackerparams::fftMultiple(s);
}

float stringPitchTolerance(int s) {
  return trackerparams::pitchTolerance(s);
}

constexpr float kSliderMixEpsilon = 1.0e-7f;

// Keeps automatic floor estimates from overwhelming user-provided slider values.
float sliderDominantMix(float base, float candidate, float maxBoost) {
  const float minBase = std::max(base, kSliderMixEpsilon);
  if (candidate <= minBase || maxBoost <= 1.f)
    return minBase;
  const float ratio = candidate / minBase;
  const float clamped = std::clamp(ratio, 1.f, maxBoost);
  return minBase * clamped;
}
}

void StringTracker::BandpassFilter::reset() {
  hp_x1 = hp_x2 = hp_y1 = hp_y2 = 0.f;
  lp_x1 = lp_x2 = lp_y1 = lp_y2 = 0.f;
}

void StringTracker::BandpassFilter::configure(float sr, float lowCutHz, float highCutHz, int stringIdx) {
  reset();
  if (sr <= 0.f) {
    hp_b0 = lp_b0 = 1.f;
    hp_b1 = hp_b2 = lp_b1 = lp_b2 = 0.f;
    hp_a1 = hp_a2 = lp_a1 = lp_a2 = 0.f;
    return;
  }

  // For low E string (index 0), use aggressive LPF to suppress harmonics
  // For other strings, use standard bandpass
  if (stringIdx == 0) {
    // Aggressive 2nd-order Butterworth LPF at 150Hz for Low E
    // This attenuates 246Hz (3rd harmonic) by ~10dB while preserving 82Hz fundamental better
    const float cutoff = 150.0f;
    const float ff = std::tan(float(M_PI) * cutoff / sr);
    const float root2 = std::sqrt(2.0f);
    const float denom = 1.0f + root2 * ff + ff * ff;
    
    // Lowpass only - no highpass for low E
    hp_b0 = 1.0f; hp_b1 = 0.0f; hp_b2 = 0.0f;
    hp_a1 = 0.0f; hp_a2 = 0.0f;
    
    // Aggressive lowpass coefficients
    lp_b0 = (ff * ff) / denom;
    lp_b1 = 2.0f * lp_b0;
    lp_b2 = lp_b0;
    lp_a1 = 2.0f * (ff * ff - 1.0f) / denom;
    lp_a2 = (1.0f - root2 * ff + ff * ff) / denom;
  } else {
    // Standard bandpass for other strings
    const float low = std::max(1.f, lowCutHz);
    const float high = std::max(low + 10.f, highCutHz);

    // 2nd-order Butterworth highpass at 'low' Hz
    {
      const float w0 = 2.0f * float(M_PI) * low / sr;
      const float cosw0 = std::cos(w0);
      const float sinw0 = std::sin(w0);
      const float alpha = sinw0 * std::sqrt(2.0f) / 2.0f;
      
      const float a0 = 1.0f + alpha;
      hp_b0 = (1.0f + cosw0) / (2.0f * a0);
      hp_b1 = -(1.0f + cosw0) / a0;
      hp_b2 = (1.0f + cosw0) / (2.0f * a0);
      hp_a1 = (-2.0f * cosw0) / a0;
      hp_a2 = (1.0f - alpha) / a0;
    }

    // 2nd-order Butterworth lowpass at 'high' Hz
    {
      const float w0 = 2.0f * float(M_PI) * high / sr;
      const float cosw0 = std::cos(w0);
      const float sinw0 = std::sin(w0);
      const float alpha = sinw0 * std::sqrt(2.0f) / 2.0f;
      
      const float a0 = 1.0f + alpha;
      lp_b0 = (1.0f - cosw0) / (2.0f * a0);
      lp_b1 = (1.0f - cosw0) / a0;
      lp_b2 = (1.0f - cosw0) / (2.0f * a0);
      lp_a1 = (-2.0f * cosw0) / a0;
      lp_a2 = (1.0f - alpha) / a0;
    }
  }

  if (stringIdx == 0) {
    std::fprintf(stderr, "Low E AGGRESSIVE LPF: 150Hz cutoff (sr=%.1f)\n", sr);
    std::fprintf(stderr, "  LP: b0=%.6f b1=%.6f b2=%.6f a1=%.6f a2=%.6f\n",
                 lp_b0, lp_b1, lp_b2, lp_a1, lp_a2);
    SessionLogger::instance().logf("filter",
                                   "Low E AGGRESSIVE LPF: 150Hz, LP[%.6f,%.6f,%.6f,%.6f,%.6f]",
                                   lp_b0, lp_b1, lp_b2, lp_a1, lp_a2);
  }
}

float StringTracker::BandpassFilter::process(float x) {
  // Highpass section (2nd-order biquad, Direct Form I) - bypassed for low E
  float hp_out;
  if (hp_b0 == 1.0f && hp_b1 == 0.0f) {
    // Bypass highpass (low E string)
    hp_out = x;
  } else {
    hp_out = hp_b0 * x + hp_b1 * hp_x1 + hp_b2 * hp_x2 - hp_a1 * hp_y1 - hp_a2 * hp_y2;
    hp_x2 = hp_x1; hp_x1 = x;
    hp_y2 = hp_y1; hp_y1 = hp_out;
  }

  // Lowpass section (2nd-order biquad, cascaded with HP output)
  const float lp_out = lp_b0 * hp_out + lp_b1 * lp_x1 + lp_b2 * lp_x2 
                       - lp_a1 * lp_y1 - lp_a2 * lp_y2;
  lp_x2 = lp_x1;
  lp_x1 = hp_out;
  lp_y2 = lp_y1;
  lp_y1 = lp_out;

  return lp_out;
}

StringTracker::StringTracker(int stringIdx,
                             const Tuning& t,
                             const TrackerConfig& c,
                             std::vector<NoteEvent>& sharedEvents,
                             std::vector<int>& activeIdx)
: _s(stringIdx), _tuning(t), _cfg(c), _events(sharedEvents), _activeIdx(activeIdx)
{
  logTrackerSettingsOnce(_tuning, _cfg);
  _filter.reset();
  _filteredScratch.reserve(2048);
  _calibrationAvgRms = 0.001f;
  _calibrationValid = false;
  refreshCalibrationTarget();
}

StringTracker::~StringTracker() {
#ifdef HAVE_AUBIO
  if (_aubioOnset) { del_aubio_onset(_aubioOnset); _aubioOnset = nullptr; }
  if (_aubioPitch) { del_aubio_pitch(_aubioPitch); _aubioPitch = nullptr; }
  if (_aubioIn) { del_fvec(_aubioIn); _aubioIn = nullptr; }
  if (_aubioOnsetOut) { del_fvec(_aubioOnsetOut); _aubioOnsetOut = nullptr; }
  if (_aubioPitchOut) { del_fvec(_aubioPitchOut); _aubioPitchOut = nullptr; }
#endif
}

void StringTracker::configureProcessing(float sr, int blockSamples) {
  if (sr <= 0.f || blockSamples <= 0)
    return;

  const std::uint64_t storeGen = trackerparams::settingsGeneration();
  const bool paramsChanged = (storeGen != _paramGeneration);
  const int desiredHop = std::max(64, blockSamples);
  if (!paramsChanged && std::fabs(sr - _currentSr) < 1e-3f && desiredHop == _hopSamples)
    return;

  if (paramsChanged)
    refreshCalibrationTarget();

  _paramGeneration = storeGen;
  _currentSr = sr;
  _hopSamples = desiredHop;
  _currentHopSec = static_cast<float>(_hopSamples) / _currentSr;

  _fftSize = 1;
  const int fftTarget = std::max(_hopSamples * stringFftMultiple(_s), _hopSamples * 4);
  while (_fftSize < fftTarget)
    _fftSize <<= 1;

  const float openHz = midiToHz(_tuning.stringMidi[_s]);
  const float lowCut = std::max(20.f, stringLowCutMultiplier(_s));
  const float highestNote = midiToHz(_tuning.stringMidi[_s] + 24);
  const float highCut = std::min(6000.f, stringHighCutMultiplier(_s));
  
  if (_s == 0) {
    SessionLogger::instance().logf("filter-config",
                                   "String %d: lowCutMult=%.2f highCutMult=%.2f -> lowCut=%.2f highCut=%.2f",
                                   _s + 1, stringLowCutMultiplier(_s), stringHighCutMultiplier(_s), lowCut, highCut);
  }
  
  _filter.configure(sr, lowCut, highCut, _s);
  // SessionLogger::instance().logf("tracker",
  //                                "[s%d] configure sr=%.1f hop=%d fft=%d low=%.1f high=%.1f",
  //                                _s + 1,
  //                                sr,
  //                                _hopSamples,
  //                                _fftSize,
  //                                lowCut,
  //                                highCut);
  const float aubioScale = stringAubioThresholdScale(_s);
  // Note: aubio library threshold is separate from onset detection threshold
  const float aubioThresh = std::clamp(0.020f * aubioScale, 0.01f, 0.18f);
  // SessionLogger::instance().logf("tracker",
  //                                "[s%d] params baseline=%.6f gate=%.4f envFloor=%.6f sustain=%.3f retrigger=%.3f pitchTol=%.3f onsetScale=%.3f aubioScale=%.2f aubioThresh=%.3f onsetSilence=%.1f pitchSilence=%.1f",
  //                                _s + 1,
  //                                stringBaselineFloor(_s),
  //                                stringGateRatio(_s),
  //                                stringEnvelopeFloor(_s),
  //                                stringSustainFloorScale(_s),
  //                                stringRetriggerGateScale(_s),
  //                                stringPitchTolerance(_s),
  //                                stringOnsetThreshold(_s, 1.0f),
  //                                aubioScale,
  //                                aubioThresh,
  //                                stringOnsetSilenceDb(_s),
  //                                stringPitchSilenceDb(_s));

  _aubioReady = false;
#ifdef HAVE_AUBIO
  if (_aubioOnset) { del_aubio_onset(_aubioOnset); _aubioOnset = nullptr; }
  if (_aubioPitch) { del_aubio_pitch(_aubioPitch); _aubioPitch = nullptr; }
  if (_aubioIn) { del_fvec(_aubioIn); _aubioIn = nullptr; }
  if (_aubioOnsetOut) { del_fvec(_aubioOnsetOut); _aubioOnsetOut = nullptr; }
  if (_aubioPitchOut) { del_fvec(_aubioPitchOut); _aubioPitchOut = nullptr; }

  _aubioOnset = new_aubio_onset("specflux", static_cast<uint_t>(_fftSize), static_cast<uint_t>(_hopSamples), static_cast<uint_t>(sr));
  const char* pitchAlgo = (_s <= 1) ? "yin" : "yinfast";
  _aubioPitch = new_aubio_pitch(pitchAlgo, static_cast<uint_t>(_fftSize), static_cast<uint_t>(_hopSamples), static_cast<uint_t>(sr));
  _aubioIn = new_fvec(static_cast<uint_t>(_hopSamples));
  _aubioOnsetOut = new_fvec(1);
  _aubioPitchOut = new_fvec(1);

  if (_aubioOnset && _aubioPitch && _aubioIn && _aubioOnsetOut && _aubioPitchOut) {
    aubio_pitch_set_unit(_aubioPitch, "Hz");
    aubio_pitch_set_silence(_aubioPitch, stringPitchSilenceDb(_s));
    aubio_pitch_set_tolerance(_aubioPitch, stringPitchTolerance(_s));
    
    // Set frequency bounds for pitch detector to help YIN focus on fundamentals
    // Low E string: constrain to 70-120 Hz range to avoid locking onto 2nd harmonic
    if (_s == 0) {
      // Low E string (82 Hz open, up to ~104 Hz at fret 12)
      // Constrain search to avoid 2nd harmonic region (164+ Hz)
      const float minFreq = 70.0f;
      const float maxFreq = 120.0f;
      std::fprintf(stderr, "StringTracker[%d]: Setting pitch range %.1f-%.1f Hz\n", _s + 1, minFreq, maxFreq);
      // Note: These functions may not exist in all aubio versions
      // aubio_pitch_set_min_freq(_aubioPitch, minFreq);
      // aubio_pitch_set_max_freq(_aubioPitch, maxFreq);
    }

    aubio_onset_set_silence(_aubioOnset, stringOnsetSilenceDb(_s));
    aubio_onset_set_threshold(_aubioOnset, aubioThresh);

    _aubioReady = true;
        std::fprintf(stderr,
           "StringTracker[%d]: Aubio initialised (hop=%d, sr=%.1f, aubioScale=%.2f, base=%.3f, onsetThresh=%.3f)\n",
             _s + 1,
             _hopSamples,
           sr,
           aubioScale,
           _cfg.onsetThreshold,
           aubioThresh);
  } else {
    std::fprintf(stderr, "StringTracker[%d]: Aubio init failed (onset=%p pitch=%p in=%p out=%p pitchOut=%p)\n",
                 _s + 1, (void*)_aubioOnset, (void*)_aubioPitch, (void*)_aubioIn, (void*)_aubioOnsetOut, (void*)_aubioPitchOut);
  }
#else
  if (!_warnedNoAubio) {
    std::fprintf(stderr, "StringTracker[%d]: Aubio support not available; live detection disabled.\n", _s + 1);
    _warnedNoAubio = true;
  }
#endif
}

void StringTracker::updateFeatures(const float* samples, int n, float sr, float t0) {
  if (_hopSamples <= 0 || !_aubioReady) {
    return;
  }

  if (n <= 0) {
    FrameFeatures f{};
    f.tSec = t0;
    _feat.push_back(f);
  } else {
    const int hop = _hopSamples;

    if (n > 0) {
      // Debug: log raw input samples before any processing
      static int rawLogCount = 0;
      if (_s == 0 && rawLogCount < 3 && samples && n >= 10) {
        int zc = 0;
        for (int i = 1; i < std::min(128, n); ++i) {
          if (samples[i-1] * samples[i] < 0) zc++;
        }
        float zcFreq = (zc / 2.0f) * (sr / std::min(128, n));
        SessionLogger::instance().logf("raw-input",
                                       "LOW E RAW INPUT: n=%d zc=%d zcFreq=%.1fHz samples=[%.5f,%.5f,%.5f,%.5f,%.5f]",
                                       n, zc, zcFreq, samples[0], samples[1], samples[2], samples[3], samples[4]);
        rawLogCount++;
      }
      
      _filteredScratch.resize(static_cast<std::size_t>(n));
      for (int i = 0; i < n; ++i) {
        const float in = samples ? samples[i] * _calibrationGain : 0.f;
        _filteredScratch[static_cast<std::size_t>(i)] = _filter.process(in);
      }
    }

    int offset = 0;
    while (offset < n) {
      const int frameLen = std::min(hop, n - offset);
      if (frameLen <= 0)
        break;

      FrameFeatures f{};
      f.tSec = t0 + (static_cast<float>(offset) + 0.5f * frameLen) / sr;
      const float* framePtr = (_filteredScratch.empty() || offset >= n)
              ? nullptr
              : _filteredScratch.data() + offset;
      const float* rawPtr = (samples && offset < n)
              ? samples + offset
              : nullptr;
      f.envelopeRms = rms(framePtr, frameLen);

      float framePeak = 0.f;
      float rawPeak = 0.f;
      if (framePtr) {
        for (int i = 0; i < frameLen; ++i)
          framePeak = std::max(framePeak, std::fabs(framePtr[i]));
      }
      if (rawPtr) {
        for (int i = 0; i < frameLen; ++i)
          rawPeak = std::max(rawPeak, std::fabs(rawPtr[i] * _calibrationGain));
      }
      const bool useFilteredForPitch = true;  // Use filtered signal for pitch detection
      const float onsetGain = framePeak > 1e-5f ? std::min(1.0f, 0.35f / framePeak) : 1.f;
      const float pitchPeak = useFilteredForPitch ? framePeak : rawPeak;
      const float pitchGain = pitchPeak > 1e-5f ? std::min(1.0f, 0.45f / pitchPeak) : 1.f;

      float onsetMarker = 0.f;
      float detectedPitchHz = -1.f;
#ifdef HAVE_AUBIO
      if (_aubioIn) {
        for (int i = 0; i < hop; ++i) {
          float directSample = 0.f;
          if (rawPtr && i < frameLen) {
            directSample = rawPtr[i] * _calibrationGain;
          } else if (framePtr && i < frameLen) {
            directSample = framePtr[i];
          }
          _aubioIn->data[i] = directSample * onsetGain;
        }
        if (_aubioOnset && _aubioOnsetOut) {
          aubio_onset_do(_aubioOnset, _aubioIn, _aubioOnsetOut);
          onsetMarker = fvec_get_sample(_aubioOnsetOut, 0);
        }
        if (_aubioPitch && _aubioPitchOut) {
          for (int i = 0; i < hop; ++i) {
            float pitchSample = 0.f;
            if (useFilteredForPitch && framePtr && i < frameLen)
              pitchSample = framePtr[i];
            else if (rawPtr && i < frameLen)
              pitchSample = rawPtr[i] * _calibrationGain;
            _aubioIn->data[i] = pitchSample * pitchGain;
          }
          
          // Verify frequency content in aubio input buffer
          static int logCount = 0;
          if (_s == 0 && logCount < 5 && frameLen == hop) {
            float maxVal = 0, rms = 0;
            for (int i = 0; i < hop; ++i) {
              maxVal = std::max(maxVal, std::fabs(_aubioIn->data[i]));
              rms += _aubioIn->data[i] * _aubioIn->data[i];
            }
            rms = std::sqrt(rms / hop);
            
            // Simple FFT to check frequency content
            std::vector<float> fft_in(hop);
            for (int i = 0; i < hop; ++i) fft_in[i] = _aubioIn->data[i];
            
            // Count zero crossings as rough frequency indicator
            int zeroCrossings = 0;
            for (int i = 1; i < hop; ++i) {
              if (fft_in[i-1] * fft_in[i] < 0) zeroCrossings++;
            }
            // Estimated frequency from zero crossings: f = (crossings/2) * (sr/hop)
            float zcFreq = (zeroCrossings / 2.0f) * (sr / hop);
            
            SessionLogger::instance().logf("aubio-input",
                                           "LOW E aubio buffer: hop=%d peak=%.5f rms=%.5f zc=%d zcFreq=%.1fHz filtered=%d samples=[%.5f,%.5f,%.5f,%.5f,%.5f]",
                                           hop, maxVal, rms, zeroCrossings, zcFreq, useFilteredForPitch ? 1 : 0,
                                           _aubioIn->data[0], _aubioIn->data[1], _aubioIn->data[2], _aubioIn->data[3], _aubioIn->data[4]);
            logCount++;
          }
          
          aubio_pitch_do(_aubioPitch, _aubioIn, _aubioPitchOut);
          const float pitchHz = fvec_get_sample(_aubioPitchOut, 0);
          
          // Debug logging for low E string pitch detection (MUTED)
          // if (_s == 0 && pitchHz > 0.f && pitchHz >= kMinPitchHz) {
          //   SessionLogger::instance().logf("detection",
          //                                  "LOW E RAW PITCH: detected=%.2fHz filtered=%d peak=%.5f",
          //                                  pitchHz, useFilteredForPitch ? 1 : 0, pitchPeak);
          // }
          
          if (pitchHz > 0.f && pitchHz >= kMinPitchHz && pitchHz <= kMaxPitchHz)
            detectedPitchHz = pitchHz;
        }
      }
#endif

      if (detectedPitchHz > 0.f) {
        const float smoothedPitch = applyPitchMedian(detectedPitchHz);
        f.pitchHz = smoothedPitch;
        const float refHz = midiToHz(_tuning.stringMidi[_s]);
        f.pitchCents = centsBetween(f.pitchHz, refHz);
      } else {
        _pitchMedianWindow.clear();
      }

      if (f.pitchHz > 0.f)
        _lastFeaturePitchHz = f.pitchHz;

      f.onsetStrength = onsetMarker;

      // if (onsetMarker > 0.f && _s == kAubioDebugString) {
      //   auto& logger = SessionLogger::instance();
      //   if (logger.enabled()) {
      //     logger.logf("tracker",
      //                "[s%d] aubio-raw t=%.4f onset=%.6f env=%.6f peak=%.6f gain=%.3f",
      //                _s + 1,
      //                f.tSec,
      //                onsetMarker,
      //                f.envelopeRms,
      //                framePeak,
      //                onsetGain);
      //   }
      // }

      if (f.envelopeRms <= 0.f && f.onsetStrength <= 0.f && detectedPitchHz <= 0.f) {
        f.onsetStrength = 0.f;
      }

      _feat.push_back(f);
      offset += hop;
    }
  }

  while (!_feat.empty() && (_feat.back().tSec - _feat.front().tSec) > 0.8f)
    _feat.pop_front();
}

bool StringTracker::detectOnset(std::size_t frameIdx) {
  if (frameIdx >= _feat.size())
    return false;

  const auto& frame = _feat[frameIdx];
  const float onsetStrength = frame.onsetStrength;
  const float envelope = frame.envelopeRms;

  // Direct onset threshold - no scaling multiplication
  const float onsetThreshold = stringOnsetThreshold(_s, 1.0f);
  const float baseFloor = stringBaselineFloor(_s);
  const float gateRatio = stringGateRatio(_s);
  const float envelopeFloorParam = stringEnvelopeFloor(_s);
  
  // Static baseline - hard noise floor, discard everything below this
  const float baseline = std::max(baseFloor, kSliderMixEpsilon);
  
  // Static envFloor - musical threshold, must be >= baseline
  const float envFloor = std::max(envelopeFloorParam, baseline);
  
  // Both gate and sustain based on envFloor (musical threshold)
  const float gateThreshold = envFloor * gateRatio;
  
  // sustainFloor can dip below envFloor (scale < 1.0) but never below baseline
  const float sustainFloorScale = stringSustainFloorScale(_s);
  const float sustainFloor = std::max(baseline, envFloor * sustainFloorScale);
  
  // Calculate retriggerGate for UI display (also computed in noteShouldClose)
  const float retriggerGateScale = stringRetriggerGateScale(_s);
  const float cappedPeak = std::min(_lastOnsetPeakRms, gateThreshold * 2.5f);
  const float retriggerGate = std::max(sustainFloor, cappedPeak * 0.4f) * retriggerGateScale;
  
  // Store for UI display
  _lastThresholds.onsetThreshold = onsetThreshold;  // Store computed threshold used in comparison
  _lastThresholds.baseline = baseline;
  _lastThresholds.gateThreshold = gateThreshold;
  _lastThresholds.envFloor = envFloor;
  _lastThresholds.sustainFloor = sustainFloor;
  _lastThresholds.retriggerGate = retriggerGate;
  
  const float separationGuard = std::max(_currentHopSec, kMinOnsetSeparationSec);
  const float timeSinceLastOnset = (_lastOnsetSec >= 0.f) ? frame.tSec - _lastOnsetSec : -1.f;
  const float guardRemaining = (_lastOnsetSec >= 0.f) ? std::max(0.f, separationGuard - timeSinceLastOnset) : 0.f;
  float activeAge = -1.f;
  if (_activeIdx[_s] >= 0 && _activeIdx[_s] < static_cast<int>(_events.size()))
    activeAge = frame.tSec - _events[_activeIdx[_s]].startSec;
  const float retriggerBlockRemaining = (_retriggerBlockUntilSec > frame.tSec)
      ? (_retriggerBlockUntilSec - frame.tSec)
      : 0.f;
  const float sliderRetriggerScale = stringRetriggerGateScale(_s);
  const float onsetDelta = onsetStrength - onsetThreshold;
  const float envDelta = envelope - gateThreshold;

  auto& sessionLogger = SessionLogger::instance();
  const bool logString = sessionLogger.enabled() && (_s == kAubioDebugString);
  const bool shouldLog = logString && (onsetStrength > onsetThreshold * 0.35f || envelope > gateThreshold * 0.7f);
  auto logDecision = [&](const char* tag) {
    (void)tag; // unused
    // if (!shouldLog)
    //   return;
    // sessionLogger.logf("tracker",
    //            "[s%d] onset-%s t=%.4f env=%.6f gate=%.6f envDelta=%.6f envFloor=%.6f onset=%.6f thresh=%.6f onsetDelta=%.6f baseline=%.6f lastPeak=%.6f baseParam=%.6f gateRatio=%.4f envParam=%.6f retriggerScale=%.3f guard=%.3f guardRemain=%.3f activeAge=%.3f retrigRemain=%.3f pitchHz=%.2f pitchCents=%.1f",
    //                    _s + 1,
    //                    tag,
    //                    frame.tSec,
    //                    envelope,
    //                    gateThreshold,
    //                    envDelta,
    //                    envFloor,
    //                    onsetStrength,
    //                    onsetThreshold,
    //                    onsetDelta,
    //                    baseline,
    //                    _lastOnsetPeakRms,
    //                    baseFloor,
    //                    gateRatio,
    //                    envelopeFloorParam,
    //                    sliderRetriggerScale,
    //                    separationGuard,
    //                    guardRemaining,
    //                    activeAge,
    //                    retriggerBlockRemaining,
    //                    frame.pitchHz,
    //                    frame.pitchCents);
  };
  if (onsetStrength <= 0.f)
    return false;

  // Check if a note is already active first - if so, exit early without logging PASS messages
  const bool noActiveNote = (_activeIdx[_s] < 0 || _activeIdx[_s] >= static_cast<int>(_events.size()));
  
  if (!noActiveNote) {
    // When a note is active, reject onset detection - note closure logic in noteShouldClose() will handle replucks
    logDecision("active-note");
    return false;
  }

  if (onsetStrength < onsetThreshold) {
    logDecision("below-threshold");
    // if (noActiveNote) {
    //   SessionLogger::instance().logf("detection", "       S%d Rejected Onset (onset=%.2f < threshold=%.2f)", 
    //                                  _s + 1, onsetStrength, onsetThreshold);
    // }
    return false;
  }

  if (_onsetLatched) {
    logDecision("latched");
    // if (noActiveNote) {
    //   SessionLogger::instance().logf("detection", "       S%d Rejected Onset (onset latched)", _s + 1);
    // }
    return false;
  }

  if (envelope < gateThreshold) {
    logDecision("below-gate");
    // if (noActiveNote) {
    //   const int estMidi = estimateMidi(frame);
    //   const int estFret = (estMidi >= 0) ? midiToFret(estMidi, _tuning.stringMidi[_s]) : -1;
    //   SessionLogger::instance().logf("detection", "     S%dF%d Rejected Note (onset=%.2f > thresh=%.2f; env=%.4f < gate=%.4f)", 
    //                                  _s + 1, estFret, onsetStrength, onsetThreshold, envelope, gateThreshold);
    // }
    return false;
  }

  if (envelope < envFloor) {
    logDecision("below-env-floor");
    // if (noActiveNote) {
    //   const int estMidi = estimateMidi(frame);
    //   const int estFret = (estMidi >= 0) ? midiToFret(estMidi, _tuning.stringMidi[_s]) : -1;
    //   SessionLogger::instance().logf("detection", "     S%dF%d Rejected Note (onset=%.2f > thresh=%.2f; env=%.4f < floor=%.4f)", 
    //                                  _s + 1, estFret, onsetStrength, onsetThreshold, envelope, envFloor);
    // }
    return false;
  }

  if (_lastOnsetSec >= 0.f && (frame.tSec - _lastOnsetSec) < separationGuard) {
    logDecision("separation-guard");
    // if (noActiveNote) {
    //   SessionLogger::instance().logf("detection", "     S%d Rejected Onset (separation: %.3fs < guard=%.3fs)", 
    //                                  _s + 1, frame.tSec - _lastOnsetSec, separationGuard);
    // }
    return false;
  }

  _onsetLatched = true;
  logDecision("accepted");
  // SessionLogger::instance().logf("tracker",
  //                                "[s%d] onset t=%.3f env=%.5f gate=%.5f envDelta=%.5f envFloor=%.5f onset=%.3f thresh=%.3f onsetDelta=%.5f baseline=%.5f lastPeak=%.5f guard=%.3f activeAge=%.3f pitch=%.2fHz pitchCents=%.1f",
  //                                _s + 1,
  //                                frame.tSec,
  //                                frame.envelopeRms,
  //                                gateThreshold,
  //                                envDelta,
  //                                envFloor,
  //                                frame.onsetStrength,
  //                                onsetThreshold,
  //                                onsetDelta,
  //                                baseline,
  //                                _lastOnsetPeakRms,
  //                                separationGuard,
  //                                activeAge,
  //                                frame.pitchHz,
  //                                frame.pitchCents);
  return true;
}

int StringTracker::estimateMidi(const FrameFeatures& frame) const {
  if (frame.pitchHz <= 0.f)
    return -1;
  const int openMidi = _tuning.stringMidi[_s];
  int midi = hzToMidi(frame.pitchHz);
  midi = std::clamp(midi, openMidi, openMidi + 24);
  return midi;
}

int StringTracker::applyLowStringBias(int midi, const FrameFeatures& frame) const {
  if (_s > 0 || midi < 0 || frame.pitchHz <= 0.f)
    return midi;

  const int openMidi = _tuning.stringMidi[_s];
  if (midi <= openMidi)
    return midi;

  const float openHz = midiToHz(openMidi);
  if (openHz <= 0.f)
    return midi;

  const float ratio = frame.pitchHz / openHz;
  if (!std::isfinite(ratio) || ratio < 1.7f)
    return midi;

  const int harmonic = static_cast<int>(std::round(ratio));
  if (harmonic < 2 || harmonic > 4)
    return midi;

  const float harmonicError = std::fabs(ratio - static_cast<float>(harmonic));
  const float tolerance = 0.12f * static_cast<float>(harmonic);
  if (harmonicError > tolerance)
    return midi;

  // Check if the detected MIDI already matches what we'd expect from the pitch
  // If midi is correct (e.g., F12 with ratio ~2.0 gives MIDI 52), don't "correct" it
  const int expectedMidi = hzToMidi(frame.pitchHz);
  if (std::abs(midi - expectedMidi) <= 1) {
    // MIDI matches the pitch Hz, so this is likely a correct detection, not a harmonic error
    return midi;
  }

  const float minEnv = std::max(stringEnvelopeFloor(_s) * 0.65f, _calibrationTargetRms * 0.55f);
  const float minOnset = stringOnsetThreshold(_s, 1.0f) * 1.6f;
  if (frame.envelopeRms < minEnv || frame.onsetStrength < minOnset)
    return midi;

  const float fundamentalHz = frame.pitchHz / static_cast<float>(harmonic);
  const int candidateMidi = std::clamp(hzToMidi(fundamentalHz), openMidi, openMidi + 24);
  if (candidateMidi == openMidi && candidateMidi < midi) {
    // SessionLogger::instance().logf("tracker",
    //                                "[s%d] harmonic-bias t=%.3f pitch=%.2fHz ratio=%.2f harmonic=%d midi=%d->%d",
    //                                _s + 1,
    //                                frame.tSec,
    //                                frame.pitchHz,
    //                                ratio,
    //                                harmonic,
    //                                midi,
    //                                candidateMidi);
    return candidateMidi;
  }

  return midi;
}

bool StringTracker::noteShouldClose(std::size_t frameIdx) const {
  if (_activeIdx[_s] < 0 || _activeIdx[_s] >= static_cast<int>(_events.size()))
    return false;
  if (frameIdx >= _feat.size())
    return false;

  const auto& frame = _feat[frameIdx];
  const auto& ev = _events[_activeIdx[_s]];
  const float age = frame.tSec - ev.startSec;
  if (age < _cfg.minNoteDurSec)
    return false;

  if (_activeHoldUntilSec > 0.f && frame.tSec < _activeHoldUntilSec)
    return false;

  // if (_s == 0 && _retriggerBlockUntilSec > 0.f && frame.tSec < _retriggerBlockUntilSec)
  //   return false;

  float avgEnv = 0.f;
  int count = 0;
  for (int k = 0; k < 5; ++k) {
    if (frameIdx < static_cast<std::size_t>(k))
      break;
    avgEnv += _feat[frameIdx - k].envelopeRms;
    ++count;
  }
  if (count == 0)
    return false;
  avgEnv /= static_cast<float>(count);

  // Use same formulas as detectOnset
  const float baseFloor = stringBaselineFloor(_s);
  const float baseline = std::max(baseFloor, kSliderMixEpsilon);
  const float envelopeFloorParam = stringEnvelopeFloor(_s);
  const float envFloor = std::max(envelopeFloorParam, baseline);
  const float sustainScale = std::max(0.05f, stringSustainFloorScale(_s));
  const float sustainFloor = std::max(baseline, envFloor * sustainScale);
  
  // Store for UI display
  _lastThresholds.sustainFloor = sustainFloor;

  // Check for sharp drop-off (immediate muting/damping)
  // Compare current envelope to the recent peak (last onset)
  if (_lastOnsetPeakRms > sustainFloor * 3.0f && avgEnv < _lastOnsetPeakRms * 0.15f && avgEnv < sustainFloor) {
    // Sharp drop detected: envelope fell below 15% of peak and below sustain floor
    // SessionLogger::instance().logf("tracker",
    //                                "[s%d] release-sharp-drop t=%.3f avgEnv=%.5f peak=%.5f ratio=%.2f floor=%.5f",
    //                                _s + 1,
    //                                frame.tSec,
    //                                avgEnv,
    //                                _lastOnsetPeakRms,
    //                                avgEnv / _lastOnsetPeakRms,
    //                                sustainFloor);
    if (_activeIdx[_s] >= 0 && _activeIdx[_s] < static_cast<int>(_events.size())) {
      const auto& active = _events[_activeIdx[_s]];
      SessionLogger::instance().logf("detection",
                                     "     S%dF%d OFF (sharp-drop: %.4f -> %.4f | sustainFloor=%.4f)",
                                     _s + 1,
                                     active.fret,
                                     _lastOnsetPeakRms,
                                     avgEnv,
                                     sustainFloor);
    }
    return true;
  }

  const bool quiet = avgEnv < sustainFloor;
  if (quiet) {
    _releaseQuietFrames = std::min(_releaseQuietFrames + 1, kReleaseQuietFrameCount);
    // SessionLogger::instance().logf("detection", "[s%d] sustain check: avgEnv=%.4f < sustainFloor=%.4f (quiet frame %d/%d)", 
    //                                _s + 1, avgEnv, sustainFloor, _releaseQuietFrames, kReleaseQuietFrameCount);
  } else {
    if (_releaseQuietFrames > 0) {
      // SessionLogger::instance().logf("detection", "[s%d] sustain check: avgEnv=%.4f >= sustainFloor=%.4f (reset quiet count from %d)", 
      //                                _s + 1, avgEnv, sustainFloor, _releaseQuietFrames);
    }
    _releaseQuietFrames = 0;
  }

  if (_releaseQuietFrames >= kReleaseQuietFrameCount) {
    // SessionLogger::instance().logf("detection", "[s%d] NOTE-OFF: sustained quiet for %d frames", _s + 1, _releaseQuietFrames);
    // SessionLogger::instance().logf("tracker",
    //                                "[s%d] release-quiet t=%.3f avgEnv=%.5f floor=%.5f quietFrames=%d",
    //                                _s + 1,
    //                                frame.tSec,
    //                                avgEnv,
    //                                sustainFloor,
    //                                _releaseQuietFrames);
    // Get active note fret for OFF message
    if (_activeIdx[_s] >= 0 && _activeIdx[_s] < static_cast<int>(_events.size())) {
      const auto& active = _events[_activeIdx[_s]];
      SessionLogger::instance().logf("detection",
                                     "     S%dF%d OFF (sustained-quiet: avgEnv=%.4f < floor=%.4f)",
                                     _s + 1,
                                     active.fret,
                                     avgEnv,
                                     sustainFloor);
    }
    return true;
  }

  const float cappedPeak = sliderDominantMix(sustainFloor, _lastOnsetPeakRms, 6.0f);
  float retriggerGate = std::max(sustainFloor, cappedPeak * 0.4f);
  retriggerGate = std::max(envFloor * 0.3f, retriggerGate * stringRetriggerGateScale(_s));
  retriggerGate = std::min(retriggerGate, sustainFloor * 6.0f);
  
  // Store for UI display
  _lastThresholds.retriggerGate = retriggerGate;
  bool allowRetriggerRelease = true;
  // if (_s == 0 && _activeForcedOpen) {
  //   const bool holdExpired = !(_activeHoldUntilSec > 0.f && frame.tSec < _activeHoldUntilSec);
  //   const float peakRef = std::max(_lastOnsetPeakRms, 1.0e-6f);
  //   const float envRatio = peakRef > 0.f ? avgEnv / peakRef : 0.f;
  //   if (!holdExpired || envRatio > 0.55f) {
  //     allowRetriggerRelease = false;
  //   } else {
  //     retriggerGate *= 1.8f;
  //   }
  // }
  if (allowRetriggerRelease && frame.onsetStrength > retriggerGate && age >= _cfg.minNoteDurSec * 0.75f) {
    // Temporal RMS comparison: compare recent RMS (last 5 hops) vs earlier RMS (hops 5-10 back)
    // A real repluck shows rising energy; a fading note shows flat or decreasing energy
    float recentRmsSum = 0.0f, earlierRmsSum = 0.0f;
    int recentCount = 0, earlierCount = 0;
    
    for (int i = 0; i < 10 && i < static_cast<int>(_feat.size()); ++i) {
      const float rms = _feat[i].envelopeRms;
      if (i < 5) {
        recentRmsSum += rms;
        recentCount++;
      } else {
        earlierRmsSum += rms;
        earlierCount++;
      }
    }
    
    const float recentAvg = (recentCount > 0) ? (recentRmsSum / recentCount) : 0.0f;
    const float earlierAvg = (earlierCount > 0) ? (earlierRmsSum / earlierCount) : 0.0f;
    const float rmsRatio = (earlierAvg > 1e-6f) ? (recentAvg / earlierAvg) : 0.0f;
    const float rmsAbsFloor = std::max(sustainFloor * 1.2f, envFloor * 0.5f);
    const bool rmsRising = (rmsRatio >= 1.4f) && (recentAvg > rmsAbsFloor);
    
    if (rmsRising) {
      // SessionLogger::instance().logf("tracker",
      //                                "[s%d] retrigger-accepted: onset=%.3f gate=%.3f ratio=%.2f (%.4f/%.4f) absFloor=%.4f age=%.3f",
      //                                _s + 1,
      //                                frame.onsetStrength,
      //                                retriggerGate,
      //                                rmsRatio,
      //                                recentAvg,
      //                                earlierAvg,
      //                                rmsAbsFloor,
      //                                age);
      // Get active note fret for OFF message
      if (_activeIdx[_s] >= 0 && _activeIdx[_s] < static_cast<int>(_events.size())) {
        const auto& active = _events[_activeIdx[_s]];
        SessionLogger::instance().logf("detection",
                                       "     S%dF%d OFF (retrigger: onset=%.3f > gate=%.3f)",
                                       _s + 1,
                                       active.fret,
                                       frame.onsetStrength,
                                       retriggerGate);
      }
      return true;
    }
  }

  return false;
}

float StringTracker::applyPitchMedian(float pitchHz) {
  if (pitchHz <= 0.f)
    return pitchHz;

  constexpr std::size_t kWindow = 5;
  _pitchMedianWindow.push_back(pitchHz);
  if (_pitchMedianWindow.size() > kWindow)
    _pitchMedianWindow.pop_front();

  if (_pitchMedianWindow.size() < 3)
    return pitchHz;

  std::array<float, kWindow> scratch{};
  const std::size_t count = _pitchMedianWindow.size();
  std::copy_n(_pitchMedianWindow.begin(), count, scratch.begin());
  const auto endIt = scratch.begin() + static_cast<std::ptrdiff_t>(count);
  std::sort(scratch.begin(), endIt);
  return scratch[count / 2];
}

bool StringTracker::updatePitchConfidence(int midi, float pitchHz) {
  if (midi < 0 || pitchHz <= 0.f) {
    _pitchConfidenceFrames = 0;
    _pitchConfidenceMidi = -1;
    _pitchConfidenceHz = -1.f;
    return false;
  }

  if (_pitchConfidenceMidi < 0) {
    _pitchConfidenceMidi = midi;
    _pitchConfidenceHz = pitchHz;
    _pitchConfidenceFrames = 1;
    return _pitchConfidenceFrames >= kPitchConfidenceFrames;
  }

  const float referenceHz = (_pitchConfidenceHz > 0.f) ? _pitchConfidenceHz : midiToHz(_pitchConfidenceMidi);
  const float centsDiff = std::fabs(centsBetween(pitchHz, referenceHz));

  if (midi == _pitchConfidenceMidi && centsDiff <= kPitchConfidenceMaxCents) {
    _pitchConfidenceFrames = std::min(_pitchConfidenceFrames + 1, 8);
    _pitchConfidenceHz = 0.8f * referenceHz + 0.2f * pitchHz;
  } else if (centsDiff <= kPitchConfidenceMaxCents * 0.6f) {
    // Allow nearby midi (e.g. slide settling) but reset frame count
    _pitchConfidenceMidi = midi;
    _pitchConfidenceHz = pitchHz;
    _pitchConfidenceFrames = 1;
  } else {
    _pitchConfidenceMidi = midi;
    _pitchConfidenceHz = pitchHz;
    _pitchConfidenceFrames = 1;
  }

  return _pitchConfidenceFrames >= kPitchConfidenceFrames;
}

int StringTracker::applyPitchHold(int midi, bool stable) {
  if (!stable || midi < 0) {
    _pitchHoldPendingMidi = -1;
    _pitchHoldPendingFrames = 0;
    _pitchHoldSilenceFrames = std::min(_pitchHoldSilenceFrames + 1, kPitchHoldReleaseFrames);
    if (_pitchHoldSilenceFrames >= kPitchHoldReleaseFrames) {
      _pitchHoldMidi = -1;
    }
    return _pitchHoldMidi;
  }

  _pitchHoldSilenceFrames = 0;

  if (_pitchHoldMidi < 0) {
    _pitchHoldMidi = midi;
    _pitchHoldPendingMidi = -1;
    _pitchHoldPendingFrames = 0;
    return _pitchHoldMidi;
  }

  if (midi == _pitchHoldMidi) {
    _pitchHoldPendingMidi = -1;
    _pitchHoldPendingFrames = 0;
    return _pitchHoldMidi;
  }

  if (_pitchHoldPendingMidi != midi) {
    _pitchHoldPendingMidi = midi;
    _pitchHoldPendingFrames = 1;
    return _pitchHoldMidi;
  }

  _pitchHoldPendingFrames = std::min(_pitchHoldPendingFrames + 1, kPitchHoldFrames);
  if (_pitchHoldPendingFrames >= kPitchHoldFrames) {
    _pitchHoldMidi = _pitchHoldPendingMidi;
    _pitchHoldPendingMidi = -1;
    _pitchHoldPendingFrames = 0;
  }

  return _pitchHoldMidi;
}

void StringTracker::refreshCalibrationTarget() {
  // sliderScale is no longer used - calibration applied in HexJackClient
  _calibrationTargetRms = std::clamp(kCalibrationBaseTargetRms,
                                     kCalibrationMinTargetRms,
                                     kCalibrationMaxTargetRms);

  // Calibration gain is no longer calculated here - it's applied in HexJackClient
  // This method is kept for logging purposes only
  _calibrationGain = 1.0f;
}

float StringTracker::lastPitchHz() const {
  return _lastFeaturePitchHz;
}

void StringTracker::processBlock(const float* samples, int n, float sr, float t0) {
  if (sr <= 0.f)
    return;

  configureProcessing(sr, n);

#ifndef HAVE_AUBIO
  (void)samples;
  (void)n;
  (void)sr;
  (void)t0;
  return;
#else
  if (!_aubioReady)
    return;

  if (!samples || n <= 0)
    return;

  const float channelPeak = [] (const float* data, int count) {
    float peak = 0.f;
    if (!data || count <= 0)
      return peak;
    for (int i = 0; i < count; ++i)
      peak = std::max(peak, std::fabs(data[i]));
    return peak;
  }(samples, n);

  if (channelPeak < 1e-6f)
    return;

  const float prevTailSec = _feat.empty() ? std::numeric_limits<float>::lowest() : _feat.back().tSec;
  const std::size_t prevFrames = _feat.size();
  updateFeatures(samples, n, sr, t0);
  if (_feat.empty())
    return;

  std::size_t startIdx = 0;
  if (prevFrames > 0 && prevTailSec > std::numeric_limits<float>::lowest()) {
    while (startIdx < _feat.size() && _feat[startIdx].tSec <= prevTailSec)
      ++startIdx;
  }

  for (std::size_t idx = startIdx; idx < _feat.size(); ++idx) {
    auto& frame = _feat[idx];

    const float env = std::max(frame.envelopeRms, 0.f);
    
    // Only update adaptive floor when no note is active to prevent tracking the note signal
    if (_activeIdx[_s] < 0) {
      const float alpha = (env > _envAdaptiveRms) ? kEnvRiseAlpha : kEnvFallAlpha;
      _envAdaptiveRms = (1.f - alpha) * _envAdaptiveRms + alpha * env;
      if (_envAdaptiveRms < kEnvMin)
        _envAdaptiveRms = kEnvMin;
    }

    _lastOnsetPeakRms *= 0.995f;

    const float latchRelease = stringOnsetThreshold(_s, 1.0f) * 0.6f;
    if (frame.onsetStrength < latchRelease)
      _onsetLatched = false;

    int midiCandidate = (frame.pitchHz > 0.f) ? estimateMidi(frame) : -1;
    
    // Apply harmonic bias correction BEFORE pitch stability check for low E string
    if (_s == 0 && midiCandidate > 0) {
      const int correctedMidi = applyLowStringBias(midiCandidate, frame);
      if (correctedMidi != midiCandidate && correctedMidi >= 0) {
        midiCandidate = correctedMidi;
      }
    }
    
    const bool pitchStable = updatePitchConfidence(midiCandidate, frame.pitchHz);
    const int heldMidi = applyPitchHold(midiCandidate, pitchStable);

    if (_activeIdx[_s] >= 0 && _activeIdx[_s] < static_cast<int>(_events.size())) {
      auto& active = _events[_activeIdx[_s]];
      active.endSec = frame.tSec;
      active.velocity = std::max(active.velocity, energyToVelocity(frame.envelopeRms));
    }

    if (detectOnset(idx)) {
      if (_activeIdx[_s] >= 0 && _activeIdx[_s] < static_cast<int>(_events.size())) {
        auto& active = _events[_activeIdx[_s]];
        active.endSec = std::max(frame.tSec, active.startSec + _cfg.minNoteDurSec);
        // SessionLogger::instance().logf("tracker",
        //                                "[s%d] note-ended (new onset - should not happen!) t=%.3f fret=%d dur=%.3f",
        //                                _s + 1,
        //                                active.endSec,
        //                                active.fret,
        //                                active.endSec - active.startSec);
        SessionLogger::instance().logf("detection",
                                       "     S%dF%d OFF (ERROR: new-onset detected despite active-note rejection)",
                                       _s + 1,
                                       active.fret);
        _activeIdx[_s] = -1;
        _releaseQuietFrames = 0;
        _activeHoldUntilSec = 0.f;
        _retriggerBlockUntilSec = 0.f;
        _activeForcedOpen = false;
      }

      if (_s == 0) {
        const int testFret = (heldMidi >= 0) ? midiToFret(heldMidi, _tuning.stringMidi[_s]) : -1;
        if (testFret >= 0 && testFret <= 3) {
          SessionLogger::instance().logf("detection",
                                         "LOW E ONSET: fret=%d pitch=%.2fHz midi=%d stable=%d env=%.4f onset=%.3f",
                                         testFret, frame.pitchHz, heldMidi, pitchStable ? 1 : 0, 
                                         frame.envelopeRms, frame.onsetStrength);
        }
      }

      if (frame.pitchHz <= 0.f || heldMidi < 0) {
        _onsetLatched = false;
        if (_s == 0) {
          SessionLogger::instance().logf("detection",
                                         "LOW E REJECT: pitch=%.2f midi=%d (no pitch or midi)",
                                         frame.pitchHz, heldMidi);
        }
        continue;
      }

      if (!pitchStable) {
        _onsetLatched = false;
        if (_s == 0) {
          SessionLogger::instance().logf("detection",
                                         "LOW E REJECT: pitch unstable (%.2fHz midi=%d)",
                                         frame.pitchHz, heldMidi);
        }
        continue;
      }

      // heldMidi has already been corrected by applyLowStringBias earlier for low E
      int midi = heldMidi;
      if (midi >= 0) {
        const int fret = midiToFret(midi, _tuning.stringMidi[_s]);
        if (_s == 0 && fret >= 0 && fret <= 3) {
          const float expectedHz = midiToHz(_tuning.stringMidi[_s] + fret);
          SessionLogger::instance().logf("detection",
                                         "LOW E ACCEPTED: detectedHz=%.2f expectedHz=%.2f midi=%d(should be %d) fret=%d",
                                         frame.pitchHz, expectedHz, midi, _tuning.stringMidi[_s] + fret, fret);
        }
        if (fret >= 0 && fret <= 24) {
          const float velocity = energyToVelocity(frame.envelopeRms);
          NoteEvent ev;
          ev.stringIdx = _s;
          ev.fret = fret;
          ev.midi = midi;
          ev.startSec = frame.tSec;
          ev.endSec = frame.tSec;
          ev.velocity = velocity;
          _events.push_back(ev);
          _activeIdx[_s] = static_cast<int>(_events.size() - 1);
          _lastOnsetPeakRms = frame.envelopeRms;
          _lastOnsetSec = frame.tSec;
          _releaseQuietFrames = 0;
          _activeHoldUntilSec = 0.f;
          _retriggerBlockUntilSec = 0.f;
          _activeForcedOpen = false;
          // if (_s == 0) {
          //   _retriggerBlockUntilSec = frame.tSec + kLowStringRetriggerGuardSec;
          //   const bool forcedOpenBias = (midi == _tuning.stringMidi[_s] && midi != beforeBiasMidi);
          //   if (forcedOpenBias) {
          //     _activeHoldUntilSec = frame.tSec + kOpenBiasMinHoldSec;
          //     _activeForcedOpen = true;
          //     SessionLogger::instance().logf("tracker",
          //                                    "[s%d] open-hold t=%.3f hold=%.3fs",
          //                                    _s + 1,
          //                                    frame.tSec,
          //                                    kOpenBiasMinHoldSec);
          //   }
          // }
          // SessionLogger::instance().logf("tracker",
          //                                "[s%d] note-start t=%.3f fret=%d midi=%d vel=%.2f env=%.5f",
          //                                _s + 1,
          //                                ev.startSec,
          //                                ev.fret,
          //                                ev.midi,
          //                                ev.velocity,
          //                                frame.envelopeRms);
          SessionLogger::instance().logf("detection",
                                         "     S%dF%d ON (onset=%.3f > thresh=%.3f | env=%.4f > gate=%.4f)",
                                         _s + 1,
                                         ev.fret,
                                         frame.onsetStrength,
                                         _lastThresholds.onsetThreshold,
                                         frame.envelopeRms,
                                         _lastThresholds.gateThreshold);
        }
      } else {
        _onsetLatched = false;
      }
      continue;
    }

    if (noteShouldClose(idx)) {
      if (_activeIdx[_s] >= 0 && _activeIdx[_s] < static_cast<int>(_events.size())) {
        auto& active = _events[_activeIdx[_s]];
        active.endSec = std::max(frame.tSec, active.startSec + _cfg.minNoteDurSec);
        // SessionLogger::instance().logf("tracker",
        //                                "[s%d] note-ended t=%.3f fret=%d dur=%.3f",
        //                                _s + 1,
        //                                active.endSec,
        //                                active.fret,
        //                                active.endSec - active.startSec);
        // S%dF%d OFF message already logged in noteShouldClose() with type details
      }
      _activeIdx[_s] = -1;
      _releaseQuietFrames = 0;
      _activeHoldUntilSec = 0.f;
      _retriggerBlockUntilSec = 0.f;
      _activeForcedOpen = false;
    }
  }
#endif
}

void StringTracker::resetState() {
  _feat.clear();
  _lastOnsetPeakRms = 0.f;
  _lastOnsetSec = -1.f;
  _filter.reset();
  _filteredScratch.clear();
  _currentSr = 0.f;
  _hopSamples = 0;
  _fftSize = 0;
  _currentHopSec = 0.f;
  _aubioReady = false;
  _onsetLatched = false;
  _pitchMedianWindow.clear();
  _pitchConfidenceFrames = 0;
  _pitchConfidenceMidi = -1;
  _pitchConfidenceHz = -1.f;
  _pitchHoldMidi = -1;
  _pitchHoldPendingMidi = -1;
  _pitchHoldPendingFrames = 0;
  _pitchHoldSilenceFrames = 0;
  _envAdaptiveRms = 0.001f;
  _releaseQuietFrames = 0;
  _activeHoldUntilSec = 0.f;
  _retriggerBlockUntilSec = 0.f;
  _activeForcedOpen = false;
  _lastFeaturePitchHz = -1.f;
#ifdef HAVE_AUBIO
  if (_aubioIn) {
    for (uint_t i = 0; i < _aubioIn->length; ++i)
      _aubioIn->data[i] = 0.f;
  }
#endif
}

void StringTracker::setCalibration(const CalibrationProfile& profile) {
  if (!profile.valid) {
    _calibrationValid = false;
    _calibrationAvgRms = 0.001f;
    refreshCalibrationTarget();
    // SessionLogger::instance().logf("tracker",
    //                                "[s%d] calibration reset target=%.5f gain=%.3f",
    //                                _s + 1,
    //                                _calibrationTargetRms,
    //                                _calibrationGain);
    return;
  }

  const std::size_t idx = static_cast<std::size_t>(_s);
  _calibrationAvgRms = std::max(profile.avgRms[idx], 1.0e-4f);
  _calibrationValid = true;
  refreshCalibrationTarget();
  _envAdaptiveRms = std::max(_envAdaptiveRms, _calibrationTargetRms);
  // SessionLogger::instance().logf("tracker",
  //                                "[s%d] calibration avg=%.5f target=%.5f gain=%.3f",
  //                                _s + 1,
  //                                _calibrationAvgRms,
  //                                _calibrationTargetRms,
  //                                _calibrationGain);
}
