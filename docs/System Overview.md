# CQT Note Detection System - Technical Overview

## Architecture Summary

The CQT (Constant-Q Transform) note detection system replaces the previous 6-instance StringTracker approach with a unified detector that processes all strings simultaneously. This enables sophisticated crosstalk rejection while providing superior frequency resolution for guitar fret detection.

---

## Signal Processing Chain

```
┌─────────────────────────────────────────────────────────────┐
│ 1. RAW AUDIO INPUT (6 channels)                             │
│    hexBuffers[0..5] → mono float buffers per string         │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 2. STAGE 1: PRE-AMPLIFICATION (Hardware Compensation)       │
│    amplified[s][i] = hexBuffers[s][i] × preAmpGain[s]      │
│                                                              │
│    preAmpGain = calibrationGainMultiplier (UI slider)       │
│    • Compensates for weak pickups                           │
│    • User-adjustable after calibration                      │
│    • Ensures signal reaches target RMS                      │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 3. RMS CALCULATION & MASTER GATE                            │
│                                                              │
│    RMS[s] = √(Σ(amplified[s][i]²) / N)                     │
│                                                              │
│    if (RMS[s] < baseline[s]):                               │
│        passedGate[s] = false  → Skip CQT, output silence    │
│    else:                                                     │
│        passedGate[s] = true   → Continue to CQT             │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 4. CQT TRANSFORM (144 bins, 36 bins/octave)                 │
│                                                              │
│    For each string s that passed gate:                      │
│        For each bin k ∈ [0..143]:                           │
│            f_k = 70Hz × 2^(k/36)                            │
│            binMagnitudes[s][k] = CQT(amplified[s], f_k)     │
│                                                              │
│    Output: 144-bin spectrum per string (~70-1400Hz)         │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 5. STAGE 2: SPATIAL WEIGHTING (Crosstalk Fairness)          │
│                                                              │
│    spatialWeight[s] = maxRMS / avgRMS[s]                   │
│    (from calibration profile, fixed after calibration)      │
│                                                              │
│    For each bin k:                                           │
│        For each string s:                                    │
│            weighted[s][k] = binMagnitudes[s][k] × spatialWeight[s] │
│                                                              │
│        dominantString = argmax_s(weighted[s][k])            │
│                                                              │
│        For all strings except dominantString:                │
│            binMagnitudes[s][k] = 0  (crosstalk suppression) │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 6. PER-STRING FRET DETECTION                                │
│    (See detailed formulas below)                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Core Formulas

### 1. Two-Stage Gain System

**Stage 1: Pre-Amplification (before CQT)**
```
amplified[s][i] = rawAudio[s][i] × preAmpGain[s]

where:
    preAmpGain[s] = calibrationGainMultiplier[s]  (from UI)
```

**Stage 2: Spatial Weighting (for crosstalk comparison)**
```
spatialWeight[s] = max(avgRMS[0..5]) / avgRMS[s]

where:
    avgRMS[s] = measured during calibration
    
weighted[s][k] = binMagnitudes[s][k] × spatialWeight[s]
```

**Key Property**: User can adjust `preAmpGain` without affecting crosstalk math, since `spatialWeight` remains constant.

---

### 2. Bin-to-Frequency Mapping

**CQT Center Frequencies:**
```
f_k = f_min × 2^(k / B)

where:
    f_min = 70 Hz          (below low E fundamental at 82.41 Hz)
    B = 36                 (bins per octave)
    k ∈ [0, 143]           (144 total bins)
    
Example frequencies:
    k=0   → 70.0 Hz
    k=36  → 140.0 Hz (1 octave above f_min)
    k=72  → 280.0 Hz (2 octaves)
    k=143 → ~1396 Hz
```

**Bin Range Clamping (per string):**
```
String | Bin Range | Coverage
-------|-----------|----------
E (0)  | 0-74      | 70-350 Hz (82 Hz open → 24th fret)
A (1)  | 15-89     | 95-465 Hz
D (2)  | 30-104    | 127-622 Hz
G (3)  | 45-119    | 170-831 Hz
B (4)  | 57-131    | 214-1048 Hz
e (5)  | 72-144    | 280-1396 Hz
```

**Bin-to-Fret Conversion:**
```
relativeBin = peakBin - binStart[string]
fret = round(relativeBin × 24 / 72)
fret = clamp(fret, 0, 24)
```

---

### 3. Slope-Aware Threshold System

**Attack Threshold (decreases 1.5% per fret):**
```
slopeMultiplier = 1.0 - (fret × 0.015)

