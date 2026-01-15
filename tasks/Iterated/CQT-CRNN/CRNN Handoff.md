You are Copilot working in an existing C++/Qt/QML project (“GuitarPi”). Implement the first-pass ML articulation classifier pipeline using TensorFlow Lite. The goal is: for each detected NoteEvent (already produced by TabEngine), extract a short per-string feature window and run a lightweight CRNN (Conv1D + GRU) to classify articulation. The ML output is advisory and should annotate NoteEvents (label + confidence) and be displayed in QML.

CONSTRAINTS
- The existing project already has main.cpp and a working TabEngine/TabEngineBridge that emits note events to QML.
- Hex input provides per-string audio channels; event windows are per-string.
- Keep latency low: run inference only on note events (not continuously every audio block).
- Must be modular: feature extraction, dataset logging, and inference should be separable.
- Training happens on PC in Python; deployment is TFLite on Pi. Implement the runtime inference + feature extraction + logging in C++.

TARGET ARTICULATION CLASSES (v1)
Use 6 classes:
- pick
- hammer_on
- pull_off
- slide
- bend_or_vibrato
- mute_or_dead
Return (label, confidence) per note event.

MODEL INPUT SPEC
For each NoteEvent, extract a 480ms window (80ms pre + 400ms post) from the same string channel.
Feature hop = 10ms → 48 frames.
Per frame features (F=53):
- 48-bin log-mel spectrogram (computed from the windowed audio, frame by frame)
- RMS (1)
- onset_strength (1) (can use existing onset metric from note detector; if not available, compute a simple spectral flux)
- pitch_midi (1) (use existing pitch tracker output for the string; if not available, leave as 0 for now)
- pitch_delta (1)
- pitch_dev_cents (1) (deviation from nearest semitone)
Total feature tensor shape: [48, 53] float32. Normalize via mean/std loaded from a JSON file produced by training.

CRNN ARCHITECTURE (TRAINING SIDE REFERENCE)
We won’t train in C++, but assume the exported TFLite model matches:
- Conv1D 32 filters, k=5, BN, ReLU, Dropout
- Conv1D 48 filters, k=5, BN, ReLU, MaxPool1D(2), Dropout
- Conv1D 64 filters, k=3, BN, ReLU, Dropout
- GRU 48 units
- Dense 64 + ReLU + Dropout
- Dense num_classes + Softmax
TFLite input: [1, 48, 53] float32
TFLite output: [1, 6] float32 probabilities

DELIVERABLES (GENERATE THESE FILES)
1) C++: ArticulationClassifierTFLite
- Files:
  - src/ml/ArticulationClassifierTFLite.h
  - src/ml/ArticulationClassifierTFLite.cpp
- Responsibilities:
  - Load TFLite model from a path (configurable)
  - Load normalization stats from JSON (mean[53], std[53])
  - Provide: classify(features[48][53]) -> (label enum, confidence float, probs[6])
  - Threading: inference should run on a worker thread; return results via callback/Qt signal.

2) C++: FeatureExtractor (per note event)
- Files:
  - src/ml/FeatureExtractor.h
  - src/ml/FeatureExtractor.cpp
- Responsibilities:
  - Maintain a rolling ring buffer per string of raw float audio samples at sampleRate (48k).
  - Provide: extractFeatureWindow(stringIdx, eventTime, preMs=80, postMs=400) -> features[48][53]
  - Implement log-mel extraction:
    - frame size 1024, hop 480 samples (10ms at 48k)
    - Hann window
    - FFT magnitude
    - 48 mel filterbanks (hardcode mel filterbank generation or precompute at init)
    - log(1e-6 + mel_energy)
  - Compute RMS per frame.
  - Onset strength:
    - if existing onset metric isn’t exposed, implement spectral flux between consecutive FFT frames.
  - Pitch features:
    - use existing pitch tracker output if accessible; otherwise stub with zeros but keep API.

3) Integration into TabEngineBridge
- Update TabEngineBridge (or a new TabArticulationAnnotator) so that:
  - When a NoteEvent is created/confirmed, it triggers feature extraction + async inference.
  - When inference returns, it attaches `articulationLabel` and `articulationConfidence` to the NoteEvent and emits an updated signal to QML.
  - Avoid blocking the audio callback thread at all costs.

4) QML: Minimal display
- Update the tab overlay UI to display the articulation label near each note (e.g., small suffix like “h”, “p”, “sl”, “b/v”, “m” or a text tag).
- Add a debug panel toggle to show confidence and raw probabilities for the last event.

5) Dataset logging (for training)
- Files:
  - src/ml/DatasetLogger.h
  - src/ml/DatasetLogger.cpp
- Responsibilities:
  - Save each event’s feature tensor + metadata (stringIdx, detected fret/midi, timestamp, predicted label/confidence) to disk in a simple format.
  - Prefer NPZ if you can write it; otherwise write:
    - JSON metadata
    - binary float32 tensor file
  - Include a CLI/config flag to enable/disable logging.

CONFIG
Add a simple config file or constants for:
- model path: models/articulation_crnn.tflite
- normalization path: models/norm_stats.json
- enableLogging bool
- sampleRate (must match engine)

IMPLEMENTATION NOTES
- Use TensorFlow Lite C++ API (tflite::FlatBufferModel, tflite::Interpreter).
- Keep allocations out of the audio thread; preallocate buffers where possible.
- The ring buffer per string should be lock-free or use minimal locking (single producer: audio thread; single consumer: worker thread) with atomic indices.
- If your build system lacks TFLite, add CMake find/package stubs and clearly comment integration points.

ACCEPTANCE CRITERIA
- App runs with classifier disabled (no model file) gracefully.
- With a valid TFLite model, notes get annotated with an articulation label within ~100ms of detection.
- No xruns introduced by ML work.
- Logging writes training samples that can be used in Python to train/iterate.

Generate all files with clean headers, clear comments, and minimal dependencies. Keep naming consistent with existing TabEngine/Bridge conventions.
