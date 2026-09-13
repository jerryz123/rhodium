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

status=0
while IFS= read -r source_file; do
  source_has_golden=false
  while IFS= read -r design_export; do
    manifest_prefix="|$source_file|$design_export|"
    manifest_count="$(grep -Fc "$manifest_prefix" tests/backend/run-circt.sh || true)"
    manifest_entry="$(grep -F "$manifest_prefix" tests/backend/run-circt.sh || true)"
    if [[ "$manifest_count" != 1 ]]; then
      echo "$source_file: $design_export requires exactly one backend manifest entry" >&2
      status=1
      continue
    fi
    reference_export="${manifest_entry##*|}"
    reference_export="${reference_export%\'}"
    [[ "$reference_export" != - ]] || continue
    source_has_golden=true

    if ! grep -Fq "def $reference_export = @str|<<{" "$source_file"; then
      echo "$source_file: $design_export requires $reference_export" >&2
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

  done < <(sed -n 's/^def \([A-Za-z0-9_]*design\) = .*/\1/p' "$source_file")

  while IFS= read -r reference_export; do
    reference_manifest_count="$(
      grep -F "|$source_file|" tests/backend/run-circt.sh \
        | grep -Fc "|$reference_export'" || true
    )"
    if [[ "$reference_manifest_count" != 1 ]]; then
      echo "$source_file: $reference_export requires exactly one golden manifest entry" >&2
      status=1
    fi
  done < <(sed -n 's/^def \([A-Za-z0-9_]*verilog_reference\) = @str|<<{.*/\1/p' "$source_file")

  if [[ "$allow_empty" == false && "$source_has_golden" == true ]]; then
    while IFS= read -r circuit_name; do
      if ! grep -Eq "^module ${circuit_name}([_(]|$)" "$source_file"; then
        echo "$source_file: circuit $circuit_name has no colocated Verilog module" >&2
        status=1
      fi
    done < <(sed -n -E 's/^(sync_)?circuit ([A-Za-z0-9_]+).*/\2/p' "$source_file")
  fi
done < <(find "${source_roots[@]}" -type f \( -name '*.rhm' -o -name '*.rhdl' \) | sort)

exit "$status"
