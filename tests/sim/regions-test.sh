#!/usr/bin/env bash
# Builds static-demand differential tests against an existing runtime fixture library.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
fixture_dir="$(realpath "${1:?supply a directory containing librhodium_sim.so}")"
cd "$repo_dir"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  tests/sim/regions_test.cpp rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp \
  -L"$fixture_dir" -lrhodium_sim -Wl,-rpath,"$fixture_dir" -o "$fixture_dir/regions-test"
"$fixture_dir/regions-test" "$fixture_dir/regions"
