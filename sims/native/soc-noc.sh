#!/usr/bin/env bash
# Builds and validates the actual eight-hart CHI NoC against its observed whole SoC.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
action="${1:?build, verify or measure}"
directory="$(realpath -m "${2:?artifact directory}")"
mkdir -p "$directory"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
if [[ "$action" == measure ]]; then flock -x 9; else flock -s 9; fi
cc="${CC:-clang}"
cxx="${CXX:-clang++}"
flags=(-O3 -march=native -DNDEBUG)
case "$action" in
build)
 source="$(realpath "${3:?retained eight-hart extracted.json}")"
 mkdir -p "$directory/runtime"
 "$cc" -std=c17 "${flags[@]}" -Wall -Wextra -Werror -pthread -fPIC -shared rhodium/sim/runtime/*.c -ldl -o "$directory/runtime/librhodium_sim.so"
 "$cxx" -std=c++17 "${flags[@]}" -Wall -Wextra -Werror sims/native/soc-noc-extract.cpp -o "$directory/extract"
 "$directory/extract" "$source" "$directory/source.json" "$directory/manifest.json" "$directory/observed-soc.json"
 noc_options=()
 case "${RDS_NOC_BIT_RELATIONS:-0}" in
  0) ;; 1) noc_options+=(--canonicalize-bit-relations) ;;
  *) printf 'RDS_NOC_BIT_RELATIONS must be 0 or 1\n' >&2; exit 2 ;;
 esac
 printf '%s\n' "${RDS_NOC_BIT_RELATIONS:-0}" > "$directory/bit-relations"
 CXX="$cxx" bash rhodium/sim/compiler/run.sh --input "$directory/source.json" --release --specialize-decoders "${noc_options[@]}" --fold-idle-fifos --regroup-selector-columns --lift-contracts --reuse-matcher-grants --share-handshake-rows --optimize-contract-programs --output "$directory/model.rsim" --report "$directory/model.json" --timing "$directory/timing.json"
 CXX="$cxx" bash rhodium/sim/compiler/run.sh --input "$directory/observed-soc.json" --release --specialize-decoders --fold-idle-fifos --lift-contracts --output "$directory/observed-soc.rsim" --timing "$directory/observed-soc-timing.json"
 "$cc" -std=c17 "${flags[@]}" sims/native/compile-model.c -L"$directory/runtime" -lrhodium_sim -Wl,-rpath,"$directory/runtime" -o "$directory/compile-model"
 for workers in 1 8; do
  RDS_FLAGS=4092941424 RDS_WORKERS="$workers" RDS_PLAN_REPORT="$directory/w$workers.json" "$directory/compile-model" "$directory/model.rsim" "$directory/w$workers.c"
  "$cc" -std=c17 "${flags[@]}" -fPIC -shared "$directory/w$workers.c" -o "$directory/w$workers.so"
 done
 RDS_FLAGS=4290056208 RDS_WORKERS=1 RDS_PLAN_REPORT="$directory/observed-soc-plan.json" "$directory/compile-model" "$directory/observed-soc.rsim" "$directory/observed-soc.c"
 "$cc" -std=c17 "${flags[@]}" -fPIC -shared "$directory/observed-soc.c" -o "$directory/observed-soc.so"
 "$cxx" -std=c++17 "${flags[@]}" -Wall -Wextra -Werror sims/native/soc-noc-replay.cpp -L"$directory/runtime" -lrhodium_sim -Wl,-rpath,"$directory/runtime" -o "$directory/replay"
 "$cxx" -std=c++17 "${flags[@]}" -Wall -Wextra -Werror sims/native/soc-noc-trace.cpp -L"$directory/runtime" -lrhodium_sim -ldl -Wl,-rpath,"$directory/runtime" -o "$directory/trace"
 "$cxx" -std=c++17 "${flags[@]}" -Wall -Wextra -Werror sims/native/soc-noc-report.cpp -o "$directory/report"
 "$cc" --version > "$directory/compiler.txt"
 printf '%s\n' '-O3 -march=native -DNDEBUG; PGO/LTO disabled' >> "$directory/compiler.txt"
 sha256sum "$source" "$directory/manifest.json" "$directory/"*.rsim "$directory/"*.c "$directory/"*.so "$directory/runtime/librhodium_sim.so" "$directory/replay" > "$directory/artifacts.sha256"
 sha256sum sims/native/soc-noc* sims/native/vvadd-loader.h rhodium/sim/runtime/*.c rhodium/sim/runtime/*.h rhodium/sim/runtime/include/*.h rhodium/sim/compiler/*.cpp rhodium/sim/compiler/*.hpp > "$directory/sources.sha256"
 ;;
verify)
 program="$(realpath "${3:?eight-hart vvadd.bin}")"
 rounds="${4:-32}"
 for workers in 1 8; do
  trace=(); if ((workers==8)); then trace=("$directory/vvadd-$rounds.trace"); fi
  taskset -c 0-7 "$directory/replay" "$directory" "$program" "$directory/observed-soc.so" "$directory/w$workers.so" "$workers" "$rounds" "${trace[@]}" > "$directory/replay-$workers-$rounds.json"
 done
 sha256sum "$program" "$directory/vvadd-$rounds.trace" > "$directory/trace-$rounds.sha256"
 ;;
measure)
 traffic="$(realpath "${3:?recorded TRACE.bin}")"
 workers="${4:?worker count}"
 case "$workers" in 1) cpus=0 ;; 8) cpus=0-7 ;; *) exit 2 ;; esac
 taskset -c "$cpus" "$directory/trace" "$directory" "$traffic" "$workers" "${5:-bulk}"
 ;;
*) printf 'unknown action: %s\n' "$action" >&2; exit 2 ;;
esac
