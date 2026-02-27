#include "NoteDetectionConfig.h"

#include <algorithm>

namespace {

// ── Default per-string values (Section 9A) ──────────────────────────────
constexpr std::array<float, 6> kDefaultTouchSensitivity      {{0.08f, 0.08f, 0.08f, 0.08f, 0.08f, 0.08f}};
constexpr std::array<float, 6> kDefaultAttackResponse        {{1.5f,  1.5f,  1.5f,  1.5f,  1.5f,  1.5f}};
constexpr std::array<float, 6> kDefaultSustainTail           {{0.02f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f}};
constexpr std::array<float, 6> kDefaultLegatoSpeed           {{2.f,   2.f,   2.f,   2.f,   2.f,   2.f}};
constexpr std::array<float, 6> kDefaultTrackingStability     {{0.65f, 0.65f, 0.65f, 0.65f, 0.65f, 0.65f}};
constexpr std::array<float, 6> kDefaultCalibrationGainMult   {{1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f}};

constexpr std::array<const char*, 6> kDefaultStringLabels {{"E", "A", "D", "G", "B", "e"}};

NoteDetectionParameterSet fromDefaults() {
    NoteDetectionParameterSet set;
    set.touchSensitivity        = kDefaultTouchSensitivity;
    set.attackResponse          = kDefaultAttackResponse;
    set.sustainTail             = kDefaultSustainTail;
    set.legatoSpeed             = kDefaultLegatoSpeed;
    set.trackingStability       = kDefaultTrackingStability;
    set.calibrationGainMultiplier = kDefaultCalibrationGainMult;
    return set;
}

const std::array<ParameterDescriptor, kNumNoteParameters> kDescriptors {{
    {NoteParameter::TouchSensitivity,        "touchSensitivity",        "Touch Sensitivity",  "Delta added to adaptive noise floor for onset threshold (lower = more sensitive).", 0.01f, 0.25f, 0.005f, false},
    {NoteParameter::AttackResponse,          "attackResponse",          "Attack Response",    "Retrigger multiplier: NewPeak must exceed CurrentRMS * this value.",                1.1f,  3.0f,  0.05f, false},
    {NoteParameter::SustainTail,             "sustainTail",             "Sustain Tail",       "RMS exit threshold for note-off (lower = longer sustain detection).",               0.005f, 0.1f, 0.001f, false},
    {NoteParameter::LegatoSpeed,             "legatoSpeed",             "Legato Speed",       "Minimum consecutive stable-pitch frames before repitch fires.",                    1.0f,  5.0f,  1.0f,  false},
    {NoteParameter::TrackingStability,       "trackingStability",       "Tracking Stability", "Minimum pitch confidence to accept pitch lock (lower = more lenient).",             0.4f,  0.95f, 0.01f, false},
    {NoteParameter::CalibrationGainMultiplier, "calibrationGainMultiplier", "Gain Trim",      "Per-string gain multiplier (set by calibration routine).",                         0.2f,  8.0f,  0.01f, false}
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
