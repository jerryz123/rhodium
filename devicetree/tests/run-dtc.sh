#!/usr/bin/env bash
# Cross-checks native device-tree output with dtc, fdtdump, and fdtget.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
for tool in dtc fdtdump fdtget; do
  if ! command -v "$tool" >/dev/null; then
    echo "device-tree interoperability test requires $tool" >&2
    exit 1
  fi
done

fixture_dir="$(mktemp -d /tmp/rhodium-devicetree.XXXXXX)"
compiled_root="${PLTCOMPILEDROOTS:-}"
owns_compiled_root=false
if [[ -z "$compiled_root" ]]; then
  compiled_root="$(mktemp -d /tmp/rhodium-devicetree-compiled.XXXXXX)"
  owns_compiled_root=true
fi
cleanup() {
  rm -rf "$fixture_dir"
  if [[ "$owns_compiled_root" == true ]]; then
    rm -rf "$compiled_root"
  fi
}
trap cleanup EXIT

env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  racket -y "$repo_dir/devicetree/tests/write-fixture.rhm" "$fixture_dir"

dtc -I dtb -O dts -o "$fixture_dir/native-roundtrip.dts" "$fixture_dir/native.dtb"
dtc -I dts -O dtb -o "$fixture_dir/from-dts.dtb" "$fixture_dir/native.dts"
fdtdump "$fixture_dir/native.dtb" > "$fixture_dir/native.dump" 2>&1
cmp "$fixture_dir/native.dtb" "$fixture_dir/from-dts.dtb"

[[ "$(fdtget "$fixture_dir/native.dtb" / compatible)" == "rhodium,dtb-test" ]]
[[ "$(fdtget -t x "$fixture_dir/native.dtb" /memory@80000000 reg)" == "0 80000000 0 2000000" ]]
[[ "$(fdtget -t x "$fixture_dir/native.dtb" /interrupt-controller phandle)" == "1" ]]
[[ "$(fdtget -t x "$fixture_dir/native.dtb" /consumer interrupt-parent)" == "1" ]]
grep -Fq 'rhodium,dtb-test' "$fixture_dir/native.dump"
