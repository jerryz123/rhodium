#!/usr/bin/env bash
# Installs the pinned FESVR library and headers used by the simulation harness.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

revision="e1fa113cfb6d55d878a3c1ea3befa8d9c13ce154"
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
install_dir="$repo_dir/.tools/fesvr-$revision"

if [[ -f "$install_dir/.complete" ]]; then
  echo "FESVR $revision is already installed at $install_dir"
  exit 0
fi

for tool in git make dtc; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "$tool is required to build FESVR" >&2
    exit 1
  fi
done

build_jobs="${FESVR_BUILD_JOBS:-4}"
work_dir="$(mktemp -d /tmp/rhodium-fesvr.XXXXXX)"
source_dir="$work_dir/source"
build_dir="$work_dir/build"
trap 'rm -rf "$work_dir"' EXIT

git init --quiet "$source_dir"
git -C "$source_dir" remote add origin https://github.com/riscv-software-src/riscv-isa-sim.git
git -C "$source_dir" fetch --quiet --depth 1 origin "$revision"
git -C "$source_dir" checkout --quiet --detach FETCH_HEAD

mkdir -p "$build_dir" "$install_dir/lib/pkgconfig"
(
  cd "$build_dir"
  "$source_dir/configure" --prefix="$install_dir" --with-boost=no
  make -j"$build_jobs" libfesvr.a
  make install-hdrs install-config-hdrs
  install -m 644 libfesvr.a "$install_dir/lib/libfesvr.a"
  install -m 644 riscv-fesvr.pc "$install_dir/lib/pkgconfig/riscv-fesvr.pc"
)
touch "$install_dir/.complete"

echo "Installed FESVR $revision at $install_dir"
