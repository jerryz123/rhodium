#!/usr/bin/env bash
# Keeps named SoC compositions independent and shared components below them.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

while IFS= read -r source; do
  for variant in mini simple tiled; do
    case "$source" in
      "socs/$variant-soc.rhdl"|"socs/$variant-soc-config.rhdl"|"socs/$variant-soc-plan.rhm"|"socs/$variant-soc/"*)
        continue
        ;;
    esac
    matches="$(rg -n "^[[:space:]]+.*\"([^\"]*/)?$variant-soc((-config|-plan)?\\.(rhdl|rhm)|/[^\\\"]+\\.(rhdl|rhm))\"" "$source" || true)"
    if [[ -n "$matches" ]]; then
      echo "$source: shared components and peer SoCs must not import $variant-soc modules" >&2
      echo "$matches" >&2
      exit 1
    fi
  done
done < <(rg --files socs -g '*.rhdl' -g '*.rhm' -g '!tests/**' -g '!**/tests/**')
