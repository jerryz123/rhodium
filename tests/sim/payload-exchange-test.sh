#!/usr/bin/env bash
# Builds reference and generated replay of exact-capacity routed payload exchange.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?supply a runtime build directory}")"
cd "$repo"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/payload-exchange-test.cpp \
 rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp \
 rhodium/sim/compiler/passes.cpp rhodium/sim/compiler/transport.cpp \
 -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$runtime/payload-exchange-test"
"$runtime/payload-exchange-test" "$runtime/payload-exchange" "${@:2}"
