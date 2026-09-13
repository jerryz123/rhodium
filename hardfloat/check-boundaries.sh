#!/usr/bin/env bash
# Enforces the public-authoring and generated-file boundaries of the HardFloat port.
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

search_sources() {
  local pattern="$1"
  shift
  if command -v rg >/dev/null 2>&1; then
    rg -n "$pattern" "$@" --glob '*.rhdl'
  else
    find "$@" -type f -name '*.rhdl' -exec grep -nHE "$pattern" {} +
  fi
}

forbidden_imports="$(search_sources \
  '^[[:space:]]+.*(rhodium/(core|analysis|frontend|backend|formal)|riscv/|cores/|sims/|tests/|examples/)' \
  hardfloat/rtl hardfloat/main.rhdl || true)"
if [[ -n "$forbidden_imports" ]]; then
  echo "HardFloat production code may import only public Rhodium libraries and its own package" >&2
  echo "$forbidden_imports" >&2
  exit 1
fi

compiled_directories="$(find hardfloat -type d -name compiled -print)"
if [[ -n "$compiled_directories" ]]; then
  echo "generated Racket bytecode must not live under hardfloat/" >&2
  echo "$compiled_directories" >&2
  exit 1
fi
