#!/usr/bin/env bash
# Checks inductive FIFO payload widths against original wide-state execution.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?supply an existing runtime build}")"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
flock -s 9
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/fifo-width-test.cpp \
 rhodium/sim/compiler/{model,semantic,passes,fifo-width}.cpp \
 -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$runtime/fifo-width-test"
"$runtime/fifo-width-test" "$runtime/fifo-width-cases"
