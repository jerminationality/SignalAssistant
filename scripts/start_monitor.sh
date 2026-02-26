#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# start_monitor.sh — Launch the SignalAssistant RMS Streamlit monitor
#
# Usage:
#   ./scripts/start_monitor.sh              # default: localhost, Core 0
#   PORT=8502 ./scripts/start_monitor.sh    # custom port
#
# On the Pi, pin to Core 0 (same as network IRQs) so that network
# interrupts don't bounce across cores towards the isolated audio core.
#
# The C++ engine must be started with GUITARPI_RMS_TELEMETRY=1.
# For remote monitoring set GUITARPI_TELEMETRY_HOST=<this-machine-ip>
# on the Pi side.
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

PORT="${PORT:-8501}"
MONITOR_SCRIPT="$ROOT_DIR/monitor.py"

if ! command -v streamlit &>/dev/null; then
    echo "Streamlit not found. Install it with:"
    echo "  pip install streamlit"
    exit 1
fi

echo "Starting RMS monitor on http://0.0.0.0:${PORT}"
echo "Open in browser: http://$(hostname -I | awk '{print $1}'):${PORT}"
echo ""
echo "Remember to launch GuitarPi with:"
echo "  GUITARPI_RMS_TELEMETRY=1 ~/SignalAssistant/bin/GuitarPi"
echo ""

# Pin to Core 0 if taskset is available (Linux only)
if command -v taskset &>/dev/null; then
    exec taskset -c 0 streamlit run "$MONITOR_SCRIPT" \
        --server.port "$PORT" \
        --server.address 0.0.0.0 \
        --server.headless true
else
    exec streamlit run "$MONITOR_SCRIPT" \
        --server.port "$PORT" \
        --server.address 0.0.0.0 \
        --server.headless true
fi
