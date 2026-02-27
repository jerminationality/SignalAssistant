#pragma once
#include <array>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// ── Peak-First Architecture: Dual-Path Detection (Sections 3-6) ─────────

struct StringThresholds {
  float onsetPeakThreshold {0.f};    // Path A: dynamic onset = noiseFloor + userSensitivityDelta
  float exitRmsThreshold {0.f};      // Path B: RMS exit threshold for note-off
  float noiseFloor {0.f};            // Adaptive noise floor (leaky integrator)
  float retriggerGate {0.f};         // NewPeak must exceed CurrentRMS * retriggerMultiplier
  float envelopeRms {0.f};           // Current RMS envelope
  float envelopePeak {0.f};          // Current instantaneous peak
};

struct Tuning {
  std::array<int, 6> stringMidi {40, 45, 50, 55, 59, 64}; // E2 A2 D3 G3 B3 E4
};

// ── Note Event with Analysis State Machine (Section 4) ──────────────────

struct NoteEvent {
  enum class AnalysisState {
    PENDING_ANALYSIS,  // T=0: Path A triggered, waiting for pitch lock
    CONFIRMED,         // T+5-10ms: Path B confirmed pitch & velocity
    CLOSED             // Note terminated (RMS fell below exit threshold)
  };

  int           stringIdx = -1;
  int           fret      = -1;
  int           midi      = -1;
  float         startSec  = 0.f;
  float         endSec    = 0.f;
  float         velocity  = 0.f;     // Derived from Path B RMS analysis
  float         peakLevel = 0.f;     // Path A: instantaneous peak at onset
  AnalysisState state     = AnalysisState::PENDING_ANALYSIS;
  std::string   articulation;        // "", "slide", "bend", "hammer", "pull", "pm"
};

struct TrackerConfig {
  // System-managed constants (Section 9B)
  static constexpr int   kBufferHop     = 128;    // 2.7ms at 48kHz
  static constexpr int   kWindowFrame   = 512;    // 10.6ms at 48kHz
  static constexpr float kAlpha         = 0.01f;  // Leaky integrator speed
  static constexpr float kAnalysisDelay = 0.010f;  // 10ms pick-noise settling
  static constexpr float kHysteresisWindow = 0.020f; // 20ms velocity averaging

  // Entry/exit thresholds (Section 6)
  static constexpr float kEntryPeakThreshold = 0.1f;  // Path A: 0.1 peak
  static constexpr float kDefaultExitRms     = 0.02f; // Path B: 0.02 RMS

  // Calibration target (Section 2)
  static constexpr float kCalibrationPeakTarget = 0.8f;

  float onsetThreshold   = 0.020f;
  float minNoteDurSec    = 0.045f;
  float hopSec           = 0.010f;
  float slideDeltaCents  = 120.f;
  float bendDeltaCents   = 35.f;
};

struct CalibrationProfile {
  std::array<float, 6> avgRms {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
  std::array<float, 6> peakLevel {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
  std::array<float, 6> multipliers {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  bool valid = false;
};

struct FrameFeatures {
  float tSec = 0.f;
  float pitchHz = -1.f;
  float pitchCents = 0.f;
  float pitchConfidence = 0.f;
  float onsetStrength = 0.f;
  float envelopeRms = 0.f;
  float instantPeak = 0.f;          // Path A: per-frame instantaneous peak
  float crestFactor = 0.f;          // peak / RMS ratio
};

class StringTracker;

using NoteEventCallback = std::function<void(bool isNoteOn, int stringIdx, int fret, float velocity)>;

class TabEngine {
public:
  TabEngine(const Tuning& t, const TrackerConfig& c);
  ~TabEngine();
  TabEngine(const TabEngine&) = delete;
  TabEngine& operator=(const TabEngine&) = delete;

  void processBlock(const float* const channels[6], int n, float sr, float t0);

  const std::vector<NoteEvent>& events() const { return _events; }
  std::string toJson(bool onlyFinished=true) const;
  void importEvents(const std::vector<NoteEvent>& events);
  std::array<float, 6> tuningDeviationCents() const;
  std::array<StringThresholds, 6> getThresholds() const;

  void setNoteEventCallback(NoteEventCallback cb) { _noteCallback = std::move(cb); }
  
  void onNoteOn(int stringIdx, int fret, float velocity);
  void onNoteOff(int stringIdx, int fret);

  std::mutex& getEventMutex() { return _eventMutex; }

private:
  void fuseEvents(float t0);

  Tuning _tuning;
  TrackerConfig _cfg;
  std::vector<NoteEvent> _events;
  std::vector<int> _activeIdx;
  mutable std::mutex _eventMutex;
  std::vector<StringTracker*> _trkPtrs;
  NoteEventCallback _noteCallback;
  std::array<bool, 6> _crosstalkMask {true, true, true, true, true, true};
};
