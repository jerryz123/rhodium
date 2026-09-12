#!/usr/bin/env bash
# Runs rotated, exclusively locked vvadd trials with identical traces and explicit physical CPU placement.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
root="${1:?expected build root}"
harts="${2:-8}"
repeats="${3:-3}"
rounds="${4:-2048}"
out="$root/harts-$harts"
results="$out/benchmark-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$results"
exec 9>/tmp/rhodium-single-worker-perf.lock
flock -s 9
"${CXX:-clang++}" -O3 -march=native -DNDEBUG -std=c++17 "$repo/sims/native/vvadd-report.cpp" -o "$results/report"
flock -u 9
flock -x 9
files=()
for ((trial=0;trial<repeats;trial++)); do
    cases=(native-1 verilator-1 native-8 verilator-8)
    for ((position=0;position<4;position++)); do
        item="${cases[(position+trial)%4]}"
        engine="${item%-*}"; workers="${item##*-}"
        cpus="${RDS_VVADD_CPUS:-0-7}"
        if [[ "$workers" == 1 ]]; then cpus="${RDS_VVADD_SINGLE_CPU:-0}"; fi
        file="$results/$item-$trial.json"
        if [[ "$engine" == native ]]; then
            model="$out/$item/model.rsim"
            if [[ ! -s "$model" ]]; then model="$out/model.rsim"; fi
            RDS_FLAGS="$(cat "$out/$item/flags")" RDS_COMPILED="$out/$item/model.so" \
              taskset -c "$cpus" "$out/$item/native-vvadd" "$model" "$out/vvadd.bin" "$harts" "$workers" "$rounds" > "$file"
        else
            taskset -c "$cpus" "$out/verilated-$workers/VSoCHarness" "$out/vvadd.bin" "$harts" "$rounds" > "$file"
            "$results/report" --minimum-seconds "${RDS_VVADD_MIN_SECONDS:-60}" "$file"
        fi
        files+=("$file")
        printf '%s trial %s complete\n' "$item" "$trial" >&2
    done
done
"$results/report" "${files[@]}" > "$results/report.json"
printf '%s\n' "$results/report.json"
