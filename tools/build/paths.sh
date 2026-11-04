#!/usr/bin/env bash
# Loads stable compiler filename mappings into a shell argument array.
# SPDX-License-Identifier: Apache-2.0

compilation_path_flags() {
  local helper mappings flag
  helper="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/paths.py"
  mappings=$(python3 "$helper" "$@") || return
  path_flags=()
  while IFS= read -r flag; do path_flags+=("$flag"); done <<< "$mappings"
  # Upstream Make/CMake and Verilator consume a shell command fragment, not argv.
  printf -v path_flags_shell '%q ' "${path_flags[@]}"
}
