#include "NoteDetectionConfig.h"

#include <algorithm>

namespace {
constexpr std::array<float, 6> kDefaultOnsetThresholdScale {{0.36f, 0.61f, 0.93f, 1.26f, 1.44f, 1.62f}};
constexpr std::array<float, 6> kDefaultBaselineFloor {{0.0010f, 0.0012f, 0.0014f, 0.0016f, 0.0018f, 0.0020f}};
constexpr std::array<float, 6> kDefaultEnvelopeFloor {{0.0015f, 0.0018f, 0.0021f, 0.0024f, 0.0027f, 0.0030f}};
constexpr std::array<float, 6> kDefaultGateRatio {{0.055f, 0.10f, 0.13f, 0.17f, 0.21f, 0.25f}};
constexpr std::array<float, 6> kDefaultSustainFloorScale {{0.58f, 0.70f, 0.82f, 1.0f, 1.0f, 1.0f}};
constexpr std::array<float, 6> kDefaultRetriggerGateScale {{1.40f, 1.25f, 1.10f, 1.0f, 1.0f, 1.0f}};
constexpr std::array<float, 6> kDefaultPeakReleaseRatio {{0.12f, 0.13f, 0.14f, 0.16f, 0.18f, 0.20f}};
constexpr std::array<float, 6> kDefaultPitchTolerance {{0.25f, 0.40f, 0.45f, 0.44f, 0.50f, 0.55f}};
constexpr std::array<float, 6> kDefaultTargetRms {{0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f}};
constexpr std::array<float, 6> kDefaultCalibrationGainMultiplier {{5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f}};
constexpr std::array<float, 6> kDefaultLowCutMultiplier {{77.5f, 102.5f, 137.5f, 187.5f, 237.5f, 317.5f}};
constexpr std::array<float, 6> kLowCutMin {{70.f, 95.f, 130.f, 180.f, 230.f, 310.f}};
constexpr std::array<float, 6> kLowCutMax {{80.f, 105.f, 140.f, 190.f, 240.f, 320.f}};
constexpr std::array<float, 6> kDefaultHighCutMultiplier {{135.f, 215.f, 290.f, 385.f, 485.f, 650.f}};
constexpr std::array<float, 6> kHighCutMin {{125.f, 210.f, 285.f, 380.f, 480.f, 645.f}};
constexpr std::array<float, 6> kHighCutMax {{165.f, 220.f, 295.f, 390.f, 490.f, 655.f}};
constexpr std::array<float, 6> kDefaultAubioThresholdScale {{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
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
    {NoteParameter::OnsetThresholdScale, "onsetThresholdScale", "Onset Threshold", "Aubio onset detection threshold (spectral flux).", 0.1f, 5.0f, 0.01f, false},
    {NoteParameter::BaselineFloor, "baselineFloor", "Baseline Floor", "Absolute noise floor.", 0.001f, 0.0100f, 0.0001f, false},
    {NoteParameter::EnvelopeFloor, "envelopeFloor", "Envelope Floor", "Minimum RMS before envelope resets to zero.", 0.001f, 0.05f, 0.0001f, false},
    {NoteParameter::GateRatio, "gateRatio", "Gate Ratio", "Multiplier applied to envelope floor for note-on threshold.", 0.005f, 10.0f, 0.005f, false},
    {NoteParameter::SustainFloorScale, "sustainFloorScale", "Sustain Floor Scale", "Multiplier applied to envelope floor for note-off threshold.", 0.10f, 2.5f, 0.01f, false},
    {NoteParameter::RetriggerGateScale, "retriggerGateScale", "Retrigger Gate Scale", "Multiplier applied to max(sustainFloor, cappedPeak × 0.4) for retrigger detection.", 0.20f, 3.0f, 0.01f, false},
    {NoteParameter::PitchTolerance, "pitchTolerance", "Pitch Tolerance", "Maximum cents deviation allowed per hop before smoothing.", 0.2f, 1.0f, 0.01f, false},
    {NoteParameter::TargetRms, "targetRms", "Target RMS", "Target RMS level for normalized signal.", 0.0001f, 0.35f, 0.0001f, false},
    {NoteParameter::CalibrationGainMultiplier, "calibrationGainMultiplier", "Gain Multiplier", "Fine-tune multiplier applied to calculated calibration gain.", 0.2f, 8.0f, 0.01f, false},
    {NoteParameter::LowCutMultiplier, "lowCutMultiplier", "High Pass (Hz)", "High-pass filter cutoff frequency.", 70.f, 325.f, 0.5f, false, true, kLowCutMin, kLowCutMax},
    {NoteParameter::HighCutMultiplier, "highCutMultiplier", "Low Pass (Hz)", "Low-pass filter cutoff frequency.", 150.f, 660.f, 1.f, false, true, kHighCutMin, kHighCutMax},
    {NoteParameter::AubioThresholdScale, "aubioThresholdScale", "Onset Threshold (aubio)", "Scaling factor for aubio onset detection threshold.", 0.5f, 3.0f, 0.05f, true},
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
