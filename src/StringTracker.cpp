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
constexpr int kPitchConfidenceFrames = 3;
constexpr float kPitchConfidenceMaxCents = 28.0f;
constexpr float kPitchConfidenceHzFloor = 0.8f;
constexpr int kPitchHoldFrames = 3;
constexpr int kPitchHoldReleaseFrames = 10;
constexpr float kEnvRiseAlpha = 0.15f;
constexpr float kEnvFallAlpha = 0.03f;
constexpr float kEnvMin = 1.0e-5f;
constexpr int kReleaseQuietFrameCount = 8;
constexpr float kOpenBiasMinHoldSec = 0.36f;
constexpr float kLowStringRetriggerGuardSec = 0.22f;
constexpr float kAdaptiveGuardMaxExtraMs = 40.0f;  // Hard-coded limit for adaptive guard extension
constexpr float kPitchWaitTimeoutSec     = 0.050f;  // Max time to wait for pitch lock after peak onset
constexpr float kMinCrestFactorForOnset = 2.0f;    // Reject noise-like signals below this peak/RMS ratio (applied to raw signal)
constexpr float kPeakDeltaOnsetRatio = 2.5f;        // Peak must jump to >=N× previous peak to trigger onset via PCM path
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
          "string%d midi=%d lowCut=%.2fHz highCut=%.2fHz baseline=%.6f noteOn=%.6f noteOff=%.6f retriggerScale=%.3f pitchTol=%.3f guardMs=%.1f",
          s + 1,
          midi,
          lowHz,
          highHz,
          trackerparams::baselineFloor(s),
          trackerparams::noteOnThreshold(s),
          trackerparams::noteOffThreshold(s),
          trackerparams::retriggerGateScale(s),
          trackerparams::pitchTolerance(s),
          trackerparams::triggerGuardMs(s));
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

float stringNoteOnThreshold(int s) {
  return trackerparams::noteOnThreshold(s);
}

