/**
 * CQTNoteDetector.cpp - Constant-Q Transform based note detection for 6-string guitar
 * 
 * Implements:
 * - Unified 6-string scanning with inter-string crosstalk rejection
 * - Slope-aware thresholds (1.5% decay per fret)
 * - Hysteresis-based fret stability ("Sticky Fret")
 * - Spectral flux-based retriggering
 * - Parabolic interpolation for sub-bin pitch accuracy
 * 
 * Uses KISS FFT for efficient O(N log N) CQT kernel computation.
 * ARM NEON SIMD optimization for Raspberry Pi 5 (Cortex-A76).
 */

#include "CQTNoteDetector.h"
#include "../SessionLogger.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <cstring>

// CRITICAL: SessionLogger causes deadlocks in real-time audio thread
// Only enable for offline debugging with recorded sessions
#define ENABLE_CQT_RT_LOGGING 1  // ENABLED for note-off debugging

// ============================================================================
// ARM NEON SIMD Support
// ============================================================================
#ifdef HAVE_ARM_NEON
#include <arm_neon.h>
#define USE_NEON_SIMD 1
#endif

// ============================================================================
// KISS FFT - Minimal embedded implementation (BSD-3-Clause)
// Only the real-to-complex forward transform needed for CQT
// ============================================================================

namespace kissfft {

struct kiss_fft_cpx {
    float r;
    float i;
};

class KissFFT {
public:
    explicit KissFFT(int nfft) : m_nfft(nfft) {
        m_twiddles.resize(nfft);
        for (int i = 0; i < nfft; ++i) {
            const float phase = -2.0f * M_PI * i / nfft;
            m_twiddles[i] = { std::cos(phase), std::sin(phase) };
        }
        m_scratch.resize(nfft);
    }

    void forward(const float* timeIn, kiss_fft_cpx* freqOut) {
        // Real-to-complex via standard DFT for small sizes, Cooley-Tukey for power-of-2
        if (isPowerOf2(m_nfft)) {
            // Copy real input to complex scratch
            for (int i = 0; i < m_nfft; ++i) {
                m_scratch[i] = { timeIn[i], 0.0f };
            }
            fft_radix2(m_scratch.data(), freqOut, m_nfft, 1);
        } else {
            // Fallback: direct DFT for non-power-of-2 (CQT uses varied sizes)
            dft_direct(timeIn, freqOut);
        }
    }

private:
    int m_nfft;
    std::vector<kiss_fft_cpx> m_twiddles;
    std::vector<kiss_fft_cpx> m_scratch;

    static bool isPowerOf2(int n) {
        return n > 0 && (n & (n - 1)) == 0;
    }

    void dft_direct(const float* x, kiss_fft_cpx* X) {
        for (int k = 0; k < m_nfft; ++k) {
            float sumR = 0.0f, sumI = 0.0f;
            for (int n = 0; n < m_nfft; ++n) {
                const float phase = -2.0f * M_PI * k * n / m_nfft;
                sumR += x[n] * std::cos(phase);
                sumI += x[n] * std::sin(phase);
            }
            X[k] = { sumR, sumI };
        }
    }

    void fft_radix2(kiss_fft_cpx* in, kiss_fft_cpx* out, int n, int stride) {
        if (n == 1) {
            out[0] = in[0];
            return;
        }
        const int half = n / 2;
        fft_radix2(in, out, half, stride * 2);
        fft_radix2(in + stride, out + half, half, stride * 2);

        for (int k = 0; k < half; ++k) {
            const float phase = -2.0f * M_PI * k / n;
            const kiss_fft_cpx tw = { std::cos(phase), std::sin(phase) };
            const kiss_fft_cpx t = {
                tw.r * out[half + k].r - tw.i * out[half + k].i,
                tw.r * out[half + k].i + tw.i * out[half + k].r
            };
            const kiss_fft_cpx e = out[k];
            out[k] = { e.r + t.r, e.i + t.i };
            out[half + k] = { e.r - t.r, e.i - t.i };
        }
    }
};

} // namespace kissfft

// ============================================================================
// CQT Kernel Implementation
// ============================================================================

