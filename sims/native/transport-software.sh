#!/usr/bin/env bash
# Builds and measures real routed queues with exact-capacity payload handle exchange.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
mode="${1:?expected emit, build, verify, measure, or report}"
directory="${2:?supply artifact directory}"
mkdir -p "$directory"
directory="$(realpath "$directory")"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
if [[ "$mode" == measure ]]; then flock -x 9; else flock -s 9; fi
case "$mode" in
emit)
 export PLTCOMPILEDROOTS="$(mktemp -d /tmp/rhodium-transport-compiled.XXXXXX)"
 trap 'rm -r -- "$PLTCOMPILEDROOTS"' EXIT
 export PLTCOLLECTS="$repo:" RDS_TRANSPORT_DIR="$directory"
 "${RACKET:-$repo/.tools/racket-9.2/bin/racket}" -y sims/native/emit-transport.rhm
 ;;
build)
 runtime="$(realpath "${RDS_TRANSPORT_RUNTIME:?set to a directory containing librhodium_sim.so}")"
 optimizer="$(rhodium/sim/compiler/run.sh --print-path)"
 exchange_option=--exchange-payload-handles
 if [[ "${RDS_TRANSPORT_PACK_HANDLES:-0}" == 1 ]]; then exchange_option=--pack-payload-handles; fi
 if [[ "${RDS_TRANSPORT_FUSE_STATE:-0}" == 1 ]]; then exchange_option=--fuse-transport-state; fi
 if [[ "${RDS_TRANSPORT_BITMAP_SLOTS:-0}" == 1 ]]; then exchange_option=--bitmap-payload-slots; fi
 for width in 75 244 1024; do
  "$optimizer" --input "$directory/$width-original.json" --release --output "$directory/$width-original.rsim"
  "$optimizer" --input "$directory/$width-native.json" --release --output "$directory/$width-native.rsim" --report "$directory/$width-native-optimized.json"
  "$optimizer" --input "$directory/$width-native.json" --release "$exchange_option" --output "$directory/$width-exchange.rsim" --report "$directory/$width-exchange-optimized.json"
 done
 transport_defines=()
 if [[ "${RDS_TRANSPORT_FORCE_INLINE:-0}" == 1 ]]; then transport_defines+=(-DRDS_TRANSPORT_FORCE_INLINE); fi
 if [[ "${RDS_TRANSPORT_BIND_MEMORY:-0}" == 1 ]]; then transport_defines+=(-DRDS_TRANSPORT_BIND_MEMORY); fi
 if [[ "${RDS_TRANSPORT_TYPED_INPUTS:-0}" == 1 ]]; then transport_defines+=(-DRDS_TRANSPORT_TYPED_INPUTS); fi
 if [[ "${RDS_TRANSPORT_LOCAL_STATE:-0}" == 1 ]]; then transport_defines+=(-DRDS_TRANSPORT_LOCAL_STATE); fi
 "${CXX:-clang++}" -std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra -Werror "${transport_defines[@]}" sims/native/transport-software.cpp rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp rhodium/sim/compiler/passes.cpp -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -ldl -o "$directory/transport-software"
 "$directory/transport-software" build "$directory"
 printf '%s\n' '-O3 -march=native -DNDEBUG; PGO/LTO off; separate libraries, identical trace/checksum' > "$directory/compiler-flags.txt"
 printf 'forced-inline=%s\n' "${RDS_TRANSPORT_FORCE_INLINE:-0}" >> "$directory/compiler-flags.txt"
 printf 'bound-memory=%s\n' "${RDS_TRANSPORT_BIND_MEMORY:-0}" >> "$directory/compiler-flags.txt"
 printf 'packed-handles=%s\n' "${RDS_TRANSPORT_PACK_HANDLES:-0}" >> "$directory/compiler-flags.txt"
 printf 'fused-state=%s\n' "${RDS_TRANSPORT_FUSE_STATE:-0}" >> "$directory/compiler-flags.txt"
 printf 'bitmap-slots=%s\n' "${RDS_TRANSPORT_BITMAP_SLOTS:-0}" >> "$directory/compiler-flags.txt"
 printf 'typed-inputs=%s\n' "${RDS_TRANSPORT_TYPED_INPUTS:-0}" >> "$directory/compiler-flags.txt"
 printf 'local-state=%s\n' "${RDS_TRANSPORT_LOCAL_STATE:-0}" >> "$directory/compiler-flags.txt"
 sha256sum "$directory/"*.rsim "$directory/"*-adapter.c.so "$directory/transport-software" > "$directory/artifacts.sha256"
 ;;
verify) "$directory/transport-software" verify "$directory" ;;
measure) taskset -c "${RDS_TRANSPORT_CPU:-0}" "$directory/transport-software" measure "$directory" "${@:3}" ;;
report) "$directory/transport-software" report "${3:?supply JSONL measurements}" ;;
*) printf 'unknown mode: %s\n' "$mode" >&2; exit 2 ;;
esac
