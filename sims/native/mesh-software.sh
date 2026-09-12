#!/usr/bin/env bash
# Builds, verifies and benchmarks offer-snapshot NoC software against a retained native mesh.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
action="${1:?build, verify, measure, replay or sweep}"
build="$(realpath -m "${2:?software build directory}")"
fixture="$(realpath "${3:?native fixture directory}")"
mkdir -p "$build"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
if [[ "$action" == measure || "$action" == replay || "$action" == sweep ]]; then flock -x 9; else flock -s 9; fi
case "$action" in
build)
 runtime="${RDS_MESH_RUNTIME:-$fixture}"
 for variant in normal stress; do
  flags=(); suffix=""; if [[ $variant == stress ]]; then flags=(-DRDS_MESH_STRESS); suffix=-stress; fi
  clang++ -std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra -Werror -pthread "${flags[@]}" sims/native/mesh-software.cpp -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$build/mesh-software$suffix"
 done
 clang++ -std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra -Werror -pthread sims/native/mesh-replay.cpp -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$build/mesh-replay"
 clang++ -std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra -Werror sims/native/mesh-code-audit.cpp -L"$runtime" -lrhodium_sim -ldl -Wl,-rpath,"$runtime" -o "$build/code-audit"
 clang++ -std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra -Werror sims/native/mesh-software-report.cpp -o "$build/report"
 clang++ --version > "$build/compiler.txt"
 printf '%s\n' '-O3 -march=native -DNDEBUG; no PGO or LTO' >> "$build/compiler.txt"
 sha256sum "$build/mesh-software" "$build/mesh-software-stress" "$build/mesh-replay" "$build/code-audit" "$runtime/librhodium_sim.so" > "$build/artifacts.sha256"
 ;;
verify)
 "$build/mesh-software" verify "$fixture"
 "$build/mesh-software-stress" verify "$fixture"
 ;;
measure)
 workers="${5:?worker count}"; case $workers in 1|2|4|8) ;; *) exit 2 ;; esac
 cpus=0; if ((workers>1)); then cpus=0-$((workers-1)); fi
 taskset -c "$cpus" "$build/mesh-software" measure "$fixture" "${@:4}"
 ;;
replay)
 workers="${4:?worker count}"; case $workers in 1|2|4|8) ;; *) exit 2 ;; esac
 cpus=0; if ((workers>1)); then cpus=0-$((workers-1)); fi
 taskset -c "$cpus" "$build/mesh-replay" "$fixture" "${@:4}"
 ;;
sweep)
 cycles="${4:-500000}"
 traffic="${5:-2}"
 output="$build/sweep-$cycles-$traffic.jsonl"
 [[ ! -e "$output" ]] || { echo "refusing to overwrite $output" >&2; exit 2; }
 configs=()
 for workers in 1 2 4 8; do configs+=("native $workers row global 1"); done
 configs+=("oracle 1 row global 1")
 for mode in functional borrowed perf; do
  for workers in 1 2 4 8; do
   configs+=("$mode $workers row global 1" "$mode $workers tile global 1")
   if ((workers>1)); then configs+=("$mode $workers tile neighbors 1"); fi
  done
 done
 for mode in borrowed perf; do
  for workers in 1 2 4 8; do configs+=("$mode $workers tile neighbors 2"); done
 done
 for trial in 0 1 2; do
  for ((j=0;j<${#configs[@]};++j)); do
   index=$(((j+trial*17)%${#configs[@]}))
   read -r mode workers layout sync unroll <<< "${configs[index]}"
   cpus=0; if ((workers>1)); then cpus=0-$((workers-1)); fi
   echo "trial=$trial $mode workers=$workers $layout $sync unroll=$unroll" >&2
   taskset -c "$cpus" "$build/mesh-software" measure "$fixture" "$mode" "$workers" "$cycles" "$traffic" "$layout" "$sync" "$unroll" >> "$output"
  done
 done
 "$build/report" "$output" > "$build/sweep-$cycles-$traffic-report.json"
 ;;
*) exit 2 ;;
esac