attackThreshold[s] = envFloor[s] × gateRatio[s] × slopeMultiplier

Example for fret 12:
    slopeMultiplier = 1.0 - (12 × 0.015) = 0.82
    → Threshold is 18% lower than fret 0
    
Example for fret 24:
    slopeMultiplier = 1.0 - (24 × 0.015) = 0.64
    → Threshold is 36% lower than fret 0
```

**Rationale**: High frets produce naturally quieter signals, so reducing the threshold compensates.

---

### 4. Onset/Sustain/Release Logic

**Note-On Condition:**
```
if (peakMagnitude > attackThreshold):
    candidateFret = fret
    confirmationCount++
    
    if (confirmationCount >= 3):
        currentFret = candidateFret
        isAttack = true
```

**Sustain Condition:**
```
sustainLimit = envFloor × sustainScale

if (currentFret >= 0 AND peakMagnitude >= sustainLimit):
    isSustaining = true
```

**Note-Off Condition:**
```
if (currentFret >= 0 AND peakMagnitude < sustainLimit):
    currentFret = -1
    isSustaining = false
```

---

### 5. Hysteresis (Sticky Fret)

**Fret Change Requires 20% Advantage:**
```
currentFretMag = binMagnitudes[binStart + (currentFret × 72/24)]
newFretMag = peakMagnitude

advantage = (newFretMag - currentFretMag) / (currentFretMag + ε)

if (newFret ≠ currentFret AND advantage > 0.20):
    candidateFret = newFret
    confirmationCount++
    
    if (confirmationCount >= 3):
        currentFret = newFret  // Fret change accepted
else:
    candidateFret = currentFret  // Stick with current fret
    confirmationCount = 3
```

**Temporal Confirmation**: Prevents jitter by requiring 3 consecutive frames (typically ~30ms at 10ms hop rate).

---

### 6. Spectral Flux (for retriggering)

**Flux Calculation:**
```
flux = √(Σ max(0, binMag[t] - binMag[t-1])²)

where sum is over all bins in string's range
```

**Retrigger Logic:**
```
if (currentFret >= 0 AND NOT isAttack):
    fluxThreshold = lastPeakMag × 0.4
    magThreshold = lastPeakMag × 0.4
    
    if (flux > fluxThreshold AND peakMag > magThreshold):
        isAttack = true  // New attack on same fret
```

**Purpose**: Detects re-articulation of the same note without requiring pitch change.

---

### 7. Fine Pitch (Parabolic Interpolation)

**Sub-bin Refinement:**
```
Given peak at bin k with magnitudes:
    y₀ = binMagnitudes[k-1]
    y₁ = binMagnitudes[k]    (peak)
    y₂ = binMagnitudes[k+1]

Parabolic offset (in bins):
    Δk = 0.5 × (y₀ - y₂) / (y₀ - 2y₁ + y₂)
    
Conversion to cents:
    centOffset = Δk × (1200 / 36)
                = Δk × 33.33 cents/bin
```

**Range**: Δk ∈ [-0.5, 0.5], so centOffset ∈ [-16.67, +16.67] cents

**Final Pitch:**
```
basePitchHz = midiToHz(openMidi[s] + fret)
finalPitchHz = basePitchHz × 2^(centOffset/1200)
```

---

## Parameter Summary

### Active Parameters (User-Tunable)

| Parameter | Formula/Usage | Typical Range |
|-----------|--------------|---------------|
| `baselineFloor` | Master gate: `RMS < baseline → skip CQT` | 0.00018-0.00042 |
| `envelopeFloor` | Musical threshold base | 0.00045-0.00105 |
| `gateRatio` | `attackThreshold = envFloor × gateRatio × slope` | 1.0-10.0 |
| `sustainFloorScale` | `sustainLimit = envFloor × sustainScale` | 0.58-1.5 |
| `preAmpGain` | Pre-CQT amplification (UI slider) | 0.5-2.0 |
| `targetRms` | Calibration target | 0.01-0.05 |

### Internal Parameters (Calibration-Derived)

| Parameter | Formula | Source |
|-----------|---------|--------|
| `spatialWeight[s]` | `max(avgRMS) / avgRMS[s]` | Calibration profile |

### Obsolete Parameters (Replaced by CQT)

| Old Parameter | CQT Replacement |
|---------------|-----------------|
| `lowCutMultiplier` / `highCutMultiplier` | Bin range clamping |
| `aubioThresholdScale` | Spectral flux |
| `pitchTolerance` | Hysteresis + confirmation |
| `retriggerGateScale` | Flux spike detection |

---

## Crosstalk Rejection Example

**Scenario**: Low E string (strong pickup, avgRMS=0.8) vs High E string (weak pickup, avgRMS=0.3)

**Calibration Phase:**
```
maxRMS = max(0.8, ..., 0.3) = 0.8

