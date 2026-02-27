#pragma once

#include <array>
#include <string>

// ── Peak-First Architecture: User-facing tuning parameters (Section 9A) ─
//
//  touchSensitivity    userSensitivityDelta   0.08   [0.01 – 0.25]
//  attackResponse      Retrigger Multiplier   1.5x   [1.1x – 3.0x]
//  sustainTail         Exit Threshold (RMS)   0.02   [0.005 – 0.1]
//  legatoSpeed         minNoteFrames          2      [1 – 5]
//  trackingStability   Pitch Confidence       0.65   [0.4 – 0.95]
//  calibrationGainMultiplier  Per-string gain (persisted from calibration)
//
// System-managed (Section 9B — non-user facing):
//  sampleRate=48000, bufferHop=128, windowFrame=512, alpha=0.01,
//  analysisDelay=10ms, hysteresisWindow=20ms, calibrationGain=dynamic,
//  noiseFloor=dynamic (leaky integrator, frozen during notes)
// ─────────────────────────────────────────────────────────────────────────

struct NoteDetectionParameterSet {
    std::array<float, 6> touchSensitivity {};
    std::array<float, 6> attackResponse {};
    std::array<float, 6> sustainTail {};
    std::array<float, 6> legatoSpeed {};
    std::array<float, 6> trackingStability {};
    std::array<float, 6> calibrationGainMultiplier {};
};

NoteDetectionParameterSet makeDefaultNoteDetectionParameters();

enum class NoteParameter {
    TouchSensitivity,
    AttackResponse,
    SustainTail,
    LegatoSpeed,
    TrackingStability,
    CalibrationGainMultiplier
};

inline constexpr int kNumNoteParameters = 6;
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
