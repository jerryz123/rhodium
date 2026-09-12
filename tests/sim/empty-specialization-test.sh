#!/usr/bin/env bash
# Validates the external semantic specializer against an existing native runtime build.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="$(realpath "${1:?supply directory containing librhodium_sim.so}")"
specializer="$(realpath "${2:?supply the compiled specialize-empty executable}")"
cd "$repo_dir"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror tests/sim/empty_specialization_test.cpp \
  rhodium/sim/compiler/model.cpp rhodium/sim/compiler/semantic.cpp \
  -L"$build_dir" -lrhodium_sim -Wl,-rpath,"$build_dir" -o "$build_dir/empty-specialization-test"
if [[ "${3:-all}" != all ]]; then
 "$build_dir/empty-specialization-test" "$build_dir/${3}-specialization" "$specializer" "--$3"
 exit
fi
"$build_dir/empty-specialization-test" "$build_dir/empty-specialization" "$specializer"
"$build_dir/empty-specialization-test" "$build_dir/invalid-pipe-specialization" "$specializer" --pipe
"$build_dir/empty-specialization-test" "$build_dir/current-state-specialization" "$specializer" --state
"$build_dir/empty-specialization-test" "$build_dir/narrow-state-specialization" "$specializer" --state-narrow
"$build_dir/empty-specialization-test" "$build_dir/wide-state-specialization" "$specializer" --state-wide
"$build_dir/empty-specialization-test" "$build_dir/ancestor-specialization" "$specializer" --ancestors
"$build_dir/empty-specialization-test" "$build_dir/grant-specialization" "$specializer" --grants
"$build_dir/empty-specialization-test" "$build_dir/local-specialization" "$specializer" --local
