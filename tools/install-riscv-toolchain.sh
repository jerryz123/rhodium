#!/usr/bin/env bash
# Installs the checksum-pinned Linux RV64 Newlib compiler used by all software suites.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
version=2026.08.27
digest=fe7dadf99dfaee59855b4be5f8d491dc66593bec295090e155a3ec51f0d14f56
install_dir="$repo_dir/.tools/riscv-toolchain"
[[ "$(uname -s)-$(uname -m)" == Linux-x86_64 ]] || {
  echo 'This CI toolchain requires x86-64 Linux; locally set RISCV_CC and ACT_OBJDUMP.' >&2
  exit 1
}
if [[ ! -f "$install_dir/.archive-sha256" ]] || [[ "$(<"$install_dir/.archive-sha256")" != "$digest" ]]; then
  # Never overlay an unrelated or partially installed toolchain.
  [[ ! -e "$install_dir" ]] || { echo "Remove or relocate the incomplete toolchain: $install_dir" >&2; exit 1; }
  temp_dir="$(mktemp -d /tmp/rhodium-riscv-toolchain.XXXXXX)"
  trap 'rm -rf "$temp_dir"' EXIT
  curl --fail --location --retry 3 \
    "https://github.com/riscv-collab/riscv-gnu-toolchain/releases/download/$version/riscv64-elf-ubuntu-24.04-gcc.tar.xz" \
    -o "$temp_dir/toolchain.tar.xz"
  echo "$digest  $temp_dir/toolchain.tar.xz" | sha256sum --check
  mkdir "$temp_dir/install"
  tar -xJf "$temp_dir/toolchain.tar.xz" -C "$temp_dir/install" --strip-components=1
  test -x "$temp_dir/install/bin/riscv64-unknown-elf-gcc"
  echo "$digest" > "$temp_dir/install/.archive-sha256"
  mkdir -p "$repo_dir/.tools"
  mv "$temp_dir/install" "$install_dir"
fi
cc="$install_dir/bin/riscv64-unknown-elf-gcc"
test "$("$cc" -dumpversion | cut -d. -f1)" -ge 15
"$cc" --version
"$install_dir/bin/riscv64-unknown-elf-objdump" --version
test -f "$("$cc" -march=rv64imafdc_zicsr_zifencei -mabi=lp64d -print-file-name=libm.a)"
