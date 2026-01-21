/**
 * YINDetector.cpp - YIN pitch detection algorithm implementation
 * 
 * Optimized for guitar pitch detection with special handling for low frequencies.
 */

#include "YINDetector.h"
#include <cstring>
#include <limits>

namespace audio {

YINDetector::YINDetector(int sampleRate, int bufferSize, float threshold)
    : m_sampleRate(sampleRate)
    , m_bufferSize(std::min(bufferSize, kMaxBufferSize))
    , m_threshold(std::clamp(threshold, 0.01f, 0.50f))
    , m_minFreq(60.0f)      // Low E slack tuning (~61Hz)
    , m_maxFreq(1400.0f)    // 24th fret high E (~1319Hz) + margin
{
    // Initialize buffers to zero
    m_diffBuffer.fill(0.0f);
    m_cmndfBuffer.fill(0.0f);
    
    // Compute tau bounds from frequency range
    // Period τ = sampleRate / frequency
    // minTau corresponds to maxFreq (short period = high freq)
    // maxTau corresponds to minFreq (long period = low freq)
    m_minTau = std::max(2, static_cast<int>(m_sampleRate / m_maxFreq));
    m_maxTau = std::min(m_bufferSize / 2, static_cast<int>(m_sampleRate / m_minFreq));
}

void YINDetector::setSampleRate(int sampleRate) {
    m_sampleRate = sampleRate;
    // Recompute tau bounds
    m_minTau = std::max(2, static_cast<int>(m_sampleRate / m_maxFreq));
    m_maxTau = std::min(m_bufferSize / 2, static_cast<int>(m_sampleRate / m_minFreq));
}

void YINDetector::setMinFrequency(float minFreq) {
    m_minFreq = std::max(20.0f, minFreq);
    m_maxTau = std::min(m_bufferSize / 2, static_cast<int>(m_sampleRate / m_minFreq));
}

void YINDetector::setMaxFrequency(float maxFreq) {
    m_maxFreq = std::min(maxFreq, static_cast<float>(m_sampleRate / 2));
    m_minTau = std::max(2, static_cast<int>(m_sampleRate / m_maxFreq));
}

YINResult YINDetector::detect(const float* audioBuffer, int numSamples) {
    YINResult result = {0.0f, 0.0f, false, 0.0f};
    
    // Validate input
    if (!audioBuffer || numSamples < 4) {
        return result;
    }
    
    // Use available samples, clamped to our buffer capacity
    const int N = std::min({numSamples, m_bufferSize, kMaxBufferSize});
    const int halfN = N / 2;
    
    // Ensure maxTau doesn't exceed available analysis range
    const int effectiveMaxTau = std::min(m_maxTau, halfN - 1);
    if (effectiveMaxTau <= m_minTau) {
        return result; // Buffer too small for desired frequency range
    }
    
    // Step 1: Compute difference function d(τ)
    computeDifference(audioBuffer, N);
    
    // Step 2: Compute Cumulative Mean Normalized Difference Function
    computeCMNDF();
    
    // Step 3: Find tau via absolute threshold (with deepest minimum fallback)
    int tauEstimate = absoluteThreshold();
    
    if (tauEstimate < m_minTau) {
        return result; // No valid pitch found
    }
    
    // Step 4: Parabolic interpolation for sub-sample accuracy
    float refinedTau = parabolicInterpolation(tauEstimate);
    
    // Convert period to frequency
    if (refinedTau > 0.0f) {
        result.frequency = static_cast<float>(m_sampleRate) / refinedTau;
        result.confidence = 1.0f - m_cmndfBuffer[tauEstimate];
        result.isPitched = true;
        result.periodSamples = refinedTau;
        
        // Final sanity check on frequency bounds
        if (result.frequency < m_minFreq || result.frequency > m_maxFreq) {
            result.isPitched = false;
            result.frequency = 0.0f;
            result.confidence = 0.0f;
        }
    }
    
    return result;
}

void YINDetector::computeDifference(const float* buffer, int N) {
    /**
     * Difference function: d(τ) = Σ_{j=0}^{W-1} (x[j] - x[j+τ])²
     * where W = N/2 (half-buffer analysis window)
     * 
     * This measures how similar the signal is to a time-shifted version.
     * For periodic signals, d(τ) has minima at τ = period.
     */
    
    const int halfN = N / 2;
    const int effectiveMaxTau = std::min(m_maxTau, halfN - 1);
    
    // d(0) = 0 by definition
    m_diffBuffer[0] = 0.0f;
    
    // Compute d(τ) for τ = 1 to effectiveMaxTau
    for (int tau = 1; tau <= effectiveMaxTau; ++tau) {
        float sum = 0.0f;
        
        // Sum squared differences over the analysis window
        // Window size = halfN - tau to ensure we don't read past buffer
        const int windowSize = halfN;
        for (int j = 0; j < windowSize; ++j) {
            const float delta = buffer[j] - buffer[j + tau];
            sum += delta * delta;
        }
        
        m_diffBuffer[tau] = sum;
    }
}

void YINDetector::computeCMNDF() {
    /**
     * Cumulative Mean Normalized Difference Function:
     * d'(τ) = d(τ) / [(1/τ) * Σ_{j=1}^{τ} d(j)]
     * 
     * Equivalently: d'(τ) = τ * d(τ) / Σ_{j=1}^{τ} d(j)
     * 
     * This normalizes d(τ) by the cumulative mean, making the threshold
     * frequency-independent. d'(τ) starts at 1.0 and dips below for periodic signals.
     */
    
    m_cmndfBuffer[0] = 1.0f;  // d'(0) = 1 by convention
    
    float runningSum = 0.0f;
    const int effectiveMaxTau = std::min(m_maxTau, m_bufferSize / 2 - 1);
    
    for (int tau = 1; tau <= effectiveMaxTau; ++tau) {
        runningSum += m_diffBuffer[tau];
        
        if (runningSum > 0.0f) {
            // d'(τ) = d(τ) * τ / runningSum
            m_cmndfBuffer[tau] = m_diffBuffer[tau] * static_cast<float>(tau) / runningSum;
        } else {
            m_cmndfBuffer[tau] = 1.0f;
        }
    }
}

int YINDetector::absoluteThreshold() {
    /**
     * Find the first τ where d'(τ) < threshold AND is a local minimum.
     * 
     * CRITICAL LOW-FREQUENCY OPTIMIZATION:
     * For notes below 150Hz (like Low E at 82Hz), the CMNDF may not dip
     * below the threshold due to harmonic interference. In this case,
     * we fall back to finding the "deepest minimum" - the τ with the
     * lowest d'(τ) value, which typically corresponds to the fundamental.
     * 
     * This is essential for reliable Low E string detection.
     */
    
    const int effectiveMaxTau = std::min(m_maxTau, m_bufferSize / 2 - 1);
    
    // Track the deepest minimum for fallback
    int deepestMinTau = -1;
    float deepestMinValue = std::numeric_limits<float>::max();
    
    // Search for first tau below threshold that's also a local minimum
    for (int tau = m_minTau; tau < effectiveMaxTau; ++tau) {
        const float current = m_cmndfBuffer[tau];
        const float prev = m_cmndfBuffer[tau - 1];
        const float next = m_cmndfBuffer[tau + 1];
        
        // Track deepest minimum
        if (current < deepestMinValue && current < prev && current < next) {
            deepestMinValue = current;
            deepestMinTau = tau;
        }
        
        // Check if below threshold AND local minimum
        if (current < m_threshold) {
            // Find the local minimum in this dip
            while (tau + 1 < effectiveMaxTau && m_cmndfBuffer[tau + 1] < m_cmndfBuffer[tau]) {
                ++tau;
            }
            return tau;
        }
    }
    
    // FALLBACK: For low frequencies, use deepest minimum if threshold not crossed
    // This handles Low E string (82Hz) where harmonics can interfere
    // Only use fallback if the deepest minimum is reasonably good (< 0.30)
    constexpr float kFallbackMaxThreshold = 0.30f;
    if (deepestMinTau > 0 && deepestMinValue < kFallbackMaxThreshold) {
        // Verify this corresponds to a low frequency (< 150Hz)
        float estimatedFreq = static_cast<float>(m_sampleRate) / deepestMinTau;
        if (estimatedFreq < 150.0f) {
            return deepestMinTau;
        }
    }
    
    // No valid pitch found
    return -1;
}

float YINDetector::parabolicInterpolation(int tauEstimate) {
    /**
     * Parabolic interpolation for sub-sample accuracy
     * 
     * Fits a parabola through the three points around tauEstimate:
     * (tau-1, d'[tau-1]), (tau, d'[tau]), (tau+1, d'[tau+1])
     * 
     * Returns the interpolated tau at the parabola's minimum.
     * This typically improves accuracy from ~1 cent to ~0.1 cent.
     */
    
    if (tauEstimate <= m_minTau || tauEstimate >= m_maxTau - 1) {
        return static_cast<float>(tauEstimate);
    }
    
    const float s0 = m_cmndfBuffer[tauEstimate - 1];
    const float s1 = m_cmndfBuffer[tauEstimate];
    const float s2 = m_cmndfBuffer[tauEstimate + 1];
    
    // Parabolic interpolation formula:
    // offset = (s0 - s2) / (2 * (s0 - 2*s1 + s2))
    const float denom = 2.0f * (s0 - 2.0f * s1 + s2);
    
    if (std::abs(denom) < 1e-9f) {
        return static_cast<float>(tauEstimate);
    }
    
    const float offset = (s0 - s2) / denom;
    
    // Clamp offset to reasonable range (-1, 1)
    const float clampedOffset = std::clamp(offset, -1.0f, 1.0f);
    
    return static_cast<float>(tauEstimate) + clampedOffset;
}

} // namespace audio
