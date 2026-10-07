#!/usr/bin/env bash
# Validates example manifest coverage and exact references selected for simple fixtures.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

allow_empty=false
source_roots=()
for argument in "$@"; do
  if [[ "$argument" == "--allow-empty" ]]; then
    allow_empty=true
  elif [[ "$argument" == --* ]]; then
    echo "usage: $0 [--allow-empty] [SOURCE_ROOT ...]" >&2
    exit 2
  else
    source_roots+=("$argument")
  fi
done
if (( ${#source_roots[@]} == 0 )); then
  source_roots=(examples)
fi

for source_root in "${source_roots[@]}"; do
  if [[ ! -e "$source_root" ]]; then
    echo "example source root not found: $source_root" >&2
    exit 2
  fi
done

# Check declared program exports, not every circuit declared in the source:
# target compilation emits only the selected top and its reachable definitions.
status=0
while IFS= read -r source_file; do
  while IFS= read -r program_export; do
    manifest_prefix="|$source_file|$program_export|"
    manifest_count="$(grep -Fc "$manifest_prefix" tools/testing/circt/run.sh || true)"
    manifest_entry="$(grep -F "$manifest_prefix" tools/testing/circt/run.sh || true)"
    if [[ "$manifest_count" != 1 ]]; then
      echo "$source_file: $program_export requires exactly one backend manifest entry" >&2
      status=1
      continue
    fi
    reference_export="${manifest_entry##*|}"
    reference_export="${reference_export%\'}"
    [[ "$reference_export" != - ]] || continue

    if ! grep -Fq "def $reference_export = @str|<<{" "$source_file"; then
      echo "$source_file: $program_export requires $reference_export" >&2
      status=1
    elif ! grep -Eq "^[[:space:]]+$reference_export$" "$source_file"; then
      echo "$source_file: $reference_export must be exported" >&2
      status=1
    elif [[ "$allow_empty" == false ]] &&
         ! awk -v start="def $reference_export = @str|<<{" '
             $0 == start { in_reference = 1; next }
             in_reference && $0 == "}>>|" { in_reference = 0 }
             in_reference && /[^[:space:]]/ { has_verilog = 1 }
             END { exit(has_verilog ? 0 : 1) }
           ' "$source_file"; then
      echo "$source_file: $reference_export must contain generated Verilog" >&2
      status=1
    fi

  done < <(sed -n 's/^def \([A-Za-z0-9_]*program\) = .*/\1/p' "$source_file")

  while IFS= read -r reference_export; do
    reference_manifest_count="$(
      grep -F "|$source_file|" tools/testing/circt/run.sh \
        | grep -Fc "|$reference_export'" || true
    )"
    if [[ "$reference_manifest_count" != 1 ]]; then
      echo "$source_file: $reference_export requires exactly one golden manifest entry" >&2
      status=1
    fi
  done < <(sed -n 's/^def \([A-Za-z0-9_]*verilog_reference\) = @str|<<{.*/\1/p' "$source_file")

done < <(find "${source_roots[@]}" -type f \( -name '*.rhm' -o -name '*.rhdl' \) | sort)

exit "$status"
