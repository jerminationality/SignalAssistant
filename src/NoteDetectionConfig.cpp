#include "NoteDetectionConfig.h"

#include <algorithm>

namespace {
constexpr std::array<const char*, 6> kDefaultStringLabels {{"E", "A", "D", "G", "B", "e"}};

constexpr std::array<float, 6> kDefaultBaselineFloor {{0.01f, 0.01f, 0.01f, 0.01f, 0.01f, 0.01f}};
constexpr std::array<float, 6> kDefaultEnvelopeFloor {{0.15f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f}};
constexpr std::array<float, 6> kDefaultGateRatio {{0.50f, 0.50f, 0.50f, 0.50f, 0.50f, 0.50f}};
constexpr std::array<float, 6> kDefaultTargetRms {{0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f}};
constexpr std::array<float, 6> kDefaultCalibrationGainMultiplier {{5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f}};
// CQT parameters
constexpr std::array<float, 6> kDefaultSpatialWeight {{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};  // Calibration-set
constexpr std::array<int, 6> kDefaultConfirmationFrames {{3, 3, 3, 3, 3, 3}};                  // Stability frames
constexpr std::array<float, 6> kDefaultFluxSensitivity {{0.20f, 0.20f, 0.20f, 0.20f, 0.20f, 0.20f}};  // Spectral flux threshold
constexpr std::array<float, 6> kDefaultSlopeDecay {{0.012f, 0.012f, 0.012f, 0.012f, 0.012f, 0.012f}};  // 1.2% per fret

NoteDetectionParameterSet fromDefaults() {
    NoteDetectionParameterSet set;
    set.baselineFloor = kDefaultBaselineFloor;
    set.envelopeFloor = kDefaultEnvelopeFloor;
    set.gateRatio = kDefaultGateRatio;
    set.targetRms = kDefaultTargetRms;
    set.calibrationGainMultiplier = kDefaultCalibrationGainMultiplier;
    set.spatialWeight = kDefaultSpatialWeight;
    set.confirmationFrames = kDefaultConfirmationFrames;
    set.fluxSensitivity = kDefaultFluxSensitivity;
    set.slopeDecay = kDefaultSlopeDecay;
    return set;
}

const std::array<ParameterDescriptor, 9> kDescriptors {{
    {NoteParameter::BaselineFloor, "baselineFloor", "Baseline", "Master gate noise floor (RMS threshold).", 0.0001f, 0.0100f, 0.0001f, false},
    {NoteParameter::EnvelopeFloor, "envelopeFloor", "Env Floor", "Note-on threshold (default 0.15, tunable ±0.1).", 0.05f, 0.25f, 0.01f, false},
    {NoteParameter::GateRatio, "gateRatio", "Gate Ratio", "Note-off multiplier: releaseThreshold = envFloor × gateRatio.", 0.1f, 1.0f, 0.01f, false},
    {NoteParameter::TargetRms, "targetRms", "Target RMS", "Calibration target for preAmpGain calculation.", 0.0001f, 0.35f, 0.0001f, false},
    {NoteParameter::CalibrationGainMultiplier, "calibrationGainMultiplier", "Gain Multiplier", "Pre-CQT signal amplification (preAmpGain).", 0.2f, 8.0f, 0.01f, false},
    {NoteParameter::SpatialWeight, "spatialWeight", "Spatial Weight", "[Calibration-Only] Crosstalk fairness multiplier.", 0.1f, 10.0f, 0.01f, false},
    {NoteParameter::ConfirmationFrames, "confirmationFrames", "Confirmation", "Fret stability frames (1=fast, 5=solid).", 1.0f, 5.0f, 1.0f, false},
    {NoteParameter::FluxSensitivity, "fluxSensitivity", "Flux Sensitivity", "Spectral flux retrigger threshold.", 0.05f, 0.50f, 0.01f, false},
    {NoteParameter::SlopeDecay, "slopeDecay", "RMS Slope", "Energy slope decay per fret (0%=flat, 2.5%=max).", 0.0f, 0.025f, 0.001f, false}
}};

} // namespace

NoteDetectionParameterSet makeDefaultNoteDetectionParameters() {
    return fromDefaults();
}

const std::array<ParameterDescriptor, 9>& parameterDescriptors() {
    return kDescriptors;
}

std::string defaultStringLabel(int stringIndex) {
    if (stringIndex < 0 || stringIndex >= static_cast<int>(kDefaultStringLabels.size()))
        return std::string("String ") + std::to_string(stringIndex + 1);
    return kDefaultStringLabels[static_cast<std::size_t>(stringIndex)];
}
