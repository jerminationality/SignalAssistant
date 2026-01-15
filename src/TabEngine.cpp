#include "TabEngine.h"
#include "CQT/CQTNoteDetector.h"
#include "NoteDetectionStore.h"
#include "SessionLogger.h"
#include "util.h"
#include <algorithm>
#include <array>
#include <sstream>
#include <iomanip>
#include <cmath>

// CRITICAL: SessionLogger causes deadlocks in real-time audio thread
// Only enable for offline debugging with recorded sessions
#define ENABLE_TAB_RT_LOGGING 1  // Enable detailed note event logging

TabEngine::TabEngine(const Tuning& t, const TrackerConfig& c)
: _tuning(t), _cfg(c), _activeIdx(6, -1)
{
  // Initialize CQT detector with default sample rate (will be updated on first processBlock)
  _cqtDetector = std::make_unique<CQTNoteDetector>(44100.0);
  
  // Initialize string states
  for (auto& state : _cqtStates) {
    state = StringCQTState{};
  }
}

TabEngine::~TabEngine() = default;

void TabEngine::processBlock(const float* const channels[6], int n, float sr, float t0) {
  if (!_cqtDetector || n <= 0 || sr <= 0.f) {
    return;
  }
  
  // Update CQT sample rate if it changed
  _cqtDetector->setSampleRate(static_cast<double>(sr));
  
  // Get current detection parameters from the store
  auto& store = NoteDetectionStore::instance();
  const auto& params = store.current();
  
  // Build DetectionParams for CQT from NoteDetectionParameterSet
  std::vector<DetectionParams> cqtParams;
  cqtParams.reserve(6);
  
  for (int s = 0; s < 6; ++s) {
    DetectionParams dp;
    dp.baseline = params.baselineFloor[s];
    dp.envFloor = params.envelopeFloor[s];
    
    // Two-stage gain system:
    // - preAmpGain: UI-adjustable (from calibrationGainMultiplier slider)
    // - spatialWeight: Calibration-fixed fairness multiplier for crosstalk
    dp.preAmpGain = params.calibrationGainMultiplier[s];
    
    // spatialWeight derived from calibration profile
    // If calibration is valid, use the inverse of the measured RMS ratios
    // Otherwise default to 1.0 (no weighting)
    if (_calibration.valid && _calibration.avgRms[s] > 0.0001f) {
      // Calculate weight to normalize this string relative to strongest string
      float maxRms = *std::max_element(_calibration.avgRms.begin(), _calibration.avgRms.end());
      dp.spatialWeight = (maxRms > 0.0001f) ? (maxRms / _calibration.avgRms[s]) : 1.0f;
    } else {
      dp.spatialWeight = 1.0f;
    }
    
    dp.gateRatio = params.gateRatio[s];
    dp.confirmationFrames = params.confirmationFrames[s];
    dp.fluxSensitivity = params.fluxSensitivity[s];
    dp.slopeDecay = params.slopeDecay[s];
    cqtParams.push_back(dp);
  }
  
  // Convert channels to non-const for CQT API (CQT doesn't modify them)
  float* hexBuffers[6];
  for (int s = 0; s < 6; ++s) {
    hexBuffers[s] = const_cast<float*>(channels[s]);
  }
  
  // Process all 6 strings in unified CQT pass
  std::vector<GuitarFrame> frames = _cqtDetector->process(hexBuffers, n, cqtParams);
  
  // Convert GuitarFrame results to NoteEvents
  for (const auto& frame : frames) {
    const int s = frame.stringID;
    if (s < 0 || s >= 6) continue;
    
    auto& state = _cqtStates[s];
    state.lastRms = frame.rmsAmplitude;
    
    // Update pitch tracking using detected pitch from CQT (for tuning mode)
    if (frame.pitchHz > 0.0f) {
      state.lastPitchHz = frame.pitchHz;
    }
    
    // Debug: Log CQT frame data for note detection debugging (DISABLED - causes XRuns)
    // if (frame.rmsAmplitude > 0.01f) {
    //   const float noteOnThresh = cqtParams[s].envFloor;
    //   const float noteOffThresh = cqtParams[s].envFloor * cqtParams[s].gateRatio;
    //   SessionLogger::instance().logf("cqt-frame", 
    //     "S%d: fret=%d attack=%d sustain=%d binMag=%.6f noteOnThresh=%.6f noteOffThresh=%.6f rms=%.4f pitchHz=%.2f",
    //     s, frame.fret, frame.isAttack ? 1 : 0, frame.isSustaining ? 1 : 0,
    //     frame.binMagnitude, noteOnThresh, noteOffThresh, frame.rmsAmplitude, frame.pitchHz);
    // }
    
    // Handle note events (only when fret is detected)
    int& activeIdx = _activeIdx[s];
    
    // Calculate MIDI note for event tracking
    const int midiNote = _tuning.stringMidi[s] + (frame.fret >= 0 ? frame.fret : 0);
    
    if (frame.fret >= 0 && frame.isAttack) {
      // New note onset
      // Close any previous note on this string
      if (activeIdx >= 0 && activeIdx < static_cast<int>(_events.size())) {
        auto& prevEvent = _events[activeIdx];
        if (prevEvent.endSec <= prevEvent.startSec) {
#if ENABLE_TAB_RT_LOGGING
          SessionLogger::instance().logf("tab-noteclose",
              "S%d: Closing previous note F%d (idx=%d) at t=%.3f (started at %.3f)",
              s, prevEvent.fret, activeIdx, t0, prevEvent.startSec);
#endif
          prevEvent.endSec = t0;
        }
      }
      
      // Create new note event
      NoteEvent evt;
      evt.stringIdx = s;
      evt.fret = frame.fret;
      evt.midi = midiNote;
      evt.startSec = t0;
      evt.endSec = 0.0f; // Will be filled on note-off
      evt.velocity = std::min(1.0f, frame.binEnergy * 10.0f); // Scale energy to 0-1 velocity
      evt.articulation = "";
      
#if ENABLE_TAB_RT_LOGGING
      const float envFloor = cqtParams[s].envFloor;
      const float baseline = cqtParams[s].baseline;
      SessionLogger::instance().logf("note-event",
          "NOTE ON      S%d F%-2d           (rms=%.4f > envFloor=%.4f, baseline=%.4f)",
          s, frame.fret, frame.binEnergy, envFloor, baseline);
#endif
      
      activeIdx = static_cast<int>(_events.size());
      _events.push_back(evt);
      state.lastFret = frame.fret;
      
    } else if (frame.isSustaining) {
      // Note continues - update if fret changed (slide/bend)
      if (activeIdx >= 0 && activeIdx < static_cast<int>(_events.size())) {
        auto& evt = _events[activeIdx];
        
        // If fret changed significantly, close old note and open new one
        if (state.lastFret >= 0 && frame.fret != state.lastFret) {
#if ENABLE_TAB_RT_LOGGING
          SessionLogger::instance().logf("note-event",
              "MOVE         S%d F%-2d -> F%-2d   (rms=%.4f, t=%.3f)",
              s, state.lastFret, frame.fret, frame.binEnergy, t0);
#endif
          evt.endSec = t0;
          
          // Create new event for the changed fret
          NoteEvent newEvt;
          newEvt.stringIdx = s;
          newEvt.fret = frame.fret;
          newEvt.midi = midiNote;
          newEvt.startSec = t0;
          newEvt.endSec = 0.0f;
          newEvt.velocity = std::min(1.0f, frame.binEnergy * 10.0f);
          newEvt.articulation = "";
          
          activeIdx = static_cast<int>(_events.size());
          _events.push_back(newEvt);
          state.lastFret = frame.fret;
        } else {
          // Same fret, continuing sustain
          // Check for retrigger (would need spectral flux from CQT to detect properly)
          // For now, just detect if attack flag is set during sustain
          if (frame.isAttack) {
#if ENABLE_TAB_RT_LOGGING
            SessionLogger::instance().logf("note-event",
                "RETRIGGER    S%d F%-2d           (rms=%.4f, attack during sustain)",
                s, frame.fret, frame.binEnergy);
#endif
          }
        }
      } else {
#if ENABLE_TAB_RT_LOGGING
        SessionLogger::instance().logf("tab-error",
            "S%d F%d: Sustaining but no active note! activeIdx=%d t=%.3f",
            s, frame.fret, activeIdx, t0);
#endif
      }
      
    } else {
      // Note released
      if (activeIdx >= 0 && activeIdx < static_cast<int>(_events.size())) {
        auto& evt = _events[activeIdx];
        if (evt.endSec <= evt.startSec) {
#if ENABLE_TAB_RT_LOGGING
          const float noteOffThresh = cqtParams[s].envFloor * cqtParams[s].gateRatio;
          SessionLogger::instance().logf("note-event",
              "NOTE OFF     S%d F%-2d           (rms=%.4f < noteOffThresh=%.4f, duration=%.3fs)",
              s, evt.fret, frame.binEnergy, noteOffThresh, t0 - evt.startSec);
#endif
          evt.endSec = t0;
        }
        activeIdx = -1;
        state.lastFret = -1;
      } else if (state.lastFret >= 0) {
#if ENABLE_TAB_RT_LOGGING
        SessionLogger::instance().logf("tab-error",
            "S%d: Note released but activeIdx invalid: %d (lastFret was %d)",
            s, activeIdx, state.lastFret);
#endif
        state.lastFret = -1;
      }
    }
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
  if (_cqtDetector) {
    _cqtDetector->reset();
  }
  for (auto& state : _cqtStates) {
    state = StringCQTState{};
  }
}

void TabEngine::applyCalibration(const CalibrationProfile& profile) {
  _calibration = profile;
  // CQT uses calibrationGainMultiplier from NoteDetectionStore directly
  // Reset detector state on calibration change
  if (_cqtDetector) {
    _cqtDetector->reset();
  }
}

std::array<float, 6> TabEngine::tuningDeviationCents() const {
  std::array<float, 6> deviations{};
  for (int s = 0; s < 6; ++s) {
    const float pitchHz = _cqtStates[s].lastPitchHz;
    const float targetHz = midiToHz(_tuning.stringMidi[s]);
    if (pitchHz > 0.f && targetHz > 0.f) {
      deviations[s] = centsBetween(pitchHz, targetHz);
      // Debug logging for tuning deviation (muted)
      // if (std::abs(deviations[s]) > 1.0f) {
      //   SessionLogger::instance().logf("tuning", "String %d: pitch=%.2fHz target=%.2fHz deviation=%.2fcents", 
      //     s, pitchHz, targetHz, deviations[s]);
      // }
    }
  }
  return deviations;
}

std::array<float, 6> TabEngine::calibrationGains() const {
  std::array<float, 6> gains{};
  auto& store = NoteDetectionStore::instance();
  const auto& params = store.current();
  for (int s = 0; s < 6; ++s) {
    gains[s] = params.calibrationGainMultiplier[s];
  }
  return gains;
}

void TabEngine::setCalibrationGain(int stringIndex, float gain) {
  if (stringIndex < 0 || stringIndex >= 6)
    return;
  auto& store = NoteDetectionStore::instance();
  store.setValue(NoteParameter::CalibrationGainMultiplier, stringIndex, gain);
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

std::array<StringThresholds, 6> TabEngine::getThresholds() const {
  std::array<StringThresholds, 6> result;
  auto& store = NoteDetectionStore::instance();
  const auto& params = store.current();
  
  for (int s = 0; s < 6; ++s) {
    StringThresholds& th = result[s];
    th.baseline = params.baselineFloor[s];
    th.envFloor = params.envelopeFloor[s];
    th.gateThreshold = params.envelopeFloor[s] * params.gateRatio[s];
    th.sustainFloor = params.envelopeFloor[s] * params.gateRatio[s];  // Same as gate threshold in CQT
    th.retriggerGate = 0.0f;  // Not used in CQT (uses spectral flux instead)
    th.onsetThreshold = 0.0f;  // Not used in CQT (uses spectral flux instead)
  }
  return result;
}
