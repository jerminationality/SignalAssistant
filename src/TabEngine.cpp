#include "TabEngine.h"
#include "StringTracker.h"
#include "SessionLogger.h"
#include "util.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace {
// ── Relative-amplitude crosstalk masking constants ────────────────────
constexpr float kCrosstalkThreshold = 0.15f;  // 15% of primary string's RMS
constexpr float kHysteresisBuffer   = 0.05f;  // Prevents chatter at borderline levels
constexpr float kMinGateLevel       = 0.01f;  // Absolute silence floor (ignore hum)
} // namespace

TabEngine::TabEngine(const Tuning& t, const TrackerConfig& c)
: _tuning(t), _cfg(c), _activeIdx(6, -1)
{
  _trkPtrs.reserve(6);
  for (int s = 0; s < 6; ++s) {
    auto* tracker = new StringTracker(s, _tuning, _cfg, _events, _activeIdx, *this);
    _trkPtrs.push_back(tracker);
  }
}

TabEngine::~TabEngine() {
  for (auto* ptr : _trkPtrs) {
    delete ptr;
  }
  _trkPtrs.clear();
}

void TabEngine::processBlock(const float* const channels[6], int n, float sr, float t0) {
  // ── Crosstalk masking: compute per-string RMS and suppress bleed ────
  std::array<float, 6> amplitudes{};
  for (int s = 0; s < 6; ++s) {
    if (!channels[s] || n <= 0) {
      amplitudes[s] = 0.f;
      continue;
    }
    float sumSq = 0.f;
    for (int i = 0; i < n; ++i) {
      const float v = channels[s][i];
      sumSq += v * v;
    }
    amplitudes[s] = std::sqrt(sumSq / static_cast<float>(n));
  }

  // Identify the primary (loudest) string
  float maxVal = 0.f;
  int primaryString = -1;
  for (int s = 0; s < 6; ++s) {
    if (amplitudes[s] > maxVal) {
      maxVal = amplitudes[s];
      primaryString = s;
    }
  }

  // Build mask: true = pass audio, false = suppress (send nullptr)
  std::array<bool, 6> mask{};
  if (maxVal < kMinGateLevel) {
    // Nobody playing — silence all
    mask.fill(false);
  } else {
    const float dynamicFloor = maxVal * kCrosstalkThreshold;
    for (int s = 0; s < 6; ++s) {
      if (s == primaryString) {
        mask[s] = true;
      } else if (amplitudes[s] > (dynamicFloor + kHysteresisBuffer)) {
        mask[s] = true;   // Legitimate chord note
      } else if (amplitudes[s] < (dynamicFloor - kHysteresisBuffer)) {
        mask[s] = false;  // Crosstalk bleed
      } else {
        // Inside hysteresis band — preserve previous state
        mask[s] = _crosstalkMask[s];
      }
    }
  }
  _crosstalkMask = mask;

  for (int s = 0; s < 6; ++s) {
    // Never mask a string that has an active note — the tracker must keep
    // processing audio so it can detect envelope decay and fire note-off.
    const bool hasActiveNote = (_activeIdx[s] >= 0
                                && _activeIdx[s] < static_cast<int>(_events.size()));
    const bool passAudio = mask[s] || hasActiveNote;
    _trkPtrs[s]->processBlock(passAudio ? channels[s] : nullptr, n, sr, t0);
  }
  fuseEvents(t0);
}