float stringNoteOffThreshold(int s) {
  return trackerparams::noteOffThreshold(s);
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

float stringTriggerGuardSec(int s) {
  return trackerparams::triggerGuardMs(s) * 0.001f;
}

float stringRepitchThreshold(int s) {
  return trackerparams::repitchThreshold(s);
}

int stringRepitchConfirmFrames(int s) {
  return trackerparams::repitchConfirmFrames(s);
}

float stringRepitchMinConfidence(int s) {
  return trackerparams::repitchMinConfidence(s);
}

float stringPitchConfidence(int s) {
  return trackerparams::pitchConfidence(s);
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

  // For all strings, use standard bandpass with configurable cutoffs.
  // Strings 0-2 (Low E, A, D): tight LPF ~450Hz to eliminate octave errors.
  // Strings 3-5 (G, B, e): wider LPF ~1200-1500Hz.
  {
    // Standard bandpass for all strings
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

  // std::fprintf(stderr, "String %d bandpass: HPF=%.1fHz LPF=%.1fHz (sr=%.1f)\n", stringIdx + 1, lowCutHz, highCutHz, sr);
  SessionLogger::instance().logf("filter",
                                 "String %d bandpass: HPF=%.1fHz LPF=%.1fHz",
                                 stringIdx + 1, lowCutHz, highCutHz);
}

float StringTracker::BandpassFilter::process(float x) {
  // Highpass section (2nd-order biquad, Direct Form I)
  const float hp_out = hp_b0 * x + hp_b1 * hp_x1 + hp_b2 * hp_x2 - hp_a1 * hp_y1 - hp_a2 * hp_y2;
  hp_x2 = hp_x1; hp_x1 = x;
  hp_y2 = hp_y1; hp_y1 = hp_out;

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
                             std::vector<int>& activeIdx,
                             TabEngine& engine)
: _s(stringIdx), _tuning(t), _cfg(c), _engine(engine), _events(sharedEvents), _activeIdx(activeIdx)
{
  logTrackerSettingsOnce(_tuning, _cfg);
  _filter.reset();
  _filteredScratch.reserve(2048);
  // Calibration is applied upstream (HexJackClient / RecordedSessionPlayer)
  // before audio reaches StringTracker — no calibration state needed here.
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

  // paramsChanged is tracked for filter reconfiguration — no calibration
  // state to refresh (calibration is applied upstream).

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
  const char* pitchAlgo = "yinfast";
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
      // std::fprintf(stderr, "StringTracker[%d]: Setting pitch range %.1f-%.1f Hz\n", _s + 1, minFreq, maxFreq);
      // Note: These functions may not exist in all aubio versions
      // aubio_pitch_set_min_freq(_aubioPitch, minFreq);
      // aubio_pitch_set_max_freq(_aubioPitch, maxFreq);
    }

    aubio_onset_set_silence(_aubioOnset, stringOnsetSilenceDb(_s));
    aubio_onset_set_threshold(_aubioOnset, aubioThresh);

    _aubioReady = true;
        // std::fprintf(stderr,
           // "StringTracker[%d]: Aubio initialised (hop=%d, sr=%.1f, aubioScale=%.2f, base=%.3f, onsetThresh=%.3f)\n",
             // _s + 1,
             // _hopSamples,
           // sr,
           // aubioScale,
           // _cfg.onsetThreshold,
           // aubioThresh);
  } else {
    // std::fprintf(stderr, "StringTracker[%d]: Aubio init failed (onset=%p pitch=%p in=%p out=%p pitchOut=%p)\n",
                 // _s + 1, (void*)_aubioOnset, (void*)_aubioPitch, (void*)_aubioIn, (void*)_aubioOnsetOut, (void*)_aubioPitchOut);
  }
#else
  if (!_warnedNoAubio) {
    // std::fprintf(stderr, "StringTracker[%d]: Aubio support not available; live detection disabled.\n", _s + 1);
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
        const float in = samples ? samples[i] : 0.f;
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

      // Consolidate RMS and peak calculation into single loop (filtered signal for RMS/envelope)
      float sumSq = 0.0f;
      float framePeak = 0.0f;
      if (framePtr) {
        for (int i = 0; i < frameLen; ++i) {
          float val = framePtr[i];
          float absVal = std::fabs(val);
          if (absVal > framePeak) framePeak = absVal;
          sumSq += val * val;
        }
        f.envelopeRms = std::sqrt(sumSq / frameLen);
      } else {
        f.envelopeRms = 0.f;
      }

      // Crest factor from raw signal — the pluck transient gives a genuine spike
      // on the raw waveform, making it reliable for distinguishing tonal attacks
      // from sustained broadband noise. Filtered signal would be near-sinusoidal
      // (crest ≈ √2), too low to gate against noise.
      if (rawPtr && frameLen > 0) {
        float rawPeak = 0.f;
        float rawSumSq = 0.f;
        for (int i = 0; i < frameLen; ++i) {
          const float absVal = std::fabs(rawPtr[i]);
          if (absVal > rawPeak) rawPeak = absVal;
          rawSumSq += rawPtr[i] * rawPtr[i];
        }
        const float rawRms = std::sqrt(rawSumSq / frameLen);
        f.crestFactor = (rawRms > 1e-8f) ? (rawPeak / rawRms) : 0.f;
        f.peakPcm = rawPeak;
      } else {
        f.crestFactor = 0.f;
        f.peakPcm = 0.f;
      }

      const bool useFilteredForPitch = true;  // Use filtered signal for pitch detection
      const float onsetGain = framePeak > 1e-5f ? std::min(1.0f, 0.35f / framePeak) : 1.f;
      const float pitchPeak = framePeak;  // Always use filtered peak (useFilteredForPitch == true)
      const float pitchGain = pitchPeak > 1e-5f ? std::min(1.0f, 0.45f / pitchPeak) : 1.f;

      float onsetMarker = 0.f;
      float detectedPitchHz = -1.f;
#ifdef HAVE_AUBIO
      if (_aubioIn) {
        for (int i = 0; i < hop; ++i) {
          float directSample = 0.f;
          if (rawPtr && i < frameLen) {
            directSample = rawPtr[i];
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
              pitchSample = rawPtr[i];
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
            
            // Simple zero-crossing frequency check (no heap allocation)
            // Use aubioIn->data directly instead of allocating a temporary vector
            
            // Count zero crossings as rough frequency indicator
            int zeroCrossings = 0;
            for (int i = 1; i < hop; ++i) {
              if (_aubioIn->data[i-1] * _aubioIn->data[i] < 0) zeroCrossings++;
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
          const float pitchConf = aubio_pitch_get_confidence(_aubioPitch);
          
          // Debug logging for low E string pitch detection (MUTED)
          // if (_s == 0 && pitchHz > 0.f && pitchHz >= kMinPitchHz) {
          //   SessionLogger::instance().logf("detection",
          //                                  "LOW E RAW PITCH: detected=%.2fHz filtered=%d peak=%.5f",
          //                                  pitchHz, useFilteredForPitch ? 1 : 0, pitchPeak);
          // }
          
          if (pitchHz > 0.f && pitchHz >= kMinPitchHz && pitchHz <= kMaxPitchHz) {
            detectedPitchHz = pitchHz;
            f.pitchConfidence = pitchConf;
          }
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

  // Direct thresholds - no complex calculations
  const float onsetThreshold = stringOnsetThreshold(_s, 1.0f);
  const float baseFloor = stringBaselineFloor(_s);
  const float baseline = std::max(baseFloor, kSliderMixEpsilon);
  const float noteOnThreshold = stringNoteOnThreshold(_s);
  const float noteOffThreshold = stringNoteOffThreshold(_s);
  
  // Calculate retriggerGate for UI display (also computed in noteShouldClose)
  const float retriggerGateScale = stringRetriggerGateScale(_s);
  const float cappedPeak = std::min(_lastOnsetPeakRms, noteOnThreshold * 2.5f);
  const float retriggerGate = std::max(noteOffThreshold, cappedPeak * 0.4f) * retriggerGateScale;
  
  // Store for UI display
  _lastThresholds.onsetThreshold = onsetThreshold;
  _lastThresholds.baseline = baseline;
  _lastThresholds.gateThreshold = noteOnThreshold;  // Note ON threshold
  _lastThresholds.envFloor = baseFloor;             // Noise floor
  _lastThresholds.sustainFloor = noteOffThreshold;  // Note OFF threshold
  _lastThresholds.retriggerGate = retriggerGate;
  _lastThresholds.retriggerDeltaRatio = trackerparams::retriggerDeltaRatio(_s);
  
  const float separationGuard = std::max(_currentHopSec, stringTriggerGuardSec(_s));
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
  const float envDelta = envelope - noteOnThreshold;

  auto& sessionLogger = SessionLogger::instance();
  const bool logString = sessionLogger.enabled() && (_s == kAubioDebugString);
  const bool shouldLog = logString && (onsetStrength > onsetThreshold * 0.35f || envelope > noteOnThreshold * 0.7f);
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
  if (onsetStrength <= 0.f) {
    // ── Peak-delta onset path ──────────────────────────────────────────────
    // Raw PCM peak responds within a single hop to a pluck transient.
    // If the peak jumps sharply relative to the previous frame, treat it as
    // an onset even when aubio's spectral flux hasn't fired yet.
    const float peakDelta = frame.peakPcm - _prevPeakPcm;
    const float minPeakJump = std::max(0.01f, _prevPeakPcm * kPeakDeltaOnsetRatio);
    if (peakDelta <= 0.f || frame.peakPcm < 0.02f || peakDelta < minPeakJump)
      return false;
    // Fall through — peak spike detected, apply remaining RMS + guard gates below
  }

  // Check if a note is already active first - if so, exit early without logging PASS messages
  const bool noActiveNote = (_activeIdx[_s] < 0 || _activeIdx[_s] >= static_cast<int>(_events.size()));
  
  // Active notes no longer block onset detection.
  // The trigger guard (separation guard) handles debouncing — if enough time
  // has elapsed since the last onset, a new onset during an active note will
  // close the old note and start a new one (retrigger).

  if (onsetStrength > 0.f && onsetStrength < onsetThreshold) {
    logDecision("below-threshold");
    return false;
  }

  if (_onsetLatched) {
    logDecision("latched");
    // if (noActiveNote) {
    //   SessionLogger::instance().logf("detection", "       S%d Rejected Onset (onset latched)", _s + 1);
    // }
    return false;
  }

  if (envelope < noteOnThreshold) {
    logDecision("below-gate");
    // if (noActiveNote) {
    //   const int estMidi = estimateMidi(frame);
    //   const int estFret = (estMidi >= 0) ? midiToFret(estMidi, _tuning.stringMidi[_s]) : -1;
    //   SessionLogger::instance().logf("detection", "     S%dF%d Rejected Note (onset=%.2f > thresh=%.2f; env=%.4f < gate=%.4f)", 
    //                                  _s + 1, estFret, onsetStrength, onsetThreshold, envelope, noteOnThreshold);
    // }
    return false;
  }

  // Crest factor gate: disabled for now — needs further tuning.
  // if (frame.crestFactor > 0.f && frame.crestFactor < kMinCrestFactorForOnset) {
  //   logDecision("low-crest");
  //   SessionLogger::instance().logf("detection",
  //                                  "       S%d Rejected Onset (crest=%.2f < min=%.2f)  RMS=%.4f",
  //                                  _s + 1, frame.crestFactor, kMinCrestFactorForOnset, envelope);
  //   return false;
  // }

  // Adaptive guard: check _retriggerBlockUntilSec (set on note-on with intensity-scaled duration)
  if (_retriggerBlockUntilSec > 0.f && frame.tSec < _retriggerBlockUntilSec) {
    logDecision("adaptive-guard");
    return false;
  }

  // Separation guard now handled by _retriggerBlockUntilSec (set on every close).

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

  const float minEnv = std::max(stringNoteOnThreshold(_s) * 0.65f, kCalibrationBaseTargetRms * 0.55f);
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

  // Use direct note OFF threshold
  const float baseFloor = stringBaselineFloor(_s);
  const float baseline = std::max(baseFloor, kSliderMixEpsilon);
  const float noteOffThreshold = stringNoteOffThreshold(_s);
  const float sustainFloor = std::max(baseline, noteOffThreshold);
  
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
                                     "[OFF]      S%d  F%-2d        RMS = %.4f  <  OFF = %.4f  (sharp-drop)",
                                     _s + 1,
                                     active.fret,
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
                                     "[OFF]      S%d  F%-2d        RMS = %.4f  <  OFF = %.4f  (sustained-quiet)",
                                     _s + 1,
                                     active.fret,
                                     avgEnv,
                                     sustainFloor);
    }
    return true;
  }

  // Onset-during-sustain detection is handled in processBlock() via
  // detectOnset() + energy-delta/decay guards.

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
    return _pitchConfidenceFrames >= trackerparams::pitchConfidenceFrames(_s);
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

  return _pitchConfidenceFrames >= trackerparams::pitchConfidenceFrames(_s);
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

// refreshCalibrationTarget() removed — calibration is applied upstream in
// HexJackClient before any audio reaches the engine.

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

    // ── Peak-first onset arming ─────────────────────────────────────────────
    // Raw PCM peak responds within a single hop to a pluck transient — one hop
    // before aubio spectral flux can accumulate enough change to fire.
    // Arm pending onset immediately so pitch evaluation starts this frame.
    if (_pendingOnsetSec < 0.f && frame.peakPcm > 0.f) {
      // Peak onset threshold: static noise floor (NoiseGate param) + NoteOn RMS gate.
      // NoiseGate is tunable; adaptive floor (_envAdaptiveRms) is tracked but not
      // used here — reserved for when adaptive floor is re-enabled.
      const float peakOnsetThreshold = stringBaselineFloor(_s) + stringNoteOnThreshold(_s);
      const bool adaptGuardClear = (frame.tSec >= _retriggerBlockUntilSec);

      if (frame.peakPcm > peakOnsetThreshold && adaptGuardClear) {
        bool suppress = false;
        _pendingOnsetPrevFret = -1;

        if (_activeIdx[_s] >= 0 && _activeIdx[_s] < static_cast<int>(_events.size())) {
          // Active note: run delta/decay check NOW at the transient frame —
          // energy change is most visible here, before the transient decays.
          const float energyDiff       = frame.envelopeRms - _prevFrameRms;
          const float deltaRatio       = trackerparams::retriggerDeltaRatio(_s);
          const float minDeltaRequired = std::max(0.05f, _prevFrameRms * deltaRatio);
          constexpr float kDecayRatioRequired = 0.55f;
          const bool noteDecayed = (_minRmsAfterOnset
                                    < _lastOnsetPeakRms * kDecayRatioRequired);

          if (energyDiff < minDeltaRequired || !noteDecayed) {
            // Sustain wobble or note never decayed — suppress
            suppress = true;
          } else {
            // Delta/decay passed — close old note immediately, then wait for pitch
            auto& oldNote = _events[static_cast<std::size_t>(_activeIdx[_s])];
            _pendingOnsetPrevFret = oldNote.fret;
            oldNote.endSec = std::max(frame.tSec,
                                      oldNote.startSec + _cfg.minNoteDurSec);
            _engine.onNoteOff(_s, oldNote.fret);
            _activeIdx[_s]          = -1;
            _releaseQuietFrames     = 0;
            _activeHoldUntilSec     = 0.f;
            _retriggerBlockUntilSec = frame.tSec + stringTriggerGuardSec(_s);
            _activeForcedOpen       = false;
          }
        }

        if (!suppress) {
          _pendingOnsetSec     = frame.tSec;
          _pendingOnsetPeak    = frame.peakPcm;
          _pendingOnsetRms     = frame.envelopeRms;
          _pendingOnsetPrevRms = _prevFrameRms;

          // Reset pitch confidence so stability is re-earned from post-onset frames.
          // This prevents pre-onset harmonic/bleed data from firing a wrong note
          // immediately at the transient. Skip reset only if the held pitch already
          // matches the previous note's midi (same-note repluck — confidence is valid).
          const int prevNoteMidi = (_pendingOnsetPrevFret >= 0)
              ? (_tuning.stringMidi[_s] + _pendingOnsetPrevFret) : -1;
          const bool sameNote = (prevNoteMidi >= 0 && heldMidi == prevNoteMidi);
          if (!sameNote) {
            _pitchConfidenceFrames  = 0;
            _pitchConfidenceMidi    = -1;
            _pitchConfidenceHz      = -1.f;
            _pitchHoldMidi          = -1;
            _pitchHoldPendingMidi   = -1;
            _pitchHoldPendingFrames = 0;
          }
        }
      }
    }

    if (_activeIdx[_s] >= 0 && _activeIdx[_s] < static_cast<int>(_events.size())) {
      auto& active = _events[_activeIdx[_s]];
      active.endSec = frame.tSec;
      active.velocity = std::max(active.velocity, energyToVelocity(frame.envelopeRms));
      
      // REPITCH: Detect discrete pitch transition during sustain (hammer-on / pull-off)
      // Algorithm: compare current pitch to active note in cents. If the
      // cents delta exceeds a dead-zone threshold (filtering vibrato/shimmer)
      // and is sustained for confirmFrames with high aubio confidence
      // AND onset strength is LOW (ruling out a repluck), fire repitch event.
      if (heldMidi >= 0 && pitchStable) {
        const int activeMidi = active.midi;
        const int pitchDelta = std::abs(heldMidi - activeMidi);
        const float thresholdCents = stringRepitchThreshold(_s) * 100.0f; // convert semitones → cents
        const int   confirmFrames = stringRepitchConfirmFrames(_s);
        const float minConfidence = stringRepitchMinConfidence(_s);

        // Cents-based dead-zone: require a significant pitch shift before
        // acknowledging a potential new note (filters vibrato/shimmer)
        const float activeNoteHz = midiToHz(activeMidi);
        const float pitchDiffCents = std::fabs(centsBetween(frame.pitchHz, activeNoteHz));

        // Prevent repitch too soon after note onset or last repitch
        const float triggerGuard = std::max(_currentHopSec, stringTriggerGuardSec(_s));
        const float timeSinceOnset = frame.tSec - active.startSec;
        const float timeSinceLastRepitch = (_lastRepitchSec > 0.f)
            ? (frame.tSec - _lastRepitchSec) : 999.f;

        if (pitchDiffCents >= thresholdCents && pitchDelta >= 1 && pitchDelta < 12
            && timeSinceOnset > triggerGuard
            && timeSinceLastRepitch > triggerGuard) {
          // Candidate matches previous candidate — increment counter
          if (_repitchCandidateMidi == heldMidi) {
            _repitchStabilityCounter++;
            _repitchLastConfidence = std::min(_repitchLastConfidence, frame.pitchConfidence);
            _repitchMaxOnsetSeen = std::max(_repitchMaxOnsetSeen, frame.onsetStrength);
          } else {
            _repitchCandidateMidi = heldMidi;
            _repitchStabilityCounter = 1;
            _repitchLastConfidence = frame.pitchConfidence;
            _repitchMaxOnsetSeen = frame.onsetStrength;
          }

          // Check stability + confidence + onset pre-flight
          if (_repitchStabilityCounter >= confirmFrames
              && _repitchLastConfidence >= minConfidence) {
            // Onset pre-flight: reject if onset was elevated at ANY point during
            // the confirm window, not just the current frame. Fret noise often
            // spikes onset briefly at the start and then decays — checking only
            // the current frame misses this. Also require the envelope is still
            // above noteOnThreshold to prevent harmonics firing during deep decay.
            const float onsetThreshold = stringOnsetThreshold(_s, 1.0f);
            const bool onsetClean = (_repitchMaxOnsetSeen < onsetThreshold)
                                    && (frame.onsetStrength < onsetThreshold);
            const bool sustainedEnough = (frame.envelopeRms >= stringNoteOnThreshold(_s));
            if (onsetClean && sustainedEnough) {
              const int oldFret = active.fret;
              const int newFret = midiToFret(heldMidi, _tuning.stringMidi[_s]);

              if (newFret >= 0 && newFret <= 24) {
                // Notify engine of note-off for old note
                _engine.onNoteOff(_s, oldFret);

                // Close the current note
                active.endSec = frame.tSec;

                // Create new event inheriting velocity
                NoteEvent ev;
                ev.stringIdx = _s;
                ev.fret = newFret;
                ev.midi = heldMidi;
                ev.startSec = frame.tSec;
                ev.endSec = frame.tSec;
                ev.velocity = active.velocity;  // inherit velocity from parent note
                {
                  std::lock_guard<std::mutex> lock(_engine.getEventMutex());
                  _events.push_back(ev);
                  _activeIdx[_s] = static_cast<int>(_events.size() - 1);
                }

                // Notify engine of note-on for new note
                _engine.onNoteOn(_s, newFret, ev.velocity);

                // Reset trigger guard so retrigger won't fire immediately
                _lastOnsetSec = frame.tSec;
                _lastRepitchSec = frame.tSec;

                SessionLogger::instance().logf("detection",
                                               "[REPITCH]  S%d  F%-2d->F%-2d   Pitch = %.2f Hz  |  Conf = %.2f  |  Delta = %d st",
                                               _s + 1, oldFret, newFret,
                                               frame.pitchHz, _repitchLastConfidence, pitchDelta);

                _repitchCandidateMidi = -1;
                _repitchStabilityCounter = 0;
                _repitchLastConfidence = 0.f;
                _repitchMaxOnsetSeen = 0.f;
              }
            } else {
              // Blocked by onset spike during confirm window or envelope too low
              SessionLogger::instance().logf("detection",
                                             "[REPITCH-BLOCK] S%d F%-2d->%d  onsetClean=%d(maxOnset=%.3f) sustained=%d(rms=%.4f>=%.4f)",
                                             _s + 1, active.fret, heldMidi,
                                             onsetClean ? 1 : 0, _repitchMaxOnsetSeen,
                                             sustainedEnough ? 1 : 0,
                                             frame.envelopeRms, stringNoteOnThreshold(_s));
              // Reset so noise doesn't keep accumulating a stale candidate
              _repitchCandidateMidi = -1;
              _repitchStabilityCounter = 0;
              _repitchLastConfidence = 0.f;
              _repitchMaxOnsetSeen = 0.f;
            }
          }
        } else {
          // Pitch matches active note or delta out of range — reset candidate
          _repitchCandidateMidi = -1;
          _repitchStabilityCounter = 0;
          _repitchLastConfidence = 0.f;
          _repitchMaxOnsetSeen = 0.f;
        }
      } else {
        // No stable pitch — reset repitch candidate
        _repitchCandidateMidi = -1;
        _repitchStabilityCounter = 0;
        _repitchLastConfidence = 0.f;
        _repitchMaxOnsetSeen = 0.f;
      }
    }

    // ── Onset fire decision ─────────────────────────────────────────────────
    // Primary: pending onset (peak-first) fires when pitch stabilizes or times out.
    // Fallback: aubio spectral flux for soft notes that didn't cross peak threshold.
    bool onsetFired = false;

    if (_pendingOnsetSec >= 0.f) {
      const float pendingAge = frame.tSec - _pendingOnsetSec;
      if ((pitchStable && heldMidi >= 0)
          || (pendingAge >= kPitchWaitTimeoutSec && heldMidi >= 0)) {
        onsetFired = true;
      } else if (pendingAge >= kPitchWaitTimeoutSec) {
        // Timed out — pitch never came, discard
        _pendingOnsetSec      = -1.f;
        _pendingOnsetPrevFret = -1;
      }
    }

    if (onsetFired) {
      _pendingOnsetSec = -1.f;

      // Peak path: old note already closed at arm time; prevFret/hadActiveNote carried.
      // Aubio fallback: old note may still be active — close it now (with delta/decay guard).
      bool hadActiveNote = (_pendingOnsetPrevFret >= 0);
      int  prevFret      = _pendingOnsetPrevFret;
      _pendingOnsetPrevFret = -1;

      if (_activeIdx[_s] >= 0 && _activeIdx[_s] < static_cast<int>(_events.size())) {
        // Aubio fallback path: active note still present, apply guards
        hadActiveNote = true;
        const float energyDiff       = frame.envelopeRms - _prevFrameRms;
        const float deltaRatio       = trackerparams::retriggerDeltaRatio(_s);
        const float minDeltaRequired = std::max(0.05f, _prevFrameRms * deltaRatio);
        constexpr float kDecayRatioRequired = 0.55f;
        const bool noteDecayed = (_minRmsAfterOnset
                                  < _lastOnsetPeakRms * kDecayRatioRequired);

        if (energyDiff < minDeltaRequired || !noteDecayed) {
          _onsetLatched = false;
          _prevFrameRms = frame.envelopeRms;
          _repitchCandidateMidi    = -1;
          _repitchStabilityCounter = 0;
          _repitchLastConfidence   = 0.f;
          _repitchMaxOnsetSeen     = 0.f;
          continue;
        }

        auto& active = _events[_activeIdx[_s]];
        active.endSec = std::max(frame.tSec, active.startSec + _cfg.minNoteDurSec);
        prevFret = active.fret;
        _engine.onNoteOff(_s, active.fret);
        _activeIdx[_s]          = -1;
        _releaseQuietFrames     = 0;
        _activeHoldUntilSec     = 0.f;
        _retriggerBlockUntilSec = frame.tSec + stringTriggerGuardSec(_s);
        _activeForcedOpen       = false;
        _repitchCandidateMidi    = -1;
        _repitchStabilityCounter = 0;
        _repitchLastConfidence   = 0.f;
        _repitchMaxOnsetSeen     = 0.f;
      }

      if (frame.pitchHz <= 0.f || heldMidi < 0) {
        _onsetLatched = false;
        continue;
      }

      // Accept if pitch is stable (3-frame consistency) OR aubio confidence meets threshold
      if (!pitchStable && frame.pitchConfidence < stringPitchConfidence(_s)) {
        _onsetLatched = false;
        continue;
      }

      // heldMidi has already been corrected by applyLowStringBias earlier for low E
      int midi = heldMidi;
      if (midi >= 0) {
        const int fret = midiToFret(midi, _tuning.stringMidi[_s]);
        // if (_s == 0 && fret >= 0 && fret <= 3) {
        //   const float expectedHz = midiToHz(_tuning.stringMidi[_s] + fret);
        //   SessionLogger::instance().logf("detection",
        //                                  "LOW E ACCEPTED: detectedHz=%.2f expectedHz=%.2f midi=%d(should be %d) fret=%d",
        //                                  frame.pitchHz, expectedHz, midi, _tuning.stringMidi[_s] + fret, fret);
        // }
        if (fret >= 0 && fret <= 24) {
          const float velocity = energyToVelocity(frame.envelopeRms);
          NoteEvent ev;
          ev.stringIdx = _s;
          ev.fret = fret;
          ev.midi = midi;
          ev.startSec = frame.tSec;
          ev.endSec = frame.tSec;
          ev.velocity = velocity;
          {
            std::lock_guard<std::mutex> lock(_engine.getEventMutex());
            _events.push_back(ev);
            _activeIdx[_s] = static_cast<int>(_events.size() - 1);
          }
          _lastOnsetPeakRms = frame.envelopeRms;
          _lastOnsetSec = frame.tSec;
          _minRmsAfterOnset = frame.envelopeRms;  // Reset decay tracker
          _releaseQuietFrames = 0;
          _activeHoldUntilSec = 0.f;
          _activeForcedOpen = false;

          // Adaptive guard: scale refractory period based on onset intensity
          {
            const float baseGuardMs = trackerparams::triggerGuardMs(_s);
            const float adaptiveAddonMs = std::min(kAdaptiveGuardMaxExtraMs, frame.envelopeRms * 100.0f);
            const float finalGuardSec = (baseGuardMs + adaptiveAddonMs) / 1000.0f;
            _retriggerBlockUntilSec = frame.tSec + finalGuardSec;
          }
          
          // Notify engine of note-on event
          _engine.onNoteOn(_s, fret, velocity);

          if (hadActiveNote) {
            SessionLogger::instance().logf("detection",
                                           "[ON]       S%d  F%-2d<-F%-2d  RMS = %.4f  >  ENV = %.4f  Delta = %.4f",
                                           _s + 1,
                                           ev.fret, prevFret,
                                           frame.envelopeRms,
                                           _lastThresholds.gateThreshold,
                                           frame.envelopeRms - _prevFrameRms);
          } else {
            SessionLogger::instance().logf("detection",
                                           "[ON]       S%d  F%-2d        RMS = %.4f  >  ENV = %.4f",
                                           _s + 1,
                                           ev.fret,
                                           frame.envelopeRms,
                                           _lastThresholds.gateThreshold);
          }
        }
      } else {
        _onsetLatched = false;
      }
      continue;
    }

    _prevFrameRms = frame.envelopeRms;
    _prevPeakPcm = frame.peakPcm;
    // Track minimum RMS since last onset for decay-based onset suppression
    if (_activeIdx[_s] >= 0)
      _minRmsAfterOnset = std::min(_minRmsAfterOnset, frame.envelopeRms);

    if (noteShouldClose(idx)) {
      if (_activeIdx[_s] >= 0 && _activeIdx[_s] < static_cast<int>(_events.size())) {
        auto& active = _events[_activeIdx[_s]];
        active.endSec = std::max(frame.tSec, active.startSec + _cfg.minNoteDurSec);
        
        // Notify engine of note-off event
        _engine.onNoteOff(_s, active.fret);
      }
      _activeIdx[_s] = -1;
      _releaseQuietFrames = 0;
      _activeHoldUntilSec = 0.f;
      _retriggerBlockUntilSec = frame.tSec + stringTriggerGuardSec(_s);
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
  _prevFrameRms = 0.f;
  _prevPeakPcm = 0.f;
  _minRmsAfterOnset = 0.f;
  _pendingOnsetSec      = -1.f;
  _pendingOnsetPeak     = 0.f;
  _pendingOnsetRms      = 0.f;
  _pendingOnsetPrevRms  = 0.f;
  _pendingOnsetPrevFret = -1;
  _lastFeaturePitchHz = -1.f;
  _lastRepitchSec = -1.f;
  _repitchCandidateMidi = -1;
  _repitchStabilityCounter = 0;
  _repitchLastConfidence = 0.f;
  _repitchMaxOnsetSeen = 0.f;
#ifdef HAVE_AUBIO
  if (_aubioIn) {
    for (uint_t i = 0; i < _aubioIn->length; ++i)
      _aubioIn->data[i] = 0.f;
  }
#endif
}

// setCalibration() removed — calibration gain is applied upstream before
// audio reaches the engine.  The engine operates solely on calibrated samples.
