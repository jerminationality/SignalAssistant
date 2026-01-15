================================================================================
                        NOTE DETECTION PARAMETERS (CQT SYSTEM)
================================================================================

--------------------------------------------------------------------------------
CALIBRATION
--------------------------------------------------------------------------------

Noise Floor Capture
    Description:  Automatic baseline measurement during calibration
    Process:      Captures 2 seconds of silence before string calibration
    Computation:  Averages RMS across all 6 strings during quiet period
    Result:       Sets baselineFloor parameter automatically

String Gain Calibration (Two-Stage System)
    Description:  Measures each string's output level for two purposes:
    
    Stage 1 - preAmpGain (UI-adjustable):
        Process:  Calculates gain to reach target RMS
        Formula:  preAmpGain = targetRms / measuredAvgRms
        Storage:  Saved to calibrationGainMultiplier (UI slider)
        Purpose:  Pre-CQT amplification to compensate for pickup sensitivity
        User:     Can adjust via slider after calibration
    
    Stage 2 - spatialWeight (calibration-fixed):
        Process:  Calculates relative sensitivity for crosstalk rejection
        Formula:  spatialWeight = max(allStringRms) / thisStringRms
        Storage:  Saved to CalibrationProfile (internal, not UI-exposed)
        Purpose:  Fair comparison across strings during spatial filtering
        User:     Cannot adjust (preserves crosstalk rejection accuracy)

--------------------------------------------------------------------------------
NOTE-ON (ONSET) PARAMETERS
--------------------------------------------------------------------------------

baseline (Master Gate)
    Description:  Hard noise floor - disregard all signal below this level
    Formula:      baseline = baselineFloor
    Input:        baselineFloor = 0.00018-0.00042 RMS (auto-set by calibration)
    Check:        if (RMS < baseline) → Skip CQT processing for this string
    Behavior:     Set by calibration, adjustable via slider
    CQT:          Applied AFTER preAmpGain, before CQT transform

envFloor
    Description:  Musical threshold - minimum RMS for valid note events
    Formula:      envFloor = envelopeFloor
    Input:        envelopeFloor = 0.00045-0.00105 RMS (user-adjustable)
    Behavior:     Static value, should be >= baseline
    CQT:          Used as base for attack and sustain thresholds

gateThreshold (Slope-Aware)
    Description:  Onset acceptance threshold with fret-dependent sensitivity
    Formula:      gateThreshold = envFloor × gateRatio × (1.0 - fret × 0.015)
    Input:        gateRatio = 1.0-10.0 (user-adjustable)
    Check:        peakRMS > gateThreshold → onset detected
    CQT Feature:  Slope factor makes high frets (15-24) more sensitive by 22-36%
    Example:      Fret 0:  threshold × 1.00
                  Fret 12: threshold × 0.82 (18% lower)
                  Fret 24: threshold × 0.64 (36% lower)

--------------------------------------------------------------------------------
NOTE-OFF (RELEASE) PARAMETERS
--------------------------------------------------------------------------------

sustainFloor
    Description:  Minimum RMS level to keep note alive
    Formula:      sustainFloor = envFloor × sustainFloorScale
    Input:        sustainFloorScale = 0.58-1.5 (user-adjustable)
    Check:        peakRMS < sustainFloor → note-off
    CQT:          Applied to CQT RMS output (bin magnitude)
    Note:         Can dip below envFloor (scale < 1.0) for quiet sustains

retriggerGate (Spectral Flux Spike Detection)
    Description:  Detects new attack during sustained note via CQT spectral flux
    Formula:      if (flux > lastPeakRMS × 0.4) AND (newRMS > lastPeakRMS × 0.4)
                  → trigger new attack on same fret
    Purpose:      Detects re-articulation without requiring pitch change

--------------------------------------------------------------------------------
FREQUENCY FILTERING (CQT NATIVE)
--------------------------------------------------------------------------------

