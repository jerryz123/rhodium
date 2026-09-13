#!/usr/bin/env bash
# Builds and verifies the pinned Verilator used by local and CI simulations.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
version=5.038
archive_sha256=f8c03105224fa034095ba6c8a06443f61f6f59e1d72f76b718f89060e905a0d4
install_dir="$repo_dir/.tools/verilator"

verify_install() {
  local actual
  actual="$(env -u VERILATOR_ROOT "$install_dir/bin/verilator" --version)"
  if [[ "$actual" != "Verilator $version "* ]]; then
    echo "Expected Verilator $version, found: $actual" >&2
    exit 1
  fi
  test -f "$install_dir/share/verilator/include/verilated.h"
  echo "$actual ($install_dir)"
}

if [[ -x "$install_dir/bin/verilator" ]]; then
  verify_install
  exit 0
fi

build_dir="$(mktemp -d /tmp/rhodium-verilator.XXXXXX)"
trap 'rm -rf "$build_dir"' EXIT
curl --fail --location --retry 3 \
  "https://codeload.github.com/verilator/verilator/tar.gz/refs/tags/v$version" \
  --output "$build_dir/source.tar.gz"
if command -v sha256sum >/dev/null 2>&1; then
  actual_sha256="$(sha256sum "$build_dir/source.tar.gz" | awk '{print $1}')"
else
  actual_sha256="$(shasum -a 256 "$build_dir/source.tar.gz" | awk '{print $1}')"
fi
if [[ "$actual_sha256" != "$archive_sha256" ]]; then
  echo "Verilator archive checksum mismatch" >&2
  exit 1
fi
tar -xzf "$build_dir/source.tar.gz" -C "$build_dir"
cd "$build_dir/verilator-$version"
unset VERILATOR_ROOT
autoconf
./configure --prefix="$install_dir"
# The simulator needs binaries and headers, not generated manual pages.
make -j"${VERILATOR_BUILD_JOBS:-4}" VL_INST_MAN_FILES=
make install VL_INST_MAN_FILES=
verify_install
