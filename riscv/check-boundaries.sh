#!/usr/bin/env bash
# Enforces that the pure RISC-V model and ISA catalog remain independent of Rhodium.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

search_sources() {
  local pattern="$1"
  local file_glob="$2"
  shift 2
  if command -v rg >/dev/null 2>&1; then
    rg -n "$pattern" "$@" --glob "$file_glob"
  else
    find "$@" -type f -name "$file_glob" -exec grep -nHE "$pattern" {} +
  fi
}

forbidden_imports="$(search_sources '^[[:space:]]*(import[[:space:]]+)?(lib\()?"[^"]*(rhodium/|flow/|circt)' '*.rhm' riscv/model riscv/isa || true)"
if [[ -n "$forbidden_imports" ]]; then
  echo "pure RISC-V model and ISA modules must not import Rhodium, flow, or CIRCT" >&2
  echo "$forbidden_imports" >&2
  exit 1
fi

adapter_implementation_imports="$(search_sources '^[[:space:]]+.*rhodium/(core|frontend|backend)/' '*.rhdl' riscv/rtl || true)"
if [[ -n "$adapter_implementation_imports" ]]; then
  echo "the RISC-V/Rhodium adapter may import only public Rhodium language and library surfaces" >&2
  echo "$adapter_implementation_imports" >&2
  exit 1
fi

unexpected_rtl_sources="$(find riscv -type f -name '*.rhdl' ! -path 'riscv/rtl/*' -print)"
if [[ -n "$unexpected_rtl_sources" ]]; then
  echo "RISC-V hardware modules must be isolated under riscv/rtl/" >&2
  echo "$unexpected_rtl_sources" >&2
  exit 1
fi
