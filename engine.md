================================================================================
AUDIO ENGINE - SIGNAL PROCESSING AUDIT
================================================================================
Date: 2026-02-26
Scope: Full path from hardware to NoteEvent, including all measurement points,
       detection logic, thresholds, and known design notes.

--------------------------------------------------------------------------------
1. HARDWARE / DRIVER LAYER
--------------------------------------------------------------------------------

Device:     Hexaphonic pickup -> ALSA hw:2,0
Server:     JACK (jackd), launched by HexJackClient if not already running
Command:    jackd -R -P70 -d alsa -d hw:2,0 -p128 -n3 -r48000 -s~
Sample rate: 48000 Hz
Buffer size: 128 frames per period (~2.7 ms)
Periods:    3 (total JACK latency ~8.1 ms before any processing)
Channels:   6 independent mono float streams (one per guitar string)

All audio in the system is normalized float PCM: range [-1.0, +1.0].
No integer conversion at any point. JACK delivers and consumes float buffers.


--------------------------------------------------------------------------------
2. JACK RT CALLBACK  [src/audio/HexJackClient.cpp :: processCallback()]
--------------------------------------------------------------------------------

Thread:     Core 3, SCHED_RR priority 90 (pinned once on first call)
Fires every: 128 frames = ~2.7 ms

Step 1 - Read raw buffers
  jack_port_get_buffer() x6 -> const float* channels[6]
  Each is 128 float samples of raw PCM from the pickup.

Step 2 - Apply calibration multipliers
  For each string s:
    calibratedSample[i] = rawSample[i] * multipliers[s]
  multipliers[s] is from CalibrationProfile, range [0.2, 8.0].
  Target post-calibration RMS = 0.25 (kTargetRms).
  If uncalibrated, multipliers default to 1.0.

Step 3 - Compute meters (for UI display only)
  RAW meters (m_rawMeters[]):
    RMS over 128 raw (pre-gain) samples.
    formula: sqrt( sum(x[i]^2) / N )
    No smoothing for strings 2-5.
    Strings 0 (Low E) and 1 (A) get EMA smoothing:
      level = prev * (1 - mix) + current * mix
      mix = 0.35 for Low E, 0.45 for A
    Rationale: fundamental frequencies near meter update rate cause flicker.

  CALIBRATED meters (m_detectionMeters[]):
    Same formula applied to the calibrated buffer.
    Same per-string EMA for Low E and A.
    These are what the UI bar meters display.
    Emitted every 40 ms via MeterPump -> hexMetersSnapshot signal.

  NOTE: These meters are display-only. They feed the QML bar graph, and are
  also used by the tuning mode to identify the loudest active string.
  They play no role in note detection decisions.

Step 4 - Forward calibrated audio to detection
  bridge->processLiveAudioBlock(channels, nframes, sampleRate)
  This passes the calibrated float buffers downstream.

Step 5 - Live monitor (optional)
  If live monitoring enabled: mix all 6 channels (sum / 6) * monitorGain
  -> stereo interleaved -> JackMonitorSink -> speaker playback

Step 6 - UDP telemetry
  6x float RMS snapshot sent to 127.0.0.1:5005 (~25 packets/sec)
  Overridable via GUITARPI_TELEMETRY_HOST env var.


--------------------------------------------------------------------------------
3. TAB ENGINE  [src/TabEngine.cpp :: processBlock()]
--------------------------------------------------------------------------------

Receives: calibrated float buffers, 128 frames, sr, timestamp t0

Step 1 - Per-string RMS for crosstalk masking
  For each string:
    RMS = sqrt( sum(sample^2) / N )  over the full 128-sample block
  This is a third independent RMS calculation (separate from meter RMS).
  Purpose: crosstalk detection only, not used for note thresholds.

Step 2 - Identify primary string
  primaryString = string with highest RMS amplitude this block.
  If max RMS < kMinGateLevel: all strings masked (silent).

