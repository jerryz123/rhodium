#!/usr/bin/env bash
# Checks stationary matcher queries through original and compiled engines without elaboration.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?supply an existing runtime build}")"
cd "$repo"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/stationary-matcher-test.cpp \
 rhodium/sim/compiler/{model,semantic,passes,regroup,flow}.cpp \
 -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$runtime/stationary-matcher-test"
"$runtime/stationary-matcher-test" "$runtime/stationary-matcher"
