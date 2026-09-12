#!/usr/bin/env bash
# Measures and verifies the complete eight-core bank-striped pointer-chase scaling matrix.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
root="$(realpath "${1:?build root containing harts-8}")"
rounds="${2:-2}"
repeats="${3:-1}"
[[ "$rounds" =~ ^[0-9]+$ && "$repeats" =~ ^[0-9]+$ ]] || exit 2
((rounds>=2 && rounds<=256 && repeats>=1)) || exit 2
out="$root/harts-8"
results="$root/chase-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$results"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
flock -s 9
"${CXX:-clang++}" -std=c++17 -O3 -march=native -DNDEBUG sims/native/vvadd-report.cpp -o "$results/report"
"${CXX:-clang++}" -std=c++17 -O3 -march=native -DNDEBUG sims/native/chase-report.cpp -o "$results/matrix"
flock -u 9
flock -x 9
artifacts=("$out/chase.bin" "$out/model.sv")
for workers in 1 2 4 8; do
  artifacts+=("$out/native-$workers/model.rsim" "$out/native-$workers/model.so" "$out/native-$workers/native-vvadd" "$out/native-$workers/flags" "$out/verilated-$workers/VSoCHarness")
done
sha256sum "${artifacts[@]}" > "$results/frozen.sha256"
git rev-parse HEAD > "$results/commit.txt"
sha256sum sims/native/{native-vvadd.cpp,verilator-vvadd.cpp,vvadd-loader.h,chase-workload.h,chase-benchmark.sh,chase-report.cpp,vvadd-report.cpp} sims/tests/programs/{chase.S,vvadd.ld} > "$results/sources.sha256"
files=()
cases=(native-1 verilator-1 native-2 verilator-2 native-4 verilator-4 native-8 verilator-8)
for ((trial=0;trial<repeats;trial++)); do
  for ((position=0;position<${#cases[@]};position++)); do
    item="${cases[(position+trial)%${#cases[@]}]}"
    engine="${item%-*}"; workers="${item##*-}"
    cpus=0; if ((workers>1)); then cpus="0-$((workers-1))"; fi
    file="$results/$item-$trial.json"
    if [[ "$engine" == native ]]; then
      env RDS_WORKLOAD=chase RDS_FLAGS="$(cat "$out/$item/flags")" RDS_COMPILED="$out/$item/model.so" \
        taskset -c "$cpus" "$out/$item/native-vvadd" "$out/$item/model.rsim" "$out/chase.bin" 8 "$workers" "$rounds" > "$file"
    else
      env RDS_WORKLOAD=chase taskset -c "$cpus" "$out/verilated-$workers/VSoCHarness" "$out/chase.bin" 8 "$rounds" > "$file"
    fi
    files+=("$file")
    "$results/report" "${files[@]}" > "$results/report.json"
    printf '%s trial %s complete\n' "$item" "$trial" >&2
  done
done
sha256sum -c "$results/frozen.sha256" > "$results/artifact-check.log"
sha256sum -c "$results/sources.sha256" > "$results/source-check.log"
"$results/matrix" "$results/report.json" > "$results/matrix.md"
printf '%s\n' "$results/matrix.md"