Step 3 - Build per-string pass/suppress mask
  dynamicFloor = maxRMS * kCrosstalkThreshold
  For each string s:
    If s == primaryString:              mask[s] = true
    If RMS > dynamicFloor + hysteresis: mask[s] = true  (legitimate chord note)
    If RMS < dynamicFloor - hysteresis: mask[s] = false (crosstalk bleed)
    Otherwise:                          mask[s] = previous state (hysteresis band)

  EXCEPTION: A string with an active note is NEVER masked, even if it falls
  below the crosstalk floor. This ensures note-off detection still fires
  as the string decays.

Step 4 - Fan out to StringTrackers
  _trkPtrs[s]->processBlock(mask[s] ? channels[s] : nullptr, n, sr, t0)
  Masked strings receive nullptr -> treated as silence inside StringTracker.

Step 5 - Fuse events (articulation tagging)
  After all 6 StringTrackers have processed:
  fuseEvents() scans closed NoteEvents for adjacent notes.
  If gap between two notes on same string < 120 ms and fret delta >= 2:
    -> articulation = "slide"
  (Bend, hammer-on, pull-off tagging is noted as TODO in source.)


--------------------------------------------------------------------------------
4. STRING TRACKER  [src/StringTracker.cpp :: processBlock()]
--------------------------------------------------------------------------------

One instance per string. All state is per-string.

--- 4a. EARLY GATE ---

  channelPeak = max(|sample[i]|) over block
  If channelPeak < 1e-6: return immediately (absolute silence / muted string)

--- 4b. CONFIGURATION ---

  configureProcessing() called on every block.
  Reconfigures bandpass filter and aubio instances if:
  - sample rate changed
  - hop size changed
  - NoteDetectionStore parameter generation changed (user edited sliders)

  Aubio instances created:
    onset detector:  "specflux" algorithm
      FFT size:      hopSamples * 16 (= 2048 samples for 128-sample hop)
      hop size:      128 samples
    pitch detector:  "yinfast" algorithm
      FFT size:      2048
      hop size:      128 samples
    Both configured with per-string aubio silence thresholds:
      onsetSilenceDb = -117 + 90 * noiseGate   (range: -117 to -27 dBFS)
      pitchSilenceDb = -122 + 90 * noiseGate   (5 dB lower than onset)

--- 4c. BANDPASS FILTER ---

  BandpassFilter: cascaded 2nd-order Butterworth biquad (HP then LP)
  Configured per-string:
    String 0 (Low E):  HPF=70Hz,  LPF=400Hz
    String 1 (A):      HPF=90Hz,  LPF=400Hz
    String 2 (D):      HPF=120Hz, LPF=500Hz
    String 3 (G):      HPF=170Hz, LPF=1200Hz
    String 4 (B):      HPF=220Hz, LPF=1500Hz
    String 5 (High E): HPF=300Hz, LPF=1800Hz
  Filter is stateful (biquad delay lines) and applied sample-by-sample.
  Filtered output stored in _filteredScratch[].

--- 4d. FEATURE EXTRACTION (updateFeatures) ---

  For each hop frame (= 128 samples = one full block):

  envelopeRms:
    Computed from the FILTERED signal.
    formula: sqrt( sum(filtered[i]^2) / frameLen )
    This is the primary detection measurement value. All note threshold
    comparisons use this number.

  framePeak (filtered):
    max(|filtered[i]|) over the frame.
    Used only for normalizing aubio input buffers (see below).

  crestFactor:
    Computed from the RAW (unfiltered) signal.
    formula: rawPeak / rawRms
    High value (~10+) = tonal pluck transient.
    Low value (~1.4) = sustained broadband noise.
    NOTE: crest factor gate is currently DISABLED in detectOnset().
    Code shows it was in use but commented out pending tuning.

  Aubio onset input normalization:
    onsetGain = min(1.0, 0.35 / framePeak)
    Raw samples * onsetGain are fed to aubio onset detector.
    Normalizes so the peak of each frame hits ~0.35 before onset analysis.
    This makes onset strength values consistent regardless of input volume.

  Aubio pitch input normalization:
    pitchGain = min(1.0, 0.45 / framePeak)
    Filtered samples * pitchGain are fed to aubio pitch detector.

  onsetStrength:
    Output of aubio_onset_do() (specflux algorithm).
    Measures rate of change of the power spectrum.
    High on attack transients / plucks. Near-zero during sustain.
    Units: aubio internal dimensionless flux value (roughly 0.0 to 5.0).

  pitchHz:
    Output of aubio_pitch_do() (yinfast algorithm) in Hz.
    -1.0 if no pitch detected (below aubio silence threshold or unvoiced).

  pitchConfidence:
    Aubio YIN confidence [0.0 to 1.0].
    Not currently used directly in threshold logic; used in repitch decisions.

  Feature history:
    FrameFeatures pushed into _feat deque.
    History pruned to last ~800 ms (0.8 sec rolling window).

