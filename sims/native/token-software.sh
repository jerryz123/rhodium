#!/usr/bin/env bash
# Builds original, lifetime-shared and software token chains with isolated timing.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
mode="${1:?expected emit, build, verify, or measure}"
directory="$(realpath -m "${2:?supply an artifact directory}")"
mkdir -p "$directory"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
if [[ $mode == measure ]];then flock -x 9;else flock -s 9;fi
case "$mode" in
emit)
 export PLTCOMPILEDROOTS="$(mktemp -d /tmp/rhodium-token-compiled.XXXXXX)"
 trap 'rm -rf "$PLTCOMPILEDROOTS"' EXIT
 export PLTCOLLECTS="$repo:" RDS_PAYLOAD_DIR="$directory"
 "${RACKET:-$repo/.tools/racket-9.2/bin/racket}" -y sims/native/emit-token.rhm
 ;;
build)
 runtime="$(realpath "${RDS_PAYLOAD_RUNTIME:?set to a directory containing librhodium_sim.so}")"
 optimizer="$(rhodium/sim/compiler/run.sh --print-path)"
 storage=(--share-payload-lifetimes)
 case "${RDS_TOKEN_STORAGE:-fifo}" in fifo) ;; sram) storage=(--payload-lifetime-sram) ;; counters) storage=(--payload-lifetime-packed-counters) ;; split) storage=(--payload-lifetime-split-control) ;; phase) storage=(--payload-lifetime-phase-index) ;; packed-phase) storage=(--payload-lifetime-phase-index --payload-lifetime-packed-counters) ;; *) echo 'RDS_TOKEN_STORAGE must be fifo, sram, counters, split, phase, or packed-phase' >&2;exit 2 ;; esac
 hash_flags=()
 case "${RDS_TOKEN_HASH:-serial}" in serial) ;; lanes) hash_flags=(-DRDS_PAYLOAD_LANE_HASH) ;; *) echo 'RDS_TOKEN_HASH must be serial or lanes' >&2;exit 2 ;; esac
 layout_flags=()
 case "${RDS_TOKEN_LAYOUT:-indirect}" in indirect) ;; static) layout_flags=(-DRDS_PAYLOAD_STATIC_STORAGE) ;; *) echo 'RDS_TOKEN_LAYOUT must be indirect or static' >&2;exit 2 ;; esac
 case "${RDS_TOKEN_DEFER_INPUTS:-0}" in 0) ;; 1) layout_flags+=(-DRDS_PAYLOAD_DEFER_INPUTS) ;; *) echo 'RDS_TOKEN_DEFER_INPUTS must be 0 or 1' >&2;exit 2 ;; esac
 case "${RDS_TOKEN_BORROW_INPUTS:-0}" in 0) ;; 1) layout_flags+=(-DRDS_PAYLOAD_BORROW_INPUTS) ;; *) echo 'RDS_TOKEN_BORROW_INPUTS must be 0 or 1' >&2;exit 2 ;; esac
 case "${RDS_TOKEN_TYPED_INPUTS:-0}" in 0) ;; 1) layout_flags+=(-DRDS_PAYLOAD_TYPED_INPUTS) ;; *) echo 'RDS_TOKEN_TYPED_INPUTS must be 0 or 1' >&2;exit 2 ;; esac
 case "${RDS_TOKEN_UNROLL_TWO:-0}" in 0) ;; 1) layout_flags+=(-DRDS_PAYLOAD_UNROLL_TWO) ;; *) echo 'RDS_TOKEN_UNROLL_TWO must be 0 or 1' >&2;exit 2 ;; esac
 case "${RDS_TOKEN_LOCAL_STATE:-0}" in 0) ;; 1)
  if [[ ${RDS_TOKEN_LAYOUT:-indirect} != static ]];then echo 'RDS_TOKEN_LOCAL_STATE requires RDS_TOKEN_LAYOUT=static' >&2;exit 2;fi
  layout_flags+=(-DRDS_PAYLOAD_LOCAL_STATE) ;; *) echo 'RDS_TOKEN_LOCAL_STATE must be 0 or 1' >&2;exit 2 ;; esac
 case "${RDS_TOKEN_BOUND_STATE:-0}" in 0) ;; 1)
  if [[ ${RDS_TOKEN_LAYOUT:-indirect} != static || ${RDS_TOKEN_LOCAL_STATE:-0} != 0 ]];then echo 'RDS_TOKEN_BOUND_STATE requires static layout and LOCAL_STATE=0' >&2;exit 2;fi
  layout_flags+=(-DRDS_PAYLOAD_BOUND_STATE) ;; *) echo 'RDS_TOKEN_BOUND_STATE must be 0 or 1' >&2;exit 2 ;; esac
 case "${RDS_TOKEN_PIPELINE:-0}" in 0) ;; 1) layout_flags+=(-DRDS_TOKEN_PIPELINE) ;; *) echo 'RDS_TOKEN_PIPELINE must be 0 or 1' >&2;exit 2 ;; esac
 for shape in 2x244 8x244 16x244 8x1024;do
  "$optimizer" --input "$directory/$shape-original.json" --release --output "$directory/$shape-original.rsim"
  "$optimizer" --input "$directory/$shape-native.json" --release --lift-contracts --output "$directory/$shape-native.rsim" --report "$directory/$shape-native-optimized.json"
  "$optimizer" --input "$directory/$shape-native.json" --release "${storage[@]}" --lift-contracts --output "$directory/$shape-lifetime.rsim" --report "$directory/$shape-lifetime-optimized.json"
 done
 "${CXX:-clang++}" -std=c++17 -O3 -march=native -DNDEBUG -DRDS_TOKEN_BENCHMARK "${hash_flags[@]}" "${layout_flags[@]}" -Wall -Wextra -Werror sims/native/payload-software.cpp rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -ldl -o "$directory/payload-software"
 printf '%s\n' "${RDS_TOKEN_STORAGE:-fifo}" > "$directory/storage-mode"
 printf '%s\n' "${RDS_TOKEN_HASH:-serial}" > "$directory/hash-mode"
 printf '%s\n' "${RDS_TOKEN_LAYOUT:-indirect}" > "$directory/layout-mode"
 printf '%s\n' "${RDS_TOKEN_DEFER_INPUTS:-0}" > "$directory/defer-inputs"
 printf '%s\n' "${RDS_TOKEN_BORROW_INPUTS:-0}" > "$directory/borrow-inputs"
 printf '%s\n' "${RDS_TOKEN_TYPED_INPUTS:-0}" > "$directory/typed-inputs"
 printf '%s\n' "${RDS_TOKEN_UNROLL_TWO:-0}" > "$directory/unroll-two"
 printf '%s\n' "${RDS_TOKEN_LOCAL_STATE:-0}" > "$directory/local-state"
 printf '%s\n' "${RDS_TOKEN_BOUND_STATE:-0}" > "$directory/bound-state"
 printf '%s\n' "${RDS_TOKEN_PIPELINE:-0}" > "$directory/fixed-pipeline"
 "$directory/payload-software" build "$directory"
 "$directory/payload-software" verify "$directory"
 sha256sum "$directory/payload-software" "$directory/"*.rsim "$directory/"*.c.so > "$directory/artifacts.sha256"
 ;;
verify) "$directory/payload-software" verify "$directory" ;;
measure) taskset -c "${RDS_PAYLOAD_CPU:-0}" "$directory/payload-software" measure "$directory" "${@:3}" ;;
*) printf 'unknown token benchmark mode: %s\n' "$mode" >&2;exit 2 ;;
esac
