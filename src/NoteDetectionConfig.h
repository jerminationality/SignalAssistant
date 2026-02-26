#pragma once

#include <array>
#include <string>

// ── User-facing tuning parameters (9 controls) ──────────────────────────
//
//  noiseGate          (0.0 – 1.0)   Silence floor / noise rejection
//  attackSensitivity  (0.5 – 3.0)   Aubio onset detector sensitivity
//  triggerGuardMs     (5   – 80)    Minimum ms between successive note-ons
//  noteOnThreshold    (0.01 – 0.35) RMS level that opens a note
//  noteOffRatio       (0.1 – 1.0)   noteOff = noteOn × ratio
//  calibrationGainMultiplier (0.2 – 8.0) Per-string gain trim
//  repitchThreshold   (0.2 – 0.8)   Semitone delta to trigger repitch
//  repitchConfirmFrames (1 – 5)     Blocks pitch must be stable before repitch
//  repitchMinConfidence (0.3 – 0.9) Minimum aubio confidence for repitch
//
// Everything else (LPF, HPF, pitchTolerance, retriggerGateScale, targetRms,
// onsetThresholdScale, silence dB, baselineFloor) is either hardcoded or
// derived inside StringTrackerParams.
// ─────────────────────────────────────────────────────────────────────────

struct NoteDetectionParameterSet {
    std::array<float, 6> noiseGate {};
    std::array<float, 6> attackSensitivity {};
    std::array<float, 6> triggerGuardMs {};
    std::array<float, 6> noteOnThreshold {};
    std::array<float, 6> noteOffRatio {};
    std::array<float, 6> calibrationGainMultiplier {};
    std::array<float, 6> repitchThreshold {};
    std::array<float, 6> repitchConfirmFrames {};
    std::array<float, 6> repitchMinConfidence {};
    std::array<float, 6> pitchConfidence {};
    std::array<float, 6> retriggerDeltaRatio {};  // relative energy-rise multiplier for retrigger gate
};

NoteDetectionParameterSet makeDefaultNoteDetectionParameters();

enum class NoteParameter {
    NoiseGate,
    AttackSensitivity,
    TriggerGuardMs,
    NoteOnThreshold,
    NoteOffRatio,
    CalibrationGainMultiplier,
    RepitchThreshold,
    RepitchConfirmFrames,
    RepitchMinConfidence,
    PitchConfidence,
    RetriggerDeltaRatio
};

inline constexpr int kNumNoteParameters = 11;
inline constexpr int kNumStrings = 6;

struct ParameterDescriptor {
    NoteParameter id;
    std::string key;
    std::string label;
    std::string description;
    float minValue;
    float maxValue;
    float step;
    bool useDecibels;
    bool perStringMinMax = false;
    std::array<float, 6> perStringMin {};
    std::array<float, 6> perStringMax {};
};

const std::array<ParameterDescriptor, kNumNoteParameters>& parameterDescriptors();

std::string defaultStringLabel(int stringIndex);
