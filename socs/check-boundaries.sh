#!/usr/bin/env bash
# Keeps named SoC compositions independent and shared components below them.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

# Capture enumeration before entering the loop so a failed find cannot pass.
sources="$(find socs -type d -name tests -prune -o -type f \
  \( -name '*.rhdl' -o -name '*.rhm' \) -print)"
while IFS= read -r source; do
  [[ -n "$source" ]] || continue
  for composition in mini-rv5stage-soc single-core-rv5stage-soc tiled-rv5stage-soc; do
    case "$source" in
      "socs/$composition.rhdl"|"socs/$composition-config.rhdl"|"socs/$composition-plan.rhm"|"socs/$composition/"*)
        continue
        ;;
    esac
    if matches="$(grep -nE "^[[:space:]]+.*\"([^\"]*/)?$composition((-config|-plan)?\\.(rhdl|rhm)|/[^\"]+\\.(rhdl|rhm))\"" "$source")"; then
      echo "$source: shared components and peer SoCs must not import $composition modules" >&2
      echo "$matches" >&2
      exit 1
    else
      status=$?
      if [[ "$status" != 1 ]]; then
        echo "$source: boundary search failed" >&2
        exit "$status"
      fi
    fi
  done
done <<< "$sources"
