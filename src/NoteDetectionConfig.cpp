#include "NoteDetectionConfig.h"

#include <algorithm>

namespace {

// ── Default per-string values for the 6 user-facing controls ────────────
constexpr std::array<float, 6> kDefaultNoiseGate             {{0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f}};
constexpr std::array<float, 6> kDefaultAttackSensitivity     {{1.3f,  1.3f,  1.3f,  1.3f,  1.3f,  1.3f}};
constexpr std::array<float, 6> kDefaultTriggerGuardMs        {{60.f,  60.f,  60.f,  60.f,  60.f,  60.f}};
constexpr std::array<float, 6> kDefaultNoteOnThreshold       {{0.015f, 0.020f, 0.025f, 0.035f, 0.045f, 0.055f}};
constexpr std::array<float, 6> kDefaultNoteOffRatio          {{0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f}};
constexpr std::array<float, 6> kDefaultCalibrationGainMult   {{5.0f,  5.0f,  5.0f,  5.0f,  5.0f,  5.0f}};
constexpr std::array<float, 6> kDefaultRepitchThreshold      {{0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f}};  // 0.5 semitones = 50 cents dead-zone
constexpr std::array<float, 6> kDefaultRepitchConfirmFrames  {{3.f,   3.f,   3.f,   3.f,   3.f,   3.f}};
constexpr std::array<float, 6> kDefaultRepitchMinConfidence  {{0.80f, 0.80f, 0.80f, 0.80f, 0.80f, 0.80f}};
constexpr std::array<float, 6> kDefaultPitchConfidence       {{0.65f, 0.65f, 0.65f, 0.65f, 0.65f, 0.65f}};
constexpr std::array<float, 6> kDefaultRetriggerDeltaRatio    {{0.65f, 0.65f, 0.65f, 0.65f, 0.65f, 0.65f}};

constexpr std::array<const char*, 6> kDefaultStringLabels {{"E", "A", "D", "G", "B", "e"}};

NoteDetectionParameterSet fromDefaults() {
    NoteDetectionParameterSet set;
    set.noiseGate               = kDefaultNoiseGate;
    set.attackSensitivity       = kDefaultAttackSensitivity;
    set.triggerGuardMs          = kDefaultTriggerGuardMs;
    set.noteOnThreshold         = kDefaultNoteOnThreshold;
    set.noteOffRatio            = kDefaultNoteOffRatio;
    set.calibrationGainMultiplier = kDefaultCalibrationGainMult;
    set.repitchThreshold       = kDefaultRepitchThreshold;
    set.repitchConfirmFrames   = kDefaultRepitchConfirmFrames;
    set.repitchMinConfidence   = kDefaultRepitchMinConfidence;
    set.pitchConfidence        = kDefaultPitchConfidence;
    set.retriggerDeltaRatio    = kDefaultRetriggerDeltaRatio;
    return set;
}

const std::array<ParameterDescriptor, kNumNoteParameters> kDescriptors {{
    {NoteParameter::NoiseGate,               "noiseGate",               "Noise Gate",          "Silence floor / noise rejection (0 = most sensitive).",        0.001f, 0.01f, 0.0001f, false},
    {NoteParameter::AttackSensitivity,       "attackSensitivity",       "Attack Sensitivity",  "Aubio onset detector sensitivity (lower = more sensitive).",   0.5f,  3.0f,  0.05f, false},
    {NoteParameter::TriggerGuardMs,          "triggerGuardMs",          "Trigger Guard (ms)",  "Minimum ms between successive note-on events.",                5.0f,  80.0f, 1.0f,  false},
    {NoteParameter::NoteOnThreshold,         "noteOnThreshold",        "Note ON",             "RMS level that opens a new note.",                             0.01f, 0.35f, 0.001f, false},
    {NoteParameter::NoteOffRatio,            "noteOffRatio",           "Note OFF Ratio",      "Note OFF = Note ON × this ratio.",                             0.1f,  1.0f,  0.01f, false},
    {NoteParameter::CalibrationGainMultiplier, "calibrationGainMultiplier", "Gain Trim",       "Per-string gain multiplier.",                                  0.2f,  8.0f,  0.01f, false},
    {NoteParameter::RepitchThreshold,      "repitchThreshold",      "Repitch Threshold",   "Semitone delta dead-zone for repitch (0.5 = 50 cents).",      0.2f,  0.8f,  0.01f, false},
    {NoteParameter::RepitchConfirmFrames,  "repitchConfirmFrames",  "Repitch Confirm",     "Blocks the new pitch must be stable before repitch fires.",    1.0f,  5.0f,  1.0f,  false},
    {NoteParameter::RepitchMinConfidence,  "repitchMinConfidence",  "Repitch Confidence",  "Minimum aubio pitch confidence to accept a repitch.",          0.3f,  0.9f,  0.01f, false},
    {NoteParameter::PitchConfidence,        "pitchConfidence",       "Onset Confidence",    "Minimum aubio confidence to accept a new note-on (lower = more lenient).", 0.0f, 1.0f, 0.01f, false},
    {NoteParameter::RetriggerDeltaRatio,    "retriggerDeltaRatio",   "Retrigger Delta",     "Relative energy-rise multiplier for retrigger gate (higher = harder to retrigger during sustain).", 0.1f, 1.0f, 0.01f, false}
}};

} // namespace

NoteDetectionParameterSet makeDefaultNoteDetectionParameters() {
    return fromDefaults();
}

const std::array<ParameterDescriptor, kNumNoteParameters>& parameterDescriptors() {
    return kDescriptors;
}

std::string defaultStringLabel(int stringIndex) {
    if (stringIndex < 0 || stringIndex >= static_cast<int>(kDefaultStringLabels.size()))
        return std::string("String ") + std::to_string(stringIndex + 1);
    return kDefaultStringLabels[static_cast<std::size_t>(stringIndex)];
}
