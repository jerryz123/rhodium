#!/usr/bin/env bash
# Refreshes checkout bytecode against the current source inventory and contents.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 REPOSITORY COMPILED-ROOT METADATA-DIRECTORY" >&2
  exit 2
fi

repo_dir="$1"
compiled_root="$2"
metadata_dir="$3"
for directory in "$repo_dir" "$compiled_root" "$metadata_dir"; do
  if [[ "$directory" != /* || "$directory" == / || "$directory" == *:* ]]; then
    echo "Rhodium's project bytecode cache requires absolute non-root directories" >&2
    exit 2
  fi
done
if [[ ! -d "$repo_dir" || ! -d "$compiled_root" ]]; then
  echo "Rhodium's project bytecode cache requires an existing repository and compiled root" >&2
  exit 2
fi

project_subtree="$compiled_root/${repo_dir#/}"
source_manifest="$metadata_dir/source-paths"
content_manifest="$metadata_dir/source-content"
mkdir -p "$metadata_dir"
next_manifest="$(mktemp "$metadata_dir/.source-paths.XXXXXX")"
next_content="$(mktemp "$metadata_dir/.source-content.XXXXXX")"
trap 'rm -f -- "$next_manifest" "$next_content"' EXIT

git -C "$repo_dir" ls-files --cached --others --exclude-standard -- \
  '*.rkt' '*.rhm' '*.rhdl' | LC_ALL=C sort -u | while IFS= read -r source; do
    if [[ -f "$repo_dir/$source" ]]; then
      printf '%s\n' "$source"
    fi
  done > "$next_manifest"
git -C "$repo_dir" hash-object --stdin-paths < "$next_manifest" \
  | paste "$next_manifest" - > "$next_content"

if [[ ! -f "$source_manifest" ]] || ! cmp -s "$source_manifest" "$next_manifest"; then
  # A moved or deleted source can leave loadable orphan bytecode behind.
  rm -rf -- "$project_subtree"
elif [[ ! -f "$content_manifest" ]] || ! cmp -s "$content_manifest" "$next_content"; then
  changed_sources="$(mktemp "$metadata_dir/.changed-sources.XXXXXX")"
  trap 'rm -f -- "$next_manifest" "$next_content" "$changed_sources"' EXIT
  awk -F '\t' 'NR == FNR { previous[$1] = $2; next }
    previous[$1] != $2 { print $1 }' "$content_manifest" "$next_content" \
    > "$changed_sources"
  changed_arguments=()
  while IFS= read -r changed_source; do
    changed_arguments+=("$repo_dir/$changed_source")
  done < "$changed_sources"
  if (( ${#changed_arguments[@]} > 0 )) && [[ -d "$project_subtree" ]]; then
    env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
      "${RACKET:-racket}" "$repo_dir/tools/invalidate-racket-build-cache.rkt" \
      "$repo_dir" "$next_manifest" "${changed_arguments[@]}"
  fi
fi

mv "$next_manifest" "$source_manifest"
mv "$next_content" "$content_manifest"
