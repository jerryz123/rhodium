#!/usr/bin/env bash
# Enforces RFPL's read-only dependency on the public Rhodium core IR.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

search_sources() {
  local pattern="$1"
  shift
  if command -v rg >/dev/null 2>&1; then
    rg -n "$pattern" "$@" --glob '*.rhm' --glob '*.rhdl'
  else
    find "$@" -type f \( -name '*.rhm' -o -name '*.rhdl' \) \
      -exec grep -nHE "$pattern" {} +
  fi
}

if search_sources '^[[:space:]]+"[^\"]*rfpl/' rhodium; then
  echo "Rhodium must not import RFPL" >&2
  exit 1
fi

if search_sources 'rhodium/frontend/' rfpl/frontend; then
  echo "RFPL annotation code must not import the Rhodium frontend" >&2
  exit 1
fi

unexpected_racket="$(find rfpl -type f -name '*.rkt' ! -path 'rfpl/main.rkt' -print)"
if [[ -n "$unexpected_racket" ]]; then
  echo "only the RFPL reader shim may use the .rkt extension" >&2
  echo "$unexpected_racket" >&2
  exit 1
fi

unexpected_rfpl="$(find . -path './.git' -prune -o -type f -name '*.rfpl' \
  ! -path './examples/rfpl/*' ! -path './rfpl/tests/*' -print)"
if [[ -n "$unexpected_rfpl" ]]; then
  echo ".rfpl files may appear only in RFPL examples and fixtures" >&2
  echo "$unexpected_rfpl" >&2
  exit 1
fi
