GuitarPi Latency & Real-Time Safety Audit Checklist

(RPi 5 · Linux · PipeWire + JACK · Scarlett 18i20)

1. Audio Thread (Hard Real-Time Rules)

 JACK audio callback performs no dynamic memory allocation

 JACK audio callback performs no locking / mutex usage

 JACK audio callback performs no logging, printing, or Qt calls

 JACK audio callback performs no ML inference

 JACK audio callback performs no file I/O

 All audio buffers are pre-allocated

 All per-callback work is O(n) with bounded time

FAIL if any blocking or heap allocation is detected in the audio callback.

2. Thread Separation & Scheduling

 Audio thread runs at real-time priority (PipeWire/JACK managed)

 Feature extraction runs in a non-RT worker thread

 ML inference runs in a separate async worker thread

 UI/QML runs on the main GUI thread only

 No thread other than the audio thread touches JACK buffers directly

3. Buffering & Ring Buffers

 Audio → analysis communication uses lock-free ring buffers

 Ring buffers are single-producer / single-consumer

 Buffer sizes are sufficient for ≥ 500 ms of audio per string

 No buffer resizing occurs during runtime

 Buffer overruns are handled gracefully (drop, not block)

4. Latency Targets (Reality-Checked)

 System sample rate is 48,000 Hz

 PipeWire/JACK default buffer size is 64 frames

 Stable fallback buffer size of 128 frames exists

 32-frame mode is treated as experimental

 UI timing does not assume <8 ms round-trip latency

Expected real-world RTL on 18i20:

64 frames → ~9–11 ms

128 frames → ~12–14 ms

5. Timestamping & Timebases

 All NoteEvents are timestamped using JACK frame time

 Frame timestamps are converted to monotonic time

 UI rendering applies a fixed presentation offset

 No dynamic latency compensation is applied at runtime

 UI never “leads” audio events

Consistency > absolute latency.

6. Feature Extraction Safety

 Feature extraction never runs on the audio thread

 Feature windows are copied once from ring buffers

 No FFT, mel, or heavy DSP runs in the audio callback

 Feature extraction tolerates missing frames

 Feature extraction time is bounded and profiled

7. ML Inference Constraints

 ML inference is event-driven, not continuous

 Inference runs only on NoteEvents

 TensorFlow Lite model is loaded once at startup

 No model loading or graph building occurs at runtime

 Inference results are advisory, not authoritative

 ML inference never blocks audio or UI threads

8. UI / QML Latency Discipline

 UI reads immutable NoteEvent data only

 UI does not trigger DSP or analysis work

 Articulation confidence visualization is optional

 UI jitter smoothing uses small queues, not sleeps

 Debug overlays are disable-able at runtime

9. Logging & Dataset Capture

 Dataset logging is fully asynchronous

 Logging uses buffered writes or background flushing

 Logging can be disabled without recompilation

 Logging failure never affects audio or inference

 Logging timestamps align with NoteEvent timestamps

10. PipeWire / System Assumptions

 CPU governor set to performance

 USB autosuspend disabled

 User has RT scheduling permission

 No XRUNs under 64-frame buffer for ≥ 10 minutes

 Multi-channel hex inputs are mapped explicitly by name

11. Failure Behavior (Critical)

 XRUNs degrade gracefully (no crashes)

 ML backlog drops events instead of blocking

 UI remains responsive under load

 Audio monitoring remains uninterrupted

 System never prioritizes UI or ML over audio

12. Final Pass / Sanity Checks

 GuitarPi runs for ≥ 1 hour at 64 frames with no XRUNs

 Tab overlay timing feels consistent to a human player

 Articulation annotations remain stable under fast playing

 Disabling ML improves headroom as expected

 System behavior matches documented latency expectations