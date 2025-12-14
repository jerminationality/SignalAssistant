#pragma once
#include <array>
#include <string>
#include <vector>

struct Tuning {
  // Low E (string 0) .. High E (string 5)
  std::array<int, 6> stringMidi {40, 45, 50, 55, 59, 64}; // E2 A2 D3 G3 B3 E4
};

struct NoteEvent {
  int         stringIdx = -1;     // 0..5
  int         fret      = -1;     // 0..24
  int         midi      = -1;     // absolute MIDI pitch
  float       startSec  = 0.f;    // seconds
  float       endSec    = 0.f;    // seconds (filled on close)
  float       velocity  = 0.f;    // 0..1 (relative)
  std::string articulation;       // "", "slide", "bend", "hammer", "pull", "pm"
};

struct TrackerConfig {
  float onsetThreshold   = 0.020f;
  float minNoteDurSec    = 0.045f;
  float hopSec           = 0.010f; // 10 ms
  float slideDeltaCents  = 120.f;  // >120c over ~60ms => slide
  float bendDeltaCents   = 35.f;   // >35c sustained => bend
};

struct CalibrationProfile {
  std::array<float, 6> avgRms {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
  std::array<float, 6> peakRms {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
  std::array<float, 6> multipliers {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  bool valid = false;
};

struct FrameFeatures {
  float tSec = 0.f;
  float pitchHz = -1.f;
  float pitchCents = 0.f;
  float onsetStrength = 0.f;
  float envelopeRms = 0.f;
};

class StringTracker; // fwd

class TabEngine {
public:
  TabEngine(const Tuning& t, const TrackerConfig& c);
  ~TabEngine();
  TabEngine(const TabEngine&) = delete;
  TabEngine& operator=(const TabEngine&) = delete;
  // channels[s] points to mono float buffer for string s; nullptr => silence
  void processBlock(const float* const channels[6], int n, float sr, float t0);

  const std::vector<NoteEvent>& events() const { return _events; }
  std::string toJson(bool onlyFinished=true) const;
  void importEvents(const std::vector<NoteEvent>& events);
  void applyCalibration(const CalibrationProfile& profile);
  std::array<float, 6> tuningDeviationCents() const;
  std::array<float, 6> calibrationGains() const;
  void setCalibrationGain(int stringIndex, float gain);

private:
  void fuseEvents(float t0); // TODO(Copilot): rules (hammer/pull/slide/bend/pm)

  Tuning _tuning;
  TrackerConfig _cfg;
  CalibrationProfile _calibration;
  std::vector<NoteEvent> _events;
  std::vector<int> _activeIdx; // per-string active event index or -1
  std::vector<StringTracker*> _trkPtrs; // owned
};
