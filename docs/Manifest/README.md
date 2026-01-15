# GuitarPi

Qt 6 + QML shell for the Raspberry Pi guitar processor project. The repository now contains real source files, a CMake project, and Pi-oriented build tooling so you can iterate on a Raspberry Pi 4 today and drop the binaries onto a Raspberry Pi 5 the moment it arrives.

## Project layout

```
.
├── CMakeLists.txt                # Qt 6 project definition
├── cmake/toolchain-rpi-aarch64.cmake
├── qml/                          # QML UI (Main.qml + pages/components)
├── scripts/install_deps_pi.sh    # Convenience installer for Pi packages
└── src/                          # C++ sources (AppController, CarlaClient stubs)
```

All feature work is tracked by `TODO(Copilot)` breadcrumbs that mirror the roadmap in `TASKS.md`.

## Quick start on Raspberry Pi 4 (arm64)

1. Flash **Raspberry Pi OS Bookworm 64-bit** (or Ubuntu 23.10+ arm64) to your Pi 4. Update firmware (`sudo rpi-update`) if you plan to reuse the SD later on Pi 5.
2. Clone this repository on the Pi and install dependencies:

   ```bash
   git clone https://github.com/your-org/guitarpi.git
   cd guitarpi
   chmod +x scripts/install_deps_pi.sh
   ./scripts/install_deps_pi.sh
   ```

3. Build the project (Qt 6 is provided by the packages above):

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ./build/GuitarPi
   ```

   Use `QT_QPA_PLATFORM=eglfs` for a fullscreen kiosk on the Pi’s HDMI output:

   ```bash
   QT_QPA_PLATFORM=eglfs ./build/GuitarPi
   ```

4. Optional: enable realtime audio tweaks on the Pi 4 so behaviour matches the future Pi 5 environment:
   - Set the CPU governor to `performance`: `sudo raspi-config nonint do_overclock 0` (or `echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`).
   - Add your user to the `audio` and `realtime` groups, configure `/etc/security/limits.d/audio.conf` for RT priority 95 / memlock unlimited.

Running the app on Pi 4 ensures the software stack, JACK/Carla integration, and presets are validated before the Pi 5 shows up. Every artifact produced this way will also run on Pi 5 because both are arm64.

## Preparing for Raspberry Pi 5

The Pi 5 introduces a faster Cortex-A76 CPU and requires the new `Bookworm` firmware stack. To make the transition painless:

- **Use 64-bit packages today.** Everything in `scripts/install_deps_pi.sh` pulls `aarch64` builds that the Pi 5 expects.
- **Adopt the `vc4-kms-v3d` driver** (`sudo raspi-config` → Advanced → GL Driver → `G2 GL (FKMS)` or `Full KMS`), which aligns with the Pi 5 display stack.
- **Validate JACK/Carla on PipeWire**: the script installs `pipewire-jack`; on Pi 5 you can keep the same setup and just bump buffer sizes if needed.
- **Keep firmware up to date**: when the Pi 5 arrives, copy the SD card over and run `sudo rpi-update` or upgrade packages so the new board boots cleanly.

## Cross-compiling from x86_64

You can build Pi 4/5 binaries on a faster desktop right now.

1. Extract a sysroot from your Pi 4 (or Pi 5 once it arrives):

   ```bash
   RSYNC_HOST=pi@raspberrypi.local
   SYSROOT=$PWD/sysroots/pi64
   mkdir -p "$SYSROOT"
   rsync -avz --delete --safe-links \
     $RSYNC_HOST:/lib $RSYNC_HOST:/usr $RSYNC_HOST:/opt/vc \
     "$SYSROOT"
   ```

2. Install a cross-toolchain on the host:

   ```bash
   sudo apt install g++-aarch64-linux-gnu qt6-base-dev qt6-declarative-dev ninja-build
   ```

3. Configure CMake with the provided toolchain file:

   ```bash
   cmake -S . -B build-rpi \
         -G Ninja \
         -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-rpi-aarch64.cmake \
         -DPI_SYSROOT=$SYSROOT \
         -DPI_TOOLCHAIN_PREFIX=aarch64-linux-gnu
   cmake --build build-rpi
   ```

4. Deploy to the Pi:

   ```bash
   rsync -av build-rpi/GuitarPi pi@raspberrypi.local:~/guitarpi/
   ```

Qt’s QML engine runs entirely on the Pi, so only the `GuitarPi` binary and `qml/` directory need to be copied. You can bundle them with `cpack` or a simple `tar` for now.

## Next steps

- Fill in `src/audio/CarlaClient.*` with the actual JACK + Carla session wiring (see `TASKS.md`).
- Persist latency/preset settings inside `AppController` (use `QSettings` on the Pi).
- Flesh out `qml/pages/*` with real data binding once signals/slots are implemented.
- Add packaging (systemd service + `.desktop`) after confirming the realtime audio pipeline.

Once these TODOs are complete, you’ll have feature parity on the Pi 4 and a ready-to-run build for the Pi 5 with zero extra work.
# SignalAssistant
