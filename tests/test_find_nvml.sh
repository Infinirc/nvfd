#!/bin/sh
set -eu

repo_dir=$(cd -- "$(dirname "$0")/.." && pwd)
# shellcheck disable=SC1091
. "$repo_dir/scripts/find-nvml.sh"

tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT

cat >"$tmp_dir/ldconfig" <<'EOF'
#!/bin/sh
cat <<'CACHE'
	libnvidia-ml.so.1 (libc6,x86-64) => /driver/lib/libnvidia-ml.so.1
CACHE
EOF
chmod +x "$tmp_dir/ldconfig"

[ "$(NVML_LIBDIR=/vendor/lib find_nvml_libdir)" = "/vendor/lib" ]
[ "$(LDCONFIG="$tmp_dir/ldconfig" NVML_LIBDIR='' find_nvml_libdir)" = "/driver/lib" ]
