#include "TabEngine.h"
#include "audio/AtomicNoteState.h"
#include "NoteDetectionStore.h"
#include "NoteLogger.h"
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
  // Initialize string states
  for (auto& state : _yinStates) {
    state = StringYINState{};
  }
  
#if ENABLE_TAB_RT_LOGGING
  // Initialize NoteLogger early so log file is created on app startup
  (void)NoteLogger::instance();
#endif
}

TabEngine::~TabEngine() = default;

void TabEngine::processBlock(const float* const channels[6], int n, float sr, float t0) {
  if (!_noteState || n <= 0 || sr <= 0.f) {
    return;
  }
  
  // Get current detection parameters from the store (for thresholds/logging)
  auto& store = NoteDetectionStore::instance();
  const auto& params = store.current();
  
  // Collect RMS and ENV values for all strings, then log ticker
  std::array<float, 6> rmsValues{};
  std::array<float, 6> envValues{};
  
  // Read note state from YIN Worker Thread (via AtomicNoteState)
  // The YIN worker has already done all pitch detection - we just consume the results
  for (int s = 0; s < 6; ++s) {
    int fret = -1;
    float energy = 0.0f;
    bool isAttack = false;
    bool isSustaining = false;
    float pitchHz = 0.0f;
    float onsetThreshold = 0.0f;
    
    // Read current state from atomic note state (populated by YIN worker)
    _noteState->readString(s, fret, energy, isAttack, isSustaining, pitchHz, onsetThreshold);
    
    // Check onset counter to reliably detect new onsets
    // This counter is incremented by YIN worker when entering ATTACK state,
    // ensuring we never miss an onset even if ATTACK only lasts one frame
    const uint32_t currentOnsetCounter = _noteState->getOnsetCounter(s);
    
    // Store for ticker logging
    rmsValues[s] = energy;
    envValues[s] = onsetThreshold;
    
    auto& state = _yinStates[s];
    state.lastRms = energy;
    
    // Detect new onset via counter change (more reliable than volatile isAttack flag)
    const bool onsetCounterChanged = (currentOnsetCounter != state.lastOnsetCounter);
    state.lastOnsetCounter = currentOnsetCounter;
    
    // Handle pending onset: onset was detected but fret wasn't valid yet
    // Now we have a valid fret, so emit the note
    bool newOnset = false;
    if (state.pendingOnset && fret >= 0) {
      newOnset = true;
      state.pendingOnset = false;
      // Use the energy from when the onset was detected
      energy = state.pendingOnsetEnergy;
    } else if (onsetCounterChanged) {
      if (fret >= 0) {
        // Normal case: onset detected with valid fret
        newOnset = true;
      } else {
        // Onset detected but fret not valid yet - store as pending
        state.pendingOnset = true;
        state.pendingOnsetEnergy = energy;
        state.pendingOnsetTime = t0;
      }
    }
    
    // Clear pending onset if:
    // 1. Signal has dropped significantly (no note materialized), OR
    // 2. Too much time elapsed (>200ms without valid fret = bad detection)
    // Don't clear during active onset detection (onsetCounterChanged or high energy)
    if (state.pendingOnset) {
      const float timeSincePending = t0 - state.pendingOnsetTime;
      const bool signalGone = (!isSustaining && !onsetCounterChanged && energy < params.noiseGateRMS[s]);
      const bool timeout = (timeSincePending > 0.2f);  // 200ms timeout
      
      if (signalGone || timeout) {
        state.pendingOnset = false;
      }
    }
    
    // Update pitch tracking for tuning mode
    if (pitchHz > 0.0f) {
      state.lastPitchHz = pitchHz;
    }
    
    // Handle note events
    int& activeIdx = _activeIdx[s];
    
    // Calculate MIDI note for event tracking
    const int midiNote = _tuning.stringMidi[s] + (fret >= 0 ? fret : 0);
    
    // Detect state transitions for note events
    // NOTE ON: onset counter changed (new note onset detected via counter)
    // NOTE OFF: wasSustaining was true but now isSustaining is false
    
    if (newOnset) {
      // New note onset detected
      // Check if this is a retrigger (same fret) or new note
      const bool isRetrigger = (state.lastFret == fret && activeIdx >= 0);
      
      // Close any previous note on this string
      if (activeIdx >= 0 && activeIdx < static_cast<int>(_events.size())) {
        auto& prevEvent = _events[activeIdx];
        if (prevEvent.endSec <= prevEvent.startSec) {
          prevEvent.endSec = t0;
        }
      }
      
      // Create new note event
      NoteEvent evt;
      evt.stringIdx = s;
      evt.fret = fret;
      evt.midi = midiNote;
      evt.startSec = t0;
      evt.endSec = 0.0f; // Will be filled on note-off
      evt.velocity = std::min(1.0f, energy * 10.0f); // Scale energy to 0-1 velocity
      evt.articulation = "";
      
#if ENABLE_TAB_RT_LOGGING
      if (isRetrigger) {
        // Retrigger on same fret
        NoteLogger::instance().logRetrigger(s, fret, energy, onsetThreshold, rmsValues, params.noiseGateRMS);
      } else {
        // New note onset
        NoteLogger::instance().logNoteOn(s, fret, energy, onsetThreshold, rmsValues, params.noiseGateRMS);
      }
#endif
      
      activeIdx = static_cast<int>(_events.size());
      _events.push_back(evt);
      state.lastFret = fret;
      
    } else if (isSustaining && fret >= 0) {
      // Note continues - check for fret change (slide/bend)
      if (activeIdx >= 0 && activeIdx < static_cast<int>(_events.size())) {
        auto& evt = _events[activeIdx];
        
        // If fret changed significantly, close old note and open new one
        // BUT: only allow repitch if RMS is still strong (> 50% of threshold)
        // This prevents pitch detection jitter during decay phase from creating spurious events
        const float minRepitchEnergy = onsetThreshold * 0.5f;
        if (state.lastFret >= 0 && fret != state.lastFret && energy > minRepitchEnergy) {
#if ENABLE_TAB_RT_LOGGING
          // Log repitch event (fret change during sustain)
          NoteLogger::instance().logRepitch(s, state.lastFret, fret, energy, onsetThreshold, rmsValues, params.noiseGateRMS);
#endif
          evt.endSec = t0;
          
          // Create new event for the changed fret
          NoteEvent newEvt;
          newEvt.stringIdx = s;
          newEvt.fret = fret;
          newEvt.midi = midiNote;
          newEvt.startSec = t0;
          newEvt.endSec = 0.0f;
          newEvt.velocity = std::min(1.0f, energy * 10.0f);
          newEvt.articulation = "";
          
          activeIdx = static_cast<int>(_events.size());
          _events.push_back(newEvt);
          state.lastFret = fret;
        }
      }
      
    } else if (state.wasSustaining && !isSustaining) {
      // Note released - transition from sustaining to not sustaining
      if (activeIdx >= 0 && activeIdx < static_cast<int>(_events.size())) {
        auto& evt = _events[activeIdx];
        if (evt.endSec <= evt.startSec) {
#if ENABLE_TAB_RT_LOGGING
          const float noteOffThresh = params.yinThreshold[s] * params.releaseRatio[s];
          NoteLogger::instance().logNoteOff(s, evt.fret, energy, noteOffThresh, rmsValues, params.noiseGateRMS);
#endif
          evt.endSec = t0;
        }
        activeIdx = -1;
        state.lastFret = -1;
      }
    }
    
    // Update state tracking for next frame
    state.wasAttack = isAttack;
    state.wasSustaining = isSustaining;
  }
  
  // Log RMS/ENV ticker after processing all strings
#if ENABLE_TAB_RT_LOGGING
  SessionLogger::instance().logRmsEnvTicker(rmsValues, envValues, params.noiseGateRMS);
#endif
  
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
  for (auto& state : _yinStates) {
    state = StringYINState{};
  }
}

void TabEngine::applyCalibration(const CalibrationProfile& profile) {
  _calibration = profile;
  // YIN worker handles calibration directly via its own setCalibration()
}

std::array<float, 6> TabEngine::tuningDeviationCents() const {
  std::array<float, 6> deviations{};
  for (int s = 0; s < 6; ++s) {
    const float pitchHz = _yinStates[s].lastPitchHz;
    const float targetHz = midiToHz(_tuning.stringMidi[s]);
    if (pitchHz > 0.f && targetHz > 0.f) {
      deviations[s] = centsBetween(pitchHz, targetHz);
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
    // Map YIN parameters to thresholds display
    th.baseline = params.noiseGateRMS[s];
    th.envFloor = params.yinThreshold[s];
    th.gateThreshold = params.yinThreshold[s] * params.releaseRatio[s];
    th.sustainFloor = params.yinThreshold[s] * params.releaseRatio[s];  // Release threshold
    th.retriggerGate = params.onsetSensitivity[s];  // YIN onset sensitivity
    th.onsetThreshold = params.noiseGateRMS[s] * params.onsetSensitivity[s];  // Onset RMS threshold
  }
  return result;
}
