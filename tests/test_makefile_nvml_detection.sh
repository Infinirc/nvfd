#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT

cat >"$tmp_dir/ldconfig" <<'EOF'
#!/bin/sh
cat <<'CACHE'
	libnvidia-ml.so.1 (libc6,x86-64) => /test/nvidia/libnvidia-ml.so.1
CACHE
EOF
chmod +x "$tmp_dir/ldconfig"

ldflags=$(make -C "$repo_dir" --no-print-directory -s \
    LDCONFIG="$tmp_dir/ldconfig" \
    --eval='print-nvml-ldflags:;@echo $(LDFLAGS)' print-nvml-ldflags)
[ "$ldflags" = "-L/test/nvidia" ]
