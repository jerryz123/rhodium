#!/usr/bin/env bash
# Builds cross-object snapshot-cache differential tests against an existing runtime.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
fixture_dir="$(realpath "${1:?supply a directory containing librhodium_sim.so}")"
cd "$repo_dir"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/flow_cache_test.cpp \
  rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp \
  -L"$fixture_dir" -lrhodium_sim -Wl,-rpath,"$fixture_dir" -o "$fixture_dir/flow-cache-test"
"$fixture_dir/flow-cache-test" "$fixture_dir/flow-cache"
