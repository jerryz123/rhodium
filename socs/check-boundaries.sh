#!/usr/bin/env bash
# Keeps platform contracts, hart adapters, and SoC shapes below product selection.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

# Capture enumeration before entering the loop so a failed find cannot pass.
sources="$(find socs -type d -name tests -prune -o -type f \
  \( -name '*.rhdl' -o -name '*.rhm' \) -print)"
while IFS= read -r source; do
  [[ -n "$source" ]] || continue
  case "$source" in
    socs/products/*) continue ;;
  esac
  if matches="$(grep -nE '^[[:space:]]+.*products/[^" ]+\.(rhdl|rhm)' "$source")"; then
    echo "$source: shared components and SoC shapes must not import product modules" >&2
    echo "$matches" >&2
    exit 1
  else
    status=$?
    if [[ "$status" != 1 ]]; then
      echo "$source: product boundary search failed" >&2
      exit "$status"
    fi
  fi
  for composition in mini-soc single-core-soc tiled-soc; do
    case "$source" in
      "socs/$composition/"*) continue ;;
    esac
    if matches="$(grep -nE "^[[:space:]]+.*\"([^\"]*/)?$composition/[^\"]+\\.(rhdl|rhm)\"" "$source")"; then
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

# Shape composition and the shared hart contract must not load a named core.
# Product defaults and per-core adapters are the only owners of that choice.
while IFS= read -r source; do
  [[ -n "$source" ]] || continue
  case "$source" in
    socs/platform/*|socs/one-hart/*|socs/mini-soc/*|socs/single-core-soc/*|socs/tiled-soc/*|socs/harts/implementation.rhdl)
      if matches="$(grep -nE '^[[:space:]]+.*(cores/(rv5stage|spike)/|harts/(rv5stage|spike)\.rhdl|core-profiles\.rhm|spike-core-profile\.rhm)' "$source")"; then
        echo "$source: core-neutral SoC modules must not import a named core or its profile" >&2
        echo "$matches" >&2
        exit 1
      else
        status=$?
        if [[ "$status" != 1 ]]; then
          echo "$source: core-neutral boundary search failed" >&2
          exit "$status"
        fi
      fi
      ;;
  esac
done <<< "$sources"