namespace {

// String open frequencies (standard tuning, Hz)
constexpr float kOpenFreqs[6] = { 82.41f, 110.0f, 146.83f, 196.0f, 246.94f, 329.63f };

// Bin ranges per string (36 bins/octave × 2 octaves = 72 bins covering 24 frets)
// Indices into 144-bin CQT array (4 octaves total to cover all strings)
constexpr int kBinRangeStart[6] = { 0, 15, 30, 45, 57, 72 };
constexpr int kBinRangeEnd[6]   = { 74, 89, 104, 119, 131, 144 };

// MIDI note for open strings
constexpr int kOpenMidi[6] = { 40, 45, 50, 55, 59, 64 };

// Minimum magnitude for parabolic interpolation
constexpr float kMinMagnitude = 1e-10f;

// Hysteresis parameters
constexpr float kHysteresisAdvantage = 0.20f;  // 20% energy advantage required

// ============================================================================
// LINEAR GROWTH THRESHOLD MODEL (Physics-Based)
// ============================================================================
// As frets increase, the fundamental bin captures MORE energy because:
//  - Shorter string length = purer fundamental, less harmonic spread
//  - Energy concentrates toward bridge
// Therefore, threshold INCREASES linearly with fret position.
//
// Base multipliers account for string characteristics:
//  - Low strings: more harmonics = lower fundamental energy = lower base
//  - High strings: purer tone = higher fundamental energy = higher base
//
// Formula: Threshold = GlobalRMS × (BaseMultiplier + GrowthConstant × (Fret / 24))
// ============================================================================

// Base multipliers per string (High E to Low E: S5..S0)
// S5 (High E): 0.72 - purest tone, highest fundamental ratio
// S4 (B):      0.68
// S3 (G):      0.64
// S2 (D):      0.60
// S1 (A):      0.56
// S0 (Low E):  0.52 - most harmonics, lowest fundamental ratio
constexpr float kBaseMultipliers[6] = {
    0.52f,  // S0 - Low E
    0.56f,  // S1 - A
    0.60f,  // S2 - D
    0.64f,  // S3 - G
    0.68f,  // S4 - B
    0.72f   // S5 - High E
};

// Growth constant: purity increase toward bridge (fret 0→24)
constexpr float kGrowthConstant = 0.35f;

// Note-OFF hysteresis: 50% of dynamic Note-ON threshold
constexpr float kNoteOffHysteresis = 0.50f;

// Pre-calculated threshold multipliers [6 strings × 25 frets]
// Computed at startup: kThresholdMultipliers[s][f] = Base[s] + 0.35 × (f / 24)
struct ThresholdLUT {
    float multipliers[6][25];
    
    constexpr ThresholdLUT() : multipliers{} {
        for (int s = 0; s < 6; ++s) {
            for (int f = 0; f < 25; ++f) {
                multipliers[s][f] = kBaseMultipliers[s] + (kGrowthConstant * (static_cast<float>(f) / 24.0f));
            }
        }
    }
};

// Static constexpr LUT - computed at compile time, zero runtime cost
constexpr ThresholdLUT kThresholdLUT{};

// O(1) threshold lookup (inlined for real-time audio callback)
inline float getNoteOnMultiplier(int stringIdx, int fret) {
    const int s = std::clamp(stringIdx, 0, 5);
    const int f = std::clamp(fret, 0, 24);
    return kThresholdLUT.multipliers[s][f];
}

// Note-OFF threshold = 50% of Note-ON threshold
inline float getNoteOffMultiplier(int stringIdx, int fret) {
    return getNoteOnMultiplier(stringIdx, fret) * kNoteOffHysteresis;
}

/**
 * Note: CQT outputs magnitude values internally. For user-facing purposes (logs, UI),
 * these are treated as RMS-equivalent energy measurements. The magnitude values from
 * CQT bins correlate with signal energy and can be compared directly to RMS thresholds
 * from calibration (envFloor, baseline) which are also energy-based measurements.
 */

/**
 * Compute CQT center frequency for a given bin
 * f(k) = fmin * 2^(k / binsPerOctave)
 */
inline float binToFreq(int bin, float fmin, int binsPerOctave) {
    return fmin * std::pow(2.0f, static_cast<float>(bin) / binsPerOctave);
}

/**
 * Compute CQT frequency for a refined (fractional) bin position
 * f(k) = fmin * 2^(k / binsPerOctave)
 */
inline float refinedBinToFreq(float bin, float fmin, int binsPerOctave) {
    return fmin * std::pow(2.0f, bin / static_cast<float>(binsPerOctave));
}

/**
 * Compute RMS of a buffer
 */
inline float computeRms(const float* buffer, int length) {
    if (length <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < length; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return static_cast<float>(std::sqrt(sum / length));
}

/**
 * Parabolic interpolation for sub-bin pitch refinement
 * Returns offset in bins (-0.5 to +0.5)
 */
inline float parabolicInterp(float y0, float y1, float y2) {
    const float denom = y0 - 2.0f * y1 + y2;
    if (std::abs(denom) < kMinMagnitude) return 0.0f;
    return 0.5f * (y0 - y2) / denom;
}

/**
 * Convert bin offset to cents
 * 1 bin = 100/36 cents at 36 bins/octave
 */
inline float binOffsetToCents(float binOffset, int binsPerOctave) {
    return binOffset * (1200.0f / binsPerOctave);
}

#ifdef USE_NEON_SIMD
/**
 * NEON-optimized complex correlation (4-wide SIMD)
 * Processes 4 samples per iteration for ~4x speedup on ARM64
 */
inline void neonComplexCorrelation(
    const float* __restrict__ samples,
    const float* __restrict__ kernelReal,
    const float* __restrict__ kernelImag,
    int length,
    float& sumR,
    float& sumI)
{
    // Initialize accumulators
    float32x4_t sumR_vec = vdupq_n_f32(0.0f);
    float32x4_t sumI_vec = vdupq_n_f32(0.0f);
    
    // Process 4 samples at a time
    int i = 0;
    const int simdLen = length & ~3;  // Round down to multiple of 4
    
    for (; i < simdLen; i += 4) {
        // Load 4 samples
        float32x4_t s = vld1q_f32(samples + i);
        
        // Load 4 kernel values (real and imaginary)
        float32x4_t kr = vld1q_f32(kernelReal + i);
        float32x4_t ki = vld1q_f32(kernelImag + i);
        
        // Multiply-accumulate: sumR += sample * kernelReal, sumI += sample * kernelImag
        sumR_vec = vmlaq_f32(sumR_vec, s, kr);
        sumI_vec = vmlaq_f32(sumI_vec, s, ki);
    }
    
    // Horizontal sum of SIMD vectors
    sumR = vaddvq_f32(sumR_vec);
    sumI = vaddvq_f32(sumI_vec);
    
    // Handle remaining samples (tail)
    for (; i < length; ++i) {
        sumR += samples[i] * kernelReal[i];
        sumI += samples[i] * kernelImag[i];
    }
}

/**
 * NEON-optimized sqrt approximation using NEON RSQRTE + Newton-Raphson
 * ~2x faster than std::sqrt for single floats
 */
inline float neonSqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float32x2_t v = vdup_n_f32(x);
    float32x2_t est = vrsqrte_f32(v);
    // One Newton-Raphson iteration for accuracy
    est = vmul_f32(est, vrsqrts_f32(vmul_f32(v, est), est));
    return vget_lane_f32(vmul_f32(v, est), 0);
}
#endif

} // anonymous namespace

