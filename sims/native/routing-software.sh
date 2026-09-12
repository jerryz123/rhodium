#!/usr/bin/env bash
# Builds and measures exact routing kernels from retained Rhodium graphs.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
mode="${1:?expected emit, build, verify, or measure}"
directory="${2:?supply an artifact directory}"
mkdir -p "$directory"
directory="$(realpath "$directory")"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
if [[ "$mode" == measure ]]; then flock -x 9; else flock -s 9; fi
case "$mode" in
emit)
    export PLTCOMPILEDROOTS="$(mktemp -d /tmp/rhodium-routing-compiled.XXXXXX)"
    trap 'rm -rf "$PLTCOMPILEDROOTS"' EXIT
    export PLTCOLLECTS="$repo:" RDS_ROUTING_DIR="$directory"
    "${RACKET:-$repo/.tools/racket-9.2/bin/racket}" -y sims/native/emit-routing.rhm
    ;;
build)
    runtime="$(realpath "${RDS_ROUTING_RUNTIME:?set to a directory containing librhodium_sim.so}")"
    optimizer="$(rhodium/sim/compiler/run.sh --print-path)"
    for shape in 1x1 3x2 7x6 16x8; do
        "$optimizer" --input "$directory/$shape-original.json" --release --output "$directory/$shape-original.rsim"
        "$optimizer" --input "$directory/$shape-native.json" --release --simplify-matcher-masks --lift-contracts \
          --output "$directory/$shape-native.rsim" --report "$directory/$shape-optimized.json"
    done
    "${CXX:-clang++}" -std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra -Werror \
      sims/native/routing-software.cpp rhodium/sim/compiler/model.cpp \
      rhodium/sim/compiler/semantic.cpp rhodium/sim/compiler/passes.cpp \
      -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -ldl -o "$directory/routing-software"
    "$directory/routing-software" build "$directory"
    printf '%s\n' '-O3 -march=native -DNDEBUG; PGO/LTO off; generated and software loops share one C translation unit' > "$directory/compiler-flags.txt"
    sha256sum "$directory/"*.rsim "$directory/"*-adapter.c.so "$directory/routing-software" > "$directory/artifacts.sha256"
    ;;
verify) "$directory/routing-software" verify "$directory" ;;
measure) taskset -c "${RDS_ROUTING_CPU:-0}" "$directory/routing-software" measure "$directory" "${@:3}" ;;
*) printf 'unknown routing-software mode: %s\n' "$mode" >&2; exit 2 ;;
esac
