#pragma once

#include <array>
#include <string>

struct NoteDetectionParameterSet {
    std::array<float, 6> onsetThresholdScale {};
    std::array<float, 6> baselineFloor {};
    std::array<float, 6> envelopeFloor {};
    std::array<float, 6> gateRatio {};
    std::array<float, 6> sustainFloorScale {};
    std::array<float, 6> retriggerGateScale {};
    std::array<float, 6> peakReleaseRatio {};
    std::array<float, 6> pitchTolerance {};
    std::array<float, 6> targetRms {};
    std::array<float, 6> calibrationGainMultiplier {};
    std::array<float, 6> lowCutMultiplier {};
    std::array<float, 6> highCutMultiplier {};
    std::array<float, 6> aubioThresholdScale {};
    std::array<float, 6> onsetSilenceDb {};
    std::array<float, 6> pitchSilenceDb {};
};

NoteDetectionParameterSet makeDefaultNoteDetectionParameters();

enum class NoteParameter {
    OnsetThresholdScale,
    BaselineFloor,
    EnvelopeFloor,
    GateRatio,
    SustainFloorScale,
    RetriggerGateScale,
    PeakReleaseRatio,
    PitchTolerance,
    TargetRms,
    CalibrationGainMultiplier,
    LowCutMultiplier,
    HighCutMultiplier,
    AubioThresholdScale,
    OnsetSilenceDb,
    PitchSilenceDb
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
};

const std::array<ParameterDescriptor, 15>& parameterDescriptors();

std::string defaultStringLabel(int stringIndex);
