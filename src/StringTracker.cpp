#include "StringTracker.h"
#include "SessionLogger.h"
#include "StringTrackerParams.h"
#include "util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

// ═══════════════════════════════════════════════════════════════════════════
// Peak-First Dual-Path Detection — StringTracker Implementation
//
//   Path A  (Peak / Transient)   raw signal peak    → onset / retrigger
//   Path B  (RMS  / Body)        filtered-signal RMS → velocity / sustain
//   Noise floor                  leaky integrator     → dynamic onset threshold
//   Pitch                        aubio YIN on filtered signal
// ═══════════════════════════════════════════════════════════════════════════

namespace {

constexpr float kMinPitchHz            = 60.0f;
constexpr float kMaxPitchHz            = 6000.0f;
constexpr int   kPitchConfidenceFrames = 3;
constexpr float kPitchConfidenceMaxCents = 28.0f;
constexpr int   kPitchHoldFrames        = 3;
constexpr int   kPitchHoldReleaseFrames = 10;
constexpr int   kReleaseQuietFrameCount = 8;     // consecutive quiet frames for note-off
constexpr float kAnalysisTimeoutMult    = 4.0f;  // × kAnalysisDelay before PENDING timeout
constexpr float kNoiseFloorMin          = 1.0e-5f;

std::once_flag gLoggedTrackerSettings;

void logTrackerSettingsOnce(const Tuning& tuning, const TrackerConfig& cfg) {
  std::call_once(gLoggedTrackerSettings, [&]() {
    auto& logger = SessionLogger::instance();
    logger.logf("tracker-settings",
                "Peak-First: minNoteDur=%.3f hopSec=%.3f slide=%.1f bend=%.1f "
                "analysisDelay=%.3f alpha=%.3f entryPeak=%.2f exitRms=%.3f",
                cfg.minNoteDurSec, cfg.hopSec,
                cfg.slideDeltaCents, cfg.bendDeltaCents,
                TrackerConfig::kAnalysisDelay, TrackerConfig::kAlpha,
                TrackerConfig::kEntryPeakThreshold, TrackerConfig::kDefaultExitRms);
    for (int s = 0; s < 6; ++s) {
      logger.logf("tracker-settings",
                  "  S%d midi=%d  low=%.1fHz high=%.1fHz  "
                  "sens=%.3f retrig=%.2fx exit=%.4f legato=%d conf=%.2f",
                  s + 1,
                  tuning.stringMidi[static_cast<std::size_t>(s)],
                  trackerparams::lowCutMultiplier(s),
                  trackerparams::highCutMultiplier(s),
                  trackerparams::noteOnThreshold(s),
                  trackerparams::attackSensitivity(s),
                  trackerparams::active(NoteParameter::SustainTail, s, 0.02f),
                  trackerparams::repitchConfirmFrames(s),
                  trackerparams::pitchConfidence(s));
    }
  });
}

inline float energyToVelocity(float rmsVal) {
  return std::clamp(rmsVal * 12.0f, 0.0f, 1.0f);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Bandpass Filter
// ═══════════════════════════════════════════════════════════════════════════

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

  const float low  = std::max(1.f, lowCutHz);
  const float high = std::max(low + 10.f, highCutHz);

  // 2nd-order Butterworth highpass at 'low' Hz
  {
    const float w0    = 2.0f * float(M_PI) * low / sr;
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 * std::sqrt(2.0f) / 2.0f;
    const float a0    = 1.0f + alpha;
    hp_b0 =  (1.0f + cosw0) / (2.0f * a0);
    hp_b1 = -(1.0f + cosw0) / a0;
    hp_b2 =  (1.0f + cosw0) / (2.0f * a0);
    hp_a1 = (-2.0f * cosw0) / a0;
    hp_a2 =  (1.0f - alpha) / a0;
  }

  // 2nd-order Butterworth lowpass at 'high' Hz
  {
    const float w0    = 2.0f * float(M_PI) * high / sr;
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 * std::sqrt(2.0f) / 2.0f;
    const float a0    = 1.0f + alpha;
    lp_b0 = (1.0f - cosw0) / (2.0f * a0);
    lp_b1 = (1.0f - cosw0) / a0;
    lp_b2 = (1.0f - cosw0) / (2.0f * a0);
    lp_a1 = (-2.0f * cosw0) / a0;
    lp_a2 = (1.0f - alpha) / a0;
  }

  std::fprintf(stderr, "String %d bandpass: HPF=%.1fHz LPF=%.1fHz (sr=%.1f)\n",
               stringIdx + 1, lowCutHz, highCutHz, sr);
  SessionLogger::instance().logf("filter",
                                 "String %d bandpass: HPF=%.1fHz LPF=%.1fHz",
                                 stringIdx + 1, lowCutHz, highCutHz);
}

float StringTracker::BandpassFilter::process(float x) {
  // Highpass biquad (Direct Form I)
  const float hp_out = hp_b0 * x + hp_b1 * hp_x1 + hp_b2 * hp_x2
                     - hp_a1 * hp_y1 - hp_a2 * hp_y2;
  hp_x2 = hp_x1; hp_x1 = x;
  hp_y2 = hp_y1; hp_y1 = hp_out;

  // Lowpass biquad cascaded with HP output
  const float lp_out = lp_b0 * hp_out + lp_b1 * lp_x1 + lp_b2 * lp_x2
                     - lp_a1 * lp_y1 - lp_a2 * lp_y2;
  lp_x2 = lp_x1; lp_x1 = hp_out;
  lp_y2 = lp_y1; lp_y1 = lp_out;

  return lp_out;
}

// ═══════════════════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════════════════

StringTracker::StringTracker(int stringIdx,
                             const Tuning& t,
                             const TrackerConfig& c,
                             std::vector<NoteEvent>& sharedEvents,
                             std::vector<int>& activeIdx,
                             TabEngine& engine)
  : _s(stringIdx), _tuning(t), _cfg(c), _engine(engine),
    _events(sharedEvents), _activeIdx(activeIdx)
{
  logTrackerSettingsOnce(_tuning, _cfg);
  _filter.reset();
  _filteredScratch.reserve(2048);
}

StringTracker::~StringTracker() {
#ifdef HAVE_AUBIO
  if (_aubioPitch)   { del_aubio_pitch(_aubioPitch);  _aubioPitch = nullptr; }
  if (_aubioIn)      { del_fvec(_aubioIn);             _aubioIn = nullptr; }
  if (_aubioPitchOut){ del_fvec(_aubioPitchOut);       _aubioPitchOut = nullptr; }
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// Configure Processing — aubio pitch + bandpass (no aubio onset)
// ═══════════════════════════════════════════════════════════════════════════

void StringTracker::configureProcessing(float sr, int blockSamples) {
  if (sr <= 0.f || blockSamples <= 0)
    return;

  const std::uint64_t storeGen = trackerparams::settingsGeneration();
  const bool paramsChanged = (storeGen != _paramGeneration);
  const int desiredHop = std::max(64, blockSamples);
  if (!paramsChanged && std::fabs(sr - _currentSr) < 1e-3f && desiredHop == _hopSamples)
    return;

  _paramGeneration = storeGen;
  _currentSr = sr;
  _hopSamples = desiredHop;
  _currentHopSec = static_cast<float>(_hopSamples) / _currentSr;

  // FFT size — kept large for frequency accuracy (YIN autocorrelation)
  _fftSize = 1;
  const int fftTarget = std::max(_hopSamples * trackerparams::fftMultiple(_s),
                                 _hopSamples * 4);
  while (_fftSize < fftTarget)
    _fftSize <<= 1;

  // Bandpass filter for per-string frequency isolation
  const float lowCut  = std::max(20.f, trackerparams::lowCutMultiplier(_s));
  const float highCut = std::min(6000.f, trackerparams::highCutMultiplier(_s));
  _filter.configure(sr, lowCut, highCut, _s);

  _aubioReady = false;
#ifdef HAVE_AUBIO
  // Tear down previous
  if (_aubioPitch)    { del_aubio_pitch(_aubioPitch);  _aubioPitch = nullptr; }
  if (_aubioIn)       { del_fvec(_aubioIn);             _aubioIn = nullptr; }
  if (_aubioPitchOut) { del_fvec(_aubioPitchOut);       _aubioPitchOut = nullptr; }

  // Pitch detection only — onset uses Path A raw peak
  _aubioPitch = new_aubio_pitch("yinfast",
                                static_cast<uint_t>(_fftSize),
                                static_cast<uint_t>(_hopSamples),
                                static_cast<uint_t>(sr));
  _aubioIn       = new_fvec(static_cast<uint_t>(_hopSamples));
  _aubioPitchOut = new_fvec(1);

  if (_aubioPitch && _aubioIn && _aubioPitchOut) {
    aubio_pitch_set_unit(_aubioPitch, "Hz");
    aubio_pitch_set_silence(_aubioPitch, trackerparams::pitchSilenceDb(_s));
    aubio_pitch_set_tolerance(_aubioPitch, trackerparams::pitchTolerance(_s));
    _aubioReady = true;
    std::fprintf(stderr,
        "StringTracker[%d]: Peak-First init (hop=%d fft=%d sr=%.1f)\n",
        _s + 1, _hopSamples, _fftSize, sr);
  } else {
    std::fprintf(stderr,
        "StringTracker[%d]: aubio pitch init failed\n", _s + 1);
  }
#else
  if (!_warnedNoAubio) {
    std::fprintf(stderr,
        "StringTracker[%d]: aubio not available; detection disabled\n", _s + 1);
    _warnedNoAubio = true;
  }
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// Pitch Helpers
// ═══════════════════════════════════════════════════════════════════════════

int StringTracker::estimateMidi(float pitchHz) const {
  if (pitchHz <= 0.f)
    return -1;
  const int openMidi = _tuning.stringMidi[static_cast<std::size_t>(_s)];
  int midi = hzToMidi(pitchHz);
  midi = std::clamp(midi, openMidi, openMidi + 24);
  return midi;
}

int StringTracker::applyLowStringBias(int midi, float pitchHz, float envelopeRms) const {
  if (_s > 0 || midi < 0 || pitchHz <= 0.f)
    return midi;

  const int openMidi = _tuning.stringMidi[static_cast<std::size_t>(_s)];
  if (midi <= openMidi)
    return midi;

  const float openHz = midiToHz(openMidi);
  if (openHz <= 0.f)
    return midi;

  const float ratio = pitchHz / openHz;
  if (!std::isfinite(ratio) || ratio < 1.7f)
    return midi;

  const int harmonic = static_cast<int>(std::round(ratio));
  if (harmonic < 2 || harmonic > 4)
    return midi;

  const float harmonicError = std::fabs(ratio - static_cast<float>(harmonic));
  const float tolerance = 0.12f * static_cast<float>(harmonic);
  if (harmonicError > tolerance)
    return midi;

  // If MIDI already matches the pitch Hz, this is correct, not a harmonic error
  const int expectedMidi = hzToMidi(pitchHz);
  if (std::abs(midi - expectedMidi) <= 1)
    return midi;

  const float minEnv = trackerparams::noteOnThreshold(_s) * 0.65f;
  if (envelopeRms < minEnv)
    return midi;

  const float fundamentalHz = pitchHz / static_cast<float>(harmonic);
  const int candidateMidi = std::clamp(hzToMidi(fundamentalHz), openMidi, openMidi + 24);
  if (candidateMidi == openMidi && candidateMidi < midi)
    return candidateMidi;

  return midi;
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

  const float referenceHz = (_pitchConfidenceHz > 0.f)
      ? _pitchConfidenceHz : midiToHz(_pitchConfidenceMidi);
  const float centsDiff = std::fabs(centsBetween(pitchHz, referenceHz));

  if (midi == _pitchConfidenceMidi && centsDiff <= kPitchConfidenceMaxCents) {
    _pitchConfidenceFrames = std::min(_pitchConfidenceFrames + 1, 8);
    _pitchConfidenceHz = 0.8f * referenceHz + 0.2f * pitchHz;
  } else if (centsDiff <= kPitchConfidenceMaxCents * 0.6f) {
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
    _pitchHoldSilenceFrames = std::min(_pitchHoldSilenceFrames + 1,
                                       kPitchHoldReleaseFrames);
    if (_pitchHoldSilenceFrames >= kPitchHoldReleaseFrames)
      _pitchHoldMidi = -1;
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

// ═══════════════════════════════════════════════════════════════════════════
// Note Lifecycle
// ═══════════════════════════════════════════════════════════════════════════

void StringTracker::closeActiveNote(float tSec, const char* reason) {
  if (_activeIdx[_s] < 0 || _activeIdx[_s] >= static_cast<int>(_events.size()))
    return;

  auto& active = _events[static_cast<std::size_t>(_activeIdx[_s])];
  active.endSec = std::max(tSec, active.startSec + _cfg.minNoteDurSec);

  // Fire note-off callback only if note was actually CONFIRMED
  if (active.state == NoteEvent::AnalysisState::CONFIRMED) {
    _engine.onNoteOff(_s, active.fret);
    SessionLogger::instance().logf("detection",
        "[OFF]      S%d  F%-2d        RMS = %.4f  (%s)",
        _s + 1, active.fret, _currentRms, reason);
  }

  active.state = NoteEvent::AnalysisState::CLOSED;
  _activeIdx[_s] = -1;
  _releaseQuietFrames = 0;

  // Unfreeze noise floor → RELEASE → IDLE on next frame
  _noiseFloorState = NoiseFloorState::RELEASE;

  // Reset repitch state
  _repitchCandidateMidi = -1;
  _repitchStabilityCounter = 0;
  _repitchLastConfidence = 0.f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Main Processing — Peak-First Dual-Path Detection
// ═══════════════════════════════════════════════════════════════════════════

float StringTracker::lastPitchHz() const {
  return _lastFeaturePitchHz;
}

void StringTracker::processBlock(const float* samples, int n, float sr, float t0) {
  if (sr <= 0.f)
    return;

  configureProcessing(sr, n);

#ifndef HAVE_AUBIO
  (void)samples; (void)n; (void)sr; (void)t0;
  return;
#else
  if (!_aubioReady)
    return;

  if (!samples || n <= 0)
    return;

  // ── Quick block-level silence check ──────────────────────────────────
  {
    float blockPeak = 0.f;
    for (int i = 0; i < n; ++i)
      blockPeak = std::max(blockPeak, std::fabs(samples[i]));
    if (blockPeak < 1e-6f)
      return;
  }

  // ── Apply bandpass filter (for Path B RMS and pitch detection) ───────
  _filteredScratch.resize(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i)
    _filteredScratch[static_cast<std::size_t>(i)] = _filter.process(samples[i]);

  // ── Process in hop-sized frames ─────────────────────────────────────
  const int hop = _hopSamples;
  int offset = 0;

  while (offset < n) {
    const int frameLen = std::min(hop, n - offset);
    if (frameLen <= 0)
      break;

    const float* rawFrame  = samples + offset;
    const float* filtFrame = _filteredScratch.data() + offset;
    const float  frameSec  = t0 + (static_cast<float>(offset)
                               + 0.5f * static_cast<float>(frameLen)) / sr;

    // ══════════════════════════════════════════════════════════════════
    // PATH A: Peak Detection (raw signal — captures the pluck transient)
    // ══════════════════════════════════════════════════════════════════
    float framePeak = 0.f;
    for (int i = 0; i < frameLen; ++i)
      framePeak = std::max(framePeak, std::fabs(rawFrame[i]));
    _currentPeak = framePeak;

    // ══════════════════════════════════════════════════════════════════
    // PATH B: RMS Computation (filtered signal — clean energy measure)
    // ══════════════════════════════════════════════════════════════════
    float sumSq = 0.f;
    for (int i = 0; i < frameLen; ++i)
      sumSq += filtFrame[i] * filtFrame[i];
    const float frameRms = std::sqrt(sumSq / static_cast<float>(frameLen));
    _currentRms = frameRms;

    // ══════════════════════════════════════════════════════════════════
    // Pitch Detection (filtered signal → aubio YIN)
    // ══════════════════════════════════════════════════════════════════
    float detectedPitchHz = -1.f;
    float pitchConf = 0.f;

    if (_aubioPitch && _aubioIn && _aubioPitchOut) {
      // Gain-normalise filtered signal for pitch detector
      float filtPeak = 0.f;
      for (int i = 0; i < frameLen; ++i)
        filtPeak = std::max(filtPeak, std::fabs(filtFrame[i]));
      const float pitchGain = filtPeak > 1e-5f
          ? std::min(1.f, 0.45f / filtPeak) : 1.f;

      for (int i = 0; i < hop; ++i)
        _aubioIn->data[i] = (i < frameLen)
            ? filtFrame[i] * pitchGain : 0.f;

      aubio_pitch_do(_aubioPitch, _aubioIn, _aubioPitchOut);
      const float hz = fvec_get_sample(_aubioPitchOut, 0);
      pitchConf = aubio_pitch_get_confidence(_aubioPitch);

      if (hz > kMinPitchHz && hz < kMaxPitchHz)
        detectedPitchHz = hz;
    }

    // ── Pitch smoothing + MIDI estimation ──────────────────────────
    if (detectedPitchHz > 0.f) {
      detectedPitchHz = applyPitchMedian(detectedPitchHz);
      _lastFeaturePitchHz = detectedPitchHz;
    } else {
      _pitchMedianWindow.clear();
    }

    int midiCandidate = (detectedPitchHz > 0.f)
        ? estimateMidi(detectedPitchHz) : -1;

    // Apply harmonic-bias correction for low E string
    if (_s == 0 && midiCandidate > 0)
      midiCandidate = applyLowStringBias(midiCandidate,
                                          detectedPitchHz, frameRms);

    const bool pitchStable = updatePitchConfidence(midiCandidate,
                                                    detectedPitchHz);
    const int heldMidi = applyPitchHold(midiCandidate, pitchStable);
    _lastPitchConf = pitchConf;

    // ══════════════════════════════════════════════════════════════════
    // Noise Floor — leaky integrator (Section 8)
    //   IDLE / RELEASE : track    ACTIVE : frozen
    // ══════════════════════════════════════════════════════════════════
    if (_noiseFloorState != NoiseFloorState::ACTIVE) {
      _noiseFloor = (TrackerConfig::kAlpha * frameRms)
                  + ((1.f - TrackerConfig::kAlpha) * _noiseFloor);
      if (_noiseFloor < kNoiseFloorMin)
        _noiseFloor = kNoiseFloorMin;

      // RELEASE → IDLE once we've resumed tracking
      if (_noiseFloorState == NoiseFloorState::RELEASE)
        _noiseFloorState = NoiseFloorState::IDLE;
    }

    const float effectiveNoiseFloor =
        (_noiseFloorState == NoiseFloorState::ACTIVE)
            ? _cachedNoiseFloor : _noiseFloor;

    // ══════════════════════════════════════════════════════════════════
    // Dynamic Thresholds (Section 8)
    //   onsetThreshold  = noiseFloor + userSensitivityDelta (touchSensitivity)
    //   exitRms         = sustainTail
    //   retriggerGate   = currentRMS × attackResponse
    // ══════════════════════════════════════════════════════════════════
    const float userSensitivity = trackerparams::noteOnThreshold(_s);
    const float dynamicOnsetThreshold = effectiveNoiseFloor + userSensitivity;
    const float exitRms = trackerparams::active(NoteParameter::SustainTail,
                                                 _s, 0.02f);
    const float retriggerMultiplier = trackerparams::attackSensitivity(_s);
    const float minConfidence = trackerparams::pitchConfidence(_s);

    // Update UI / debug thresholds
    _lastThresholds.onsetPeakThreshold = dynamicOnsetThreshold;
    _lastThresholds.exitRmsThreshold   = exitRms;
    _lastThresholds.noiseFloor         = effectiveNoiseFloor;
    _lastThresholds.retriggerGate      = _currentRms * retriggerMultiplier;
    _lastThresholds.envelopeRms        = frameRms;
    _lastThresholds.envelopePeak       = framePeak;

    // ══════════════════════════════════════════════════════════════════
    // State Machine (Section 4 — Parallel Note Event Construction)
    // ══════════════════════════════════════════════════════════════════
    const bool hasActive =
        (_activeIdx[_s] >= 0
         && _activeIdx[_s] < static_cast<int>(_events.size()));

    if (!hasActive) {
      // ── IDLE: check for Path A onset ────────────────────────────
      if (framePeak > dynamicOnsetThreshold) {
        const float guardSec = trackerparams::triggerGuardMs(_s) / 1000.f;
        const bool guardOk  = (_lastOnsetSec < 0.f)
                            || ((frameSec - _lastOnsetSec) >= guardSec);

        if (guardOk) {
          // T=0ms (Scout): Path A triggered
          _noiseFloorState = NoiseFloorState::ACTIVE;
          _cachedNoiseFloor = _noiseFloor;

          NoteEvent ev;
          ev.stringIdx = _s;
          ev.state     = NoteEvent::AnalysisState::PENDING_ANALYSIS;
          ev.peakLevel = framePeak;
          ev.startSec  = frameSec;
          ev.endSec    = frameSec;
          {
            std::lock_guard<std::mutex> lock(_engine.getEventMutex());
            _events.push_back(ev);
            _activeIdx[_s] = static_cast<int>(_events.size() - 1);
          }

          _lastOnsetSec  = frameSec;
          _analysisFrameCount = 0;
          _pendingPeakLevel   = framePeak;
          _releaseQuietFrames = 0;
          _retriggerBlockUntilSec = frameSec + guardSec;

          SessionLogger::instance().logf("detection",
              "[SCOUT]    S%d         Peak = %.4f  >  Onset = %.4f  "
              "(floor=%.4f + sens=%.4f)",
              _s + 1, framePeak, dynamicOnsetThreshold,
              effectiveNoiseFloor, userSensitivity);
        }
      }

    } else {
      // ── Active note present ─────────────────────────────────────
      // Save index for safe access after potential push_back
      const int curIdx = _activeIdx[_s];
      auto& active = _events[static_cast<std::size_t>(curIdx)];
      active.endSec = frameSec;

      // ─────────────────────────────────────────────────────────────
      // PENDING_ANALYSIS: T+5-10ms (Analyst) — wait for pitch lock
      // ─────────────────────────────────────────────────────────────
      if (active.state == NoteEvent::AnalysisState::PENDING_ANALYSIS) {
        _analysisFrameCount++;
        const float elapsed = frameSec - active.startSec;

        if (elapsed >= TrackerConfig::kAnalysisDelay) {
          // Attempt pitch confirmation from Path B data
          if (heldMidi >= 0
              && (pitchStable || pitchConf >= minConfidence)) {
            const int fret = midiToFret(heldMidi,
                _tuning.stringMidi[static_cast<std::size_t>(_s)]);

            if (fret >= 0 && fret <= 24) {
              active.state    = NoteEvent::AnalysisState::CONFIRMED;
              active.midi     = heldMidi;
              active.fret     = fret;
              active.velocity = energyToVelocity(frameRms);

              _engine.onNoteOn(_s, fret, active.velocity);

              SessionLogger::instance().logf("detection",
                  "[ON]       S%d  F%-2d        RMS = %.4f  |  "
                  "Peak = %.4f  |  Pitch = %.1fHz  |  Delay = %.1fms",
                  _s + 1, fret, frameRms, active.peakLevel,
                  detectedPitchHz, elapsed * 1000.f);
            } else {
              closeActiveNote(frameSec, "invalid-fret");
            }
          } else if (elapsed > TrackerConfig::kAnalysisDelay
                               * kAnalysisTimeoutMult) {
            // Couldn't lock pitch within timeout — discard
            closeActiveNote(frameSec, "analysis-timeout");
          }
        }

      // ─────────────────────────────────────────────────────────────
      // CONFIRMED: T+Duration (Continuous) — sustain / retrigger / repitch
      // ─────────────────────────────────────────────────────────────
      } else if (active.state == NoteEvent::AnalysisState::CONFIRMED) {
        active.velocity = std::max(active.velocity,
                                   energyToVelocity(frameRms));
        const float noteAge = frameSec - active.startSec;

        // ── Retrigger: Path A detects new peak (Section 5) ────────
        const float retriggerGate = _currentRms * retriggerMultiplier;
        if (framePeak > retriggerGate
            && frameSec > _retriggerBlockUntilSec
            && noteAge  > _cfg.minNoteDurSec) {

          // Save values before push_back invalidates reference
          const int   oldFret = active.fret;
          const float activeRms = _currentRms;

          SessionLogger::instance().logf("detection",
              "[RETRIG]   S%d  F%-2d        Peak = %.4f  >  "
              "Gate = %.4f  (RMS=%.4f x %.1fx)",
              _s + 1, oldFret, framePeak, retriggerGate,
              activeRms, retriggerMultiplier);

          _engine.onNoteOff(_s, oldFret);
          active.state  = NoteEvent::AnalysisState::CLOSED;
          active.endSec = frameSec;

          // Push new PENDING_ANALYSIS event
          NoteEvent ev;
          ev.stringIdx = _s;
          ev.state     = NoteEvent::AnalysisState::PENDING_ANALYSIS;
          ev.peakLevel = framePeak;
          ev.startSec  = frameSec;
          ev.endSec    = frameSec;
          {
            std::lock_guard<std::mutex> lock(_engine.getEventMutex());
            _events.push_back(ev);
            _activeIdx[_s] = static_cast<int>(_events.size() - 1);
          }

          _lastOnsetSec       = frameSec;
          _analysisFrameCount = 0;
          _pendingPeakLevel   = framePeak;
          _releaseQuietFrames = 0;

          const float guardSec = trackerparams::triggerGuardMs(_s) / 1000.f;
          _retriggerBlockUntilSec = frameSec + guardSec;

          // Reset repitch
          _repitchCandidateMidi    = -1;
          _repitchStabilityCounter = 0;
          _repitchLastConfidence   = 0.f;

        // ── Repitch: Path B detects pitch change, Path A silent (Section 5)
        } else if (heldMidi >= 0 && pitchStable
                   && heldMidi != active.midi) {
          const int   confirmFrames = trackerparams::repitchConfirmFrames(_s);
          const float repitchMinConf = trackerparams::repitchMinConfidence(_s);
          const float guardSec = trackerparams::triggerGuardMs(_s) / 1000.f;
          const float timeSinceRepitch = (_lastRepitchSec > 0.f)
              ? (frameSec - _lastRepitchSec) : 999.f;

          if (noteAge > guardSec && timeSinceRepitch > guardSec) {
            if (_repitchCandidateMidi == heldMidi) {
              _repitchStabilityCounter++;
              _repitchLastConfidence = std::min(_repitchLastConfidence,
                                                pitchConf);
            } else {
              _repitchCandidateMidi    = heldMidi;
              _repitchStabilityCounter = 1;
              _repitchLastConfidence   = pitchConf;
            }

            // Confirm: stable for N frames, high confidence, Path A silent
            if (_repitchStabilityCounter >= confirmFrames
                && _repitchLastConfidence >= repitchMinConf
                && framePeak < dynamicOnsetThreshold) {

              // Save values before push_back invalidates reference
              const int   oldFret = active.fret;
              const int   oldMidi = active.midi;
              const float inheritVelocity = active.velocity;
              const float inheritPeak     = active.peakLevel;

              const int newFret = midiToFret(heldMidi,
                  _tuning.stringMidi[static_cast<std::size_t>(_s)]);

              if (newFret >= 0 && newFret <= 24) {
                _engine.onNoteOff(_s, oldFret);
                active.state  = NoteEvent::AnalysisState::CLOSED;
                active.endSec = frameSec;

                NoteEvent ev;
                ev.stringIdx = _s;
                ev.state     = NoteEvent::AnalysisState::CONFIRMED;
                ev.fret      = newFret;
                ev.midi      = heldMidi;
                ev.startSec  = frameSec;
                ev.endSec    = frameSec;
                ev.velocity  = inheritVelocity;
                ev.peakLevel = inheritPeak;
                {
                  std::lock_guard<std::mutex> lock(_engine.getEventMutex());
                  _events.push_back(ev);
                  _activeIdx[_s] = static_cast<int>(_events.size() - 1);
                }

                _engine.onNoteOn(_s, newFret, ev.velocity);
                _lastOnsetSec  = frameSec;
                _lastRepitchSec = frameSec;

                const int pitchDelta = std::abs(heldMidi - oldMidi);
                SessionLogger::instance().logf("detection",
                    "[REPITCH]  S%d  F%-2d->F%-2d   "
                    "Pitch = %.2f Hz  |  Conf = %.2f  |  Delta = %d st",
                    _s + 1, oldFret, newFret,
                    detectedPitchHz, _repitchLastConfidence, pitchDelta);
              }

              _repitchCandidateMidi    = -1;
              _repitchStabilityCounter = 0;
              _repitchLastConfidence   = 0.f;
            }
          }
        } else {
          // Pitch matches active note or no stable pitch — reset repitch
          _repitchCandidateMidi    = -1;
          _repitchStabilityCounter = 0;
          _repitchLastConfidence   = 0.f;
        }

        // ── Termination: Path B RMS below exit threshold (Section 6) ──
        // Re-check active note (may have changed after retrigger/repitch)
        if (_activeIdx[_s] >= 0
            && _activeIdx[_s] < static_cast<int>(_events.size())) {
          const auto& current =
              _events[static_cast<std::size_t>(_activeIdx[_s])];
          if (current.state == NoteEvent::AnalysisState::CONFIRMED
              && (frameSec - current.startSec) > _cfg.minNoteDurSec) {
            if (frameRms < exitRms) {
              _releaseQuietFrames++;
              if (_releaseQuietFrames >= kReleaseQuietFrameCount) {
                closeActiveNote(frameSec, "sustained-quiet");
              }
            } else {
              _releaseQuietFrames = 0;
            }
          }
        }
      } // end CONFIRMED
    } // end hasActive

    offset += frameLen;
  } // end hop loop
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// Reset
// ═══════════════════════════════════════════════════════════════════════════

void StringTracker::resetState() {
  // Path A
  _currentPeak  = 0.f;
  _lastOnsetSec = -1.f;

  // Path B
  _currentRms        = 0.f;
  _releaseQuietFrames = 0;

  // Noise floor
  _noiseFloorState  = NoiseFloorState::IDLE;
  _noiseFloor       = 0.001f;
  _cachedNoiseFloor = 0.001f;

  // Analysis delay
  _analysisFrameCount = 0;
  _pendingPeakLevel   = 0.f;

  // Pitch
  _lastFeaturePitchHz   = -1.f;
  _pitchConfidenceHz    = -1.f;
  _pitchConfidenceMidi  = -1;
  _pitchConfidenceFrames = 0;
  _pitchHoldMidi         = -1;
  _pitchHoldPendingMidi  = -1;
  _pitchHoldPendingFrames = 0;
  _pitchHoldSilenceFrames = 0;
  _pitchMedianWindow.clear();
  _lastPitchConf = 0.f;

  // Retrigger
  _retriggerBlockUntilSec = 0.f;

  // Repitch
  _lastRepitchSec          = -1.f;
  _repitchCandidateMidi    = -1;
  _repitchStabilityCounter = 0;
  _repitchLastConfidence   = 0.f;

  // Processing
  _filter.reset();
  _filteredScratch.clear();
  _currentSr   = 0.f;
  _hopSamples  = 0;
  _fftSize     = 0;
  _currentHopSec = 0.f;
  _aubioReady  = false;

#ifdef HAVE_AUBIO
  if (_aubioIn) {
    for (uint_t i = 0; i < _aubioIn->length; ++i)
      _aubioIn->data[i] = 0.f;
  }
#endif
}
