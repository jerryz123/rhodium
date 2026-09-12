#!/usr/bin/env bash
# Runs focused batched-cycle regressions against an already built runtime.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
directory="$(realpath "${1:?runtime directory}")"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
flock -s 9
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/bulk-cycles-test.cpp rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp -L"$directory" -lrhodium_sim -ldl -Wl,-rpath,"$directory" -o "$directory/bulk-cycles-test"
timeout 120 "$directory/bulk-cycles-test" "$directory/bulk-tests"
