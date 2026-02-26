#!/usr/bin/env bash
# Fast native build script for Raspberry Pi
# Usage: ./build.sh  (or set your 'build' alias to call this)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
JOBS=$(nproc)

# Configure only if build.ninja is missing or CMakeLists.txt is newer
if [[ ! -f "$BUILD_DIR/build.ninja" || "$SCRIPT_DIR/CMakeLists.txt" -nt "$BUILD_DIR/build.ninja" ]]; then
    echo "Configuring..."
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-O3 -march=armv8-a -mtune=cortex-a76 -flto=auto" \
        -DCMAKE_EXE_LINKER_FLAGS="-flto=auto"
fi

echo "Building with $JOBS jobs..."
exec ninja -C "$BUILD_DIR" -j"$JOBS"
