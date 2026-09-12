#!/usr/bin/env bash
# Builds verified interconnected payload models and runs isolated timing commands.
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
 export PLTCOMPILEDROOTS="$(mktemp -d /tmp/rhodium-payload-compiled.XXXXXX)"
 trap 'rm -rf "$PLTCOMPILEDROOTS"' EXIT
 export PLTCOLLECTS="$repo:" RDS_PAYLOAD_DIR="$directory"
 "${RACKET:-$repo/.tools/racket-9.2/bin/racket}" -y sims/native/emit-payload.rhm
 ;;
build)
 runtime="$(realpath "${RDS_PAYLOAD_RUNTIME:?set to a directory containing librhodium_sim.so}")"
 optimizer="$(rhodium/sim/compiler/run.sh --print-path)"
 for shape in 2x244 8x244 16x244 8x1024;do
  "$optimizer" --input "$directory/$shape-original.json" --release --output "$directory/$shape-original.rsim"
  "$optimizer" --input "$directory/$shape-native.json" --release --lift-contracts --output "$directory/$shape-native.rsim" --report "$directory/$shape-native-optimized.json"
  "$optimizer" --input "$directory/$shape-native.json" --release --payload-pool-size 31 --lift-contracts --output "$directory/$shape-pool.rsim" --report "$directory/$shape-pool-optimized.json"
 done
 "${CXX:-clang++}" -std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra -Werror sims/native/payload-software.cpp rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -ldl -o "$directory/payload-software"
 "$directory/payload-software" build "$directory"
 "$directory/payload-software" verify "$directory"
 sha256sum "$directory/payload-software" "$directory/"*.rsim "$directory/"*.c.so > "$directory/artifacts.sha256"
 ;;
verify) "$directory/payload-software" verify "$directory" ;;
measure) taskset -c "${RDS_PAYLOAD_CPU:-0}" "$directory/payload-software" measure "$directory" "${@:3}" ;;
*) printf 'unknown payload benchmark mode: %s\n' "$mode" >&2;exit 2 ;;
esac
