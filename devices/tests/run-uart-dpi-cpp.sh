#!/usr/bin/env bash
# Compiles and checks UART DPI PTY transport, binary logs, and host I/O failures.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="$(mktemp -d /tmp/uart-dpi-cpp.XXXXXX)"
trap 'rm -rf "$build_dir"' EXIT
source "$repo_dir/tools/build/paths.sh"
compilation_path_flags --build "$build_dir"

"${CXX:-c++}" "${path_flags[@]}" -std=c++20 -Wall -Wextra -Werror \
  -I"$repo_dir/devices/uart/dpi" \
  "$repo_dir/devices/uart/dpi/uart_dpi.cc" \
  "$repo_dir/devices/tests/uart_dpi_test.cc" \
  -o "$build_dir/uart_dpi_test"
"$build_dir/uart_dpi_test"
