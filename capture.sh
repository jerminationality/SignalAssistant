#!/usr/bin/env bash
set -uo pipefail

# Resolve the repository root relative to this script so it works on other hosts as well.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

: > live-startup.log
echo "Captured logs will stream to live-startup.log"
./build/bin/GuitarPi "$@" 2>&1 | tee -a live-startup.log || true
exit_code=${PIPESTATUS[0]}
printf '\nCaptured log stored at %s/live-startup.log\n' "$PWD"
exit "$exit_code"