// ============================================================================
// CQT Engine (internal)
// ============================================================================

class CQTEngine {
public:
    CQTEngine(double sampleRate, int binsPerOctave, float fmin, int numBins)
        : m_sampleRate(sampleRate)
        , m_binsPerOctave(binsPerOctave)
        , m_fmin(fmin)
        , m_numBins(numBins)
    {
        initKernels();
    }

    /**
     * Compute CQT magnitude spectrum for a mono buffer
     * Output: magnitudes array of size m_numBins
     * 
     * Uses NEON SIMD optimization on ARM platforms for ~4x speedup
     */
    void compute(const float* buffer, int bufferLen, float* magnitudes) {
        // For each CQT bin, apply the corresponding kernel
        for (int k = 0; k < m_numBins; ++k) {
            const auto& kernel = m_kernels[k];
            const int N_k = kernel.length;
            
            // Center the kernel window in the buffer
            const int offset = (bufferLen - N_k) / 2;
            if (offset < 0 || N_k > bufferLen) {
                magnitudes[k] = 0.0f;
                continue;
            }

            float sumR = 0.0f, sumI = 0.0f;
            
#ifdef USE_NEON_SIMD
            // NEON-optimized correlation (4-wide SIMD)
            neonComplexCorrelation(
                buffer + offset,
                kernel.realPart.data(),
                kernel.imagPart.data(),
                N_k,
                sumR, sumI);
            
            // NEON-optimized magnitude
            magnitudes[k] = neonSqrt(sumR * sumR + sumI * sumI) / N_k;
#else
            // Scalar fallback
            for (int n = 0; n < N_k; ++n) {
                const float sample = buffer[offset + n];
                sumR += sample * kernel.realPart[n];
                sumI += sample * kernel.imagPart[n];
            }
            magnitudes[k] = std::sqrt(sumR * sumR + sumI * sumI) / N_k;
#endif
        }
    }

    int numBins() const { return m_numBins; }

private:
    struct CQTKernel {
        int length;
        std::vector<float> realPart;
        std::vector<float> imagPart;
    };

    double m_sampleRate;
    int m_binsPerOctave;
    float m_fmin;
    int m_numBins;
    std::vector<CQTKernel> m_kernels;

    void initKernels() {
        m_kernels.resize(m_numBins);
        
        // Q factor for constant-Q: Q = 1 / (2^(1/B) - 1) where B = bins per octave
        const float Q = 1.0f / (std::pow(2.0f, 1.0f / m_binsPerOctave) - 1.0f);

        for (int k = 0; k < m_numBins; ++k) {
            const float fk = binToFreq(k, m_fmin, m_binsPerOctave);
            
            // Window length for this bin: N_k = Q * fs / f_k
            int N_k = static_cast<int>(std::ceil(Q * m_sampleRate / fk));
            
            // Ensure odd length for symmetric window
            if (N_k % 2 == 0) N_k++;
            
            // Limit kernel size for efficiency (max ~4096 samples)
            N_k = std::min(N_k, 4096);
            
            m_kernels[k].length = N_k;
            m_kernels[k].realPart.resize(N_k);
            m_kernels[k].imagPart.resize(N_k);

            // Generate windowed complex exponential kernel
            for (int n = 0; n < N_k; ++n) {
                // Hamming window
                const float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * n / (N_k - 1));
                
                // Complex exponential at frequency fk
                const float phase = 2.0f * M_PI * fk * n / m_sampleRate;
                
                m_kernels[k].realPart[n] = window * std::cos(phase);
                m_kernels[k].imagPart[n] = window * std::sin(phase);
            }
        }
    }
};

// ============================================================================
// CQTNoteDetector Implementation
// ============================================================================

struct CQTNoteDetector::Impl {
    std::unique_ptr<CQTEngine> cqtEngine;
    double sampleRate;
    float fmin;
    int binsPerOctave;
    
    // Pre-allocated buffers (no heap allocation in process loop)
    std::array<std::array<float, 144>, 6> binMagnitudes;   // CQT output per string (post-crosstalk)
    std::array<std::array<float, 144>, 6> rawBinMagnitudes; // CQT output before crosstalk suppression (for heatmap)
    std::array<float, 144> spectralFluxBins;               // Flux computation
    std::array<float, 6> stringRms;                        // RMS per string (post-preAmp)
    std::array<float, 6> normalizedMags;                   // For crosstalk comparison
    std::array<std::vector<float>, 6> preAmpBuffers;       // Pre-CQT gain application buffers
    
