#!/usr/bin/env bash
# Builds focused C++ structural-provenance checks without frontend elaboration.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
fixture_dir="${1:?supply an output directory}"
mkdir -p "$fixture_dir"
fixture_dir="$(realpath "$fixture_dir")"
cd "$repo_dir"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/semantic_structure_test.cpp \
  rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp \
  rhodium/sim/compiler/passes.cpp rhodium/sim/compiler/regroup.cpp \
  -o "$fixture_dir/semantic-structure-test"
"$fixture_dir/semantic-structure-test" "$fixture_dir"
