#!/usr/bin/env bash
# Compares coherent MiniSoC execution across native workers and Verilator at O2.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="${NATIVE_BUILD_DIR:-/tmp/rhodium-native-soc/compare}"
compiled_root="$(mktemp -d /tmp/rhodium-native-compare-compiled.XXXXXX)"
trap 'rm -rf "$compiled_root"' EXIT
mkdir -p "$build_dir"
build_dir="$(cd "$build_dir" && pwd)"
cd "$repo_dir"
unset RDS_COMPILED
export PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir":
RDS_REPORT="$build_dir/mini" RDS_MODEL="$build_dir/mini.rsim" "${RACKET:-racket}" -y sims/native/emit-mini.rhm
"${RACKET:-racket}" -y sims/emit-soc-harness.rhm sims/native/mini-harness.rhdl > "$build_dir/mini.mlir"
"${CIRCT_OPT:-$repo_dir/.tools/firtool-1.155.0/bin/circt-opt}" --canonicalize --cse --prettify-verilog \
  --lower-seq-hlmem --lower-sim-to-sv --lower-verif-to-sv \
  --lower-seq-to-sv='disable-mem-randomization=true disable-reg-randomization=true' --lower-seq-firmem \
  --hw-memory-sim='disable-mem-randomization=true disable-reg-randomization=true read-enable-mode=undefined' \
  --sv-mask-non-synthesizable='mode=ifdef macro=SYNTHESIS' \
  --export-verilog "$build_dir/mini.mlir" -o /dev/null > "$build_dir/mini.sv"
"${CC:-cc}" -std=c17 ${RDS_NATIVE_CFLAGS:--O2} -g -pthread -Wall -Wextra -Werror ${RDS_SANITIZER_FLAGS:-} \
  sims/native/mini-smoke.c rhodium/sim/runtime/*.c -ldl -o "$build_dir/mini-smoke"
"${CC:-cc}" -std=c17 ${RDS_NATIVE_CFLAGS:--O2} -pthread -Wall -Wextra -Werror \
  sims/native/compile-model.c rhodium/sim/runtime/*.c -ldl -o "$build_dir/compile-model"
"${VERILATOR:-verilator}" --cc -O2 --assert --Wno-UNOPTFLAT --Wno-SYMRSVDWORD \
  --top-module SoCHarness --Mdir "$build_dir/verilated-mini" \
  --exe "$repo_dir/sims/native/verilator-smoke.cpp" -CFLAGS '-O2' \
  -MAKEFLAGS 'OPT_FAST=-O2 OPT_SLOW=-O2 OPT_GLOBAL=-O2' --build -j "${BUILD_JOBS:-2}" \
  "$build_dir/mini.sv" > "$build_dir/verilator-build.log" 2>&1 || {
    cat "$build_dir/verilator-build.log" >&2; exit 1;
  }
"$build_dir/verilated-mini/VSoCHarness" "${RDS_MAX_CYCLES:-200000}" > "$build_dir/verilator.trace"
RDS_FLAGS=1 RDS_WORKERS=1 "$build_dir/mini-smoke" "$build_dir/mini.original.rsim" "${RDS_MAX_CYCLES:-200000}" > "$build_dir/original.trace"
rg '^PASS:' "$build_dir/original.trace" > "$build_dir/original.result"
diff -u "$build_dir/verilator.trace" "$build_dir/original.result"
for workers in 1 2 4; do
  RDS_FLAGS=0 RDS_WORKERS="$workers" "$build_dir/mini-smoke" "$build_dir/mini.rsim" "${RDS_MAX_CYCLES:-200000}" > "$build_dir/native-$workers.trace"
  rg '^PASS:' "$build_dir/native-$workers.trace" > "$build_dir/native-$workers.result"
  diff -u "$build_dir/verilator.trace" "$build_dir/native-$workers.result"
  RDS_PLAN_REPORT="$build_dir/plan-$workers.json" RDS_FLAGS=0 RDS_WORKERS="$workers" "$build_dir/compile-model" "$build_dir/mini.rsim" "$build_dir/blocks-$workers.c"
  "${CC:-cc}" -std=c17 ${RDS_NATIVE_CFLAGS:--O2} -fPIC -shared ${RDS_SANITIZER_FLAGS:-} "$build_dir/blocks-$workers.c" -o "$build_dir/blocks-$workers.so"
  RDS_FLAGS=0 RDS_WORKERS="$workers" RDS_COMPILED="$build_dir/blocks-$workers.so" \
    "$build_dir/mini-smoke" "$build_dir/mini.rsim" "${RDS_MAX_CYCLES:-200000}" > "$build_dir/compiled-$workers.trace"
  rg '^PASS:' "$build_dir/compiled-$workers.trace" > "$build_dir/compiled-$workers.result"
  diff -u "$build_dir/verilator.trace" "$build_dir/compiled-$workers.result"
done
RDS_PLAN_REPORT="$build_dir/plan-replicated-6.json" RDS_FLAGS=576 RDS_WORKERS=6 \
  "$build_dir/compile-model" "$build_dir/mini.rsim" "$build_dir/blocks-replicated-6.c"
"${CC:-cc}" -std=c17 ${RDS_NATIVE_CFLAGS:--O2} -fPIC -shared ${RDS_SANITIZER_FLAGS:-} "$build_dir/blocks-replicated-6.c" -o "$build_dir/blocks-replicated-6.so"
RDS_FLAGS=576 RDS_WORKERS=6 RDS_COMPILED="$build_dir/blocks-replicated-6.so" \
  "$build_dir/mini-smoke" "$build_dir/mini.rsim" "${RDS_MAX_CYCLES:-200000}" > "$build_dir/compiled-replicated-6.trace"
rg '^PASS:' "$build_dir/compiled-replicated-6.trace" > "$build_dir/compiled-replicated-6.result"
diff -u "$build_dir/verilator.trace" "$build_dir/compiled-replicated-6.result"
python3 sims/native/inspect_ir.py "$build_dir/mini.original.json" "$build_dir/mini.optimized.json" \
  --plan "$build_dir/plan-replicated-6.json" --source "$build_dir/blocks-replicated-6.c" --output "$build_dir/ir-comparison.json"
cat "$build_dir/verilator.trace"
echo 'Original IR, scheduled/compiled 1/2/4 workers, and compiled replicated 6 workers match Verilator O2 transactions and cycles.'
