#!/usr/bin/env bash
# Builds focused semantic coverage of guarded selector-column word regrouping.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?supply an existing runtime build}")"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
flock -s 9
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/selector-columns-test.cpp \
 rhodium/sim/compiler/{model,semantic,passes,selectors}.cpp \
 -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$runtime/selector-columns-test"
"$runtime/selector-columns-test" "$runtime/selector-columns-cases"
