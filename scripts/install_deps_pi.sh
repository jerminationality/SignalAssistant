#!/usr/bin/env bash
set -euo pipefail

if [[ $(uname -m) != "aarch64" ]]; then
  echo "This script is meant to run directly on a 64-bit Raspberry Pi." >&2
  exit 1
fi

sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  qt6-base-dev \
  qt6-declarative-dev \
  qt6-quickcontrols2-dev \
  qt6-svg-dev \
  jackd2 \
  carla \
  carla-bridge-lv2 \
  carla-bridge-win64 \
  pipewire-jack \
  python3-yaml \
  libsndfile1-dev

sudo systemctl --now enable jackd.service || true

echo "Dependencies installed. Reboot recommended to ensure JACK/Carla services are ready."
