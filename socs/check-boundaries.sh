#!/usr/bin/env bash
# Keeps named SoC compositions independent and shared components below them.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

# Capture enumeration before entering the loop so a failed find cannot pass.
sources="$(find socs -type d -name tests -prune -o -type f \
  \( -name '*.rhdl' -o -name '*.rhm' \) -print)"
while IFS= read -r source; do
  [[ -n "$source" ]] || continue
  for variant in mini simple tiled; do
    case "$source" in
      "socs/$variant-soc.rhdl"|"socs/$variant-soc-config.rhdl"|"socs/$variant-soc-plan.rhm"|"socs/$variant-soc/"*)
        continue
        ;;
    esac
    if matches="$(grep -nE "^[[:space:]]+.*\"([^\"]*/)?$variant-soc((-config|-plan)?\\.(rhdl|rhm)|/[^\"]+\\.(rhdl|rhm))\"" "$source")"; then
      echo "$source: shared components and peer SoCs must not import $variant-soc modules" >&2
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
