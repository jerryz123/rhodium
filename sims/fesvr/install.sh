#!/usr/bin/env bash
# Installs the pinned FESVR library and headers used by the simulation harness.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
spike_dir="$repo_dir/riscv/riscv-isa-sim"
patch_dir="$repo_dir/riscv/riscv-isa-sim-patches"
patched_submodule_tool="$repo_dir/riscv/patched_submodule.py"
python_command="${PYTHON:-python3}"

for tool in "$python_command" git make dtc; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "$tool is required to build FESVR" >&2
    exit 1
  fi
done

identity="$("$python_command" "$patched_submodule_tool" identity \
  --repository "$repo_dir" --submodule riscv/riscv-isa-sim --series "$patch_dir/series")"
install_dir="${FESVR_PREFIX:-$repo_dir/.tools/fesvr-$identity}"

if [[ -f "$install_dir/.complete" ]] && [[ "$(< "$install_dir/.complete")" == "$identity" ]]; then
  echo "FESVR $identity is already installed at $install_dir"
  exit 0
fi
if [[ -e "$install_dir" ]]; then
  echo "FESVR install path contains an incomplete or different build: $install_dir" >&2
  exit 1
fi

build_jobs="${FESVR_BUILD_JOBS:-4}"
work_dir="$(mktemp -d /tmp/rhodium-fesvr.XXXXXX)"
source_dir="$work_dir/source"
build_dir="$work_dir/build"
staged_install_dir="$work_dir/install"
trap 'rm -rf "$work_dir"' EXIT

git -C "$repo_dir" submodule update --init riscv/riscv-isa-sim
expected_revision="$(git -C "$repo_dir" ls-files --stage riscv/riscv-isa-sim | awk '{print $2}')"
actual_revision="$(git -C "$spike_dir" rev-parse HEAD)"
if [[ "$actual_revision" != "$expected_revision" ]]; then
  echo "riscv-isa-sim submodule is at $actual_revision, expected $expected_revision" >&2
  exit 1
fi
"$python_command" "$patched_submodule_tool" materialize \
  --source "$spike_dir" --series "$patch_dir/series" --output "$source_dir" \
  --git "$(command -v git)"

mkdir -p "$build_dir" "$staged_install_dir/lib/pkgconfig"
(
  cd "$build_dir"
  "$source_dir/configure" --prefix="$staged_install_dir" --with-boost=no
  make -j"$build_jobs" libfesvr.a
  make install-hdrs install-config-hdrs
  install -m 644 libfesvr.a "$staged_install_dir/lib/libfesvr.a"
  install -m 644 riscv-fesvr.pc "$staged_install_dir/lib/pkgconfig/riscv-fesvr.pc"
)
printf '%s\n' "$identity" > "$staged_install_dir/.complete"
mkdir -p "$(dirname "$install_dir")"
mv "$staged_install_dir" "$install_dir"

echo "Installed FESVR $identity at $install_dir"
