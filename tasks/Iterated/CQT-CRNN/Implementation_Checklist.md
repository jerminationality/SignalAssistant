# Implementation Audit: CQTNoteDetector

✅ **STATUS**: CQT system fully integrated into TabEngine, replacing 6 StringTracker instances

## 1. Architecture & Real-Time Safety
- [x] **Unified Processing**: The `process()` function handles all 6 channels in one call (crucial for spatial filtering).
  - ✓ Line 312-318: Single `process(hexBuffers, bufferLength, params)` call handles all strings
- [x] **State Persistence**: Each string has its own persistent `StringState` (to track current fret and hysteresis counters).
  - ✓ Line 287-294: `StringState states[6]` in CQTNoteDetector class
- [x] **Zero-Allocation**: No `new`, `malloc`, or `std::vector::push_back` inside the audio processing loop.
  - ✓ Lines 176-180: Pre-allocated arrays in `Impl` struct
  - ✓ Line 306: `results.reserve(6)` before loop, only 6 fixed pushes

## 2. Parameter Mapping (Note Detection.md)
- [x] **Master Gate**: `if (rmsAmplitude < baseline)` is used to bypass CQT for silent strings.
  - ✓ Line 332: `passedGate[s] = (rms >= params[s].baseline)`
  - ✓ Line 336-340: Skips CQT computation if gated
- [x] **Two-Stage Gain System**: Separates hardware compensation from crosstalk logic
  - ✓ **Stage 1 - preAmpGain**: Applied to raw audio before CQT (UI-adjustable via calibrationGainMultiplier slider)
  - ✓ **Stage 2 - spatialWeight**: Applied to CQT bins for crosstalk comparison (calibration-fixed fairness multiplier)
  - ✓ Line 319-345: preAmpGain amplifies raw input, RMS calculated on amplified signal
  - ✓ Line 359: `weightedMag = binMagnitudes[s][bin] * spatialWeight` for crosstalk comparison
  - ✓ TabEngine.cpp lines 45-55: spatialWeight calculated from calibration profile RMS ratios
- [x] **Onset Logic**: `isAttack` triggers only if `magnitude > (envFloor * gateRatio)`.
  - ✓ Line 441: `attackThreshold = params[s].envFloor * params[s].gateRatio * slopeMultiplier`
  - ✓ Lines 480-486: `frame.isAttack = true` when confirmation reached
- [x] **Note-Off Logic**: `isSustaining` becomes false when `magnitude < (envFloor * sustainScale)`.
  - ✓ Line 447: `sustainLimit = params[s].envFloor * params[s].sustainScale`
  - ✓ Line 454-457: Note release when `peakMag < sustainLimit`

## 3. The 24-Fret "Golden Thread" Logic
- [x] **Slope-Aware Sensitivity**: Thresholds decrease by 1.5% per fret index (`1.0 - (fret * 0.015)`).
  - ✓ Line 440: `slopeMultiplier = 1.0f - (rawFret * SLOPE_DECAY)` where `SLOPE_DECAY = 0.015f`
- [x] **Search Clamping**: String 0 only scans bins 0-74, String 5 only scans 72-144, etc. (Prevents octave errors).
  - ✓ Lines 134-135: `kBinRangeStart` and `kBinRangeEnd` arrays define per-string ranges
  - ✓ Lines 403-405: `binStart = kBinRangeStart[s]`, `binEnd = min(kBinRangeEnd[s], 144)`
- [x] **Spatial Filter**: A bin is "killed" on String A if its normalized magnitude is higher on String B (Cross-string bleed rejection).
  - ✓ Lines 350-374: For each bin, find dominant string by normalized magnitude, zero other strings

## 4. Stability & Pitch
- [x] **Hysteresis (Sticky Fret)**: Fret changes require a 20% magnitude advantage over the current fret.
  - ✓ Line 139: `kHysteresisAdvantage = 0.20f`
  - ✓ Lines 491-493: `advantage = (peakMag - currentFretMag) / (currentFretMag + 1e-10f)`
  - ✓ Line 495: Check `advantage > kHysteresisAdvantage`
- [x] **Temporal Confirmation**: Fret changes must persist for 3 frames before the output `fret` value updates.
  - ✓ Line 140: `kConfirmationFrames = 3`
  - ✓ Lines 474-486, 495-509: Confirmation counter must reach 3 before fret updates

## 5. Integration Status
- [x] **TabEngine Migration**: Replaced 6 `StringTracker` instances with single `CQTNoteDetector`
  - ✓ TabEngine.h: Uses `std::unique_ptr<CQTNoteDetector> _cqtDetector`
  - ✓ TabEngine.cpp: Unified processing in `processBlock()` with parameter mapping
- [x] **Parameter Flow**: NoteDetectionStore → DetectionParams → CQT
  - ✓ Lines 37-47 (TabEngine.cpp): Maps `NoteDetectionParameterSet` to `DetectionParams`
- [x] **Event Generation**: GuitarFrame → NoteEvent conversion
  - ✓ Lines 55-149 (TabEngine.cpp): Converts CQT frames to note events with proper onset/sustain/release

## 6. Obsolete Parameters (CQT Replaces These)

### ❌ No Longer Used
- **`lowCutMultiplier`** / **`highCutMultiplier`**
  - **Replaced by**: Range Clamping (kBinRangeStart/kBinRangeEnd arrays)
  - **Why**: CQT bin ranges natively filter each string's frequency range
  - **Action**: Can hide these sliders in UI or mark as "Legacy - CQT mode"

- **`aubioThresholdScale`** / **`onsetSilenceDb`** / **`pitchSilenceDb`**
  - **Replaced by**: CQT Spectral Flux (computed directly from bin energy changes)
  - **Why**: CQT doesn't use Aubio library
  - **Action**: Can hide these sliders in UI

- **`pitchTolerance`**
  - **Replaced by**: Hysteresis (20% magnitude advantage + 3-frame confirmation)
  - **Why**: CQT uses discrete fret bins with confirmation, not continuous pitch smoothing
  - **Action**: Can hide this slider

- **`retriggerGateScale`**
  - **Replaced by**: Built-in flux spike detection (40% of peak magnitude threshold)
  - **Why**: CQT has dedicated retrigger logic in lines 529-536
  - **Action**: Can hide this slider

### ✅ Still Active
- **`baselineFloor`**: Master gate threshold
- **`envelopeFloor`**: Musical threshold base  
- **`gateRatio`**: Attack multiplier (now includes slope factor)
- **`sustainFloorScale`**: Release threshold multiplier
- **`calibrationGainMultiplier`**: **Maps to preAmpGain** - UI-adjustable pre-CQT amplification
  - User can manually boost/cut individual strings without breaking crosstalk math
  - Calibration sets initial values based on target RMS
- **`targetRms`**: Used during calibration
- **`onsetThresholdScale`**: Kept for compatibility but not actively used in CQT

### 🔧 Internal (Calibration-Derived)
- **`spatialWeight`**: Calculated from calibration profile, not exposed in UI
  - Normalizes pickup sensitivity for fair crosstalk comparison
  - Formula: `maxRms / avgRms[string]` from calibration data
  - Remains constant even when user adjusts calibrationGainMultiplier slider

### 📋 UI Cleanup Recommendations
1. **Hide/Disable** obsolete sliders when using CQT mode
2. **Group active parameters** in simplified tuning panel
3. **Add tooltip**: "CQT mode: frequency filters built-in"
4. Consider **legacy mode toggle** if reverting to StringTracker is needed