#!/usr/bin/env bash
# Installs ACT's isolated Python/Ruby dependencies and the patched Sail reference model.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
act_dir="$repo_dir/sw/riscv-arch-test"
venv_dir="${ACT_VENV:-$repo_dir/.tools/act-venv}"
export BUNDLE_PATH="${ACT_BUNDLE_PATH:-$repo_dir/.tools/act-bundle}"
export BUNDLE_GEMFILE="$act_dir/framework/src/act/data/Gemfile"
export XDG_CACHE_HOME="$repo_dir/.tools/act-cache"
export XDG_DATA_HOME="$repo_dir/.tools/act-data"
python="${PYTHON:-python3}"
"$python" -c 'import sys; assert sys.version_info >= (3, 10), "ACT needs Python 3.10+; set PYTHON"'
command -v bundle >/dev/null || { echo 'ACT needs Ruby 3.2+ and Bundler on PATH' >&2; exit 1; }
git -C "$repo_dir" submodule update --init sw/riscv-arch-test
temp_dir="$(mktemp -d /tmp/rhodium-act-setup.XXXXXX)"
trap 'rm -rf "$temp_dir"' EXIT
# UDB 0.1.17's dependency installer selects CPU but not OS and downloads ELF
# libz3.so on macOS. Preinstall the matching official native release locally.
if [[ "$(uname -s)-$(uname -m)" == Darwin-arm64 ]]; then
  z3_dir="$XDG_CACHE_HOME/udb/z3/z3-5.1.0/arm64"
  if [[ ! -f "$z3_dir/.native-5.1.0" || ! -f "$z3_dir/libz3.so" ]]; then
    curl --fail --location https://github.com/Z3Prover/z3/releases/download/z3-5.1.0/z3-5.1.0-arm64-osx-13.3.zip -o "$temp_dir/z3.zip"
    actual="$(shasum -a 256 "$temp_dir/z3.zip" | cut -d ' ' -f 1)"
    [[ "$actual" == 81d29e934fd863079a74af35eecaeaef8047e0e12414d33ca322b358d68383db ]] || { echo 'Z3 archive checksum mismatch' >&2; exit 1; }
    unzip -q "$temp_dir/z3.zip" -d "$temp_dir"
    mkdir -p "$z3_dir"
    install -m 644 "$temp_dir/z3-5.1.0-arm64-osx-13.3/bin/libz3.dylib" "$z3_dir/libz3.so"
    touch "$z3_dir/.native-5.1.0"
  fi
fi
"$python" -m venv "$venv_dir"
"$venv_dir/bin/python" -m pip install -e "$act_dir/framework" -e "$act_dir/generators/testgen" -e "$act_dir/generators/coverage"
bundle check || bundle install

PYTHON="$python" bash "$repo_dir/sims/arch-test/install-sail.sh"
