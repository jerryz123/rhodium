#!/usr/bin/env bash
# Builds the independent C++ semantic specializer without frontend elaboration.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
compiler_dir="$repo_dir/rhodium/sim/compiler"
cxx="${CXX:-c++}"
flags=(-std=c++17 -O2 -Wall -Wextra -Werror)
key="$( { "$cxx" --version; "$cxx" -dumpmachine; declare -p flags; cat "$repo_dir/sims/native/specialize-empty.cpp" "$compiler_dir"/{model,semantic,passes}.cpp "$compiler_dir"/*.hpp; "$cxx" -std=c++17 -E -x c++ -include "$compiler_dir/model.hpp" /dev/null; } | sha256sum | cut -d ' ' -f 1)"
cache="${RDS_SPECIALIZER_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/rhodium-specializer}"
mkdir -p "$cache"
if [[ ! -x "$cache/$key" ]]; then
  temporary="$(mktemp "$cache/.build.XXXXXX")"
  trap 'rm -f "$temporary"' EXIT
  "$cxx" "${flags[@]}" "$repo_dir/sims/native/specialize-empty.cpp" "$compiler_dir"/{model,semantic,passes}.cpp -o "$temporary"
  chmod +x "$temporary"
  mv "$temporary" "$cache/$key"
fi
if [[ "${1:-}" == --print-path ]]; then printf '%s\n' "$cache/$key"; exit 0; fi
exec "$cache/$key" "$@"