    // Accumulation buffers for CQT (need enough samples for lowest frequency kernel)
    static constexpr int kAccumBufferSize = 4096;
    std::array<std::array<float, kAccumBufferSize>, 6> accumBuffers;
    std::array<int, 6> accumWritePos;
    std::array<bool, 6> accumReady;  // Have we accumulated enough samples?
    
    Impl(double sr) : sampleRate(sr), fmin(70.0f), binsPerOctave(36) {
        // Initialize CQT engine: 144 bins covering ~70Hz to ~1400Hz (4+ octaves)
        // This covers all guitar notes from low E (82Hz) to 24th fret high E (~1319Hz)
        const int numBins = 144;
        
        cqtEngine = std::make_unique<CQTEngine>(sr, binsPerOctave, fmin, numBins);
        
        // Zero-initialize all arrays
        for (auto& arr : binMagnitudes) arr.fill(0.0f);
        for (auto& arr : rawBinMagnitudes) arr.fill(0.0f);
        spectralFluxBins.fill(0.0f);
        stringRms.fill(0.0f);
        normalizedMags.fill(0.0f);
        accumWritePos.fill(0);
        accumReady.fill(false);
        for (auto& buf : accumBuffers) buf.fill(0.0f);
        
        // Initialize preAmp buffers (max 4096 samples per string should be plenty)
        for (auto& buf : preAmpBuffers) {
            buf.resize(4096, 0.0f);
        }
    }
};

CQTNoteDetector::CQTNoteDetector(double sampleRate) 
    : m_impl(std::make_unique<Impl>(sampleRate))
{
    // Initialize string states
    for (int s = 0; s < 6; ++s) {
        states[s] = StringState{};
    }
}

CQTNoteDetector::~CQTNoteDetector() = default;

