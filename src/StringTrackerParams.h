#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "NoteDetectionStore.h"

namespace trackerparams {

// ── Uniform 2048-sample FFT window: 128 × 16 = 2048 ──────────────────
constexpr std::array<int, 6>   kFftMultipliers{{16, 16, 16, 16, 16, 16}};

// ── Hardcoded per-string constants (removed from tuning panel) ────────
constexpr std::array<float, 6> kLowCutHz  {{70.f, 90.f, 120.f, 170.f, 220.f, 300.f}};
constexpr std::array<float, 6> kHighCutHz {{400.f, 400.f,  500.f,  1200.f, 1500.f, 1800.f}};
constexpr std::array<float, 6> kRetriggerGateScale {{1.40f, 1.25f, 1.10f, 1.0f, 1.0f, 1.0f}};
constexpr float kPitchTolerance  = 0.10f;
constexpr float kOnsetThreshold  = 0.10f;   // fixed spectral-flux gate
constexpr float kTargetRms       = 0.25f;

// ── Generation counter for hot-reload detection ───────────────────────
inline std::uint64_t settingsGeneration() {
    return NoteDetectionStore::instance().activeGeneration();
}

// ── Atomic read of a user-facing parameter ────────────────────────────
inline float active(NoteParameter param, int s, float fallback) {
    if (s < 0 || s >= kNumStrings)
        return fallback;
    return NoteDetectionStore::instance().activeValue(param, s);
}

// ── User-facing parameters (6 knobs) ─────────────────────────────────

inline float noiseGate(int s) {
    return active(NoteParameter::NoiseGate, s, 0.30f);
}

inline float attackSensitivity(int s) {
    return active(NoteParameter::AttackSensitivity, s, 1.0f);
}

inline float triggerGuardMs(int s) {
    return active(NoteParameter::TriggerGuardMs, s, 45.f);
}

inline float noteOnThreshold(int s) {
    return active(NoteParameter::NoteOnThreshold, s, 0.020f);
}

inline float noteOffRatio(int s) {
    return active(NoteParameter::NoteOffRatio, s, 0.60f);
}

inline float calibrationGainMultiplier(int s) {
    return active(NoteParameter::CalibrationGainMultiplier, s, 5.0f);
}

inline float repitchThreshold(int s) {
    return active(NoteParameter::RepitchThreshold, s, 0.5f);
}

inline int repitchConfirmFrames(int s) {
    return static_cast<int>(active(NoteParameter::RepitchConfirmFrames, s, 3.f));
}

inline float repitchMinConfidence(int s) {
    return active(NoteParameter::RepitchMinConfidence, s, 0.85f);
}

inline float pitchConfidence(int s) {
    return active(NoteParameter::PitchConfidence, s, 0.70f);
}

inline float retriggerDeltaRatio(int s) {
    return active(NoteParameter::RetriggerDeltaRatio, s, 0.50f);
}

// ── Derived / hardcoded accessors (used by StringTracker) ─────────────

inline float baselineFloor(int s) {
    // noiseGate 0→0.0005, 0.3→~0.0014, 1→0.01
    const float ng = noiseGate(s);
    return 0.0005f * std::pow(20.f, ng);
}

inline float noteOffThreshold(int s) {
    return noteOnThreshold(s) * noteOffRatio(s);
}

inline float onsetSilenceDb(int s) {
    // noiseGate 0→-117dB, 0.3→-90dB, 1→-27dB
    return -117.f + 90.f * noiseGate(s);
}

inline float pitchSilenceDb(int s) {
    // slightly deeper than onset silence
    return -122.f + 90.f * noiseGate(s);
}

inline float aubioThresholdScale(int s) {
    return attackSensitivity(s);
}

inline float onsetThresholdScale(int /*s*/, float base) {
    return base * kOnsetThreshold;
}

inline float pitchTolerance(int /*s*/) {
    return kPitchTolerance;
}

inline float lowCutMultiplier(int s) {
    if (s < 0 || s >= kNumStrings) return 77.5f;
    return kLowCutHz[static_cast<std::size_t>(s)];
}

inline float highCutMultiplier(int s) {
    if (s < 0 || s >= kNumStrings) return 450.f;
    return kHighCutHz[static_cast<std::size_t>(s)];
}

inline float retriggerGateScale(int s) {
    if (s < 0 || s >= kNumStrings) return 1.0f;
    return kRetriggerGateScale[static_cast<std::size_t>(s)];
}

inline float targetRms(int /*s*/) {
    return kTargetRms;
}

inline int fftMultiple(int s) {
    if (s < 0 || s >= static_cast<int>(kFftMultipliers.size()))
        return 4;
    return kFftMultipliers[static_cast<std::size_t>(s)];
}

} // namespace trackerparams
