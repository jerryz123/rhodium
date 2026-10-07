#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
fixture="$(mktemp -d /tmp/rhodium-core-boundaries.XXXXXX)"
trap 'rm -rf "$fixture"' EXIT
cp -R "$repo_dir/cores" "$fixture/cores"
mkdir -p "$fixture/bin" "$fixture/cores/mmu/nested"
for tool in bash dirname find grep; do
  ln -s "$(command -v "$tool")" "$fixture/bin/$tool"
done
audit() {
  PATH="$fixture/bin" bash "$fixture/cores/check-boundaries.sh"
}
expect_failure() {
  local expected=$1
  if audit > "$fixture/output" 2>&1; then
    echo "core boundary audit unexpectedly succeeded" >&2
    exit 1
  fi
  grep -q "$expected" "$fixture/output"
}
check_shared_ownership() {
  audit
  for directory in cores cores/csr cores/mmu/nested cores/cache/chi; do
    local probe="$fixture/$directory/shared-probe.rhdl"
    printf '  lib("riscv/rtl/csr.rhdl") open\n' > "$probe"
    audit
    for core in rv5stage rv2wide spike; do
      printf '  lib("cores/%s/profile.rhm") open\n' "$core" > "$probe"
      expect_failure 'shared processor components must not import named-core'
      printf '  "../%s/profile.rhm" open\n' "$core" > "$probe"
      expect_failure 'shared processor components must not import named-core'
    done
    printf '  lib("sims/cosim/pass.rhm") open\n' > "$probe"
    expect_failure 'must not import simulators'
    rm "$probe"
  done
  # Named cores may consume the shared families; tests may integrate named cores.
  printf '  lib("cores/csr/file.rhdl") open\n  lib("cores/mmu/tlb.rhdl") open\n' > "$fixture/cores/rv2wide/shared-probe.rhdl"
  printf '  lib("cores/rv5stage/core.rhdl") open\n' > "$fixture/cores/csr/tests/shared-probe.rhdl"
  audit
}
check_shared_ownership
if command -v rg >/dev/null 2>&1; then
  ln -s "$(command -v rg)" "$fixture/bin/rg"
  check_shared_ownership
fi
echo "Core boundary audit regressions passed"
