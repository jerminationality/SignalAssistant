#pragma once
/**
 * ButterworthFilter.h - Per-String Band-Pass Filtering
 * 
 * 2nd-order Butterworth filters for isolating fundamental frequency ranges
 * and suppressing mechanical crosstalk between hex pickup strings.
 * 
 * Filter Specifications (44.1kHz):
 * - String 6 (Low E): 75 Hz  to 800 Hz
 * - String 5 (A):     100 Hz to 1,000 Hz
 * - String 4 (D):     140 Hz to 1,400 Hz
 * - String 3 (G):     185 Hz to 2,000 Hz
 * - String 2 (B):     235 Hz to 2,800 Hz
 * - String 1 (High E):315 Hz to 4,000 Hz
 * 
 * DSP Order: [Raw Input] -> [Band-Pass Filter] -> [RMS/CQT Logic]
 */

#include <array>
#include <cmath>

namespace audio {

/**
 * Biquad filter coefficients (Direct Form II Transposed)
 */
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;  // Feedforward
    double a1 = 0.0, a2 = 0.0;            // Feedback (a0 normalized to 1.0)
};

/**
 * Single biquad filter stage
 */
class BiquadFilter {
public:
    BiquadFilter() = default;
    
    void setCoefficients(const BiquadCoeffs& coeffs) {
        m_coeffs = coeffs;
    }
    
    void reset() {
        m_z1 = m_z2 = 0.0;
    }
    
    /**
     * Process a single sample (Direct Form II Transposed)
     */
    inline double process(double in) {
        double out = in * m_coeffs.b0 + m_z1;
        m_z1 = in * m_coeffs.b1 + m_z2 - m_coeffs.a1 * out;
        m_z2 = in * m_coeffs.b2 - m_coeffs.a2 * out;
        return out;
    }
    
    /**
     * Process a block of samples in-place
     */
    void processBlock(float* samples, int count) {
        for (int i = 0; i < count; ++i) {
            samples[i] = static_cast<float>(process(static_cast<double>(samples[i])));
        }
    }
    
private:
    BiquadCoeffs m_coeffs;
    double m_z1 = 0.0, m_z2 = 0.0;  // Delay elements
};

/**
 * Calculate Butterworth highpass or lowpass coefficients
 * 
 * @param freq Cutoff frequency in Hz
 * @param sampleRate Sample rate in Hz
 * @param isHighPass True for highpass, false for lowpass
 * @return Normalized biquad coefficients
 */
inline BiquadCoeffs calculateButterworth(double freq, double sampleRate, bool isHighPass) {
    BiquadCoeffs coeffs;
    constexpr double Q = 0.70710678118;  // Standard Butterworth flatness (1/sqrt(2))
    
    const double omega = 2.0 * M_PI * freq / sampleRate;
    const double sn = std::sin(omega);
    const double cs = std::cos(omega);
    const double alpha = sn / (2.0 * Q);
    
    double b0, b1, b2, a0, a1, a2;
    
    if (isHighPass) {
        b0 = (1.0 + cs) / 2.0;
        b1 = -(1.0 + cs);
        b2 = (1.0 + cs) / 2.0;
    } else {  // Lowpass
        b0 = (1.0 - cs) / 2.0;
        b1 = 1.0 - cs;
        b2 = (1.0 - cs) / 2.0;
    }
    
    a0 = 1.0 + alpha;
    a1 = -2.0 * cs;
    a2 = 1.0 - alpha;
    
    // Normalize by a0
    coeffs.b0 = b0 / a0;
    coeffs.b1 = b1 / a0;
    coeffs.b2 = b2 / a0;
    coeffs.a1 = a1 / a0;
    coeffs.a2 = a2 / a0;
    
    return coeffs;
}

/**
 * Cascaded band-pass filter (highpass + lowpass)
 */
class BandPassFilter {
public:
    BandPassFilter() = default;
    
    /**
     * Configure the band-pass filter
     * 
     * @param lowCutoff  Highpass cutoff frequency (Hz)
     * @param highCutoff Lowpass cutoff frequency (Hz)
     * @param sampleRate Sample rate (Hz)
     */
    void configure(double lowCutoff, double highCutoff, double sampleRate) {
        m_highpass.setCoefficients(calculateButterworth(lowCutoff, sampleRate, true));
        m_lowpass.setCoefficients(calculateButterworth(highCutoff, sampleRate, false));
    }
    
    void reset() {
        m_highpass.reset();
        m_lowpass.reset();
    }
    
    /**
     * Process a single sample through the band-pass filter
     */
    inline double process(double in) {
        return m_lowpass.process(m_highpass.process(in));
    }
    
    /**
     * Process a block of samples in-place
     */
    void processBlock(float* samples, int count) {
        for (int i = 0; i < count; ++i) {
            samples[i] = static_cast<float>(process(static_cast<double>(samples[i])));
        }
    }
    
private:
    BiquadFilter m_highpass;
    BiquadFilter m_lowpass;
};

/**
 * Pre-CQT band-pass filter bank for 6-string guitar
 * 
 * Isolates each string's fundamental frequency range to reduce
 * crosstalk from adjacent strings on hex pickups.
 */
class HexBandPassBank {
public:
    HexBandPassBank() {
        configure(44100.0);  // Default sample rate
    }
    
    /**
     * Configure all filters for the given sample rate
     */
    void configure(double sampleRate) {
        // Filter specifications per string (Hz)
        // String indices: 0=High E, 1=B, 2=G, 3=D, 4=A, 5=Low E
        // Note: Reordered to match typical hex pickup wiring
        constexpr std::array<double, 6> lowCutoffs  = { 315.0, 235.0, 185.0, 140.0, 100.0,  75.0 };
        constexpr std::array<double, 6> highCutoffs = {4000.0, 2800.0, 2000.0, 1400.0, 1000.0, 800.0 };
        
        for (int s = 0; s < 6; ++s) {
            m_filters[s].configure(lowCutoffs[s], highCutoffs[s], sampleRate);
        }
        m_sampleRate = sampleRate;
    }
    
    /**
     * Reset all filter states (call on sample rate change or discontinuity)
     */
    void reset() {
        for (auto& filter : m_filters) {
            filter.reset();
        }
    }
    
    /**
     * Process a single sample for one string
     */
    inline float process(int stringIdx, float sample) {
        if (stringIdx < 0 || stringIdx >= 6) return sample;
        return static_cast<float>(m_filters[stringIdx].process(static_cast<double>(sample)));
    }
    
    /**
     * Process a block of samples for one string in-place
     */
    void processBlock(int stringIdx, float* samples, int count) {
        if (stringIdx < 0 || stringIdx >= 6) return;
        m_filters[stringIdx].processBlock(samples, count);
    }
    
    /**
     * Process all 6 strings for a single sample frame
     */
    void processFrame(std::array<float, 6>& samples) {
        for (int s = 0; s < 6; ++s) {
            samples[s] = static_cast<float>(m_filters[s].process(static_cast<double>(samples[s])));
        }
    }
    
    /**
     * Get current sample rate
     */
    double sampleRate() const { return m_sampleRate; }
    
private:
    std::array<BandPassFilter, 6> m_filters;
    double m_sampleRate = 44100.0;
};

} // namespace audio
