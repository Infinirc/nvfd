#!/bin/sh
set -eu

header=$(cd -- "$(dirname "$0")/.." && pwd)/include/nvml_api.h

grep -q '^typedef unsigned int nvmlFanControlPolicy_t;' "$header"
grep -q '^#define NVML_FAN_POLICY_TEMPERATURE_CONTINOUS_SW 0$' "$header"
grep -q '^#define NVML_FAN_POLICY_MANUAL 1$' "$header"
