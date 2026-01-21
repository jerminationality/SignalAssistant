#pragma once

#include <array>
#include <string>

struct NoteDetectionParameterSet {
    // YIN Detection Parameters (5 core params)
    std::array<float, 6> yinThreshold {};              // YIN pitch confidence (0.05-0.20)
    std::array<float, 6> noiseGateRMS {};              // Noise floor RMS threshold (0.005-0.05)
    std::array<float, 6> onsetSensitivity {};          // RMS ratio for onset detection (1.1-2.0)
    std::array<float, 6> releaseRatio {};              // Note-off threshold ratio (0.2-0.6)
    std::array<int, 6> fretStabilityFrames {};         // Fret stability hysteresis (1-5)
    
    // Calibration parameters (kept from CQT)
    std::array<float, 6> targetRms {};
    std::array<float, 6> calibrationGainMultiplier {}; // Maps to preAmpGain
    std::array<float, 6> spatialWeight {};             // Calibration-fixed crosstalk weight
};

NoteDetectionParameterSet makeDefaultNoteDetectionParameters();

enum class NoteParameter {
    // YIN Detection Parameters
    YINThreshold,
    NoiseGateRMS,
    OnsetSensitivity,
    ReleaseRatio,
    FretStabilityFrames,
    // Calibration Parameters
    TargetRms,
    CalibrationGainMultiplier,
    SpatialWeight
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

const std::array<ParameterDescriptor, 8>& parameterDescriptors();

std::string defaultStringLabel(int stringIndex);
