#!/usr/bin/env bash
# Checks request-prefix matcher grant reuse against original state transitions.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?supply an existing runtime build}")"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
flock -s 9
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/matcher-prefix-test.cpp \
 rhodium/sim/compiler/{model,semantic,passes,regroup,flow}.cpp \
 -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$runtime/matcher-prefix-test"
"$runtime/matcher-prefix-test" "$runtime/matcher-prefix-cases"
