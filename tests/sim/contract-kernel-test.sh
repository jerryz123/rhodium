#!/usr/bin/env bash
# Builds whole-contract differential fixtures against an existing runtime library.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
fixture_dir="$(realpath "${1:?supply a directory containing librhodium_sim.so}")"
cd "$repo_dir"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  tests/sim/contract-kernel-test.cpp rhodium/sim/compiler/model.cpp \
  rhodium/sim/compiler/semantic.cpp rhodium/sim/compiler/passes.cpp \
  rhodium/sim/compiler/contracts.cpp rhodium/sim/compiler/regroup.cpp rhodium/sim/compiler/flow.cpp -L"$fixture_dir" -lrhodium_sim \
  -Wl,-rpath,"$fixture_dir" -o "$fixture_dir/contract-kernel-test"
"$fixture_dir/contract-kernel-test" "$fixture_dir/contracts"
