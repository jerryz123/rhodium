#!/usr/bin/env bash
# Builds payload-pool differential checks against an existing native runtime.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?supply a runtime build directory}")"
cd "$repo"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/payload-pool-test.cpp \
  rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp \
  rhodium/sim/compiler/passes.cpp rhodium/sim/compiler/payloads.cpp \
  -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$runtime/payload-pool-test"
"$runtime/payload-pool-test" "$runtime/payload-pool"
