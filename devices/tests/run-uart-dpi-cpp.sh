#!/usr/bin/env bash
# Compiles and runs the standalone UART DPI pseudo-terminal model test.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="$(mktemp -d /tmp/uart-dpi-cpp.XXXXXX)"
trap 'rm -rf "$build_dir"' EXIT

"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror \
  -I"$repo_dir/devices/dpi" \
  "$repo_dir/devices/dpi/uart_dpi.cc" \
  "$repo_dir/devices/tests/uart_dpi_test.cc" \
  -o "$build_dir/uart_dpi_test"
"$build_dir/uart_dpi_test"