--- 4e. ADAPTIVE FLOOR ---

  _envAdaptiveRms: exponential moving average of envelopeRms.
  Updated only when no note is active (prevents note signal from raising floor).
    Rise alpha (kEnvRiseAlpha): 0.15
    Fall alpha (kEnvFallAlpha): 0.03 (slow decay)
  Minimum clamped to kEnvMin = 1e-5.
  NOTE: _envAdaptiveRms is maintained in state but is NOT currently used in
  any threshold calculation. It exists as infrastructure but is inactive.

--- 4f. ONSET LATCH ---

  _onsetLatched: prevents multiple onset triggers per attack transient.
  Set to true when onset is accepted.
  Released when onsetStrength drops below: onsetThreshold * 0.6

--- 4g. ONSET DETECTION (detectOnset) ---

  Evaluates the most recent FrameFeatures entry.
  All gates must pass for an onset to be accepted:

  Gate 1: onsetStrength > onsetThreshold
    onsetThreshold = cfg.onsetThreshold * kOnsetThreshold * attackSensitivity
    kOnsetThreshold = 0.10 (hardcoded)
    attackSensitivity = user slider [0.5, 3.0], default 1.0
    Effective default: 0.020 * 0.10 * 1.0 = 0.002 (very low; aubio output is ~0.1-3.0)
    NOTE: The onsetThreshold in TrackerConfig (0.020) is multiplied by the
    hardcoded kOnsetThreshold (0.10), yielding a very small absolute value.
    This may be an artifact of earlier parameter refactoring.

  Gate 2: _onsetLatched == false
    Prevents double-triggering on the same transient.

  Gate 3: envelopeRms >= noteOnThreshold
    noteOnThreshold = user slider, default 0.020 (calibrated RMS units)
    This ensures the string has actual signal energy, not just a noise spike.

  Gate 4 (DISABLED): crestFactor >= 2.0
    Would reject noise-like signals. Commented out, marked as needing tuning.

  Time guards (checked in order):
    Adaptive guard: if current time < _retriggerBlockUntilSec -> reject
      _retriggerBlockUntilSec is set on note-on, scaled by onset intensity
    Static separation guard: if timeSinceLastOnset < triggerGuardMs -> reject
      triggerGuardMs = user slider [5, 80 ms], default 45 ms
      Low strings (0) also have a hardcoded 220 ms extra guard for repluck debounce

  On acceptance:
    _onsetLatched = true
    Caller (processBlock) records _lastOnsetSec = frame.tSec
    Note-on event fired.

