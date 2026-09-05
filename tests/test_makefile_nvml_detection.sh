#!/bin/sh
set -eu

repo_dir=$(cd -- "$(dirname "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT

cat >"$tmp_dir/ldconfig" <<'EOF'
#!/bin/sh
cat <<'CACHE'
	libnvidia-ml.so.1 (libc6,x86-64) => /test/nvidia/libnvidia-ml.so.1
CACHE
EOF
chmod +x "$tmp_dir/ldconfig"

# The Make expression must reach make literally.
# shellcheck disable=SC2016
ldflags=$(make -C "$repo_dir" --no-print-directory -s \
    LDCONFIG="$tmp_dir/ldconfig" \
    --eval='print-nvml-ldflags:;@echo $(NVML_LDFLAGS)' print-nvml-ldflags)
[ "$ldflags" = "-L/test/nvidia" ]

# shellcheck disable=SC2016
override_flags=$(make -C "$repo_dir" --no-print-directory -s \
    LDCONFIG=/does/not/exist NVML_LIBDIR=/vendor/lib \
    --eval='print-nvml-override:;@echo $(NVML_LDFLAGS)' print-nvml-override)
[ "$override_flags" = "-L/vendor/lib" ]
