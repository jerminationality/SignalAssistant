# GuitarPi — Implementation Roadmap (for GitHub Copilot)

Use this checklist alongside the scaffold to drive development. Each item maps to TODOs in the code files (see the TODO Breadcrumbs Patch) and to UI elements in QML.

## 1) Carla / JACK Graph
- [ ] Detect/attach to **JACK**; launch or attach to a **Carla** rack/session
- [ ] Instantiate default chain: **Gate → EQ → Amp → IR → Limiter**
- [ ] Wire ports: `system:capture_1/2` → Gate in; final plugin out → `system:playback_1/2`
- [ ] Expose graph start/stop; clean teardown without blocking RT threads
- [ ] (v2) Prepare for **6× hex inputs** and per-string subgraphs

## 2) Realtime Safety & Telemetry
- [ ] Ensure no dynamic allocation/locks in audio callback paths
- [ ] Emit `xrunsChanged(int)`; surface count on **HomePage**
- [ ] Compute latency text from **sample rate + buffer size**; bind to `LatencyBadge`
- [ ] Metering: emit `metersChanged(inL,inR,outL,outR)`; bind to `Meter` components

## 3) Preset System
- [ ] Define **preset schema** (YAML/JSON): chain layout, plugin IDs/params, IR file path
- [ ] Implement `savePreset(name)` and `loadPreset(name)` in `AppController`
- [ ] Implement `availablePresets()` by scanning a presets directory
- [ ] Version presets so future schema changes can migrate cleanly

## 4) IR Management
- [ ] Add a **file picker** for IR WAVs; update the IR plugin at runtime
- [ ] Persist last-used IR per preset; validate file presence at load
- [ ] Support short/long IR modes (latency vs realism)

## 5) Settings & Devices
- [ ] Persist **sample rate**, **buffer size**, and selected device
- [ ] Update latency display on change; reflect in audio engine
- [ ] (v2) Add **hex input mapping** UI (string → channel) and per-string trims

## 6) Recording & Re‑amp (MVP)
- [ ] **Record DI** (pre‑FX) to WAV while monitoring post‑FX bus
- [ ] Simple **re‑amp**: route DI file → chain → outputs; A/B with live input
- [ ] Manage take list; basic file housekeeping

## 7) UI/UX Polish
- [ ] Error toasts for missing plugins, graph failures, or device changes
- [ ] Keyboard shortcuts for nav; plan **MIDI footswitch** hook points (future)
- [ ] Improve **Rig** page to draggable nodes with parameter panels

## 8) Packaging
- [ ] Add cross‑compile notes / `toolchain.cmake` for Pi
- [ ] Provide a `.desktop` launcher and install rules

---

### References (where to implement)
- Graph & RT hooks → `src/audio/CarlaClient.*`
- Presets/Settings/Latency text → `src/AppController.*`
- Meters/Badges/Controls → `qml/pages/*` + `qml/components/*`

> Tip: Search for `TODO(Copilot)` in the scaffold to jump to the exact spots.
