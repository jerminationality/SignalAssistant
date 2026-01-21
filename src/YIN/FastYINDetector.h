#pragma once
/**
 * FastYINDetector.h - Dual-buffer YIN with onset detection
 * 
 * VERSION 3 - Onset triggers on RMS, pitch confirms later
 * 
 * Key insight: YIN often can't detect pitch during attack transients
 * because the signal is non-periodic. We trigger onset on RMS rise,
 * then wait for pitch to stabilize during the sustain phase.
 */

#include "YINDetector.h"
#include <array>
#include <cmath>

namespace audio {

/**
 * Note detection state machine
 */
enum class NoteState {
    IDLE,       // No note playing (below noise gate)
    ATTACK,     // New note onset detected (waiting for pitch)
    SUSTAIN,    // Note sustaining (pitch confirmed or timeout)
    RELEASE     // Note releasing (RMS dropping)
};

/**
 * Parameters for FastYIN detection
 */
struct FastYINParams {
    float yinThreshold = 0.10f;       // YIN confidence threshold (0.05-0.30)
    float noiseGateRMS = 0.015f;      // Minimum RMS for note detection
    float onsetSensitivity = 1.4f;    // RMS ratio for onset (1.2-2.0)
    float releaseRatio = 0.35f;       // Release when RMS < peak * releaseRatio
    int fretStabilityFrames = 2;      // Frames to confirm fret change
};

/**
 * Result from FastYIN processing
 */
struct FastYINResult {
    YINResult pitchResult;     // From full YIN analysis
    NoteState state;           // Current note state
    bool isOnset;              // True on new note attack
    bool isSustaining;         // True while note held (ATTACK or SUSTAIN)
    float currentRMS;          // Current signal RMS
    float peakRMS;             // Peak RMS since attack
    float onsetThreshold;      // Adaptive threshold that triggered onset
    int detectedFret;          // Fret number (-1 if none)
    int stableFret;            // Stability-confirmed fret
};

/**
 * Fast YIN Detector with dual-buffer onset detection
 */
class FastYINDetector {
public:
    FastYINDetector(int sampleRate = 48000, 
                    int onsetBufferSize = 512,
                    int pitchBufferSize = 2048,
                    int stringIdx = 0);
    ~FastYINDetector() = default;
    
    FastYINDetector(const FastYINDetector&) = delete;
    FastYINDetector& operator=(const FastYINDetector&) = delete;
    FastYINDetector(FastYINDetector&&) noexcept = default;
    FastYINDetector& operator=(FastYINDetector&&) noexcept = default;
    
    FastYINResult process(const float* newSamples, int numSamples, 
                          const FastYINParams& params);
    
    void reset();
    void setSampleRate(int sampleRate);
    int sampleRate() const { return m_sampleRate; }
    NoteState state() const { return m_state; }
    int stringIndex() const { return m_stringIdx; }

private:
    static float computeRMS(const float* buffer, int numSamples);
    void updateStateMachine(float rms, const YINResult& pitch, const FastYINParams& params);
    int updateFretStability(int detectedFret, const FastYINParams& params);
    
    int m_sampleRate;
    int m_onsetBufferSize;
    int m_pitchBufferSize;
    int m_stringIdx;
    
    YINDetector m_onsetYIN;
    YINDetector m_pitchYIN;
    
    static constexpr int kMaxPitchBuffer = 4096;
    std::array<float, kMaxPitchBuffer> m_ringBuffer;
    int m_writePos = 0;
    int m_samplesAccumulated = 0;
    
    // State machine
    NoteState m_state = NoteState::IDLE;
    float m_peakRMS = 0.0f;
    float m_lastRMS = 0.0f;
    float m_lastOnsetThreshold = 0.0f;
    int m_sustainFrameCount = 0;
    int m_attackFrameCount = 0;      // Frames spent in ATTACK waiting for pitch
    
    // Fret stability
    int m_currentFret = -1;
    int m_candidateFret = -1;
    int m_fretStabilityCount = 0;
    
    YINResult m_lastPitch;
};

// Helper functions unchanged
inline std::pair<int, int> getAdaptiveBufferSizes(int stringIdx) {
    switch (stringIdx) {
        case 0: case 1: return {512, 2048};
        case 2: case 3: return {512, 1024};
        case 4: case 5: default: return {256, 512};
    }
}

inline float getStringMinFrequency(int stringIdx) {
    constexpr float kMinFreq[6] = {74.0f, 99.0f, 132.0f, 176.0f, 222.0f, 296.0f};
    if (stringIdx < 0 || stringIdx >= 6) return 60.0f;
    return kMinFreq[stringIdx];
}

inline float getStringMaxFrequency(int stringIdx) {
    constexpr float kMaxFreq[6] = {350.0f, 467.0f, 623.0f, 831.0f, 1046.0f, 1400.0f};
    if (stringIdx < 0 || stringIdx >= 6) return 1400.0f;
    return kMaxFreq[stringIdx];
}

} // namespace audio
