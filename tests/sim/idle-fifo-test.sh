#!/usr/bin/env bash
# Replays inductively empty FIFOs and independent bypass queries without frontend elaboration.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?supply an existing runtime build}")"
cd "$repo"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/idle-fifo-test.cpp \
 rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp rhodium/sim/compiler/passes.cpp \
 -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$runtime/idle-fifo-test"
"$runtime/idle-fifo-test" "$runtime/idle-fifo"
