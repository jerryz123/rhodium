#!/usr/bin/env bash
# Builds and validates the shared-service partition fixture without frontend elaboration.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
mode="${1:?build, verify or measure}"
directory="${2:?artifact directory}"
mkdir -p "$directory"
directory="$(realpath "$directory")"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
if [[ "$mode" == measure ]]; then flock -x 9; else flock -s 9; fi
case "$mode" in
build|build-noc|build-mesh)
 clang -std=c17 -O3 -march=native -DNDEBUG -Wall -Wextra -Werror -pthread -fPIC -shared rhodium/sim/runtime/*.c -ldl -o "$directory/librhodium_sim.so"
 clang++ -std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra -Werror sims/native/partition-star.cpp rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp rhodium/sim/compiler/passes.cpp rhodium/sim/compiler/regroup.cpp rhodium/sim/compiler/flow.cpp -L"$directory" -lrhodium_sim -Wl,-rpath,"$directory" -o "$directory/partition-star"
 "$directory/partition-star" "$mode" "$directory" "${3:-0}"
 clang --version > "$directory/compiler.txt"
 printf '%s\n' '-O3 -march=native -DNDEBUG; no PGO or LTO' >> "$directory/compiler.txt"
 sha256sum "$directory/model.rsim" "$directory/"*.so "$directory/partition-star" > "$directory/artifacts.sha256"
 ;;
verify) "$directory/partition-star" verify "$directory" ;;
measure)
 case "${4:?worker count}" in 1) cpus=0 ;; 2) cpus=0-1 ;; 4) cpus=0-3 ;; 8) cpus=0-7 ;; *) exit 2 ;; esac
 taskset -c "${RDS_PARTITION_CPUS:-$cpus}" "$directory/partition-star" measure "$directory" "${@:3}"
 ;;
*) exit 2 ;;
esac
