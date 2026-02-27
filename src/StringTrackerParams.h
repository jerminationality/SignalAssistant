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

// ── User-facing parameters (peak-first: 6 knobs, Section 9A) ─────────
//
//  Old internal accessor  →  New NoteParameter source
//  noiseGate              →  derived from TouchSensitivity
//  attackSensitivity      →  AttackResponse
//  triggerGuardMs         →  hardcoded (system-managed, Section 9B)
//  noteOnThreshold        →  TouchSensitivity
//  noteOffRatio           →  derived from SustainTail / TouchSensitivity
//  calibrationGainMult    →  CalibrationGainMultiplier
//  repitchThreshold       →  TrackingStability
//  repitchConfirmFrames   →  LegatoSpeed
//  repitchMinConfidence   →  derived from TrackingStability + offset
//  pitchConfidence        →  TrackingStability
//  retriggerDeltaRatio    →  derived from AttackResponse

inline float noiseGate(int s) {
    // TouchSensitivity is the delta above adaptive noise floor (0.01-0.25).
    // Scale to the old noiseGate range so derived dB helpers remain valid.
    return active(NoteParameter::TouchSensitivity, s, 0.08f) * 3.75f;
}

inline float attackSensitivity(int s) {
    return active(NoteParameter::AttackResponse, s, 1.5f);
}

inline float triggerGuardMs(int /*s*/) {
    return 45.f;   // system-managed (Section 9B)
}

inline float noteOnThreshold(int s) {
    return active(NoteParameter::TouchSensitivity, s, 0.08f);
}

inline float noteOffRatio(int s) {
    const float tail = active(NoteParameter::SustainTail, s, 0.02f);
    const float onTh = noteOnThreshold(s);
    return (onTh > 0.f) ? std::clamp(tail / onTh, 0.1f, 1.0f) : 0.60f;
}

inline float calibrationGainMultiplier(int s) {
    return active(NoteParameter::CalibrationGainMultiplier, s, 1.0f);
}

inline float repitchThreshold(int s) {
    return active(NoteParameter::TrackingStability, s, 0.65f);
}

inline int repitchConfirmFrames(int s) {
    return static_cast<int>(active(NoteParameter::LegatoSpeed, s, 2.f));
}

inline float repitchMinConfidence(int s) {
    return std::min(active(NoteParameter::TrackingStability, s, 0.65f) + 0.20f, 0.98f);
}

inline float pitchConfidence(int s) {
    return active(NoteParameter::TrackingStability, s, 0.65f);
}

inline float retriggerDeltaRatio(int s) {
    // Higher AttackResponse → harder to retrigger → larger delta ratio
    return 1.f / active(NoteParameter::AttackResponse, s, 1.5f);
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
