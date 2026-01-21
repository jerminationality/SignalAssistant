#pragma once
/**
 * YINDetector.h - YIN pitch detection algorithm for monophonic audio
 * 
 * YIN is a time-domain autocorrelation-based pitch detector optimized for:
 * - Low latency (no FFT required)
 * - Excellent low-frequency detection (critical for Low E string at 82Hz)
 * - High accuracy with sub-cent precision via parabolic interpolation
 * 
 * Algorithm Reference:
 * de Cheveigné, A., & Kawahara, H. (2002). "YIN, a fundamental frequency 
 * estimator for speech and music." JASA 111(4), 1917-1930.
 * 
 * Key optimizations for guitar:
 * - "Deepest minimum" fallback for low strings where threshold may not be crossed
 * - Configurable tau search range for frequency band limiting
 * - Zero heap allocation in detect() path for real-time safety
 */

#include <cstdint>
#include <cmath>
#include <array>
#include <algorithm>

namespace audio {

/**
 * Result from YIN pitch detection
 */
struct YINResult {
    float frequency;      // Detected frequency in Hz (0 if not pitched)
    float confidence;     // Pitch confidence 0.0-1.0 (1.0 - CMNDF minimum)
    bool isPitched;       // True if valid pitch detected above threshold
    float periodSamples;  // Detected period in samples (for debugging)
};

/**
 * YIN Pitch Detector
 * 
 * Implements the full YIN algorithm with low-frequency optimization:
 * 1. Difference function: d(τ) = Σ(x[j] - x[j+τ])²
 * 2. Cumulative Mean Normalized Difference Function (CMNDF)
 * 3. Absolute threshold detection with "deepest minimum" fallback
 * 4. Parabolic interpolation for sub-sample accuracy
 * 
 * Thread Safety: 
 * - detect() is reentrant (no shared mutable state)
 * - Not thread-safe if called concurrently on same instance
 */
class YINDetector {
public:
    /**
     * Construct YIN detector
     * 
     * @param sampleRate Audio sample rate (e.g., 48000)
     * @param bufferSize Analysis window size (2048 recommended for low freq)
     * @param threshold  CMNDF threshold (0.10 typical, lower = stricter)
     */
    YINDetector(int sampleRate = 48000, int bufferSize = 2048, float threshold = 0.10f);
    ~YINDetector() = default;
    
    // Non-copyable (owns internal buffers)
    YINDetector(const YINDetector&) = delete;
    YINDetector& operator=(const YINDetector&) = delete;
    
    // Move-constructible
    YINDetector(YINDetector&&) noexcept = default;
    YINDetector& operator=(YINDetector&&) noexcept = default;
    
    /**
     * Detect pitch from audio buffer
     * 
     * @param audioBuffer Input audio samples (mono, float, -1.0 to 1.0)
     * @param numSamples  Number of samples in buffer (should match bufferSize)
     * @return YINResult with frequency, confidence, and pitched flag
     * 
     * CRITICAL: For low frequencies (< 150Hz), uses "deepest minimum" fallback
     * if threshold is not crossed, ensuring Low E string (82Hz) detection.
     */
    YINResult detect(const float* audioBuffer, int numSamples);
    
    /**
     * Set YIN threshold
     * 
     * @param threshold CMNDF threshold (0.05-0.30 typical)
     *                  Lower = more strict pitch detection
     *                  Higher = more permissive (may detect noise)
     */
    void setThreshold(float threshold) { m_threshold = std::clamp(threshold, 0.01f, 0.50f); }
    float threshold() const { return m_threshold; }
    
    /**
     * Set minimum detectable frequency
     * 
     * @param minFreq Minimum frequency in Hz (default 60Hz for Low E slack)
     *                Affects tau search range: maxTau = sampleRate / minFreq
     */
    void setMinFrequency(float minFreq);
    float minFrequency() const { return m_minFreq; }
    
    /**
     * Set maximum detectable frequency
     * 
     * @param maxFreq Maximum frequency in Hz (default 1400Hz for 24th fret high E)
     *                Affects tau search range: minTau = sampleRate / maxFreq
     */
    void setMaxFrequency(float maxFreq);
    float maxFrequency() const { return m_maxFreq; }
    
    /**
     * Update sample rate (recomputes tau bounds)
     */
    void setSampleRate(int sampleRate);
    int sampleRate() const { return m_sampleRate; }
    
    /**
     * Get buffer size
     */
    int bufferSize() const { return m_bufferSize; }

private:
    // Step 1: Compute difference function d(τ)
    void computeDifference(const float* buffer, int N);
    
    // Step 2: Compute CMNDF d'(τ) = d(τ) / [(1/τ) * Σd(j)]
    void computeCMNDF();
    
    // Step 3: Find first tau below threshold (or deepest minimum for low freq)
    int absoluteThreshold();
    
    // Step 4: Parabolic interpolation for sub-sample accuracy
    float parabolicInterpolation(int tauEstimate);
    
    // Configuration
    int m_sampleRate;
    int m_bufferSize;
    float m_threshold;
    float m_minFreq;
    float m_maxFreq;
    
    // Computed from frequency bounds
    int m_minTau;  // sampleRate / maxFreq
    int m_maxTau;  // sampleRate / minFreq (clamped to bufferSize/2)
    
    // Pre-allocated working buffers (avoid heap allocation in detect())
    static constexpr int kMaxBufferSize = 4096;
    std::array<float, kMaxBufferSize> m_diffBuffer;    // Difference function d(τ)
    std::array<float, kMaxBufferSize> m_cmndfBuffer;   // CMNDF d'(τ)
};

/**
 * Convert frequency to MIDI note number
 * 
 * @param freq Frequency in Hz
 * @return MIDI note number (69 = A4 = 440Hz)
 */
inline int frequencyToMIDI(float freq) {
    if (freq <= 0.0f) return -1;
    return static_cast<int>(std::round(12.0f * std::log2(freq / 440.0f) + 69.0f));
}

/**
 * Convert MIDI note to frequency
 * 
 * @param midi MIDI note number
 * @return Frequency in Hz
 */
inline float midiToFrequency(int midi) {
    return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f);
}

/**
 * Convert frequency to fret number for a given string
 * 
 * @param freq Frequency in Hz
 * @param stringIdx String index 0-5 (Low E to High E)
 * @return Fret number 0-24, or -1 if out of range
 * 
 * Standard tuning open string MIDI notes:
 * String 0 (Low E): MIDI 40 (82.41 Hz)
 * String 1 (A):     MIDI 45 (110.00 Hz)
 * String 2 (D):     MIDI 50 (146.83 Hz)
 * String 3 (G):     MIDI 55 (196.00 Hz)
 * String 4 (B):     MIDI 59 (246.94 Hz)
 * String 5 (High E): MIDI 64 (329.63 Hz)
 */
inline int frequencyToFret(float freq, int stringIdx) {
    constexpr int kOpenStringMIDI[6] = {40, 45, 50, 55, 59, 64};
    
    if (stringIdx < 0 || stringIdx >= 6) return -1;
    if (freq <= 0.0f) return -1;
    
    int midi = frequencyToMIDI(freq);
    int fret = midi - kOpenStringMIDI[stringIdx];
    
    // Clamp to valid fret range
    if (fret < 0 || fret > 24) return -1;
    return fret;
}

} // namespace audio
