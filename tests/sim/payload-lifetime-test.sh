#!/usr/bin/env bash
# Builds reference-versus-generated checks for ordered token lifetime lowering.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?supply a runtime build directory}")"
cd "$repo"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/payload-lifetime-test.cpp \
 rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp \
 rhodium/sim/compiler/passes.cpp rhodium/sim/compiler/lifetimes.cpp \
 -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$runtime/payload-lifetime-test"
"$runtime/payload-lifetime-test" "$runtime/payload-lifetime"
