#!/bin/sh

find_nvml_libdir() {
    if [ -n "${NVML_LIBDIR:-}" ]; then
        printf '%s\n' "$NVML_LIBDIR"
        return
    fi

    "${LDCONFIG:-/sbin/ldconfig}" -p 2>/dev/null |
        awk '/libnvidia-ml\.so\.1 \(libc6,/{print $NF; exit}' |
        xargs -r dirname
}
