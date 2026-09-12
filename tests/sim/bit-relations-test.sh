#!/usr/bin/env bash
# Builds and runs focused decoder/packed-bit equivalence regressions without frontend elaboration.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?runtime directory}")"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
flock -s 9
"${CXX:-clang++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/bit-relations-test.cpp rhodium/sim/compiler/{model,semantic,passes,bit-relations}.cpp -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$runtime/bit-relations-test"
"$runtime/bit-relations-test" "$runtime/bit-relations-cases"
