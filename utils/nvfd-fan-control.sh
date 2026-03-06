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

# Acquire file lock to prevent multiple instances
exec 200>"$LOCKFILE"
if ! flock -n 200; then
    echo "ERROR: Another instance of nvfd-fan-control is already running" >&2
    echo "       Remove $LOCKFILE if no instance is running" >&2
    exit 1
fi

# Initialize
declare -a GPU_MODES=()
NUM_GPUS=$(nvidia-smi --list-gpus 2>/dev/null | wc -l)

[[ "$NUM_GPUS" -eq 0 ]] && { echo "ERROR: No NVIDIA GPUs detected!" >&2; exit 1; }

echo "[INFO] Detected $NUM_GPUS GPU(s)"
for i in $(seq 0 $((NUM_GPUS - 1))); do
  GPU_MODES+=("")
done

# Graceful shutdown
cleanup() {
  echo "[INFO] Shutting down, resetting all GPUs to auto mode..."
  for i in $(seq 0 $((NUM_GPUS - 1))); do
    "$NVFD" "$i" auto >/dev/null 2>&1 || true
  done
  rm -f "$LOCKFILE"
  exit 0
}
trap cleanup SIGINT SIGTERM

# Main loop
echo "[INFO] Fan control started (threshold-up: ${THRESHOLD_UP}°C, threshold-down: ${THRESHOLD_DOWN}°C)"

while true; do
  for i in $(seq 0 $((NUM_GPUS - 1))); do
    current_mode="${GPU_MODES[$i]:-unknown}"
    
    temp=$(nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits --id=$i)
    
    [[ "$VERBOSE" == "true" ]] && echo "[INFO] GPU $i: ${temp}°C | Mode: $current_mode"
    
    if [[ "$temp" -ge "$THRESHOLD_UP" ]]; then
      if [[ "$current_mode" != "curve" ]]; then
        if "$NVFD" "$i" curve >/dev/null 2>&1; then
          GPU_MODES[$i]="curve"
          [[ "$VERBOSE" == "true" ]] && echo "[INFO] GPU $i → curve mode (temp: ${temp}°C, threshold-up: ${THRESHOLD_UP}°C)"
        else
          echo "[ERROR] Failed to set GPU $i to curve mode" >&2
        fi
      fi
    elif [[ "$temp" -le "$THRESHOLD_DOWN" ]]; then
      if [[ "$current_mode" != "auto" ]]; then
        if "$NVFD" "$i" auto >/dev/null 2>&1; then
          GPU_MODES[$i]="auto"
          [[ "$VERBOSE" == "true" ]] && echo "[INFO] GPU $i → auto mode (temp: ${temp}°C, threshold-down: ${THRESHOLD_DOWN}°C)"
        else
          echo "[ERROR] Failed to set GPU $i to auto mode" >&2
        fi
      fi
    fi
    # If temp is between DOWN and UP, keep current mode (no action)
  done
  
  sleep 10
done