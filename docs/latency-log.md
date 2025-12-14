# Round-trip Latency Measurements (Scarlett 2i2 @ 48 kHz)

Date: 2025-10-07
Hardware: Raspberry Pi 4 (8 GB), Focusrite Scarlett 2i2 (3rd Gen) via USB
Software path: PipeWire 1.4.2 (Pi OS Trixie), WirePlumber 0.4.x, JACK clients via `pw-jack`
Loopback: Line Out 1 → Line In 1 (¼″ TRS patch), INST/LINE switch set to LINE, direct monitor off

All measurements captured with `jack_iodelay` after ensuring strong loopback signal. PipeWire clock configured on-the-fly via:

```
pw-metadata -n settings 0 clock.force-rate 48000
pw-metadata -n settings 0 clock.force-quantum <frames>
```

| Buffer request (`PIPEWIRE_LATENCY`) | PipeWire reported latency (frames) | Typical round-trip (ms) | Notes |
| ----------------------------------- | ---------------------------------- | ----------------------- | ----- |
| 128/48000                           | capture/playback ≈ 192             | 20.8 – 21.3             | Stable test tone, consistent with JACK extra latency of ~584 frames. Matches initial run before forcing a smaller quantum. |
| 64/48000                            | capture/playback ≈ 96              | 17.2 (best) – 18.3      | Occasional minima down to 11.9 ms right after the quantum change, but settles near 17 ms without additional JACK backend offsets. |

### Observations
- PipeWire’s JACK bridge still reports higher-than-requested periods (192 frames at the 128-frame request) unless the clock quantum is forced down. Even then, there remains ~630 frames of device+bridge latency that `jack_iodelay` suggests compensating with offsets (`-I/-O ≈ 317`).
- Staying within the PipeWire graph, the Pi 4 comfortably handles 64-frame requests; xruns were not observed during the tests.
- Reaching the roadmap target (≈11–13 ms @ 128 / 48 kHz) will likely require running native JACK with two periods and the Scarlett’s internal latency offsets applied, or migrating this configuration to the Pi 5 where PipeWire’s USB scheduling has more headroom.

### Next steps
1. Persist the `clock.force-rate`/`clock.force-quantum` settings via a WirePlumber policy override so measurements survive reboots.
2. Capture an additional run with native JACK (`jackd -d alsa -p 128 -n 2 -r 48000`) to determine whether PipeWire bridging accounts for the extra ~7–8 ms.
3. Re-test on Raspberry Pi 5 hardware when available to validate the roadmap latency targets under the same methodology.
