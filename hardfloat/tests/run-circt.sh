#!/usr/bin/env bash
# Lowers and simulates the permanent combinational and iterative HardFloat fixtures through CIRCT and Verilator.
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
test_dir="$repo_dir/hardfloat/tests"
tmp_dir="$(mktemp -d /tmp/rhodium-hardfloat-circt.XXXXXX)"
trap 'rm -rf "$tmp_dir"' EXIT

circt_opt="${CIRCT_OPT:-$repo_dir/.tools/firtool-1.155.0/bin/circt-opt}"
if [[ ! -x "$circt_opt" ]]; then
  circt_opt="$(command -v circt-opt || true)"
fi
if [[ -z "$circt_opt" ]]; then
  echo "circt-opt not found; run 'make setup-circt' or set CIRCT_OPT" >&2
  exit 1
fi

lower_fixture() {
  local emitter="$1"
  local stem="$2"
  tools/run-racket.sh -S "$repo_dir" "$emitter" > "$tmp_dir/$stem.mlir"
  "$circt_opt" --canonicalize --cse \
    --lower-sim-to-sv --lower-verif-to-sv \
    --lower-seq-to-sv='disable-mem-randomization=true disable-reg-randomization=true' \
    --sv-mask-non-synthesizable='mode=ifdef macro=SYNTHESIS' \
    --prettify-verilog --export-verilog "$tmp_dir/$stem.mlir" \
    -o /dev/null > "$tmp_dir/$stem.sv"
}

cd "$repo_dir"
lower_fixture hardfloat/tests/emit-hardfloat.rhm hardfloat
lower_fixture hardfloat/tests/emit-numeric.rhm numeric
lower_fixture hardfloat/tests/emit-divide-sqrt.rhm divide-sqrt
lower_fixture hardfloat/tests/emit-divide-sqrt-f64.rhm divide-sqrt-f64

verilator --binary --timing --assert --build-jobs 0 \
  --top-module hardfloat_representation_tb \
  --Mdir "$tmp_dir/obj" \
  "$tmp_dir/hardfloat.sv" "$test_dir/verilator/representation_tb.sv" \
  > "$tmp_dir/verilator.log" 2>&1 || {
    cat "$tmp_dir/verilator.log" >&2
    exit 1
  }
"$tmp_dir/obj/Vhardfloat_representation_tb"

verilator --binary --timing --assert --build-jobs 0 \
  --top-module hardfloat_numeric_tb \
  --Mdir "$tmp_dir/numeric-obj" \
  "$tmp_dir/numeric.sv" "$test_dir/verilator/numeric_tb.sv" \
  > "$tmp_dir/numeric-verilator.log" 2>&1 || {
    cat "$tmp_dir/numeric-verilator.log" >&2
    exit 1
  }
"$tmp_dir/numeric-obj/Vhardfloat_numeric_tb"

verilator --binary --timing --assert --build-jobs 0 \
  --top-module hardfloat_divide_sqrt_tb \
  --Mdir "$tmp_dir/divide-sqrt-obj" \
  "$tmp_dir/divide-sqrt.sv" "$test_dir/verilator/divide_sqrt_tb.sv" \
  > "$tmp_dir/divide-sqrt-verilator.log" 2>&1 || {
    cat "$tmp_dir/divide-sqrt-verilator.log" >&2
    exit 1
  }
"$tmp_dir/divide-sqrt-obj/Vhardfloat_divide_sqrt_tb"

verilator --binary --timing --assert --build-jobs 0 \
  --top-module hardfloat_divide_sqrt_f64_tb \
  --Mdir "$tmp_dir/divide-sqrt-f64-obj" \
  "$tmp_dir/divide-sqrt-f64.sv" "$test_dir/verilator/divide_sqrt_f64_tb.sv" \
  > "$tmp_dir/divide-sqrt-f64-verilator.log" 2>&1 || {
    cat "$tmp_dir/divide-sqrt-f64-verilator.log" >&2
    exit 1
  }
"$tmp_dir/divide-sqrt-f64-obj/Vhardfloat_divide_sqrt_f64_tb"
