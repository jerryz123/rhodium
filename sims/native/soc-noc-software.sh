#!/usr/bin/env bash
# Builds and checks the direct software model of the retained eight-hart SoC NoC.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
action="${1:?build, verify or measure}"
out="$(realpath -m "${2:?artifact directory}")"
mkdir -p "$out"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
if [[ "$action" == measure ]]; then flock -x 9; else flock -s 9; fi
cc="${CC:-clang}"
cxx="${CXX:-clang++}"
flags=(-O3 -march=native -DNDEBUG)
case "$action" in
build)
 source="$(realpath "${3:?typed NoC source.json from soc-noc.sh}")"
 workers="${RDS_NOC_SOFTWARE_WORKERS:-1}"
 case "$workers" in 1|8) ;; *) printf 'software workers must be one or eight\n' >&2; exit 2 ;; esac
 printf '%s\n' "$workers" > "$out/workers"
 CXX="$cxx" bash rhodium/sim/compiler/run.sh --input "$source" --release --specialize-decoders --canonicalize-bit-relations --output "$out/base.rsim" --report "$out/base.json"
 for tool in export codegen report; do
  "$cxx" -std=c++17 "${flags[@]}" -Wall -Wextra -Werror "sims/native/soc-noc-software-$tool.cpp" -o "$out/$tool"
 done
 "$out/export" "$out/base.json" "$out/glue.json" > "$out/topology.json"
 "$out/codegen" "$out/topology.json" "$out/glue.json" "$workers" > "$out/software.c"
 "$cc" -std=c17 "${flags[@]}" -pthread -I"$repo/sims/native" -fPIC -shared "$out/software.c" -o "$out/software.so"
 "$cxx" -std=c++17 "${flags[@]}" -Wall -Wextra -Werror sims/native/soc-noc-software.cpp -ldl -o "$out/driver"
 "$cc" --version > "$out/compiler.txt"
 printf '%s\n' '-O3 -march=native -DNDEBUG; PGO/LTO disabled' >> "$out/compiler.txt"
 sha256sum "$source" "$out/base.json" "$out/glue.json" "$out/topology.json" "$out/software.c" "$out/software.so" "$out/driver" > "$out/artifacts.sha256"
 sha256sum sims/native/soc-noc-software* > "$out/sources.sha256"
 ;;
verify|measure)
 trace="$(realpath "${3:?actual-SoC recorded TRACE.bin}")"
 workers="$(cat "$out/workers")"
 case "$workers" in 1) cpus=0 ;; 8) cpus=0-7 ;; *) exit 2 ;; esac
 taskset -c "$cpus" "$out/driver" "$action" "$out/software.so" "$trace" "$workers"
 ;;
*) printf 'unknown action: %s\n' "$action" >&2; exit 2 ;;
esac
