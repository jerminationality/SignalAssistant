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
      const float openHz = midiToHz(midi);
      const float highestNote = midiToHz(midi + 24);
      const float lowHz = openHz * trackerparams::lowCutMultiplier(s);
      const float highHz = highestNote * trackerparams::highCutMultiplier(s);
      logger.logf(
          "tracker-settings",
          "string%d midi=%d lowCut=%.2fHz highCut=%.2fHz baseline=%.6f gateRatio=%.5f envFloor=%.6f sustainScale=%.3f retriggerScale=%.3f peakRelease=%.3f pitchTol=%.3f onsetScale=%.3f",
          s + 1,
          midi,
          lowHz,
          highHz,
          trackerparams::baselineFloor(s),
          trackerparams::gateRatio(s),
          trackerparams::envelopeFloor(s),
          trackerparams::sustainFloorScale(s),
          trackerparams::retriggerGateScale(s),
          trackerparams::peakReleaseRatio(s),
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

float stringPeakReleaseRatio(int s) {
  return trackerparams::peakReleaseRatio(s);
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
  hpState = 0.f;
  hpPrevInput = 0.f;
  lpState = 0.f;
}

void StringTracker::BandpassFilter::configure(float sr, float lowCutHz, float highCutHz, int stringIdx) {
  reset();
  if (sr <= 0.f) {
    hpAlpha = 0.f;
    lpBeta = 1.f;
    return;
  }

  const float low = std::max(1.f, lowCutHz);
  const float high = std::max(low + 10.f, highCutHz);

  hpAlpha = std::exp(-2.0f * float(M_PI) * low / sr);
  lpBeta = std::exp(-2.0f * float(M_PI) * high / sr);
  if (stringIdx == 0) {
    std::fprintf(stderr, "Low E bandpass: %.1f–%.1f Hz (sr=%.1f)\n",
                 low, high, sr);
  }
}

float StringTracker::BandpassFilter::process(float x) {
  const float hp = hpAlpha * (hpState + x - hpPrevInput);
  hpPrevInput = x;
  hpState = hp;

  const float lp = (1.0f - lpBeta) * hp + lpBeta * lpState;
  lpState = lp;
  return lp;
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
  const float lowCut = std::max(20.f, openHz * stringLowCutMultiplier(_s));
  const float highestNote = midiToHz(_tuning.stringMidi[_s] + 24);
  const float highCut = std::min(6000.f, highestNote * stringHighCutMultiplier(_s));
  _filter.configure(sr, lowCut, highCut, _s);
  SessionLogger::instance().logf("tracker",
                                 "[s%d] configure sr=%.1f hop=%d fft=%d low=%.1f high=%.1f",
                                 _s + 1,
                                 sr,
                                 _hopSamples,
                                 _fftSize,
                                 lowCut,
                                 highCut);
  const float aubioScale = stringAubioThresholdScale(_s);
  const float aubioThresh = std::clamp(_cfg.onsetThreshold * aubioScale, 0.01f, 0.18f);
  SessionLogger::instance().logf("tracker",
                                 "[s%d] params baseline=%.6f gate=%.4f envFloor=%.6f sustain=%.3f retrigger=%.3f peakRelease=%.3f pitchTol=%.3f onsetScale=%.3f aubioScale=%.2f aubioThresh=%.3f onsetSilence=%.1f pitchSilence=%.1f",
                                 _s + 1,
                                 stringBaselineFloor(_s),
                                 stringGateRatio(_s),
                                 stringEnvelopeFloor(_s),
                                 stringSustainFloorScale(_s),
                                 stringRetriggerGateScale(_s),
                                 stringPeakReleaseRatio(_s),
                                 stringPitchTolerance(_s),
                                 stringOnsetThreshold(_s, _cfg.onsetThreshold),
                                 aubioScale,
                                 aubioThresh,
                                 stringOnsetSilenceDb(_s),
                                 stringPitchSilenceDb(_s));

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
      const bool useFilteredForPitch = (_s <= 1);
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
          aubio_pitch_do(_aubioPitch, _aubioIn, _aubioPitchOut);
          const float pitchHz = fvec_get_sample(_aubioPitchOut, 0);
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

      if (onsetMarker > 0.f && _s == kAubioDebugString) {
        auto& logger = SessionLogger::instance();
        if (logger.enabled()) {
          logger.logf("tracker",
                     "[s%d] aubio-raw t=%.4f onset=%.6f env=%.6f peak=%.6f gain=%.3f",
                     _s + 1,
                     f.tSec,
                     onsetMarker,
                     f.envelopeRms,
                     framePeak,
                     onsetGain);
        }
      }

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

  const float sliderOnsetScale = stringOnsetThreshold(_s, 1.0f);
  const float onsetThreshold = sliderOnsetScale * _cfg.onsetThreshold;
  const float baseFloor = stringBaselineFloor(_s);
  const float gateRatio = stringGateRatio(_s);
  const float envelopeFloorParam = stringEnvelopeFloor(_s);
  const float sliderBaseline = std::max(baseFloor, kSliderMixEpsilon);
  float baseline = sliderBaseline;
  const float floorCandidate = baseline;
  baseline = sliderDominantMix(baseline, _envAdaptiveRms * 0.4f, 4.0f);
  baseline = sliderDominantMix(baseline, _lastOnsetPeakRms * 0.9f, 3.0f);
  const float adaptiveMetric = _envAdaptiveRms;
  const float gateThreshold = baseline * gateRatio;
  float envFloor = std::max(envelopeFloorParam, baseline * 0.7f);
  envFloor = sliderDominantMix(envFloor, _envAdaptiveRms * 0.6f, 3.0f);
  envFloor = sliderDominantMix(envFloor, _lastOnsetPeakRms * 0.5f, 2.5f);
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
    if (!shouldLog)
      return;
    sessionLogger.logf("tracker",
               "[s%d] onset-%s t=%.4f env=%.6f gate=%.6f envDelta=%.6f envFloor=%.6f onset=%.6f thresh=%.6f onsetDelta=%.6f baseline=%.6f floor=%.6f adapt=%.6f lastPeak=%.6f baseParam=%.6f gateRatio=%.4f envParam=%.6f onsetScale=%.3f retriggerScale=%.3f guard=%.3f guardRemain=%.3f activeAge=%.3f retrigRemain=%.3f pitchHz=%.2f pitchCents=%.1f",
                       _s + 1,
                       tag,
                       frame.tSec,
                       envelope,
                       gateThreshold,
                       envDelta,
                       envFloor,
                       onsetStrength,
                       onsetThreshold,
                       onsetDelta,
                       baseline,
                       floorCandidate,
                       adaptiveMetric,
                       _lastOnsetPeakRms,
                       baseFloor,
                       gateRatio,
                       envelopeFloorParam,
                       sliderOnsetScale,
                       sliderRetriggerScale,
                       separationGuard,
                       guardRemaining,
                       activeAge,
                       retriggerBlockRemaining,
                       frame.pitchHz,
                       frame.pitchCents);
  };
  if (onsetStrength <= 0.f)
    return false;

  if (onsetStrength < onsetThreshold) {
    logDecision("below-threshold");
    return false;
  }

  if (_onsetLatched) {
    logDecision("latched");
    return false;
  }

  if (envelope < gateThreshold) {
    logDecision("below-gate");
    return false;
  }

  if (envelope < envFloor) {
    logDecision("below-env-floor");
    return false;
  }

  if (_lastOnsetSec >= 0.f && (frame.tSec - _lastOnsetSec) < separationGuard) {
    logDecision("separation-guard");
    return false;
  }

  if (_activeIdx[_s] >= 0 && _activeIdx[_s] < static_cast<int>(_events.size())) {
    const auto& active = _events[_activeIdx[_s]];
    if (frame.tSec - active.startSec < _cfg.minNoteDurSec * 0.6f) {
      logDecision("active-guard");
      return false;
    }
  }

  _onsetLatched = true;
  logDecision("accepted");
  SessionLogger::instance().logf("tracker",
                                 "[s%d] onset t=%.3f env=%.5f gate=%.5f envDelta=%.5f envFloor=%.5f onset=%.3f thresh=%.3f onsetDelta=%.5f baseline=%.5f adaptive=%.5f lastPeak=%.5f guard=%.3f activeAge=%.3f pitch=%.2fHz pitchCents=%.1f",
                                 _s + 1,
                                 frame.tSec,
                                 frame.envelopeRms,
                                 gateThreshold,
                                 envDelta,
                                 envFloor,
                                 frame.onsetStrength,
                                 onsetThreshold,
                                 onsetDelta,
                                 baseline,
                                 adaptiveMetric,
                                 _lastOnsetPeakRms,
                                 separationGuard,
                                 activeAge,
                                 frame.pitchHz,
                                 frame.pitchCents);
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
  const float tolerance = 0.08f * static_cast<float>(harmonic);
  if (harmonicError > tolerance)
    return midi;

  const float minEnv = std::max(stringEnvelopeFloor(_s) * 0.65f, _calibrationTargetRms * 0.55f);
  const float minOnset = stringOnsetThreshold(_s, _cfg.onsetThreshold) * 1.6f;
  if (frame.envelopeRms < minEnv || frame.onsetStrength < minOnset)
    return midi;

  const float fundamentalHz = frame.pitchHz / static_cast<float>(harmonic);
  const int candidateMidi = std::clamp(hzToMidi(fundamentalHz), openMidi, openMidi + 24);
  if (candidateMidi == openMidi && candidateMidi < midi) {
    SessionLogger::instance().logf("tracker",
                                   "[s%d] harmonic-bias t=%.3f pitch=%.2fHz ratio=%.2f harmonic=%d midi=%d->%d",
                                   _s + 1,
                                   frame.tSec,
                                   frame.pitchHz,
                                   ratio,
                                   harmonic,
                                   midi,
                                   candidateMidi);
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

  if (_s == 0 && _retriggerBlockUntilSec > 0.f && frame.tSec < _retriggerBlockUntilSec)
    return false;

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

  const float envelopeFloor = stringEnvelopeFloor(_s);
  const float sliderEnvFloor = std::max(envelopeFloor, kSliderMixEpsilon);
  const float sustainScale = std::max(0.05f, stringSustainFloorScale(_s));
  const float sustainFloor = sliderEnvFloor * sustainScale;

  const bool quiet = avgEnv < sustainFloor;
  if (quiet) {
    _releaseQuietFrames = std::min(_releaseQuietFrames + 1, kReleaseQuietFrameCount);
  } else {
    _releaseQuietFrames = 0;
  }

  if (_releaseQuietFrames >= kReleaseQuietFrameCount) {
    SessionLogger::instance().logf("tracker",
                                   "[s%d] release-quiet t=%.3f avgEnv=%.5f floor=%.5f quietFrames=%d",
                                   _s + 1,
                                   frame.tSec,
                                   avgEnv,
                                   sustainFloor,
                                   _releaseQuietFrames);
    return true;
  }

  const float cappedPeak = sliderDominantMix(sustainFloor, _lastOnsetPeakRms, 6.0f);
  float retriggerGate = std::max(sustainFloor, cappedPeak * 0.4f);
  retriggerGate = std::max(sliderEnvFloor * 0.3f, retriggerGate * stringRetriggerGateScale(_s));
  retriggerGate = std::min(retriggerGate, sustainFloor * 6.0f);
  bool allowRetriggerRelease = true;
  if (_s == 0 && _activeForcedOpen) {
    const bool holdExpired = !(_activeHoldUntilSec > 0.f && frame.tSec < _activeHoldUntilSec);
    const float peakRef = std::max(_lastOnsetPeakRms, 1.0e-6f);
    const float envRatio = peakRef > 0.f ? avgEnv / peakRef : 0.f;
    if (!holdExpired || envRatio > 0.55f) {
      allowRetriggerRelease = false;
    } else {
      retriggerGate *= 1.8f;
    }
  }
  if (allowRetriggerRelease && frame.onsetStrength > retriggerGate && age >= _cfg.minNoteDurSec * 0.75f) {
    SessionLogger::instance().logf("tracker",
                                   "[s%d] release-retrigger t=%.3f onset=%.3f gate=%.3f age=%.3f",
                                   _s + 1,
                                   frame.tSec,
                                   frame.onsetStrength,
                                   retriggerGate,
                                   age);
    return true;
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
    const float alpha = (env > _envAdaptiveRms) ? kEnvRiseAlpha : kEnvFallAlpha;
    _envAdaptiveRms = (1.f - alpha) * _envAdaptiveRms + alpha * env;
    if (_envAdaptiveRms < kEnvMin)
      _envAdaptiveRms = kEnvMin;

    _lastOnsetPeakRms *= 0.995f;

    const float latchRelease = stringOnsetThreshold(_s, _cfg.onsetThreshold) * 0.6f;
    if (frame.onsetStrength < latchRelease)
      _onsetLatched = false;

    const int midiCandidate = (frame.pitchHz > 0.f) ? estimateMidi(frame) : -1;
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
        SessionLogger::instance().logf("tracker",
                                       "[s%d] note-ended (new onset) t=%.3f fret=%d dur=%.3f",
                                       _s + 1,
                                       active.endSec,
                                       active.fret,
                                       active.endSec - active.startSec);
        _activeIdx[_s] = -1;
        _releaseQuietFrames = 0;
        _activeHoldUntilSec = 0.f;
        _retriggerBlockUntilSec = 0.f;
        _activeForcedOpen = false;
      }

      if (frame.pitchHz <= 0.f || heldMidi < 0) {
        _onsetLatched = false;
        continue;
      }

      if (!pitchStable) {
        _onsetLatched = false;
        continue;
      }

      int midi = heldMidi;
      const int beforeBiasMidi = midi;
      midi = applyLowStringBias(midi, frame);
      if (midi >= 0) {
        const int fret = midiToFret(midi, _tuning.stringMidi[_s]);
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
          if (_s == 0) {
            _retriggerBlockUntilSec = frame.tSec + kLowStringRetriggerGuardSec;
            const bool forcedOpenBias = (midi == _tuning.stringMidi[_s] && midi != beforeBiasMidi);
            if (forcedOpenBias) {
              _activeHoldUntilSec = frame.tSec + kOpenBiasMinHoldSec;
              _activeForcedOpen = true;
              SessionLogger::instance().logf("tracker",
                                             "[s%d] open-hold t=%.3f hold=%.3fs",
                                             _s + 1,
                                             frame.tSec,
                                             kOpenBiasMinHoldSec);
            }
          }
          SessionLogger::instance().logf("tracker",
                                         "[s%d] note-start t=%.3f fret=%d midi=%d vel=%.2f env=%.5f",
                                         _s + 1,
                                         ev.startSec,
                                         ev.fret,
                                         ev.midi,
                                         ev.velocity,
                                         frame.envelopeRms);
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
        SessionLogger::instance().logf("tracker",
                                       "[s%d] note-ended t=%.3f fret=%d dur=%.3f",
                                       _s + 1,
                                       active.endSec,
                                       active.fret,
                                       active.endSec - active.startSec);
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
    SessionLogger::instance().logf("tracker",
                                   "[s%d] calibration reset target=%.5f gain=%.3f",
                                   _s + 1,
                                   _calibrationTargetRms,
                                   _calibrationGain);
    return;
  }

  const std::size_t idx = static_cast<std::size_t>(_s);
  _calibrationAvgRms = std::max(profile.avgRms[idx], 1.0e-4f);
  _calibrationValid = true;
  refreshCalibrationTarget();
  _envAdaptiveRms = std::max(_envAdaptiveRms, _calibrationTargetRms);
  SessionLogger::instance().logf("tracker",
                                 "[s%d] calibration avg=%.5f target=%.5f gain=%.3f",
                                 _s + 1,
                                 _calibrationAvgRms,
                                 _calibrationTargetRms,
                                 _calibrationGain);
}
