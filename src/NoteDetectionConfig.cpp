#include "NoteDetectionConfig.h"

#include <algorithm>

namespace {
constexpr std::array<const char*, 6> kDefaultStringLabels {{"E", "A", "D", "G", "B", "e"}};

// YIN Detection Parameters - optimized defaults for hexaphonic guitar
constexpr std::array<float, 6> kDefaultYINThreshold {{0.10f, 0.10f, 0.10f, 0.10f, 0.10f, 0.10f}};     // Pitch confidence
constexpr std::array<float, 6> kDefaultNoiseGateRMS {{0.015f, 0.015f, 0.015f, 0.015f, 0.015f, 0.015f}}; // Noise floor
constexpr std::array<float, 6> kDefaultOnsetSensitivity {{1.4f, 1.4f, 1.4f, 1.4f, 1.4f, 1.4f}};        // Attack detection
constexpr std::array<float, 6> kDefaultReleaseRatio {{0.35f, 0.35f, 0.35f, 0.35f, 0.35f, 0.35f}};      // Note-off threshold
constexpr std::array<int, 6> kDefaultFretStabilityFrames {{2, 2, 2, 2, 2, 2}};                          // Anti-jitter frames

// Calibration parameters
constexpr std::array<float, 6> kDefaultTargetRms {{0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f}};
constexpr std::array<float, 6> kDefaultCalibrationGainMultiplier {{5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f}};
constexpr std::array<float, 6> kDefaultSpatialWeight {{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};  // Calibration-set

NoteDetectionParameterSet fromDefaults() {
    NoteDetectionParameterSet set;
    set.yinThreshold = kDefaultYINThreshold;
    set.noiseGateRMS = kDefaultNoiseGateRMS;
    set.onsetSensitivity = kDefaultOnsetSensitivity;
    set.releaseRatio = kDefaultReleaseRatio;
    set.fretStabilityFrames = kDefaultFretStabilityFrames;
    set.targetRms = kDefaultTargetRms;
    set.calibrationGainMultiplier = kDefaultCalibrationGainMultiplier;
    set.spatialWeight = kDefaultSpatialWeight;
    return set;
}

const std::array<ParameterDescriptor, 8> kDescriptors {{
    // YIN Detection Parameters
    {NoteParameter::YINThreshold, "yinThreshold", "YIN Threshold", "Pitch confidence threshold (lower = more sensitive, higher = less false positives).", 0.05f, 0.20f, 0.01f, false},
    {NoteParameter::NoiseGateRMS, "noiseGateRMS", "Noise Gate", "RMS noise floor threshold (signals below are ignored).", 0.005f, 0.05f, 0.001f, false},
    {NoteParameter::OnsetSensitivity, "onsetSensitivity", "Onset Sens", "RMS ratio for onset detection (higher = less sensitive).", 1.1f, 2.0f, 0.05f, false},
    {NoteParameter::ReleaseRatio, "releaseRatio", "Release Ratio", "Note-off threshold as ratio of peak RMS.", 0.2f, 0.6f, 0.01f, false},
    {NoteParameter::FretStabilityFrames, "fretStabilityFrames", "Fret Stability", "Frames to hold fret before changing (anti-jitter).", 1.0f, 5.0f, 1.0f, false},
    // Calibration Parameters
    {NoteParameter::TargetRms, "targetRms", "Target RMS", "Calibration target for preAmpGain calculation.", 0.0001f, 0.35f, 0.0001f, false},
    {NoteParameter::CalibrationGainMultiplier, "calibrationGainMultiplier", "Gain Multiplier", "Pre-amplification gain for signal normalization.", 0.2f, 8.0f, 0.01f, false},
    {NoteParameter::SpatialWeight, "spatialWeight", "Spatial Weight", "[Calibration-Only] Crosstalk fairness multiplier.", 0.1f, 10.0f, 0.01f, false}
}};

} // namespace

NoteDetectionParameterSet makeDefaultNoteDetectionParameters() {
    return fromDefaults();
}

const std::array<ParameterDescriptor, 8>& parameterDescriptors() {
    return kDescriptors;
}

std::string defaultStringLabel(int stringIndex) {
    if (stringIndex < 0 || stringIndex >= static_cast<int>(kDefaultStringLabels.size()))
        return std::string("String ") + std::to_string(stringIndex + 1);
    return kDefaultStringLabels[static_cast<std::size_t>(stringIndex)];
}
