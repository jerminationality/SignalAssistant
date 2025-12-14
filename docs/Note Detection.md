================================================================================
                        NOTE DETECTION PARAMETERS
================================================================================

--------------------------------------------------------------------------------
NOTE-ON (ONSET) PARAMETERS
--------------------------------------------------------------------------------

Onset Threshold
    Description:  Aubio spectral flux detection threshold
    Range:        0.02-4.0 (direct value)
    Behavior:     Higher = requires stronger attack transient to trigger

baseline
    Description:  Adaptive noise floor estimate
    Formula:      baseline = mix(baselineFloor, adaptiveRms × 0.9, 
                                  lastOnsetPeakRms × 0.9)
    Input:        baselineFloor = 0.00018-0.00042 RMS (user-adjustable)

gateThreshold
    Description:  Proportional onset acceptance threshold
    Formula:      gateThreshold = baseline × gateRatio
    Input:        gateRatio = 0.055-0.25 (user-adjustable)
    Check:        envelope > gateThreshold

envFloor
    Description:  Absolute minimum RMS for valid note events
    Formula:      envFloor = mix(max(envelopeFloor, baseline × 0.7),
                                  adaptiveRms × 0.6, lastOnsetPeakRms × 0.5)
    Input:        envelopeFloor = 0.00045-0.00105 RMS (user-adjustable)
    Check:        envelope > envFloor

--------------------------------------------------------------------------------
NOTE-OFF (RELEASE) PARAMETERS
--------------------------------------------------------------------------------

sustainFloor
    Description:  Minimum RMS to keep note alive
    Formula:      sustainFloor = envelopeFloor × sustainFloorScale
    Input:        sustainFloorScale = 0.58-1.5 (user-adjustable)
    Check:        avgEnvelope < sustainFloor for N consecutive frames → release

retriggerGate
    Description:  Threshold for new attack during sustained note
    Formula:      retriggerGate = max(sustainFloor, cappedPeak × 0.4) 
                                  × retriggerGateScale
    Input:        retriggerGateScale = 1.0-1.4 (user-adjustable)
    Check:        newOnsetStrength > retriggerGate → release old, start new

--------------------------------------------------------------------------------
OTHER PARAMETERS
--------------------------------------------------------------------------------

Peak Release Ratio
    Description:  Envelope decay target as fraction of recent peak
    Range:        0.12-0.20 (user-adjustable)
    Usage:        Tracking when envelope has decayed sufficiently

Pitch Tolerance
    Description:  Maximum cents deviation per hop before pitch smoothing
    Range:        0.4-0.55 (user-adjustable)

================================================================================
                        ONSET ACCEPTANCE FLOW
================================================================================

For an onset to be accepted, ALL of the following must pass:

    1. onsetStrength > onsetThreshold     (aubio spectral flux)
    2. envelope > gateThreshold           (proportional to noise floor)
    3. envelope > envFloor                (absolute minimum)

================================================================================