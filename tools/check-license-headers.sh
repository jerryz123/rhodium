#!/usr/bin/env bash
# Verifies SPDX identifiers on every tracked source, test, script, configuration, and documentation file.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(git rev-parse --show-toplevel)"
status=0
source=worktree

case "${1:-}" in
  "") ;;
  --cached) source=index ;;
  *)
    printf 'usage: %s [--cached]\n' "$0" >&2
    exit 2
    ;;
esac

list_paths() {
  if [[ "$source" == index ]]; then
    git -C "$repo_dir" diff --cached --name-only --diff-filter=ACMR -z
  else
    git -C "$repo_dir" ls-files -z
  fi
}

while IFS= read -r -d '' path; do
  case "$path" in
    LICENSE|NOTICE|DCO|hardfloat/LICENSE.md|riscv/riscv-arch-test|riscv/riscv-isa-tests|vlsi/double_wide_openframe)
      continue
      ;;
  esac

  case "$path" in
    *.rhm|*.rhdl|*.rkt|*.rktd|*.rfpl|*.invalid|*.c|*.cc|*.cpp|*.h|*.hpp|*.sv|*.svh|*.v|*.py|*.sh|*.S|*.ld|*.tcl|*.sql|*.el|*.md|*.mlir|*.yaml|*.yml|*.ini|*.mk|*.inc|*.in|*.txt|*.gitignore|.gitmodules|Makefile|*/Makefile|*/CMakeLists.txt|.githooks/pre-commit)
      ;;
    *)
      continue
      ;;
  esac

  identifier=Apache-2.0
  case "$path" in
    hardfloat/*) identifier=BSD-3-Clause ;;
  esac

  if [[ "$source" == index ]]; then
    header="$(git -C "$repo_dir" show ":$path" | sed -n '1,12p')"
  else
    header="$(sed -n '1,12p' "$repo_dir/$path")"
  fi

  if ! grep -Fq "SPDX-License-Identifier: $identifier" <<< "$header"; then
    printf 'missing SPDX-License-Identifier: %s: %s\n' "$identifier" "$path" >&2
    status=1
  fi
done < <(list_paths)

exit "$status"
