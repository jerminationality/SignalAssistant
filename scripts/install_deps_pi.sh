#!/usr/bin/env bash
set -euo pipefail

if [[ $(uname -m) != "aarch64" ]]; then
  echo "This script is meant to run directly on a 64-bit Raspberry Pi." >&2
  exit 1
fi

sudo apt update

REQUIRED_PACKAGES=(
  build-essential
  cmake
  ninja-build
  qt6-base-dev
  qt6-declarative-dev
  qml6-module-qtquick-controls
  libqt6quickcontrols2-6
  qt6-svg-dev
  jackd2
  pipewire-jack
  python3-yaml
  libsndfile1-dev
  rtkit
)

OPTIONAL_PACKAGES=(
  carla
  carla-bridge-lv2
  carla-bridge-win64
)

echo "Installing required packages: ${REQUIRED_PACKAGES[*]}"
sudo apt install -y "${REQUIRED_PACKAGES[@]}"

missing_optional=()

for pkg in "${OPTIONAL_PACKAGES[@]}"; do
  if ! apt-cache show "$pkg" >/dev/null 2>&1; then
    echo "Package $pkg not found in the current APT sources; skipping." >&2
    missing_optional+=("$pkg (not available)")
    continue
  fi

  if ! sudo apt install -y "$pkg"; then
    echo "Package $pkg failed to install; skipping." >&2
    missing_optional+=("$pkg (install failed)")
  fi
done

if (( ${#missing_optional[@]} > 0 )); then
  echo ""
  echo "The following optional packages could not be installed:"
  for pkg in "${missing_optional[@]}"; do
    echo "  - $pkg"
  done
  echo ""
  echo "Carla binaries are not currently available via default Raspberry Pi OS repositories." >&2
  echo "Consider installing Carla manually (e.g. from KXStudio builds or by compiling from source) if you need plugin hosting." >&2
fi

if systemctl list-unit-files | grep -q '^jackd\.service'; then
  sudo systemctl --now enable jackd.service
else
  echo "No jackd.service unit found; manage JACK manually (e.g. via qjackctl) if needed." >&2
fi

echo "Dependencies installed. Reboot recommended to ensure JACK/PipeWire services are ready."
