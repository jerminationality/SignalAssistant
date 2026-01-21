#pragma once
/**
 * YINConfig.h - YIN pitch detection configuration
 * 
 * Simplified 5-parameter system (vs 9 CQT parameters):
 * - yinThreshold:       Pitch confidence threshold (0.05-0.30)
 * - noiseGateRMS:       Minimum RMS for note detection (0.001-0.10)
 * - onsetSensitivity:   RMS ratio for onset detection (1.1-3.0)
 * - releaseRatio:       Release threshold ratio (0.1-0.8)
 * - fretStabilityFrames: Fret stability hysteresis (1-10)
 * 
 * Parameters are global (not per-string) for simplicity.
 */

#include <array>
#include <atomic>
#include <string>

namespace audio {

/**
 * YIN detection parameters
 */
struct YINParameters {
    float yinThreshold = 0.10f;       // Pitch confidence threshold
    float noiseGateRMS = 0.015f;      // Minimum RMS for detection
    float onsetSensitivity = 1.4f;    // RMS ratio for onset
    float releaseRatio = 0.35f;       // Release threshold ratio
    int fretStabilityFrames = 2;      // Fret stability hysteresis
    
    // Per-string gain adjustments (from calibration)
    std::array<float, 6> stringGain = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
};

/**
 * Default YIN parameters
 */
inline YINParameters makeDefaultYINParameters() {
    return YINParameters{};
}

/**
 * Parameter descriptor for UI
 */
struct YINParameterDescriptor {
    std::string key;
    std::string label;
    std::string description;
    float minValue;
    float maxValue;
    float step;
    float defaultValue;
};

/**
 * Get parameter descriptors for UI generation
 */
inline const std::array<YINParameterDescriptor, 5>& yinParameterDescriptors() {
    static const std::array<YINParameterDescriptor, 5> descriptors = {{
        {"yinThreshold", "YIN Threshold", "Pitch confidence threshold (lower = stricter)", 
         0.05f, 0.30f, 0.01f, 0.10f},
        {"noiseGateRMS", "Noise Gate", "Minimum RMS for note detection", 
         0.001f, 0.10f, 0.001f, 0.015f},
        {"onsetSensitivity", "Onset Sensitivity", "RMS ratio for attack detection (higher = less sensitive)", 
         1.1f, 3.0f, 0.1f, 1.4f},
        {"releaseRatio", "Release Ratio", "Release when RMS drops to this ratio of peak", 
         0.1f, 0.8f, 0.05f, 0.35f},
        {"fretStabilityFrames", "Fret Stability", "Frames to confirm fret change (1=fast, 5=solid)", 
         1.0f, 10.0f, 1.0f, 2.0f}
    }};
    return descriptors;
}

/**
 * Atomic version of YIN parameters for thread-safe access
 */
struct YINParametersAtomic {
    std::atomic<float> yinThreshold{0.10f};
    std::atomic<float> noiseGateRMS{0.015f};
    std::atomic<float> onsetSensitivity{1.4f};
    std::atomic<float> releaseRatio{0.35f};
    std::atomic<int> fretStabilityFrames{2};
    std::array<std::atomic<float>, 6> stringGain = {};
    
    YINParametersAtomic() {
        for (auto& g : stringGain) {
            g.store(1.0f);
        }
    }
    
    void store(const YINParameters& params) {
        yinThreshold.store(params.yinThreshold, std::memory_order_relaxed);
        noiseGateRMS.store(params.noiseGateRMS, std::memory_order_relaxed);
        onsetSensitivity.store(params.onsetSensitivity, std::memory_order_relaxed);
        releaseRatio.store(params.releaseRatio, std::memory_order_relaxed);
        fretStabilityFrames.store(params.fretStabilityFrames, std::memory_order_release);
        for (int i = 0; i < 6; ++i) {
            stringGain[i].store(params.stringGain[i], std::memory_order_relaxed);
        }
    }
    
    YINParameters load() const {
        YINParameters params;
        params.yinThreshold = yinThreshold.load(std::memory_order_relaxed);
        params.noiseGateRMS = noiseGateRMS.load(std::memory_order_relaxed);
        params.onsetSensitivity = onsetSensitivity.load(std::memory_order_relaxed);
        params.releaseRatio = releaseRatio.load(std::memory_order_relaxed);
        params.fretStabilityFrames = fretStabilityFrames.load(std::memory_order_acquire);
        for (int i = 0; i < 6; ++i) {
            params.stringGain[i] = stringGain[i].load(std::memory_order_relaxed);
        }
        return params;
    }
};

} // namespace audio
