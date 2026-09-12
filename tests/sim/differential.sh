#!/usr/bin/env bash
# Compares native execution to CIRCT-generated Verilator code compiled explicitly at O2.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="${1:?pass the directory containing emitted simulator fixtures}"
circt_opt="${CIRCT_OPT:-$repo_dir/.tools/firtool-1.155.0/bin/circt-opt}"
verilator="${VERILATOR:-verilator}"
fixture="${RDS_BENCH_MODEL:-hierarchy}"
case "$fixture" in
  hierarchy) top=Hierarchy; cflags=-O2; default_cycles=1000000 ;;
  pipeline) top=Pipeline; cflags='-O2 -DRDS_PIPELINE'; default_cycles=20000 ;;
  *) echo "unknown RDS_BENCH_MODEL: $fixture" >&2; exit 2 ;;
esac
"$verilator" --version
"${CC:-cc}" --version | head -n 1
"$circt_opt" --lower-seq-to-sv='disable-mem-randomization=true disable-reg-randomization=true' \
  --export-verilog "$build_dir/$fixture.mlir" -o /dev/null > "$build_dir/$fixture.sv"
"${CC:-cc}" -std=c17 -pthread -O2 -Wall -Wextra -Werror \
  -I"$repo_dir/rhodium/sim/runtime/include" \
  "$repo_dir/tests/sim/native-driver.c" "$repo_dir"/rhodium/sim/runtime/*.c -ldl -o "$build_dir/native-driver"
"$verilator" --cc -O2 --top-module "$top" --Mdir "$build_dir/verilated-$fixture" \
  --exe "$repo_dir/tests/sim/verilator-driver.cpp" \
  -CFLAGS "$cflags" -MAKEFLAGS 'OPT_FAST=-O2 OPT_SLOW=-O2 OPT_GLOBAL=-O2' \
  --build -j 2 "$build_dir/$fixture.sv" > "$build_dir/verilator-build.log" 2>&1 || {
    cat "$build_dir/verilator-build.log" >&2
    exit 1
  }
python3 "$repo_dir/tests/sim/compare.py" "$build_dir" "${RDS_BENCH_CYCLES:-$default_cycles}" "$fixture"