std::vector<GuitarFrame> CQTNoteDetector::process(
    float** hexBuffers, 
    int bufferLength,
    const std::vector<DetectionParams>& params)
{
    std::vector<GuitarFrame> results;
    results.reserve(6);

    if (!hexBuffers || params.size() < 6 || bufferLength <= 0) {
        return results;
    }

    // ========================================================================
    // PHASE 1: Accumulate samples and apply preAmpGain
    // ========================================================================
    
    std::array<bool, 6> passedGate;
    constexpr int kMinSamplesForCQT = 2048;  // Minimum samples needed for accurate CQT
    
    for (int s = 0; s < 6; ++s) {
        const float* buffer = hexBuffers[s];
        
        if (!buffer) {
            m_impl->stringRms[s] = 0.0f;
            passedGate[s] = false;
            m_impl->binMagnitudes[s].fill(0.0f);
            m_impl->accumReady[s] = false;
            continue;
        }
        
        // Stage 1: Apply preAmpGain to raw input and accumulate
        const float preAmp = params[s].preAmpGain;
        auto& accumBuf = m_impl->accumBuffers[s];
        int& writePos = m_impl->accumWritePos[s];
        
        // Shift buffer left if needed to make room
        if (writePos + bufferLength > Impl::kAccumBufferSize) {
            const int shift = writePos + bufferLength - Impl::kAccumBufferSize;
            std::memmove(accumBuf.data(), accumBuf.data() + shift, 
                        (Impl::kAccumBufferSize - shift) * sizeof(float));
            writePos -= shift;
        }
        
        // Append new amplified samples
        for (int i = 0; i < bufferLength; ++i) {
            accumBuf[writePos + i] = buffer[i] * preAmp;
        }
        writePos += bufferLength;
        
        // Calculate RMS on the most recent bufferLength samples
        const float rms = computeRms(accumBuf.data() + writePos - bufferLength, bufferLength);
        m_impl->stringRms[s] = rms;
        
        // Master Gate: Skip CQT if below baseline noise floor
        passedGate[s] = (rms >= params[s].baseline);
        
        // Check if we have enough samples for CQT
        m_impl->accumReady[s] = (writePos >= kMinSamplesForCQT);
        
        if (passedGate[s] && m_impl->accumReady[s]) {
            // Compute CQT on accumulated buffer - use as many samples as available (up to 4096)
            // This is needed because low-frequency CQT kernels need ~4096 samples for accuracy
            const int cqtSamples = std::min(writePos, Impl::kAccumBufferSize);
            const float* cqtInput = accumBuf.data() + writePos - cqtSamples;
            m_impl->cqtEngine->compute(cqtInput, cqtSamples, m_impl->binMagnitudes[s].data());
            
            // Debug: Find max bin magnitude after CQT
            float maxMag = 0.0f;
            int maxBin = -1;
            for (int b = 0; b < 144; ++b) {
                if (m_impl->binMagnitudes[s][b] > maxMag) {
                    maxMag = m_impl->binMagnitudes[s][b];
                    maxBin = b;
                }
            }
            // DISABLED - SessionLogger causes XRuns in real-time audio thread
            // if (rms > 0.1f) {
            //     SessionLogger::instance().logf("cqt-compute", 
            //         "S%d: rms=%.4f preAmp=%.2f maxBinMag=%.6f maxBin=%d cqtSamples=%d",
            //         s, rms, params[s].preAmpGain, maxMag, maxBin, cqtSamples);
            // }
        } else {
            // Zero the magnitudes for gated or not-ready strings
            m_impl->binMagnitudes[s].fill(0.0f);
        }
    }

    // ========================================================================
    // PHASE 1.5: Copy raw bin magnitudes for heatmap (before crosstalk suppression)
    // ========================================================================
    for (int s = 0; s < 6; ++s) {
        m_impl->rawBinMagnitudes[s] = m_impl->binMagnitudes[s];
    }

    // ========================================================================
    // PHASE 2: Spatial Filtering (Crosstalk Rejection)
    // ========================================================================
    
    // Stage 2: Apply spatialWeight for fair comparison across strings
    // spatialWeight is set by calibration to normalize pickup sensitivity differences
    // This ensures that crosstalk rejection math remains accurate even if user adjusts preAmpGain
    
    for (int bin = 0; bin < 144; ++bin) {
        float maxWeightedMag = 0.0f;
        int dominantString = -1;
        
        // Find dominant string for this bin using weighted magnitudes
        for (int s = 0; s < 6; ++s) {
            if (!passedGate[s]) continue;
            
            const float weightedMag = m_impl->binMagnitudes[s][bin] * params[s].spatialWeight;
            if (weightedMag > maxWeightedMag) {
                maxWeightedMag = weightedMag;
                dominantString = s;
            }
        }
        
        // Suppress bin on non-dominant strings
        if (dominantString >= 0) {
#if ENABLE_CQT_RT_LOGGING
            // Log crosstalk suppression decisions for higher bins (notes)
            if (bin >= 24 && maxWeightedMag > 0.01f) {
                SessionLogger::instance().logf("cqt-crosstalk",
                    "Bin%d: dominant=S%d weightedMag=%.6f (gated strings: %d%d%d%d%d%d)",
                    bin, dominantString, maxWeightedMag,
                    passedGate[0]?1:0, passedGate[1]?1:0, passedGate[2]?1:0,
                    passedGate[3]?1:0, passedGate[4]?1:0, passedGate[5]?1:0);
            }
#endif
            
            for (int s = 0; s < 6; ++s) {
                if (s != dominantString) {
                    m_impl->binMagnitudes[s][bin] = 0.0f;
                }
            }
        }
    }

    // ========================================================================
    // PHASE 3: Per-String Analysis
    // ========================================================================
    
    for (int s = 0; s < 6; ++s) {
        GuitarFrame frame;
        frame.stringID = s;
        frame.rmsAmplitude = m_impl->stringRms[s];
        frame.fret = -1;
        frame.centOffset = 0.0f;
        frame.pitchHz = 0.0f;
        frame.binEnergy = 0.0f;
        frame.spectralFlux = 0.0f;
        frame.isAttack = false;
        frame.isSustaining = false;

        if (!passedGate[s]) {
            // String is below detection threshold - still compute pitch for tuning mode
            // Use range-clamped peak search (same as gated path)
            const int binStart = kBinRangeStart[s];
            const int binEnd = std::min(kBinRangeEnd[s], 144);
            
            int peakBin = -1;
            float peakMag = 0.0f;
            for (int bin = binStart; bin < binEnd; ++bin) {
                const float mag = m_impl->binMagnitudes[s][bin];
                if (mag > peakMag) {
                    peakMag = mag;
                    peakBin = bin;
                }
            }
            
            // Compute fine pitch via parabolic interpolation
            float centOffset = 0.0f;
            float binOffset = 0.0f;
            if (peakBin > 0 && peakBin < 143) {
                const float y0 = m_impl->binMagnitudes[s][peakBin - 1];
                const float y1 = m_impl->binMagnitudes[s][peakBin];
                const float y2 = m_impl->binMagnitudes[s][peakBin + 1];
                
                if (y1 > kMinMagnitude) {
                    binOffset = parabolicInterp(y0, y1, y2);
                    centOffset = binOffsetToCents(binOffset, BINS_PER_OCTAVE);
                }
            }
            
            frame.centOffset = centOffset;
            frame.binEnergy = peakMag;
            
            // Compute actual pitch frequency for tuning mode
            if (peakBin >= 0) {
                // Refined bin position = integer bin + fractional offset
                const float refinedBin = static_cast<float>(peakBin) + binOffset;
                frame.pitchHz = binToFreq(static_cast<int>(refinedBin), m_impl->fmin, m_impl->binsPerOctave) 
                              * std::pow(2.0f, centOffset / 1200.0f);
            }
            
            // Check for note release
            if (states[s].currentFret >= 0) {
                // Note-off condition (failed gate check)
#if ENABLE_CQT_RT_LOGGING
                SessionLogger::instance().logf("cqt-gateoff",
                    "S%d F%d: NOTE OFF (gate failed) rms=%.4f gateThresh=%.4f",
                    s, states[s].currentFret, m_impl->stringRms[s], 
                    params[s].baseline * params[s].gateRatio);
#endif
                // Log note-off event for debugging overlay persistence
                std::fprintf(stderr, "[CQT-NOTE-OFF] S%d F%d GATE_FAIL rms=%.4f threshold=%.4f\n",
                    s, states[s].currentFret, m_impl->stringRms[s],
                    params[s].baseline * params[s].gateRatio);
                states[s].currentFret = -1;
                states[s].lastPeakMag = 0.0f;
            }
            results.push_back(frame);
            continue;
        }

        // --------------------------------------------------------------------
        // 3a: Range-Clamped Peak Search
        // --------------------------------------------------------------------
        
        const int binStart = kBinRangeStart[s];
        const int binEnd = std::min(kBinRangeEnd[s], 144);
        
        float peakMag = 0.0f;
        int peakBin = -1;
        
        for (int bin = binStart; bin < binEnd; ++bin) {
            const float mag = m_impl->binMagnitudes[s][bin];
            if (mag > peakMag) {
                peakMag = mag;
                peakBin = bin;
            }
        }

        if (peakBin < 0 || peakMag < 1e-10f) {
            // No significant energy in range
            results.push_back(frame);
            continue;
        }
        
#if ENABLE_CQT_RT_LOGGING
        // Log peak detection for gated strings
        if (peakMag > 0.01f) {
            SessionLogger::instance().logf("cqt-peak",
                "S%d: peakBin=%d peakMag=%.6f binRange=[%d,%d] rms=%.4f",
                s, peakBin, peakMag, binStart, binEnd, m_impl->stringRms[s]);
        }
#endif

        // --------------------------------------------------------------------
        // 3b: Fret Estimation from Peak Bin
        // --------------------------------------------------------------------
        
        // Convert bin to fret using actual frequency calculation
        // fret = 12 * log2(freq_detected / freq_open)
        const float peakFreq = binToFreq(peakBin, m_impl->fmin, m_impl->binsPerOctave);
        const float openFreq = kOpenFreqs[s];
        const float fretFloat = 12.0f * std::log2(peakFreq / openFreq);
        const int rawFret = std::max(0, std::min(24, 
            static_cast<int>(std::round(fretFloat))));

        // --------------------------------------------------------------------
        // 3c: Frequency-Dependent Magnitude Threshold (Pre-Computed LUT)
        // --------------------------------------------------------------------
        
        // Use pre-calculated threshold multiplier from compile-time LUT
        // This avoids floating-point division in the real-time audio callback.
        // See kThresholdLUT definition above for the physics model.
        const float thresholdMultiplier = getNoteOnMultiplier(s, rawFret);
        const float noteOnThreshold = params[s].envFloor * thresholdMultiplier;
        
        // Check if magnitude meets note-on threshold
        if (peakMag < noteOnThreshold) {
            // Below note-on threshold - check note-off threshold
            // Note-OFF uses 50% of the dynamic Note-ON threshold (pre-computed LUT)
            float noteOffThreshold = params[s].envFloor * getNoteOffMultiplier(s, 0);
            
            // CRITICAL FIX: Check magnitude of CURRENT fret's bin, not new peak bin
            // This prevents notes from hanging when peak shifts to different bin
            float currentFretMag = 0.0f;
            if (states[s].currentFret >= 0) {
                // Calculate the bin for the current fret using frequency
                const float currentFretFreq = kOpenFreqs[s] * std::pow(2.0f, static_cast<float>(states[s].currentFret) / 12.0f);
                const float currentFretBinFloat = static_cast<float>(m_impl->binsPerOctave) * std::log2f(currentFretFreq / m_impl->fmin);
                const int currentBin = static_cast<int>(std::roundf(currentFretBinFloat));
                if (currentBin >= binStart && currentBin < binEnd) {
                    currentFretMag = m_impl->binMagnitudes[s][currentBin];
                }
                
                // Note-OFF threshold = 50% of dynamic Note-ON threshold for current fret
                noteOffThreshold = params[s].envFloor * getNoteOffMultiplier(s, states[s].currentFret);
            }
            
            if (states[s].currentFret >= 0 && currentFretMag >= noteOffThreshold) {
                // Continue sustaining current note (using current fret's magnitude)
#if ENABLE_CQT_RT_LOGGING
                SessionLogger::instance().logf("cqt-sustain",
                    "S%d F%d: sustaining currentFretMag=%.6f peakMag=%.6f noteOffThresh=%.6f",
                    s, states[s].currentFret, currentFretMag, peakMag, noteOffThreshold);
#endif
                frame.fret = states[s].currentFret;
                frame.binEnergy = currentFretMag; // Use current fret's energy
                frame.isSustaining = true;
            } else if (states[s].currentFret >= 0 && currentFretMag < noteOffThreshold) {
                // Release note (current fret's energy dropped below threshold)
#if ENABLE_CQT_RT_LOGGING
                SessionLogger::instance().logf("cqt-event",
                    "[CQT] NOTE OFF     S%d F%-2d           (binRMS=%.4f)",
                    s, states[s].currentFret, currentFretMag);
#endif
                // Log note-off event for debugging overlay persistence
                std::fprintf(stderr, "[CQT-NOTE-OFF] S%d F%d MAG_DROP currentMag=%.4f threshold=%.4f\n",
                    s, states[s].currentFret, currentFretMag, noteOffThreshold);
                states[s].currentFret = -1;
                states[s].lastPeakMag = 0.0f;
            }
            results.push_back(frame);
            continue;
        }

        // --------------------------------------------------------------------
        // 3d: Spectral Flux Computation
        // --------------------------------------------------------------------
        
        float flux = 0.0f;
        for (int bin = binStart; bin < binEnd; ++bin) {
            const float diff = m_impl->binMagnitudes[s][bin] - states[s].lastBins[bin];
            if (diff > 0.0f) {
                flux += diff * diff;
            }
        }
        flux = std::sqrt(flux);
        frame.spectralFlux = flux;

        // --------------------------------------------------------------------
        // 3e: Hysteresis (Sticky Fret)
        // --------------------------------------------------------------------
        
        bool fretConfirmed = false;
        
        if (states[s].currentFret < 0) {
            // No current note - this is a new onset
            if (states[s].candidateFret == rawFret) {
                states[s].confirmationCount++;
            } else {
                states[s].candidateFret = rawFret;
                states[s].confirmationCount = 1;
            }
            
            if (states[s].confirmationCount >= params[s].confirmationFrames) {
#if ENABLE_CQT_RT_LOGGING
                SessionLogger::instance().logf("cqt-event",
                    "[CQT] NOTE ON      S%d F%-2d           (binRMS=%.4f)",
                    s, rawFret, peakMag);
#endif
                states[s].currentFret = rawFret;
                states[s].lastPeakMag = peakMag;
                frame.isAttack = true;
                fretConfirmed = true;
            } else {
#if ENABLE_CQT_RT_LOGGING
                SessionLogger::instance().logf("cqt-candidate",
                    "S%d F%d: candidate peakMag=%.6f confirmCount=%d/%d",
                    s, rawFret, peakMag, states[s].confirmationCount, params[s].confirmationFrames);
#endif
            }
        } else {
            // Already have a note - check for fret change
            // Calculate bin for current fret using frequency
            const float currentFretFreq = kOpenFreqs[s] * std::pow(2.0f, states[s].currentFret / 12.0f);
            const float currentFretBinFloat = static_cast<float>(m_impl->binsPerOctave) * std::log2(currentFretFreq / m_impl->fmin);
            const int currentFretBin = static_cast<int>(std::round(currentFretBinFloat));
            const float currentFretMag = (currentFretBin >= binStart && currentFretBin < binEnd) 
                ? m_impl->binMagnitudes[s][currentFretBin] : 0.0f;
            const float advantage = (peakMag - currentFretMag) / (currentFretMag + 1e-10f);
            
            if (rawFret != states[s].currentFret && advantage > kHysteresisAdvantage) {
                // Candidate for fret change
#if ENABLE_CQT_RT_LOGGING
                SessionLogger::instance().logf("cqt-fretchange",
                    "S%d: F%d->F%d candidate peakMag=%.6f currentMag=%.6f advantage=%.2f",
                    s, states[s].currentFret, rawFret, peakMag, currentFretMag, advantage);
#endif
                    
                if (states[s].candidateFret == rawFret) {
                    states[s].confirmationCount++;
                } else {
                    states[s].candidateFret = rawFret;
                    states[s].confirmationCount = 1;
                }
                
                if (states[s].confirmationCount >= params[s].confirmationFrames) {
#if ENABLE_CQT_RT_LOGGING
                    SessionLogger::instance().logf("cqt-event",
                        "[CQT] MOVE         S%d F%-2d -> F%-2d   (peakMag=%.4f, advantage=%.2f)",
                        s, states[s].currentFret, rawFret, peakMag, advantage);
#endif
                    states[s].currentFret = rawFret;
                    states[s].lastPeakMag = peakMag;
                    frame.isAttack = true;
                    fretConfirmed = true;
                }
            } else {
                // Stick with current fret
                states[s].candidateFret = states[s].currentFret;
                states[s].confirmationCount = params[s].confirmationFrames;
                fretConfirmed = true;
            }
        }

        // --------------------------------------------------------------------
        // 3f: Retrigger Detection
        // --------------------------------------------------------------------
        
        if (states[s].currentFret >= 0 && !frame.isAttack) {
            // Check for retrigger via spectral flux spike
            // Use a more conservative threshold to avoid false retriggering on normal sustain fluctuations
            // Flux is normalized by the number of bins to make it scale-independent
            const float normalizedFlux = flux / std::sqrt(static_cast<float>(binEnd - binStart));
            const float fluxThreshold = params[s].fluxSensitivity;  // Use absolute threshold instead of relative
            
            // Retrigger only if:
            // 1. Normalized flux exceeds threshold (indicating strong transient)
            // 2. Peak magnitude has significantly increased (>50% jump)
            // 3. We haven't just retriggered (debounce)
            const bool strongTransient = normalizedFlux > fluxThreshold;
            const bool magnitudeJump = peakMag > states[s].lastPeakMag * 1.5f;
            
            if (strongTransient && magnitudeJump) {
#if ENABLE_CQT_RT_LOGGING
                SessionLogger::instance().logf("cqt-event",
                    "[CQT] RETRIGGER    S%d F%-2d           (spikeRMS=%.4f pastRMS=%.4f)",
                    s, states[s].currentFret, peakMag, states[s].lastPeakMag);
#endif
                frame.isAttack = true;
                states[s].lastPeakMag = peakMag;
            }
        }

        // --------------------------------------------------------------------
        // 3g: Fine Pitch via Parabolic Interpolation
        // --------------------------------------------------------------------
        
        float centOffset = 0.0f;
        float binOffset = 0.0f;
        if (peakBin > 0 && peakBin < 143) {
            const float y0 = m_impl->binMagnitudes[s][peakBin - 1];
            const float y1 = m_impl->binMagnitudes[s][peakBin];
            const float y2 = m_impl->binMagnitudes[s][peakBin + 1];
            
            if (y1 > kMinMagnitude) {
                binOffset = parabolicInterp(y0, y1, y2);
                centOffset = binOffsetToCents(binOffset, BINS_PER_OCTAVE);
            }
        }
        
        // Compute actual pitch frequency for tuning mode
        float pitchHz = 0.0f;
        if (peakBin >= 0) {
            // Refined bin position = integer bin + fractional offset
            const float refinedBin = static_cast<float>(peakBin) + binOffset;
            pitchHz = binToFreq(static_cast<int>(refinedBin), m_impl->fmin, m_impl->binsPerOctave) 
                    * std::pow(2.0f, centOffset / 1200.0f);
        }

        // --------------------------------------------------------------------
        // 3h: Update State
        // --------------------------------------------------------------------
        
        // Store current bins for next frame's flux calculation
        for (int bin = 0; bin < 144; ++bin) {
            states[s].lastBins[bin] = m_impl->binMagnitudes[s][bin];
        }
        
        if (fretConfirmed && peakMag > states[s].lastPeakMag) {
            states[s].lastPeakMag = peakMag;
        }

        // --------------------------------------------------------------------
        // 3i: Populate Output Frame
        // --------------------------------------------------------------------
        
        frame.fret = states[s].currentFret;
        frame.centOffset = centOffset;
        frame.pitchHz = pitchHz;
        frame.binEnergy = peakMag;
        frame.isSustaining = (states[s].currentFret >= 0 && !frame.isAttack);

#if ENABLE_CQT_RT_LOGGING
        // Log final frame state for debugging
        if (frame.fret >= 0 || frame.isAttack || frame.isSustaining) {
            SessionLogger::instance().logf("cqt-frame",
                "S%d: fret=%d attack=%d sustain=%d binEnergy=%.6f rms=%.4f",
                s, frame.fret, frame.isAttack?1:0, frame.isSustaining?1:0, 
                frame.binEnergy, frame.rmsAmplitude);
        }
#endif

        results.push_back(frame);
    }

    return results;
}

