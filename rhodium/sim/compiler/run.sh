#!/usr/bin/env bash
# Builds a content-addressed standalone optimizer outside the checkout, then executes it.
set -euo pipefail
compiler_dir="$(cd "$(dirname "$0")" && pwd)"
cxx="${CXX:-c++}"
compiler_flags=(-std=c++17 -O2 -Wall -Wextra -Werror)
# Hash compiler identity, installed header versions, flags, and all owned sources.
optimizer_key="$( { "$cxx" --version; "$cxx" -dumpmachine; declare -p compiler_flags; cat "$compiler_dir"/*.cpp "$compiler_dir"/*.hpp; "$cxx" -std=c++17 -E -x c++ -include "$compiler_dir/model.hpp" /dev/null; } | sha256sum | cut -d ' ' -f 1)"
optimizer_cache="${RDS_OPTIMIZER_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/rhodium-optimizer}"
mkdir -p "$optimizer_cache"
optimizer_binary="$optimizer_cache/$optimizer_key"
if [[ ! -x "$optimizer_binary" ]]; then
  optimizer_temp="$(mktemp "$optimizer_cache/.build.XXXXXX")"
  trap 'rm -f "$optimizer_temp"' EXIT
  "$cxx" "${compiler_flags[@]}" "$compiler_dir"/*.cpp -o "$optimizer_temp"
  chmod +x "$optimizer_temp"
  mv -f "$optimizer_temp" "$optimizer_binary"
fi
if [[ "${1:-}" == --print-path ]]; then
  printf '%s\n' "$optimizer_binary"
  exit 0
fi
exec "$optimizer_binary" "$@"
