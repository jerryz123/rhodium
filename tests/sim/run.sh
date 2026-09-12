#!/usr/bin/env bash
# Builds the C runtime and compares emitted models against independent oracles.
set -euo pipefail
mode="${1:-native}"
case "$mode" in native|--differential) ;; *) echo "usage: $0 [--differential]" >&2; exit 2 ;; esac
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="$(mktemp -d /tmp/rhodium-native-sim.XXXXXX)"
compiled_root="$(mktemp -d /tmp/rhodium-native-compiled.XXXXXX)"
trap 'rm -rf "$build_dir" "$compiled_root"' EXIT
cd "$repo_dir"
bash tests/sim/benchmark-single-test.sh
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/loader-trace-test.cpp -o "$build_dir/loader-trace-test"
"$build_dir/loader-trace-test"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/workload-host-test.cpp -o "$build_dir/workload-host-test"
"$build_dir/workload-host-test"
export RDS_TEST_DIR="$build_dir"
export PYTHONDONTWRITEBYTECODE=1
export RDS_OPTIMIZER="${RDS_OPTIMIZER:-$(bash rhodium/sim/compiler/run.sh --print-path)}"
env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  "${RACKET:-racket}" -y tests/sim/emit-fixtures.rhm
env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  "${RACKET:-racket}" -y tests/sim/emit-library.rhm
env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  "${RACKET:-racket}" -y tests/sim/emit-native-objects.rhm
env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  "${RACKET:-racket}" -y tests/sim/emit-replication.rhm
env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  "${RACKET:-racket}" -y tests/sim/emit-matcher.rhm
env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  "${RACKET:-racket}" -y tests/sim/emit-alu.rhm
env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  "${RACKET:-racket}" -y tests/sim/emit-contracts.rhm
env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  "${RACKET:-racket}" -y tests/sim/emit-tlb.rhm
runtime_sources=(rhodium/sim/runtime/*.c)
runtime_flags=()
if [[ "$("${CC:-cc}" -dumpmachine)" == x86_64*-linux* ]]; then
  runtime_sources+=(rhodium/sim/runtime/set-clear-x86_64.S)
  runtime_flags+=(-DRDS_HAVE_X86_64_ASM)
  export RDS_TEST_ASM=1
fi
"${CC:-cc}" -std=c17 -pthread -O2 -g -Wall -Wextra -Werror -fPIC -shared \
  ${RDS_SANITIZER_FLAGS:-} "${runtime_flags[@]}" "${runtime_sources[@]}" -ldl \
  -o "$build_dir/librhodium_sim.so"
python3 tests/sim/compiler_test.py "$build_dir"
python3 tests/sim/semantic_test.py "$build_dir"
python3 tests/sim/runtime_test.py "$build_dir"
python3 tests/sim/codegen_test.py "$build_dir"
python3 tests/sim/inspection_test.py "$build_dir"
bash tests/sim/bulk-cycles-test.sh "$build_dir"
bash tests/sim/fifo-width-test.sh "$build_dir"
bash tests/sim/fifo-derived-test.sh "$build_dir"
bash tests/sim/ring-selection-test.sh "$build_dir"
bash tests/sim/selector-columns-test.sh "$build_dir"
bash tests/sim/bit-relations-test.sh "$build_dir"
bash tests/sim/matcher-prefix-test.sh "$build_dir"
python3 tests/sim/matcher_test.py "$build_dir"
bash tests/sim/packed-matcher-test.sh "$build_dir"
bash tests/sim/idle-fifo-test.sh "$build_dir"
bash tests/sim/stationary-matcher-test.sh "$build_dir"
bash tests/sim/fifo-batch-test.sh "$build_dir"
bash tests/sim/contract-kernel-test.sh "$build_dir"
bash tests/sim/decoder-specialization-test.sh "$build_dir"
bash tests/sim/payload-pool-test.sh "$build_dir"
bash tests/sim/payload-lifetime-test.sh "$build_dir"
bash tests/sim/payload-exchange-test.sh "$build_dir"
bash tests/sim/regions-test.sh "$build_dir"
bash tests/sim/tlb-test.sh "$build_dir"
python3 tests/sim/alu_test.py "$build_dir"
python3 tests/sim/library_test.py "$build_dir"
python3 tests/sim/native_objects_test.py "$build_dir"
RDS_TEST_COMPILED=1 python3 tests/sim/native_objects_test.py "$build_dir"
if [[ "$mode" == --differential ]]; then
  bash tests/sim/differential.sh "$build_dir"
fi
