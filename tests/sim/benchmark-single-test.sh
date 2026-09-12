#!/usr/bin/env bash
# Builds and exercises the compiled benchmark gate without frontend elaboration.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
test_dir="$(mktemp -d /tmp/rhodium-benchmark-test.XXXXXX)"
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  "$repo_dir/tests/sim/benchmark_single_test.cpp" -o "$test_dir/test"
"$test_dir/test"
