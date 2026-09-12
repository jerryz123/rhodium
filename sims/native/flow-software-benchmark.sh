#!/usr/bin/env bash
# Rotates verified software/native flow microbenchmarks with identical host optimization flags.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
out="$(realpath -m "${1:?supply artifact directory}")"
runtime="$(realpath "${2:?supply matching O3 native runtime static library}")"
mkdir -p "$out"
cd "$repo_dir"
exec 9>/tmp/rhodium-single-worker-perf.lock
flock -s 9
"${CXX:-clang++}" -std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra -Werror \
    sims/native/flow-software.cpp rhodium/sim/compiler/{model,semantic,passes}.cpp \
    "$runtime" -pthread -ldl -o "$out/flow-software"
"${CXX:-clang++}" --version > "$out/compiler.txt"
"${CC:-clang}" --version >> "$out/compiler.txt"
printf '%s\n' '-O3 -march=native -DNDEBUG; PGO=off; LTO=off; one host thread' > "$out/flags.txt"
"$out/flow-software" build "$out/models" 2> "$out/build.log"
"$out/flow-software" verify "$out/models" 2> "$out/verify.log"
sha256sum "$out/flow-software" "$runtime" "$out/models/"*.{rsim,c,so} > "$out/frozen.sha256"
flock -u 9
flock -x 9
: > "$out/samples.jsonl"
variants=(api phases direct lifted software direct-inline lifted-inline software-inline)
for trial in 0 1 2; do
    for kind in 0 1 2 3; do
        for traffic in 0 1; do
            for offset in "${!variants[@]}"; do
                variant="${variants[$(((offset+trial)%${#variants[@]}))]}"
                taskset -c "${RDS_FLOW_CPU:-0}" "$out/flow-software" measure "$out/models" \
                    "$kind" "$traffic" "$variant" "${RDS_FLOW_REPEATS:-8192}" >> "$out/samples.jsonl"
            done
            "$out/flow-software" report "$out/samples.jsonl" > "$out/report.json"
            printf 'verified trial=%s kind=%s traffic=%s\n' "$trial" "$kind" "$traffic"
        done
    done
done
sha256sum --check "$out/frozen.sha256"
