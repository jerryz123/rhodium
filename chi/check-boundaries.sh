#!/usr/bin/env bash
# Recursively audits CHI production imports and the pure CHI-to-NoC boundary.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

# Keep enumeration outside process substitution so failures propagate.
source_list="$(find chi -path chi/tests -prune -o -type f \
  \( -name '*.rhdl' -o -name '*.rhm' \) -print)"
sources=()
while IFS= read -r source; do
  [[ -z "$source" ]] || sources+=("$source")
done <<< "$source_list"
if [[ ${#sources[@]} -eq 0 ]]; then
  echo "CHI production source enumeration was empty" >&2
  exit 1
fi

search_imports() {
  local status=0
  if command -v rg >/dev/null 2>&1; then
    rg -n "$@" || status=$?
  else
    grep -nHE "$@" || status=$?
  fi
  if [[ "$status" -gt 1 ]]; then
    return "$status"
  fi
}

implementation_imports="$(search_imports \
  '^[[:space:]]+.*rhodium/(core|frontend|backend)/' "${sources[@]}")"
if [[ -n "$implementation_imports" ]]; then
  echo "CHI sources may import only public Rhodium language and standard-library surfaces" >&2
  echo "$implementation_imports" >&2
  exit 1
fi

pure_noc_imports="$(search_imports '^[[:space:]]+.*(rhodium/|circt)' \
  chi/noc/noc-authoring.rhm)"
if [[ -n "$pure_noc_imports" ]]; then
  echo "pure CHI-to-NoC compilation must not import Rhodium or CIRCT modules" >&2
  echo "$pure_noc_imports" >&2
  exit 1
fi

misplaced_sources="$(find rhodium/std -type f -path '*chi*' \
  \( -name '*.rhdl' -o -name '*.rhm' \) -print)"
if [[ -n "$misplaced_sources" ]]; then
  echo "CHI sources must remain outside rhodium/std" >&2
  echo "$misplaced_sources" >&2
  exit 1
fi