spatialWeight[Low E] = 0.8 / 0.8 = 1.0
spatialWeight[High E] = 0.8 / 0.3 = 2.67
```

**During Playback** (bin 80 analysis, user boosted High E preAmp to 1.5):
```
Raw CQT output:
    binMag[Low E][80] = 0.05  (crosstalk bleed)
    binMag[High E][80] = 0.08 (actual note)

Weighted comparison:
    weighted[Low E] = 0.05 × 1.0 = 0.05
    weighted[High E] = 0.08 × 2.67 = 0.21

Result: High E wins → Low E bin[80] = 0 (crosstalk suppressed)
```

**Key**: Even though user adjusted preAmpGain, `spatialWeight` ensures fair comparison!

---

## Performance Characteristics

### Computational Complexity
- **CQT**: O(N log N) per string via FFT-based kernels
- **Crosstalk filtering**: O(144 × 6) = O(1) constant time
- **Fret detection**: O(72) per string (bin range scan)

### Memory Footprint
- **CQT kernels**: ~200KB (pre-computed, shared across strings)
- **Working buffers**: ~4KB per string × 6 = 24KB
- **State tracking**: ~1KB for 6 StringStates

### Latency
- **Algorithmic**: Same as hop size (typically 10ms)
- **Hysteresis delay**: 3 frames × hop = ~30ms worst-case for fret changes
- **Total**: ~40ms end-to-end (competitive with commercial systems)

---

## Advantages Over Previous System

| Aspect | StringTracker (FFT) | CQTNoteDetector |
|--------|---------------------|-----------------|
| **Frequency Resolution** | ~12 bins/octave | 36 bins/octave (3× better) |
| **Crosstalk Rejection** | None (6 independent trackers) | Unified spatial filtering |
| **High Fret Accuracy** | Fixed threshold (misses quiet notes) | Slope-aware (1.5% decay/fret) |
| **Stability** | Pitch smoothing + tolerance | Hysteresis + temporal confirmation |
| **Onset Detection** | Aubio spectral flux (external) | Native CQT flux (no dependencies) |
| **Filter Management** | Manual HP/LP per string | Automatic bin range clamping |
| **Gain Architecture** | Single-stage (breaks crosstalk) | Two-stage (UI-safe) |

---

## Calibration Workflow

1. **Noise Floor Capture** (2 seconds silence)
   - Sets `baselineFloor[0..5]` automatically

2. **Per-String RMS Measurement** (1.25 seconds each)
   - User plucks each string individually
   - Records `avgRMS[s]` and `peakRMS[s]`

3. **Gain Calculation**
   ```
   preAmpGain[s] = targetRms / avgRMS[s]
   spatialWeight[s] = max(avgRMS) / avgRMS[s]
   ```

4. **Storage**
   - `preAmpGain[s]` → saved to `calibrationGainMultiplier[s]` (UI-editable)
   - `spatialWeight[s]` → saved to `CalibrationProfile` (fixed)

5. **User Adjustment** (optional)
   - Slider changes `preAmpGain` without affecting `spatialWeight`
   - Crosstalk math remains accurate

---

## Future Enhancements (Potential)

- **Adaptive thresholds**: Dynamic adjustment based on playing dynamics
- **Harmonic analysis**: Use 2nd/3rd harmonics for ambiguous frets
- **Multi-fret detection**: Support for chords (currently single-note per string)
- **Bend tracking**: Continuous pitch bend curves from centOffset history
- **Machine learning**: Train fret classifier on user's specific guitar/pickup
