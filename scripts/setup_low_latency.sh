#!/usr/bin/env bash
set -euo pipefail

# Apply Raspberry Pi audio low-latency tweaks:
# 1. Set the CPU frequency governor to 'performance' on every core.
# 2. Pin USB-related IRQs (xhci/dwc) to a dedicated CPU core (default: highest-numbered core).
#
# Usage:
#   sudo ./scripts/setup_low_latency.sh            # use last CPU core for USB interrupts
#   sudo TARGET_CPU=2 ./scripts/setup_low_latency.sh  # pin USB IRQs to CPU2 instead
#   sudo DRY_RUN=1 ./scripts/setup_low_latency.sh  # show actions without applying
#
# The script is idempotent and can be re-run safely. Changes are volatile and will reset on reboot
# unless you schedule this script via cron/systemd.

if [[ ${EUID} -ne 0 ]]; then
  echo "This script must be run as root (use sudo)." >&2
  exit 1
fi

log() {
  printf '==> %s\n' "$1"
}

warn() {
  printf 'WARNING: %s\n' "$1" >&2
}

readonly cpu_count=$(nproc)

if [[ ${cpu_count} -lt 1 ]]; then
  warn "Unable to determine CPU count."
  exit 1
fi

target_cpu=${TARGET_CPU:-$((cpu_count - 1))}

if ! [[ ${target_cpu} =~ ^[0-9]+$ ]] || (( target_cpu < 0 || target_cpu >= cpu_count )); then
  warn "TARGET_CPU must be an integer between 0 and $((cpu_count - 1))."
  exit 1
fi

if (( target_cpu >= 32 )); then
  warn "This script currently supports target CPUs below 32 due to affinity mask limitations."
  exit 1
fi

readonly dry_run=${DRY_RUN:-0}

# Compute hexadecimal affinity mask (little-endian bitmask of CPUs).
affinity_mask_hex=$(printf '%X' $((1 << target_cpu)))

log "CPU cores detected: ${cpu_count}"
log "Target CPU for USB IRQ affinity: CPU${target_cpu} (mask 0x${affinity_mask_hex})"

# Step 1: set CPU governor to performance (and raise min freq).
log "Setting CPU governor to 'performance' for all cores"
for cpu_dir in /sys/devices/system/cpu/cpu[0-9]*; do
  [[ -d ${cpu_dir} ]] || continue
  cpu=${cpu_dir##*cpu}
  governor_file="${cpu_dir}/cpufreq/scaling_governor"
  min_freq_file="${cpu_dir}/cpufreq/scaling_min_freq"
  max_freq_file="${cpu_dir}/cpufreq/cpuinfo_max_freq"

  if [[ ! -w ${governor_file} ]]; then
    warn "Governor file not writable for CPU${cpu}; skipping."
    continue
  fi

  if [[ ${dry_run} -eq 1 ]]; then
    log "[dry-run] Would set CPU${cpu} governor to performance"
  else
    echo performance > "${governor_file}" || warn "Failed to set governor for CPU${cpu}"
  fi

  if [[ -r ${max_freq_file} && -w ${min_freq_file} ]]; then
    max_freq=$(<"${max_freq_file}")
    if [[ -n ${max_freq} ]]; then
      if [[ ${dry_run} -eq 1 ]]; then
        log "[dry-run] Would set CPU${cpu} min frequency to ${max_freq}"
      else
        echo "${max_freq}" > "${min_freq_file}" || warn "Failed to raise min freq for CPU${cpu}"
      fi
    fi
  fi
done

# Step 2: Pin USB-related IRQs to the chosen CPU.
log "Configuring USB IRQ affinity"
irq_pattern='usb|xhci_hcd|dwc2|dwc_otg|pci\-dwc3'

mapfile -t usb_irqs < <(grep -iE "${irq_pattern}" /proc/interrupts | awk '{print $1}' | tr -d ':')

write_irq_affinity() {
  local irq="$1"
  local affinity_hex="$2"
  local affinity_file="/proc/irq/${irq}/smp_affinity"
  local affinity_list_file="/proc/irq/${irq}/smp_affinity_list"

  if [[ ${dry_run} -eq 1 ]]; then
    log "[dry-run] Would set IRQ ${irq} affinity to 0x${affinity_hex} (CPU list ${target_cpu})"
    return 0
  fi

  if [[ -w ${affinity_file} ]]; then
    if echo "${affinity_hex}" > "${affinity_file}" 2>/dev/null; then
      log "Pinned IRQ ${irq} to mask 0x${affinity_hex}"
      return 0
    fi
  fi

  if [[ -w ${affinity_list_file} ]]; then
    if echo "${target_cpu}" > "${affinity_list_file}" 2>/dev/null; then
      log "Pinned IRQ ${irq} via affinity list to CPU${target_cpu}"
      return 0
    fi
  fi

  warn "Failed to set affinity for IRQ ${irq}"
  return 1
}

if (( ${#usb_irqs[@]} == 0 )); then
  warn "No USB-related IRQs found via pattern '${irq_pattern}'."
else
  for irq in "${usb_irqs[@]}"; do
    write_irq_affinity "${irq}" "${affinity_mask_hex}" || true
  done
fi

log "Low-latency tweaks applied. Note: settings revert on reboot; schedule this script at boot for persistence."
