#!/usr/bin/env bash
# =============================================================================
# nvfd-fan-control.sh - Temperature-aware per-GPU fan mode switching
# =============================================================================

set -euo pipefail

NVFD="${NVFD:-$(command -v nvfd)}"
THRESHOLD_UP=45
THRESHOLD_DOWN=35
VERBOSE=false
LOCKFILE="/var/run/nvfd-fan-control.lock"

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Temperature-aware per-GPU fan mode switching for nvfd.

Options:
  -u, --threshold-up TEMP   Temperature to activate curve mode (default: 45)
  -d, --threshold-down TEMP # Temperature to activate auto mode (default: 35)
  -v, --verbose             Enable verbose logging
  -h, --help                Show this help message

Examples:
  $(basename "$0")                              # Use defaults (up: 45°C, down: 35°C)
  $(basename "$0") --threshold-up 50 -d 40      # Custom thresholds with hysteresis
  $(basename "$0") -u 50 -d 40 -v               # Verbose mode with custom thresholds

Hysteresis:
  - Switches to curve mode when temperature rises above --threshold-up
  - Switches to auto mode when temperature falls below --threshold-down
  - Between thresholds: keeps current mode (prevents thrashing)

EOF
  exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
  -u|--threshold-up)
    [[ -z "${2:-}" || "$2" == -* ]] && { echo "ERROR: --threshold-up requires a value" >&2; exit 1; }
    THRESHOLD_UP="$2"; shift 2
    ;;
  -d|--threshold-down)
    [[ -z "${2:-}" || "$2" == -* ]] && { echo "ERROR: --threshold-down requires a value" >&2; exit 1; }
    THRESHOLD_DOWN="$2"; shift 2
    ;;
  -v|--verbose) VERBOSE=true; shift ;;
  -h|--help) usage ;;
  *) echo "ERROR: Unknown option: $1" >&2; usage ;;
  esac
done

# Validate thresholds
[[ ! "$THRESHOLD_UP" =~ ^[0-9]+$ ]] && { echo "ERROR: Invalid threshold-up: $THRESHOLD_UP" >&2; exit 1; }
[[ ! "$THRESHOLD_DOWN" =~ ^[0-9]+$ ]] && { echo "ERROR: Invalid threshold-down: $THRESHOLD_DOWN" >&2; exit 1; }

# Check nvfd is available
[[ -z "$NVFD" || ! -x "$NVFD" ]] && { echo "ERROR: nvfd command not found" >&2; exit 1; }

# Check for root privileges 
[[ "$EUID" -ne 0 ]] && { echo "ERROR: This script must be run as root (use sudo)" >&2; exit 1; }

# Wait for nvfd service to be active (up to 30 seconds)
# Systemd handles restart rate limiting via StartLimitBurst/StartLimitIntervalSec
echo "[INFO] Waiting for nvfd service to be ready..."
for i in {1..30}; do
  systemctl is-active --quiet nvfd.service && break
  [[ $i -eq 30 ]] && { echo "[ERROR] nvfd service not ready after 30 seconds. Exiting." >&2; exit 1; }
  sleep 1
done
echo "[INFO] nvfd service is ready"

# Acquire file lock to prevent multiple instances
exec 200>"$LOCKFILE"
if ! flock -n 200; then
    echo "ERROR: Another instance of nvfd-fan-control is already running" >&2
    echo "       Remove $LOCKFILE if no instance is running" >&2
    exit 1
fi

# Initialize
declare -a GPU_MODES=()
declare -a GPU_NAMES=()
NUM_GPUS=$(nvidia-smi --list-gpus 2>/dev/null | wc -l)

[[ "$NUM_GPUS" -eq 0 ]] && { echo "ERROR: No NVIDIA GPUs detected!" >&2; exit 1; }

# Get GPU names
for i in $(seq 0 $((NUM_GPUS - 1))); do
  GPU_MODES+=("")
  GPU_NAMES[$i]=$(nvidia-smi --query-gpu=name --format=csv,noheader,nounits --id=$i)
done

echo "[INFO] Detected $NUM_GPUS GPU(s)"

# Graceful shutdown
cleanup() {
  trap - EXIT
  echo "[INFO] Shutting down, resetting all GPUs to auto mode..."
  for i in $(seq 0 $((NUM_GPUS - 1))); do
    "$NVFD" "$i" auto >/dev/null 2>&1 || true
  done
}
# EXIT as well as the signals: `set -e` aborts and unexpected errors must not
# leave the GPUs in curve mode with no supervisor. The lock file is deliberately
# not removed - the flock is released when this process exits, and unlinking the
# file lets the next instance lock a fresh inode while this one still holds it.
trap cleanup EXIT INT TERM

# Main loop
echo "[INFO] Fan control started (threshold-up: ${THRESHOLD_UP}°C, threshold-down: ${THRESHOLD_DOWN}°C)"

while true; do
  for i in $(seq 0 $((NUM_GPUS - 1))); do
    current_mode="${GPU_MODES[$i]:-unknown}"
    gpu_name="${GPU_NAMES[$i]}"
    
    # A driver reset or a transient hiccup makes this fail; under `set -e` that
    # would kill the supervisor and strand every GPU in whatever mode it is in.
    if ! temp=$(nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits --id="$i") \
       || [[ ! "$temp" =~ ^[0-9]+$ ]]; then
      echo "[WARN] GPU $i: temperature unavailable, skipping this poll" >&2
      continue
    fi

    [[ "$VERBOSE" == "true" ]] && echo "[INFO] GPU $i ($gpu_name): ${temp}°C | Mode: $current_mode"
    
    if [[ "$temp" -ge "$THRESHOLD_UP" ]]; then
      if [[ "$current_mode" != "curve" ]]; then
        if "$NVFD" "$i" curve >/dev/null 2>&1; then
          GPU_MODES[$i]="curve"
          [[ "$VERBOSE" == "true" ]] && echo "[INFO] GPU $i ($gpu_name) → curve mode (temp: ${temp}°C, threshold-up: ${THRESHOLD_UP}°C)"
        else
          echo "[ERROR] Failed to set GPU $i ($gpu_name) to curve mode" >&2
        fi
      fi
    elif [[ "$temp" -le "$THRESHOLD_DOWN" ]]; then
      if [[ "$current_mode" != "auto" ]]; then
        if "$NVFD" "$i" auto >/dev/null 2>&1; then
          GPU_MODES[$i]="auto"
          [[ "$VERBOSE" == "true" ]] && echo "[INFO] GPU $i ($gpu_name) → auto mode (temp: ${temp}°C, threshold-down: ${THRESHOLD_DOWN}°C)"
        else
          echo "[ERROR] Failed to set GPU $i ($gpu_name) to auto mode" >&2
        fi
      fi
    fi
    # If temp is between DOWN and UP, keep current mode (no action)
  done
  
  sleep 10
done
