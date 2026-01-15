# UI Mapping: Tuning Panel to CQT Engine

## 1. Global Controls (Persistence)
| UI Slider | CQT Variable | Internal Action |
| :--- | :--- | :--- |
| **Gain Multiplier** | `preAmpGain` | Scales raw input *before* the Master Gate. |
| **Baseline** | `baselineFloor` | The RMS threshold for the Stage 3 Master Gate. |
| **Env Floor** | `envFloor` | The base CQT magnitude threshold (Musical Floor). |
| **Gate Ratio** | `gateRatio` | Multiplier for `envFloor` to set Attack threshold. |
| **Sustain Scale**| `sustainScale` | Multiplier for `envFloor` to set Release point. |

## 2. Parameter Logic Changes
- **Slope-Awareness (Auto):** The `envFloor` slider now controls the *base* of a curve. The engine automatically reduces this value by 1.5% per fret. The developer does NOT need to provide a high-fret sensitivity slider.
- **Crosstalk Rejection (Hidden):** The `spatialWeight` is a hidden internal multiplier derived from calibration. It should not be exposed as a slider to prevent users from breaking the physics-based rejection logic.

## 3. Repurposing "Pitch Tolerance"
The old "Pitch Tolerance" slider should be renamed to **"Stability"**. 
- **Internal Mapping:** It should control the `confirmationCount` (the number of frames a pitch must be stable before triggering).
- **Range:** 2 (Fast/Risky) to 6 (Solid/Slow). Default = 3.