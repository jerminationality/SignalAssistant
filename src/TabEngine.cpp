#include "TabEngine.h"
#include "StringTracker.h"
#include "util.h"
#include <algorithm>
#include <array>
#include <sstream>
#include <iomanip>

TabEngine::TabEngine(const Tuning& t, const TrackerConfig& c)
: _tuning(t), _cfg(c), _activeIdx(6, -1)
{
  _trkPtrs.reserve(6);
  for (int s = 0; s < 6; ++s) {
    auto* tracker = new StringTracker(s, _tuning, _cfg, _events, _activeIdx);
    tracker->setCalibration(_calibration);
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
  for (int s = 0; s < 6; ++s) {
    _trkPtrs[s]->processBlock(channels[s], n, sr, t0);
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
  _events = events;
  std::fill(_activeIdx.begin(), _activeIdx.end(), -1);
  if (events.empty()) {
    for (auto* trk : _trkPtrs) {
      if (trk)
        trk->resetState();
    }
  }
}

void TabEngine::applyCalibration(const CalibrationProfile& profile) {
  _calibration = profile;
  for (auto* trk : _trkPtrs) {
    if (trk)
      trk->setCalibration(profile);
  }
}

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
  oss << "]";
  return oss.str();
}
