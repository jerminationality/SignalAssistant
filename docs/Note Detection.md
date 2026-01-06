================================================================================
                        NOTE DETECTION PARAMETERS
================================================================================

--------------------------------------------------------------------------------
CALIBRATION
--------------------------------------------------------------------------------

Noise Floor Capture
    Description:  Automatic baseline measurement during calibration
    Process:      Captures 2 seconds of silence before string calibration
    Computation:  Averages RMS across all 6 strings during quiet period
    Result:       Sets baselineFloor parameter automatically

String Gain Calibration
    Description:  Measures each string's output level for normalization
    Process:      2 seconds per string, user plucks each string when prompted
    Result:       Sets calibrationGain per string to normalize signal levels

--------------------------------------------------------------------------------
NOTE-ON (ONSET) PARAMETERS
--------------------------------------------------------------------------------

Onset Threshold
    Description:  Aubio spectral flux detection threshold (direct value)
    Range:        0.1-5.0 (direct comparison with onset strength, per-string)
    Behavior:     Higher = requires stronger attack transient to trigger
    Typical:      0.5-2.0 for most playing styles

baseline
    Description:  Hard noise floor - disregard all signal below this level
    Formula:      baseline = max(baselineFloor, epsilon)
    Input:        baselineFloor = 0.00018-0.00042 RMS (auto-set by calibration)
    Behavior:     Set by calibration, adjustable via slider (not saved to tuning presets)
    Note:         Like calibrationGain - calibration writes it, slider can adjust it

envFloor
    Description:  Musical threshold - minimum RMS for valid note events
    Formula:      envFloor = max(envelopeFloor, baseline)
    Input:        envelopeFloor = 0.00045-0.00105 RMS (user-adjustable)
    Check:        envelope > envFloor
    Behavior:     Static value, always >= baseline (hard noise floor)

gateThreshold
    Description:  Onset acceptance threshold 
    Formula:      gateThreshold = envFloor × gateRatio
    Input:        gateRatio = 1.0-10.0 (user-adjustable)
    Check:        envelope > gateThreshold

--------------------------------------------------------------------------------
NOTE-OFF (RELEASE) PARAMETERS
--------------------------------------------------------------------------------

sustainFloor
    Description:  Minimum RMS to keep note alive
    Formula:      sustainFloor = max(baseline, envFloor × sustainFloorScale)
    Input:        sustainFloorScale = 0.58-1.5 (user-adjustable)
    Check:        avgEnvelope < sustainFloor for N consecutive frames → release
    Note:         Can dip below envFloor (scale < 1.0) but never below baseline

retriggerGate
    Description:  Threshold for new attack during sustained note
    Formula:      retriggerGate = max(sustainFloor, cappedPeak × 0.4) 
                                  × retriggerGateScale
                  rmsRiseThreshold = max(sustainFloor × 1.5, lastPeak × 0.25)
    Input:        retriggerGateScale = 1.0-1.4 (user-adjustable)
    Check:        (onsetStrength > retriggerGate) AND (avgEnv > rmsRiseThreshold)
                  → release old, start new
    Note:         Requires BOTH onset detection AND RMS above threshold to prevent
                  false retriggers from fading notes where harmonic content shifts
                  produce onset spikes but RMS is actually decreasing

--------------------------------------------------------------------------------
FILTER PARAMETERS (PER-STRING)
--------------------------------------------------------------------------------

High Pass Filter
    Description:  Removes frequencies below string's fundamental
    Ranges:       Low E: 70-80 Hz    | A: 95-105 Hz   | D: 130-140 Hz
                  G: 180-190 Hz      | B: 230-240 Hz  | High E: 310-320 Hz

Low Pass Filter  
    Description:  Removes frequencies above playable range
    Ranges:       Low E: 155-165 Hz  | A: 210-220 Hz  | D: 285-295 Hz
                  G: 380-390 Hz      | B: 480-490 Hz  | High E: 645-655 Hz

--------------------------------------------------------------------------------
OTHER PARAMETERS
--------------------------------------------------------------------------------

Pitch Tolerance
    Description:  Maximum cents deviation per hop before pitch smoothing
    Range:        0.4-0.55 (user-adjustable)

Target RMS
    Description:  Target signal level for calibration normalization
    Range:        0.01-0.05 RMS (user-adjustable)

Gain Multiplier
    Description:  Manual gain adjustment after calibration
    Range:        0.5-2.0 (user-adjustable, per-string)

================================================================================
                        ONSET ACCEPTANCE FLOW
================================================================================

For an onset to be accepted, ALL of the following must pass:

    1. onsetStrength > onsetThreshold     (aubio spectral flux)
    2. envelope > gateThreshold           (proportional to noise floor)
    3. envelope > envFloor                (absolute minimum)

================================================================================
                        THRESHOLD RELATIONSHIPS
================================================================================

Expected numerical ordering (lowest to highest):

    baseline < sustainFloor ≤ envFloor < gateThreshold

Where:
    - baseline:       Noise floor (scaled by calibration)
    - sustainFloor:   Note decay cutoff (allows quiet sustain)
    - envFloor:       Minimum valid signal (filters residual noise)
    - gateThreshold:  Attack detection (requires strong onset)

================================================================================