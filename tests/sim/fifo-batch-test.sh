#!/usr/bin/env bash
# Builds focused FIFO batching replay with an existing runtime library and no elaboration.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
runtime="$(realpath "${1:?supply an existing runtime build}")"
cd "$repo"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/fifo-batch-test.cpp \
 rhodium/sim/compiler/{model,semantic}.cpp -L"$runtime" -lrhodium_sim -Wl,-rpath,"$runtime" -o "$runtime/fifo-batch-test"
"$runtime/fifo-batch-test" "$runtime/fifo-batch"
