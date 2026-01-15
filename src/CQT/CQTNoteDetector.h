#pragma once

#include <array>
#include <memory>
#include <vector>

/**
 * Guitar detection frame output from CQT analysis
 * 
 * ENERGY MEASUREMENT CONVENTION:
 * All energy/amplitude measurements are expressed as RMS-equivalent values for
 * consistency across the system. Internally, the CQT computes magnitude values
 * (sqrt of power), which directly correlate with RMS energy measurements and can
 * be compared against calibrated thresholds (envFloor, baseline, etc.) that are
 * also in RMS units. Users only need to think in terms of RMS amplitude.
 */
struct GuitarFrame {
    int stringID;           // 0-5 (Low E to High E)
    int fret;               // 0-24 (stabilized via hysteresis), -1 if no note
    float rmsAmplitude;     // RMS amplitude of input buffer
    float centOffset;       // Sub-bin pitch offset in cents (for bends/vibrato)
    float pitchHz;          // Detected pitch frequency in Hz (for tuning mode)
    float binEnergy;        // Peak CQT bin energy (internally magnitude, exposed as RMS-equivalent)
    float spectralFlux;     // CQT energy change rate (for onset detection)
    bool isAttack;          // True if this is a new note attack
    bool isSustaining;      // True if note is sustaining from previous frame
};

/**
 * Per-string detection parameters (maps from existing NoteDetectionParameterSet)
 */
struct DetectionParams {
    float baseline;         // Noise floor baseline (Master Gate threshold)
    float envFloor;         // Base RMS threshold - scaled by frequency model to find bin thresholds
    float preAmpGain;       // Pre-CQT gain (UI-adjustable, compensates for pickup output)
    float spatialWeight;    // Post-CQT weight (calibration-fixed, for crosstalk fairness)
    float gateRatio;        // Note-off multiplier (release = envFloor * gateRatio)
    int confirmationFrames; // Fret stability hysteresis (1-5 frames)
    float fluxSensitivity;  // Spectral flux retrigger threshold (0.05-0.50)
    float slopeDecay;       // [DEPRECATED] Legacy slope parameter - replaced by frequency model
};

/**
 * CQT-based note detector for 6-string guitar
 * 
 * Key features:
 * - Unified 6-string scanning with inter-string crosstalk rejection
 * - Frequency-dependent thresholds (compensates for harmonic complexity)
 * - Hysteresis-based fret stability ("Sticky Fret" - 20% advantage + 3 frames)
 * - Spectral flux-based retriggering (replaces Aubio onset detection)
 * - Parabolic interpolation for sub-bin pitch accuracy
 * 
 * CQT Configuration:
 * - 36 bins per octave (3x better than standard FFT)
 * - 144 total bins covering ~70Hz to ~1400Hz (all guitar frets)
 * - Per-string range clamping (72 bins per string for 24 frets)
 * 
 * Threshold Model:
 * - Dynamic scaling: Threshold = RMS × (Base + Growth × Fret/24)
 * - Base multipliers: 0.72 (High E) down to 0.52 (Low E)
 * - Growth constant: 0.35 (purity increase toward bridge)
 * - Accounts for: Low strings = more harmonics = lower fundamental energy
 */
class CQTNoteDetector {
public:
    explicit CQTNoteDetector(double sampleRate = 44100.0);
    ~CQTNoteDetector();
    
    // Non-copyable (owns unique resources)
    CQTNoteDetector(const CQTNoteDetector&) = delete;
    CQTNoteDetector& operator=(const CQTNoteDetector&) = delete;
    
    // Move-constructible
    CQTNoteDetector(CQTNoteDetector&&) noexcept = default;
    CQTNoteDetector& operator=(CQTNoteDetector&&) noexcept = default;

    /**
     * Process all 6 strings in a unified pass
     * 
     * @param hexBuffers   Array of 6 float buffer pointers (one per string)
     * @param bufferLength Number of samples in each buffer
     * @param params       Detection parameters per string (must have 6 elements)
     * @return Vector of GuitarFrame results (one per string, in order)
     * 
     * Note: hexBuffers[i] may be nullptr to indicate silence on that string
     */
    std::vector<GuitarFrame> process(
        float** hexBuffers, 
        int bufferLength,
        const std::vector<DetectionParams>& params);
    
    /**
     * Process all 6 strings WITHOUT heap allocation (for real-time audio thread)
     * 
     * @param hexBuffers   Array of 6 float buffer pointers (one per string)
     * @param bufferLength Number of samples in each buffer
     * @param params       Detection parameters per string (std::array, no allocation)
     * @param outFrames    Pre-allocated output array (exactly 6 elements)
     * 
     * This overload is preferred for TIER 2 processing as it avoids all heap allocations.
     */
    void processNoAlloc(
        float** hexBuffers, 
        int bufferLength,
        const std::array<DetectionParams, 6>& params,
        std::array<GuitarFrame, 6>& outFrames);
    
    /**
     * Reset all string states (call after recalibration or mode change)
     */
    void reset();
    
    /**
     * Update sample rate (recreates CQT kernels)
     */
    void setSampleRate(double sampleRate);
    
    /**
     * Get bin magnitude for a specific string and fret (for UI heatmap)
     * Returns the CQT magnitude at the bin corresponding to the given fret's frequency.
     * MUST only be called AFTER processNoAlloc() on the same CQT frame data.
     * 
     * @param stringIdx 0-5 (Low E to High E)
     * @param fretIdx   0-24 (open to 24th fret)
     * @return CQT bin magnitude (0.0 - 1.0+ range, RMS-equivalent)
     */
    float getFretBinMagnitude(int stringIdx, int fretIdx) const;
    
    /**
     * Get threshold for a specific string and fret
     * Returns the scaled threshold value from the physics-based model.
     * 
     * @param stringIdx 0-5 (Low E to High E)  
     * @param fretIdx   0-24 (open to 24th fret)
     * @return Threshold multiplier (multiply by envFloor for actual threshold)
     */
    static float getThresholdMultiplier(int stringIdx, int fretIdx);

private:
    // CQT configuration constants
    static constexpr int BINS_PER_OCTAVE = 36;

    // Per-string state for hysteresis and flux tracking
    struct StringState {
        int currentFret = -1;
        int candidateFret = -1;
        int confirmationCount = 0;
        float lastBins[144] = {0.0f};
        float lastPeakMag = 0.0f;
    };
    StringState states[6];
    
    // Pimpl for CQT engine and working buffers
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
