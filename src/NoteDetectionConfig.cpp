#include "NoteDetectionConfig.h"

#include <algorithm>

namespace {
constexpr std::array<float, 6> kDefaultOnsetThresholdScale {{0.006f, 0.009f, 0.0116f, 0.014f, 0.016f, 0.018f}};
constexpr std::array<float, 6> kDefaultBaselineFloor {{0.00018f, 0.00022f, 0.00026f, 0.00032f, 0.00037f, 0.00042f}};
constexpr std::array<float, 6> kDefaultEnvelopeFloor {{0.00045f, 0.00055f, 0.00065f, 0.00078f, 0.00090f, 0.00105f}};
constexpr std::array<float, 6> kDefaultGateRatio {{0.055f, 0.10f, 0.13f, 0.17f, 0.21f, 0.25f}};
constexpr std::array<float, 6> kDefaultSustainFloorScale {{0.58f, 0.70f, 0.82f, 1.0f, 1.0f, 1.0f}};
constexpr std::array<float, 6> kDefaultRetriggerGateScale {{1.40f, 1.25f, 1.10f, 1.0f, 1.0f, 1.0f}};
constexpr std::array<float, 6> kDefaultPeakReleaseRatio {{0.12f, 0.13f, 0.14f, 0.16f, 0.18f, 0.20f}};
constexpr std::array<float, 6> kDefaultPitchTolerance {{0.40f, 0.40f, 0.45f, 0.44f, 0.50f, 0.55f}};
constexpr std::array<float, 6> kDefaultTargetRms {{0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f}};
constexpr std::array<float, 6> kDefaultCalibrationGainMultiplier {{5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f}};
constexpr std::array<float, 6> kDefaultLowCutMultiplier {{0.45f, 0.50f, 0.58f, 0.65f, 0.65f, 0.65f}};
constexpr std::array<float, 6> kDefaultHighCutMultiplier {{1.35f, 1.28f, 1.18f, 1.10f, 1.10f, 1.10f}};
constexpr std::array<float, 6> kDefaultAubioThresholdScale {{1.2f, 1.35f, 1.6f, 1.8f, 1.8f, 1.8f}};
constexpr std::array<float, 6> kDefaultOnsetSilenceDb {{-85.f, -85.f, -75.f, -75.f, -75.f, -75.f}};
constexpr std::array<float, 6> kDefaultPitchSilenceDb {{-90.f, -90.f, -80.f, -80.f, -80.f, -80.f}};

constexpr std::array<const char*, 6> kDefaultStringLabels {{"E", "A", "D", "G", "B", "e"}};

NoteDetectionParameterSet fromDefaults() {
    NoteDetectionParameterSet set;
    set.onsetThresholdScale = kDefaultOnsetThresholdScale;
    set.baselineFloor = kDefaultBaselineFloor;
    set.envelopeFloor = kDefaultEnvelopeFloor;
    set.gateRatio = kDefaultGateRatio;
    set.sustainFloorScale = kDefaultSustainFloorScale;
    set.retriggerGateScale = kDefaultRetriggerGateScale;
    set.peakReleaseRatio = kDefaultPeakReleaseRatio;
    set.pitchTolerance = kDefaultPitchTolerance;
    set.targetRms = kDefaultTargetRms;
    set.calibrationGainMultiplier = kDefaultCalibrationGainMultiplier;
    set.lowCutMultiplier = kDefaultLowCutMultiplier;
    set.highCutMultiplier = kDefaultHighCutMultiplier;
    set.aubioThresholdScale = kDefaultAubioThresholdScale;
    set.onsetSilenceDb = kDefaultOnsetSilenceDb;
    set.pitchSilenceDb = kDefaultPitchSilenceDb;
    return set;
}

const std::array<ParameterDescriptor, 15> kDescriptors {{
    {NoteParameter::OnsetThresholdScale, "onsetThresholdScale", "Onset Threshold", "Aubio onset detection threshold (spectral flux).", 0.02f, 4.0f, 0.001f, false},
    {NoteParameter::BaselineFloor, "baselineFloor", "Baseline Floor", "Adaptive noise floor estimate.", 0.00002f, 0.0100f, 0.00001f, false},
    {NoteParameter::EnvelopeFloor, "envelopeFloor", "Envelope Floor", "Minimum RMS before envelope resets to zero.", 0.00005f, 0.0080f, 0.00005f, false},
    {NoteParameter::GateRatio, "gateRatio", "Gate Ratio", "Multiplier applied to baseline floor for note-on decisions.", 0.005f, 10.0f, 0.005f, false},
    {NoteParameter::SustainFloorScale, "sustainFloorScale", "Sustain Floor Scale", "Multiplier applied to envelope floor for note-off decisions.", 0.10f, 2.5f, 0.01f, false},
    {NoteParameter::RetriggerGateScale, "retriggerGateScale", "Retrigger Gate Scale", "Threshold multiplier used to retrigger open strings.", 0.20f, 3.0f, 0.01f, false},
    {NoteParameter::PeakReleaseRatio, "peakReleaseRatio", "Peak Release Ratio", "Envelope decay target expressed as fraction of recent peak.", 0.02f, 0.60f, 0.005f, false},
    {NoteParameter::PitchTolerance, "pitchTolerance", "Pitch Tolerance", "Maximum cents deviation allowed per hop before smoothing.", 0.2f, 1.0f, 0.01f, false},
    {NoteParameter::TargetRms, "targetRms", "Target RMS", "Target RMS level for normalized signal.", 0.0001f, 0.35f, 0.0001f, false},
    {NoteParameter::CalibrationGainMultiplier, "calibrationGainMultiplier", "Gain Multiplier", "Fine-tune multiplier applied to calculated calibration gain.", 0.2f, 8.0f, 0.01f, false},
    {NoteParameter::LowCutMultiplier, "lowCutMultiplier", "Low Cut Multiplier", "Multiplier applied to open-string pitch to derive HPF cutoff.", 0.3f, 0.9f, 0.01f, false},
    {NoteParameter::HighCutMultiplier, "highCutMultiplier", "High Cut Multiplier", "Multiplier applied to 24th-fret pitch to derive LPF cutoff.", 0.8f, 1.8f, 0.02f, false},
    {NoteParameter::AubioThresholdScale, "aubioThresholdScale", "Onset Threshold (aubio)", "Scaling factor for aubio onset detection threshold.", 0.5f, 3.0f, 0.05f, false},
    {NoteParameter::OnsetSilenceDb, "onsetSilenceDb", "Onset Silence (dB)", "Silence level fed to aubio onset detector.", -120.f, -30.f, 1.f, true},
    {NoteParameter::PitchSilenceDb, "pitchSilenceDb", "Pitch Silence (dB)", "Silence level fed to aubio pitch tracker.", -120.f, -30.f, 1.f, true}
}};

} // namespace

NoteDetectionParameterSet makeDefaultNoteDetectionParameters() {
    return fromDefaults();
}

const std::array<ParameterDescriptor, 15>& parameterDescriptors() {
    return kDescriptors;
}

std::string defaultStringLabel(int stringIndex) {
    if (stringIndex < 0 || stringIndex >= static_cast<int>(kDefaultStringLabels.size()))
        return std::string("String ") + std::to_string(stringIndex + 1);
    return kDefaultStringLabels[static_cast<std::size_t>(stringIndex)];
}
