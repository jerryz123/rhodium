#!/usr/bin/env bash
# Builds focused differential coverage of enqueue-time FIFO derived fields.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?supply an existing runtime build}")"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
flock -s 9
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/fifo-derived-test.cpp \
 rhodium/sim/compiler/{model,semantic,passes,fifo-derived}.cpp \
 -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$runtime/fifo-derived-test"
"$runtime/fifo-derived-test" "$runtime/fifo-derived-cases"
