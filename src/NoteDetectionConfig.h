#pragma once

#include <array>
#include <string>

struct NoteDetectionParameterSet {
    std::array<float, 6> baselineFloor {};
    std::array<float, 6> envelopeFloor {};
    std::array<float, 6> gateRatio {};
    std::array<float, 6> targetRms {};
    std::array<float, 6> calibrationGainMultiplier {}; // Maps to preAmpGain
    std::array<float, 6> spatialWeight {};             // Calibration-fixed crosstalk weight
    std::array<int, 6> confirmationFrames {};          // Stability frames (1-5)
    std::array<float, 6> fluxSensitivity {};           // Spectral flux retrigger threshold (0.05-0.50)
    std::array<float, 6> slopeDecay {};                // Energy slope decay per fret (0.0-0.025)
};

NoteDetectionParameterSet makeDefaultNoteDetectionParameters();

enum class NoteParameter {
    BaselineFloor,
    EnvelopeFloor,
    GateRatio,
    TargetRms,
    CalibrationGainMultiplier,
    SpatialWeight,
    ConfirmationFrames,
    FluxSensitivity,
    SlopeDecay
};

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

const std::array<ParameterDescriptor, 9>& parameterDescriptors();

std::string defaultStringLabel(int stringIndex);
