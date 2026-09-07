#!/usr/bin/env bash
# Installs ACT's isolated Python/Ruby dependencies and the checksum-pinned Sail reference model.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
act_dir="$repo_dir/riscv/riscv-arch-test"
venv_dir="${ACT_VENV:-$repo_dir/.tools/act-venv}"
sail_dir="$repo_dir/.tools/sail-0.13.1"
export BUNDLE_PATH="${ACT_BUNDLE_PATH:-$repo_dir/.tools/act-bundle}"
export BUNDLE_GEMFILE="$act_dir/framework/src/act/data/Gemfile"
export XDG_CACHE_HOME="$repo_dir/.tools/act-cache"
export XDG_DATA_HOME="$repo_dir/.tools/act-data"
python="${PYTHON:-python3}"
"$python" -c 'import sys; assert sys.version_info >= (3, 10), "ACT needs Python 3.10+; set PYTHON"'
command -v bundle >/dev/null || { echo 'ACT needs Ruby 3.2+ and Bundler on PATH' >&2; exit 1; }
git -C "$repo_dir" submodule update --init riscv/riscv-arch-test
temp_dir="$(mktemp -d /tmp/rhodium-act-setup.XXXXXX)"
trap 'rm -rf "$temp_dir"' EXIT
# UDB 0.1.16's dependency installer selects CPU but not OS and downloads ELF
# libz3.so on macOS. Preinstall the matching official native release locally.
if [[ "$(uname -s)-$(uname -m)" == Darwin-arm64 ]]; then
  z3_dir="$XDG_CACHE_HOME/udb/z3/z3-5.0.0/arm64"
  if [[ ! -f "$z3_dir/.native-5.0.0" || ! -f "$z3_dir/libz3.so" ]]; then
    curl --fail --location https://github.com/Z3Prover/z3/releases/download/z3-5.0.0/z3-5.0.0-arm64-osx-13.3.zip -o "$temp_dir/z3.zip"
    actual="$(shasum -a 256 "$temp_dir/z3.zip" | cut -d ' ' -f 1)"
    [[ "$actual" == 28b21e9e64b50c1f45535ae9c0e35e5a5f0d0770afd9a7df939edd88051c2bdc ]] || { echo 'Z3 archive checksum mismatch' >&2; exit 1; }
    unzip -q "$temp_dir/z3.zip" -d "$temp_dir"
    mkdir -p "$z3_dir"
    install -m 644 "$temp_dir/z3-5.0.0-arm64-osx-13.3/bin/libz3.dylib" "$z3_dir/libz3.so"
    touch "$z3_dir/.native-5.0.0"
  fi
fi
"$python" -m venv "$venv_dir"
"$venv_dir/bin/python" -m pip install -e "$act_dir/framework" -e "$act_dir/generators/testgen" -e "$act_dir/generators/coverage"
bundle check || bundle install

if [[ -x "$sail_dir/bin/sail_riscv_sim" ]] && [[ "$("$sail_dir/bin/sail_riscv_sim" --version)" == 0.13.1 ]]; then
  exit 0
fi
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) asset=Mac-arm64; digest=53d0c6fd84edd898728e7ba01c1575e66e5f17efd098847c5273690abbbd0737 ;;
  Linux-x86_64) asset=Linux-x86_64; digest=ee052f64494a2f5f071afd9c2cb4aa5eaae4ba84753e4f77e442b4f83f2e9469 ;;
  Linux-aarch64) asset=Linux-aarch64; digest=3cd33a323d6749aec4667e54f71d2bf8e8e6e220a4e4bafd9083440f9a7e55f0 ;;
  *) echo 'Install Sail 0.13.1 for this platform and set ACT_SAIL' >&2; exit 1 ;;
esac
curl --fail --location "https://github.com/riscv/sail-riscv/releases/download/0.13.1/sail-riscv-$asset.tar.gz" -o "$temp_dir/sail.tar.gz"
actual="$(shasum -a 256 "$temp_dir/sail.tar.gz" | cut -d ' ' -f 1)"
[[ "$actual" == "$digest" ]] || { echo 'Sail archive checksum mismatch' >&2; exit 1; }
mkdir -p "$sail_dir"
tar -xzf "$temp_dir/sail.tar.gz" -C "$sail_dir" --strip-components=1
"$sail_dir/bin/sail_riscv_sim" --version
