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

class StringTracker {
public:
  // events/out indices are shared with TabEngine so we can open/close notes centrally
  StringTracker(int stringIdx,
                const Tuning& tuning,
                const TrackerConfig& cfg,
                std::vector<NoteEvent>& sharedEvents,
                std::vector<int>& activeIdx);
  ~StringTracker();

  // mono samples may be nullptr => treat as silence
  void processBlock(const float* samples, int n, float sr, float blockStartSec);
  void resetState();
  void setCalibration(const CalibrationProfile& profile);
  float lastPitchHz() const;
  float calibrationGain() const { return _calibrationGain; }
  // void setCalibrationGain(float gain);  // Legacy - unused

private:
  void configureProcessing(float sr, int blockSamples);
  void updateFeatures(const float* samples, int n, float sr, float t0);
  bool detectOnset(std::size_t frameIdx);
  int  estimateMidi(const FrameFeatures& frame) const;
  int  applyLowStringBias(int midi, const FrameFeatures& frame) const;
  bool noteShouldClose(std::size_t frameIdx) const;
  float applyPitchMedian(float pitchHz);
  bool updatePitchConfidence(int midi, float pitchHz);
  int  applyPitchHold(int midi, bool stable);
  void refreshCalibrationTarget();

  struct BandpassFilter {
    float hpAlpha = 0.f;
    float lpBeta = 0.f;
    float hpState = 0.f;
    float hpPrevInput = 0.f;
    float lpState = 0.f;
    void reset();
    void configure(float sr, float lowCutHz, float highCutHz, int stringIdx);
    float process(float x);
  };

  int _s = 0;
  const Tuning& _tuning;
  const TrackerConfig& _cfg;
  std::deque<FrameFeatures> _feat; // rolling ~500ms
  std::vector<NoteEvent>& _events;
  std::vector<int>& _activeIdx;    // per-string active idx reference

  float _lastOnsetPeakRms = 0.f;
  float _lastOnsetSec = -1.f;
  float _currentSr = 0.f;
  int   _hopSamples = 0;
  int   _fftSize = 0;
  float _currentHopSec = 0.f;
  std::uint64_t _paramGeneration = 0;
  BandpassFilter _filter;
  std::vector<float> _filteredScratch;
  bool _aubioReady = false;
  bool _onsetLatched = false;
  float _pitchConfidenceHz = -1.f;
  int   _pitchConfidenceMidi = -1;
  int   _pitchConfidenceFrames = 0;
  int   _pitchHoldMidi = -1;
  int   _pitchHoldPendingMidi = -1;
  int   _pitchHoldPendingFrames = 0;
  int   _pitchHoldSilenceFrames = 0;
  float _envAdaptiveRms = 0.001f;
  mutable int _releaseQuietFrames = 0;
  float _activeHoldUntilSec = 0.f;
  float _retriggerBlockUntilSec = 0.f;
  bool _activeForcedOpen = false;
  float _calibrationAvgRms = 0.001f;
  float _calibrationGain = 1.f;
  float _calibrationTargetRms = 0.0018f;
  bool  _calibrationValid = false;
  float _lastFeaturePitchHz = -1.f;
#ifndef HAVE_AUBIO
  bool _warnedNoAubio = false;
#endif
  std::deque<float> _pitchMedianWindow;

#ifdef HAVE_AUBIO
  aubio_onset_t* _aubioOnset = nullptr;
  aubio_pitch_t* _aubioPitch = nullptr;
  fvec_t*        _aubioIn = nullptr;
  fvec_t*        _aubioOnsetOut = nullptr;
  fvec_t*        _aubioPitchOut = nullptr;
#endif
};
