#pragma once

#include <algorithm>
#include <cstdint>

#include "NoteDetectionStore.h"

namespace trackerparams {

constexpr std::array<int, 6>   kFftMultipliers{{8, 7, 6, 5, 4, 4}};

inline std::uint64_t settingsGeneration() {
    return NoteDetectionStore::instance().activeGeneration();
}

inline float active(NoteParameter param, int s, float fallback) {
    if (s < 0 || s >= kNumStrings)
        return fallback;
    return NoteDetectionStore::instance().activeValue(param, s);
}

inline float lowCutMultiplier(int s) {
    const float fallback = (s == 0) ? 0.45f : (s == 1 ? 0.50f : (s == 2 ? 0.58f : 0.65f));
    return active(NoteParameter::LowCutMultiplier, s, fallback);
}

inline float highCutMultiplier(int s) {
    const float fallback = (s == 0) ? 1.35f : (s == 1 ? 1.28f : (s == 2 ? 1.18f : 1.10f));
    return active(NoteParameter::HighCutMultiplier, s, fallback);
}

inline float onsetThresholdScale(int s, float base) {
    const float fallback = base;
    return base * active(NoteParameter::OnsetThresholdScale, s, fallback);
}

inline float baselineFloor(int s) {
    return active(NoteParameter::BaselineFloor, s, 0.0004f);
}

inline float gateRatio(int s) {
    return active(NoteParameter::GateRatio, s, 0.2f);
}

inline float envelopeFloor(int s) {
    return active(NoteParameter::EnvelopeFloor, s, 0.0008f);
}

inline float targetRms(int s) {
    return active(NoteParameter::TargetRms, s, 0.0018f);
}

inline float calibrationGainMultiplier(int s) {
    return active(NoteParameter::CalibrationGainMultiplier, s, 1.0f);
}

inline float peakReleaseRatio(int s) {
    return active(NoteParameter::PeakReleaseRatio, s, 0.15f);
}

inline float sustainFloorScale(int s) {
    return active(NoteParameter::SustainFloorScale, s, 1.0f);
}

inline float retriggerGateScale(int s) {
    return active(NoteParameter::RetriggerGateScale, s, 1.0f);
}

inline int fftMultiple(int s) {
    if (s < 0 || s >= static_cast<int>(kFftMultipliers.size()))
        return 4;
    return kFftMultipliers[static_cast<std::size_t>(s)];
}

inline float pitchTolerance(int s) {
    return active(NoteParameter::PitchTolerance, s, 0.40f);
}

inline float aubioThresholdScale(int s) {
    const float fallback = (s == 0) ? 1.2f : (s == 1 ? 1.35f : (s == 2 ? 1.6f : 1.8f));
    return active(NoteParameter::AubioThresholdScale, s, fallback);
}

inline float onsetSilenceDb(int s) {
    const float fallback = (s <= 1) ? -85.f : -75.f;
    return active(NoteParameter::OnsetSilenceDb, s, fallback);
}

inline float pitchSilenceDb(int s) {
    const float fallback = (s <= 1) ? -90.f : -80.f;
    return active(NoteParameter::PitchSilenceDb, s, fallback);
}

} // namespace trackerparams
