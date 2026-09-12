#!/usr/bin/env bash
# Compares constant-field decoder specialization with original and independent software execution.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?supply a runtime build directory}")"
cd "$repo"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/decoder-specialization-test.cpp \
 rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp rhodium/sim/compiler/passes.cpp \
 -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$runtime/decoder-specialization-test"
"$runtime/decoder-specialization-test" "$runtime/decoder-specialization"