--- 4h. PITCH PROCESSING ---

  estimateMidi():
    pitchHz -> MIDI via 12 * log2(Hz / 440) + 69
    Result clamped to [openMidi, openMidi + 24] (string's 0-24 fret range)

  applyLowStringBias() (Low E string only):
    Corrects YIN harmonic locking on Low E (82 Hz fundamental, 164 Hz 2nd harmonic).
    If detected pitch is an integer multiple (2x, 3x, 4x) of open string:
      and the ratio error < 12% of harmonic number
      and envelopeRms >= noteOnThreshold * 0.65
      and onsetStrength >= onsetThreshold * 1.6
      -> divide pitchHz by harmonic, re-estimate MIDI as fundamental
    Only fires for fret 0 correction (candidateMidi == openMidi).

  updatePitchConfidence():
    Requires kPitchConfidenceFrames (3) consecutive frames:
      - All at same MIDI note (within kPitchConfidenceMaxCents = 28 cents)
      - Frequency drift < kPitchConfidenceHzFloor = 0.8 Hz
    After 3 stable frames: pitchStable = true
    Resets on any frame that doesn't match.

  applyPitchHold():
    Once pitch is stable, holds last confirmed MIDI for up to kPitchHoldFrames (3)
    frames without new confirmation.
    After kPitchHoldReleaseFrames (10) consecutive frames without stable pitch:
      held pitch released -> heldMidi = -1

  Median filter (_pitchMedianWindow):
    Window of recent pitch values; median selected to suppress outliers.

--- 4i. NOTE-ON ---

  Triggered when detectOnset() returns true.
  fret = heldMidi - stringMidi[s]
  If heldMidi not yet confirmed, fret may be -1 at note-on and updated later.
  velocity = clamp(envelopeRms * 12.0, 0.0, 1.0)
  NoteEvent opened: { stringIdx, fret, midi, startSec, endSec=startSec, velocity }
  _lastOnsetPeakRms = envelopeRms at onset
  _activeHoldUntilSec = startSec + kOpenBiasMinHoldSec (0.36 s)
    Note cannot be closed during this window regardless of envelope.
  _retriggerBlockUntilSec = startSec + guard duration
    guard is scaled: triggerGuardSec + (onset intensity factor * up to 40 ms extra)
  TabEngine::onNoteOn() called -> NoteEventCallback -> UI / logger

--- 4j. NOTE-OFF ---

  noteShouldClose() evaluated each frame while note is active.

  Minimum duration gate:
    age = current time - note.startSec
    If age < minNoteDurSec (default 0.045 s = 45 ms): cannot close.

  Active hold gate:
    If current time < _activeHoldUntilSec (0.36 s from onset): cannot close.

  sustainFloor = max(baselineFloor, noteOffThreshold)
    noteOffThreshold = noteOnThreshold * noteOffRatio
    noteOffRatio = user slider [0.1, 1.0], default 0.60
    Default: noteOffThreshold = 0.020 * 0.60 = 0.012

  avgEnv = mean of envelopeRms over last 5 frames (~50 ms lookback)

  Sharp-drop path (immediate close):
    If lastOnsetPeakRms > sustainFloor * 3.0
    AND avgEnv < lastOnsetPeakRms * 0.15
    AND avgEnv < sustainFloor
    -> close immediately (string was damped/muted)

  Sustained-quiet path:
    If avgEnv < sustainFloor: increment _releaseQuietFrames
    Else: reset to 0
    If _releaseQuietFrames >= kReleaseQuietFrameCount (8 frames = ~80 ms):
      -> close note

  On close: NoteEvent.endSec = frame.tSec
  TabEngine::onNoteOff() called -> NoteEventCallback -> UI / logger

--- 4k. REPITCH (hammer-on / pull-off detection) ---

  Evaluated each frame while a note is active.
  If heldMidi != activeMidi and the delta is [1, 12) semitones:
    and time since onset > triggerGuardSec
    and centsBetween(pitchHz, activeNoteHz) >= repitchThreshold * 100 cents
    -> increment _repitchStabilityCounter for that candidate MIDI

  Fires when:
    _repitchStabilityCounter >= repitchConfirmFrames (default 3)
    AND _repitchLastConfidence >= repitchMinConfidence (default 0.85)
    AND max onset seen during confirm window < onsetThreshold (onset was clean)
    AND envelopeRms >= noteOnThreshold (still sustaining)

  On fire:
    Close active NoteEvent
    Open new NoteEvent at heldMidi (inherits velocity from parent)
    Articulation not set here (set by fuseEvents() as "slide" if applicable)
    _lastRepitchSec = frame.tSec (guards against immediate re-fire)

  NOTE: Distinguishes repitch from repluck by requiring onset to be CLEAN
  (below threshold). A repluck would produce a new onset spike; a hammer-on
  would not.


--------------------------------------------------------------------------------
5. PARAMETER HIERARCHY (all units: calibrated RMS unless noted)
--------------------------------------------------------------------------------

baselineFloor = 0.0005 * 20^noiseGate
  noiseGate: user slider [0.0, 1.0]
  Range: 0.0005 (noiseGate=0) to ~0.01 (noiseGate=1)
  This is the hard noise floor. Signal below this is ignored everywhere.

noteOnThreshold: user slider [0.01, 0.35], default 0.020
  The minimum envelopeRms (filtered, bandpassed) for a note to open.

noteOffThreshold = noteOnThreshold * noteOffRatio
  noteOffRatio: user slider [0.1, 1.0], default 0.60
  Default: 0.012

onsetThreshold (effective) = cfg.onsetThreshold * kOnsetThreshold * attackSensitivity
  cfg.onsetThreshold = 0.020 (TrackerConfig)
  kOnsetThreshold = 0.10 (hardcoded)
  attackSensitivity = user slider [0.5, 3.0]
  Units: aubio specflux dimensionless output

triggerGuardMs: user slider [5, 80 ms], default 45 ms
  Minimum inter-onset time after note-on.

retriggerGateScale: hardcoded per string [1.4, 1.25, 1.10, 1.0, 1.0, 1.0]
  Scales the retriggerGate threshold (display only; not in active detection
  path after retrigger detection was moved into onset guard system).


--------------------------------------------------------------------------------
6. SIGNAL UNITS SUMMARY
--------------------------------------------------------------------------------

  Raw PCM:           float [-1.0, +1.0]  (JACK native)
  RMS (anywhere):    float [0.0, ~0.35]  (same scale as PCM, always positive)
  Calibrated RMS:    float targeting ~0.25 at reference loudness
  onsetStrength:     float ~[0.0, 5.0]  (aubio specflux, dimensionless)
  pitchHz:           float [60.0, 6000.0] Hz
  pitchConfidence:   float [0.0, 1.0]   (aubio YIN)
  noiseGate slider:  float [0.0, 1.0]   (normalized, nonlinear mapping)
  aubio silence:     float [-122, -27] dBFS


--------------------------------------------------------------------------------
7. KNOWN DESIGN NOTES / POTENTIAL REWORK POINTS
--------------------------------------------------------------------------------

1. Crest factor gate disabled.
   Code for gate 4 in detectOnset() is commented out with note "needs tuning".
   When enabled it would reject noise-like onsets (crestFactor < 2.0).
   Has not been active in recent builds.

2. _envAdaptiveRms maintained but unused.
   State is tracked per-frame but the value is never read for any threshold.
   Exists as leftover infrastructure from an earlier design.

3. onsetThreshold numeric chain may be confusing.
   TrackerConfig.onsetThreshold (0.020) * kOnsetThreshold (0.10) = 0.002
   Then compared against aubio output in the ~0.1-5.0 range.
   The layered multipliers make the effective value hard to reason about.
   onsetThreshold is stored in "note-on" units (RMS) but compared against
   onset flux (dimensionless). These are different physical quantities
   sharing a parameter name.

4. Retrigger threshold (retriggerGate) computed in detectOnset() for display
   but no longer used in actual detection. Retrigger is now handled purely
   by the trigger guard time window. The computation is kept only to populate
   the UI threshold display.

5. Low E harmonic bias fires only for fret 0 (open string).
   The correction is conservative: it only redirects to openMidi when that
   is lower than the detected MIDI. Any fretted note that happens to match
   a harmonic of a lower note is not corrected.

6. fuseEvents() articulation tagging is partial.
   Only "slide" is implemented (gap < 120 ms, fret delta >= 2).
   Bend, hammer-on, and pull-off are listed as TODO in the source.
   Repitch (hammer/pull during sustain) is detected in StringTracker but
   does not set articulation on the NoteEvent.

7. No overlap between JACK buffer and detection hop.
   Detection window = exactly one JACK period (128 samples).
   There is no overlap-add or multi-hop accumulation per detection step.
   This means the effective time resolution = buffer size (~2.7 ms).

================================================================================