void TabEngine::fuseEvents(float /*t0*/) {
  std::array<int, 6> lastFinished{};
  lastFinished.fill(-1);

  const int total = static_cast<int>(_events.size());
  for (int i = 0; i < total; ++i) {
    auto& ev = _events[i];
    if (ev.stringIdx < 0 || ev.stringIdx >= 6)
      continue;

    const bool finished = ev.endSec > ev.startSec;
    if (!finished)
      continue;

    const int prevIdx = lastFinished[ev.stringIdx];
    if (prevIdx >= 0 && prevIdx < total) {
      auto& prev = _events[prevIdx];
      if (prev.endSec > prev.startSec) {
        const float gap = ev.startSec - prev.endSec;
        if (gap >= 0.f && gap < 0.12f) {
          const int delta = ev.fret - prev.fret;
          const int absDelta = delta >= 0 ? delta : -delta;

          if (absDelta >= 2) {
            if (ev.articulation.empty())
              ev.articulation = "slide";
            if (prev.articulation.empty())
              prev.articulation = "slide";
          } else if (delta == 1 || delta == 2) {
            if (ev.articulation.empty())
              ev.articulation = "hammer";
          } else if (delta == -1 || delta == -2) {
            if (ev.articulation.empty())
              ev.articulation = "pull";
          } else if (absDelta == 0 && gap < 0.06f) {
            if (ev.velocity < prev.velocity * 0.7f && ev.articulation.empty())
              ev.articulation = "pm";
          }
        }
      }
    }

    if (ev.articulation.empty()) {
      const float duration = ev.endSec - ev.startSec;
      if (duration < 0.18f && ev.velocity < 0.30f)
        ev.articulation = "pm";
    }

    lastFinished[ev.stringIdx] = i;
  }
}

void TabEngine::importEvents(const std::vector<NoteEvent>& events) {
  {
    std::lock_guard<std::mutex> lock(_eventMutex);
    _events = events;
  }
  std::fill(_activeIdx.begin(), _activeIdx.end(), -1);
  if (events.empty()) {
    for (auto* trk : _trkPtrs) {
      if (trk)
        trk->resetState();
    }
  }
}

// applyCalibration / calibrationGains / setCalibrationGain removed —
// calibration is applied to raw audio upstream (HexJackClient / RecordedSessionPlayer)
// before it reaches the engine.  The engine only sees calibrated samples.

std::array<float, 6> TabEngine::tuningDeviationCents() const {
  std::array<float, 6> deviations{};
  for (int s = 0; s < 6; ++s) {
    const auto* tracker = _trkPtrs[static_cast<std::size_t>(s)];
    if (!tracker)
      continue;
    const float pitchHz = tracker->lastPitchHz();
    const float targetHz = midiToHz(_tuning.stringMidi[static_cast<std::size_t>(s)]);
    if (pitchHz > 0.f && targetHz > 0.f) {
      deviations[static_cast<std::size_t>(s)] = centsBetween(pitchHz, targetHz);
    }
  }
  return deviations;
}

std::string TabEngine::toJson(bool onlyFinished) const {
  std::ostringstream oss;
  oss << "[";
  {
    std::lock_guard<std::mutex> lock(_eventMutex);
    bool first = true;
    for (const auto& e : _events) {
      if (onlyFinished && e.endSec <= e.startSec) continue;
      if (!first) oss << ",";
      first = false;
      oss << "{"
          << "\"string\":" << e.stringIdx
          << ",\"fret\":" << e.fret
          << ",\"midi\":" << e.midi
          << ",\"start\":" << std::fixed << std::setprecision(6) << e.startSec
          << ",\"end\":"   << std::fixed << std::setprecision(6) << e.endSec
          << ",\"vel\":"   << std::fixed << std::setprecision(3) << e.velocity
          << ",\"art\":\"" << e.articulation << "\""
          << "}";
    }
  }
  oss << "]";
  return oss.str();
}

std::array<StringThresholds, 6> TabEngine::getThresholds() const {
  std::array<StringThresholds, 6> result;
  for (std::size_t i = 0; i < _trkPtrs.size(); ++i) {
    if (_trkPtrs[i]) {
      result[i] = _trkPtrs[i]->getThresholds();
    }
  }
  return result;
}

void TabEngine::onNoteOn(int stringIdx, int fret, float velocity) {
  if (_noteCallback) {
    _noteCallback(true, stringIdx, fret, velocity);
  }
}

void TabEngine::onNoteOff(int stringIdx, int fret) {
  if (_noteCallback) {
    _noteCallback(false, stringIdx, fret, 0.f);
  }
}
