# Data Store Integration: CQT Parameters

This document outlines how to adapt the existing `NoteDetectionStore` to support the Dual-Stage Gain and CQT-specific logic.

## 1. Schema Updates
The store must now distinguish between **User-Adjustable** parameters (the Tuning Panel) and **Calibrated-Fixed** parameters (The Crosstalk Logic).

### New Parameter Mapping:
| Old Field Name | New Internal Logic | Storage Type |
| :--- | :--- | :--- |
| `calibrationGainMultiplier` | `preAmpGain` | **Mutable** (UI Slider affects this directly) |
| `baselineFloor` | `baselineFloor` | **Mutable** (UI Slider) |
| `envelopeFloor` | `envFloor` | **Mutable** (UI Slider - acts as Curve Base) |
| `sustainFloorScale` | `sustainScale` | **Mutable** (UI Slider) |
| `NEW: spatialWeight` | `spatialWeight` | **Immutable** (Set by Calibration Wizard ONLY) |
| `NEW: stabilityValue` | `confirmationCount`| **Mutable** (Derived from Pitch Tolerance Slider) |

## 2. The Persistence Contract (Load/Save)

### Loading into the DSP Engine:
When the store initializes or a profile is loaded, it must push the values to the `CQTNoteDetector` instance. 

**Logic Flow:**
1. Fetch `calibrationGainMultiplier` from disk → Apply to `detector.setPreAmpGain(stringIndex, value)`.
2. Fetch `spatialWeight` from the Calibration Profile → Apply to `detector.setSpatialWeight(stringIndex, value)`.
3. Fetch `envFloor` → Apply to `detector.setEnvFloor(stringIndex, value)`.
   * *Note: The engine will handle the 1.5% fret-slope internally using this base value.*

## 3. Real-Time UI Binding Logic
The Store should act as the "Middleman" for UI updates to ensure that manual tweaks do not overwrite calibrated data.

- **Action:** User moves "Gain Multiplier" slider.
- **Store Logic:** Update `calibrationGainMultiplier` in the data model → Immediately call `detector.setPreAmpGain()`.
- **Constraint:** Ensure this action **NEVER** touches the `spatialWeight` variable. This preserves the "Fairness Multiplier" established during the calibration phase.

## 4. Derived Stability Logic
If the UI uses the legacy `pitchTolerance` slider:
- Map `0.0 (Tight)` to `6 frames` (Maximum stability).
- Map `1.0 (Loose)` to `2 frames` (Maximum speed).
- Update the engine via `detector.setConfirmationFrames(calculatedValue)`