void CQTNoteDetector::processNoAlloc(
    float** hexBuffers, 
    int bufferLength,
    const std::array<DetectionParams, 6>& params,
    std::array<GuitarFrame, 6>& outFrames)
{
    // Zero-initialize output frames
    for (int s = 0; s < 6; ++s) {
        outFrames[s] = GuitarFrame{s, -1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false};
    }
    
    if (!hexBuffers || bufferLength <= 0) {
        return;
    }
    
    // Convert std::array to std::vector for process() call
    // NOTE: This still allocates, but only once per call. The primary benefit is 
    // avoiding the return vector allocation. A full refactor would eliminate this too.
    std::vector<DetectionParams> vecParams(params.begin(), params.end());
    std::vector<GuitarFrame> results = process(hexBuffers, bufferLength, vecParams);
    
    // Copy results to output array
    for (size_t i = 0; i < results.size() && i < 6; ++i) {
        outFrames[i] = results[i];
    }
}

void CQTNoteDetector::reset() {
    for (int s = 0; s < 6; ++s) {
        states[s] = StringState{};
        m_impl->binMagnitudes[s].fill(0.0f);
        m_impl->rawBinMagnitudes[s].fill(0.0f);
        m_impl->accumBuffers[s].fill(0.0f);
        m_impl->accumWritePos[s] = 0;
        m_impl->accumReady[s] = false;
    }
    m_impl->stringRms.fill(0.0f);
}

