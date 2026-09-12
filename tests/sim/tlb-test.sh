#!/usr/bin/env bash
# Builds and runs native TLB differential replay against retained original-RTL fixtures.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
fixture_dir="$(realpath "${1:?supply an emitted TLB fixture directory}")"
cd "$repo_dir"
node tests/sim/tlb-host-fixture.cjs "$fixture_dir"
"${RDS_OPTIMIZER:-$repo_dir/rhodium/sim/compiler/run.sh}" --input "$fixture_dir/tlb-host.json" --output "$fixture_dir/tlb-host.rsim" --no-optimize
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/tlb_test.cpp \
  -L"$fixture_dir" -lrhodium_sim -Wl,-rpath,"$fixture_dir" -o "$fixture_dir/tlb-test"
"$fixture_dir/tlb-test" "$fixture_dir"
