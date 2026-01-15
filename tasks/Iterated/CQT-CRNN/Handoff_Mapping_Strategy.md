# CQT Transition Strategy: Mapping Existing Parameters

## 1. Primary Gates & Calibration
* **baselineFloor / baseline**: Continues to act as the "Master Gate." If `RMS < baseline`, the CQT engine for that string remains idle.
* **calibrationGain**: Used to normalize string levels before "Spatial Filtering" (crosstalk rejection) to ensure a fair comparison.

## 2. Threshold Evolution (Slope-Aware)
* **envFloor**: Sets the base musical threshold.
* **gateRatio**: Sets the attack threshold for the 0th fret: `Attack = envFloor * gateRatio`.
* **The Slope**: Thresholds now decay by 1.5% per fret index, making high-fret notes (15-24) more sensitive.

## 3. Note-Off & Release Logic
* **sustainFloorScale**: Used to calculate frequency-specific release: `sustainLimit = envFloor * sustainScale`.
* **Hysteresis**: Replaces the need for high `Pitch Tolerance`. A new fret must lead by 20% magnitude for 3 frames to update.

## 4. Retriggering & Articulation
* **retriggerGate**: Replaced by CQT Spectral Flux. If a flux spike occurs while a note is active and the new magnitude is > 40% of the current peak, a new Note-On is triggered.
* **Spectral Flux**: Replaces Aubio onset detection; it measures the rate of energy change within the specific CQT bins.

## 5. Filter Replacement
* **High/Low Pass**: Your manual frequency ranges (e.g., Low E: 70-165Hz) are now handled natively by the **Search Clamping** logic, scanning only the 72 bins relevant to each string's physical 24-fret reach.