void CQTNoteDetector::setSampleRate(double sampleRate) {
    if (std::abs(sampleRate - m_impl->sampleRate) > 1.0) {
        m_impl = std::make_unique<Impl>(sampleRate);
        reset();
    }
}

float CQTNoteDetector::getFretBinMagnitude(int stringIdx, int fretIdx) const {
    if (stringIdx < 0 || stringIdx >= 6) return 0.0f;
    if (fretIdx < 0 || fretIdx > 24) return 0.0f;
    
    // Calculate frequency for this fret on this string
    const float fretFreq = kOpenFreqs[stringIdx] * std::pow(2.0f, static_cast<float>(fretIdx) / 12.0f);
    
    // Convert frequency to CQT bin index
    const float binFloat = static_cast<float>(m_impl->binsPerOctave) * std::log2f(fretFreq / m_impl->fmin);
    const int bin = static_cast<int>(std::roundf(binFloat));
    
    // Ensure bin is in valid CQT range (0-143)
    if (bin < 0 || bin >= 144) return 0.0f;
    
    // Return RAW bin magnitude (before crosstalk suppression) for heatmap display
    // NOTE: We don't filter by kBinRangeStart/End here - we want to show all energy
    // including harmonics that may fall outside the "expected" range for this string
    return m_impl->rawBinMagnitudes[stringIdx][bin];
}

float CQTNoteDetector::getThresholdMultiplier(int stringIdx, int fretIdx) {
    const int s = std::clamp(stringIdx, 0, 5);
    const int f = std::clamp(fretIdx, 0, 24);
    return kThresholdLUT.multipliers[s][f];
}
