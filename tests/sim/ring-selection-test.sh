#!/usr/bin/env bash
# Builds focused generated-code coverage for direct FIFO ring-head selection.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?supply an existing runtime build}")"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
flock -s 9
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/ring-selection-test.cpp \
 rhodium/sim/compiler/{model,semantic}.cpp -L"$runtime" -lrhodium_sim \
 -Wl,-rpath,"$runtime" -o "$runtime/ring-selection-test"
"$runtime/ring-selection-test" "$runtime/ring-selection-cases"
