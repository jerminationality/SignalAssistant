#pragma once
#include "TabEngine.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#ifdef HAVE_AUBIO
extern "C" {
#include <aubio/aubio.h>
}
#endif

// ═══════════════════════════════════════════════════════════════════════════
// Peak-First Dual-Path Detection Architecture (Sections 3-8)
//
//   Path A  (Peak/Transient) — raw signal peak → onset/retrigger
//   Path B  (RMS/Body)       — filtered RMS    → velocity/sustain/note-off
//
//   Noise floor: leaky integrator, frozen while note ACTIVE, resumes on RELEASE
//   State flow:  PENDING_ANALYSIS → CONFIRMED → CLOSED
// ═══════════════════════════════════════════════════════════════════════════

class StringTracker {
public:
  // events/activeIdx are shared with TabEngine for centralised note management
  StringTracker(int stringIdx,
                const Tuning& tuning,
                const TrackerConfig& cfg,
                std::vector<NoteEvent>& sharedEvents,
                std::vector<int>& activeIdx,
                TabEngine& engine);
  ~StringTracker();

  // mono samples may be nullptr → treat as silence
  void processBlock(const float* samples, int n, float sr, float blockStartSec);
  void resetState();
  float lastPitchHz() const;
  StringThresholds getThresholds() const { return _lastThresholds; }

private:
  // ─── Processing Pipeline ─────────────────────────────────────────────
  void configureProcessing(float sr, int blockSamples);

  // ─── Pitch Helpers ───────────────────────────────────────────────────
  int   estimateMidi(float pitchHz) const;
  int   applyLowStringBias(int midi, float pitchHz, float envelopeRms) const;
  float applyPitchMedian(float pitchHz);
  bool  updatePitchConfidence(int midi, float pitchHz);
  int   applyPitchHold(int midi, bool stable);

  // ─── Note Lifecycle ──────────────────────────────────────────────────
  void closeActiveNote(float tSec, const char* reason);

  // ─── Bandpass Filter (2nd-order Butterworth HP + LP cascade) ─────────
  struct BandpassFilter {
    float hp_b0 = 0.f, hp_b1 = 0.f, hp_b2 = 0.f;
    float hp_a1 = 0.f, hp_a2 = 0.f;
    float hp_x1 = 0.f, hp_x2 = 0.f;
    float hp_y1 = 0.f, hp_y2 = 0.f;

    float lp_b0 = 0.f, lp_b1 = 0.f, lp_b2 = 0.f;
    float lp_a1 = 0.f, lp_a2 = 0.f;
    float lp_x1 = 0.f, lp_x2 = 0.f;
    float lp_y1 = 0.f, lp_y2 = 0.f;

    void reset();
    void configure(float sr, float lowCutHz, float highCutHz, int stringIdx);
    float process(float x);
  };

  // ─── Noise Floor State Machine (Section 8) ──────────────────────────
  enum class NoiseFloorState { IDLE, ACTIVE, RELEASE };

  // ─── Core State ──────────────────────────────────────────────────────
  int _s = 0;
  const Tuning& _tuning;
  const TrackerConfig& _cfg;
  TabEngine& _engine;
  std::vector<NoteEvent>& _events;
  std::vector<int>& _activeIdx;

  // Processing configuration
  float _currentSr = 0.f;
  int   _hopSamples = 0;
  int   _fftSize = 0;
  float _currentHopSec = 0.f;
  std::uint64_t _paramGeneration = 0;
  BandpassFilter _filter;
  std::vector<float> _filteredScratch;
  bool _aubioReady = false;

  // ─── Path A: Peak / Transient ────────────────────────────────────────
  float _currentPeak = 0.f;
  float _lastOnsetSec = -1.f;

  // ─── Path B: RMS / Body ──────────────────────────────────────────────
  float _currentRms = 0.f;
  int   _releaseQuietFrames = 0;

  // ─── Noise Floor ────────────────────────────────────────────────────
  NoiseFloorState _noiseFloorState = NoiseFloorState::IDLE;
  float _noiseFloor = 0.001f;
  float _cachedNoiseFloor = 0.001f;

  // ─── Analysis Delay (PENDING_ANALYSIS → CONFIRMED timing) ───────────
  int   _analysisFrameCount = 0;
  float _pendingPeakLevel = 0.f;

  // ─── Pitch State ────────────────────────────────────────────────────
  float _lastFeaturePitchHz = -1.f;
  float _pitchConfidenceHz = -1.f;
  int   _pitchConfidenceMidi = -1;
  int   _pitchConfidenceFrames = 0;
  int   _pitchHoldMidi = -1;
  int   _pitchHoldPendingMidi = -1;
  int   _pitchHoldPendingFrames = 0;
  int   _pitchHoldSilenceFrames = 0;
  std::deque<float> _pitchMedianWindow;
  float _lastPitchConf = 0.f;

  // ─── Retrigger ──────────────────────────────────────────────────────
  float _retriggerBlockUntilSec = 0.f;

  // ─── Repitch ────────────────────────────────────────────────────────
  float _lastRepitchSec = -1.f;
  int   _repitchCandidateMidi = -1;
  int   _repitchStabilityCounter = 0;
  float _repitchLastConfidence = 0.f;

  // ─── UI / Debug ─────────────────────────────────────────────────────
  mutable StringThresholds _lastThresholds;

#ifndef HAVE_AUBIO
  bool _warnedNoAubio = false;
#endif

#ifdef HAVE_AUBIO
  // Pitch detection only — onset detection uses Path A peak, not aubio
  aubio_pitch_t* _aubioPitch = nullptr;
  fvec_t*        _aubioIn = nullptr;
  fvec_t*        _aubioPitchOut = nullptr;
#endif
};