Bin Range Clamping
    Description:  Each string scans only bins within its physical frequency range
    Ranges:       String 0 (E): bins 0-74   → 70-350 Hz   (24 frets covered)
                  String 1 (A): bins 15-89  → 95-465 Hz
                  String 2 (D): bins 30-104 → 127-622 Hz
                  String 3 (G): bins 45-119 → 170-831 Hz
                  String 4 (B): bins 57-131 → 214-1048 Hz
                  String 5 (e): bins 72-144 → 280-1396 Hz
    Benefit:      Automatic octave error prevention, no manual filter tuning

--------------------------------------------------------------------------------
OTHER PARAMETERS
--------------------------------------------------------------------------------

Hysteresis & Stability
    Description:  Discrete fret bins with stability mechanisms
    Mechanism:    - 20% magnitude advantage required for fret change
                  - 3 consecutive frames (Sticky Fret)
    Purpose:      Prevents fret jitter and false oscillations

Target RMS
    Description:  Target signal level for calibration normalization
    Range:        0.01-0.05 RMS (user-adjustable)
    CQT:          Used to calculate initial preAmpGain during calibration

Gain Multiplier (calibrationGainMultiplier → preAmpGain)
    Description:  Pre-CQT signal amplification (Stage 1 gain)
    Range:        0.5-2.0 (user-adjustable, per-string)
    CQT:          Applied to raw audio BEFORE CQT transform
    Purpose:      Compensates for pickup sensitivity, ensures signal clears baseline
    Safety:       User adjustments don't affect crosstalk rejection (spatialWeight is separate)

================================================================================
                        CQT ONSET ACCEPTANCE FLOW
================================================================================

For an onset to be accepted, the following sequence occurs:

    1. Master Gate Check:
       if (RMS < baseline) → Skip CQT processing for this string

    2. CQT Transform:
       Compute 144-bin spectrum (36 bins/octave, ~70-1400Hz)

    3. Spatial Filtering (Crosstalk Rejection):
       For each bin: dominant = argmax(binRMS[s] × spatialWeight[s])
       Suppress bin on all non-dominant strings

    4. Peak Search (Range-Clamped):
       Search only bins within string's 24-fret range
       Find peak bin and estimate fret

    5. Slope-Aware Threshold Check:
       attackThreshold = envFloor × gateRatio × (1.0 - fret × 0.015)
       if (peakRMS < attackThreshold) → No onset

    6. Hysteresis (Sticky Fret):
       If changing fret: require 20% RMS advantage
       
    7. Temporal Confirmation:
       New fret must persist for 3 consecutive frames
       
    8. Note-On Accepted:
       isAttack = true, output fret with centOffset (parabolic interpolation)

================================================================================
                        THRESHOLD RELATIONSHIPS (CQT)
================================================================================

Expected numerical ordering (lowest to highest):

    baseline < sustainFloor ≤ envFloor < gateThreshold

Where:
    - baseline:         Noise floor (Master Gate, pre-CQT)
    - sustainFloor:     Note decay cutoff (envFloor × sustainScale)
    - envFloor:         Musical threshold base
    - gateThreshold:    Attack threshold (envFloor × gateRatio × slope)

Special Notes:
    - Slope factor decreases threshold by 1.5% per fret
    - Fret 24: gateThreshold is 36% lower than fret 0
    - spatialWeight ensures fair crosstalk comparison regardless of preAmpGain

================================================================================
                        CQT SYSTEM ADVANTAGES
================================================================================

Frequency Resolution:
    - 36 bins/octave (vs FFT's ~12)
    - 3× better pitch discrimination
    - Native bin-to-fret mapping

Crosstalk Rejection:
    - Unified 6-string processing
    - Spatial filtering with fair comparison
    - Eliminates false detections from adjacent strings

High Fret Accuracy:
    - Slope-aware thresholds (1.5% decay per fret)
    - Compensates for naturally quieter high notes
    - 22-36% more sensitive at frets 15-24

Stability:
    - Hysteresis (20% advantage for fret changes)
    - Temporal confirmation (3-frame persistence)
    - No pitch smoothing artifacts

Simplicity:
    - No manual HP/LP filter tuning
    - Native spectral flux (no Aubio dependency)
    - Automatic frequency range handling

Two-Stage Gain:
    - preAmpGain: User-adjustable (UI slider)
    - spatialWeight: Calibration-fixed (crosstalk fairness)
    - User can boost/cut without breaking crosstalk math

================================================================================