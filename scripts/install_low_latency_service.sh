#!/usr/bin/env bash
set -euo pipefail

# Install or update the systemd unit that applies GuitarPi low-latency tweaks at boot.
#
# Usage:
#   sudo ./scripts/install_low_latency_service.sh
#
# Optional environment variables:
#   INSTALL_PATH=/usr/local/sbin/guitarpi-lowlatency.sh
#   SERVICE_NAME=guitarpi-lowlatency
#
# To override the CPU used for USB IRQ affinity at boot, create
# /etc/default/<SERVICE_NAME> with a line such as:
#   TARGET_CPU=2
# Any variables exported there (e.g. DRY_RUN=1) will be passed to the script.

if [[ ${EUID} -ne 0 ]]; then
  echo "This installer must be run as root (use sudo)." >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_SCRIPT="${SCRIPT_DIR}/setup_low_latency.sh"

if [[ ! -x ${SOURCE_SCRIPT} ]]; then
  echo "Required script ${SOURCE_SCRIPT} is missing or not executable." >&2
  exit 1
fi

INSTALL_PATH="${INSTALL_PATH:-/usr/local/sbin/guitarpi-setup-lowlatency.sh}"
SERVICE_NAME="${SERVICE_NAME:-guitarpi-lowlatency}"
SERVICE_PATH="/etc/systemd/system/${SERVICE_NAME}.service"
ENV_PATH="/etc/default/${SERVICE_NAME}"

install -Dm0755 "${SOURCE_SCRIPT}" "${INSTALL_PATH}"

cat <<EOF > "${SERVICE_PATH}"
[Unit]
Description=GuitarPi Low-Latency Tweaks
After=multi-user.target

[Service]
Type=oneshot
EnvironmentFile=-${ENV_PATH}
ExecStart=${INSTALL_PATH}
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable --now "${SERVICE_NAME}.service"

cat <<EOF
Installed ${SERVICE_NAME}.service
  Script: ${INSTALL_PATH}
  Unit:   ${SERVICE_PATH}
  Env:    ${ENV_PATH} (optional)

The service runs once at boot and applies the low-latency tuning. To adjust the
CPU target or enable dry-run mode, edit ${ENV_PATH} and add lines such as:
  TARGET_CPU=2
  DRY_RUN=1

Reboot to confirm the service fires automatically, or test immediately with:
  sudo systemctl restart ${SERVICE_NAME}.service
EOF
