#!/usr/bin/env bash
# Times incremental compilation of the positive CI entrypoints and attests the completed root.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiled_root="${PLTCOMPILEDROOTS:-}"

if [[ -z "$compiled_root" || "$compiled_root" == *:* ]]; then
  echo "Rhodium compilation requires exactly one compiled root" >&2
  exit 2
fi

sources=()
while IFS= read -r source; do
  sources+=("$source")
done < <(make -C "$repo_dir" --no-print-directory print-racket-compile-sources)
if (( ${#sources[@]} == 0 )); then
  echo "Rhodium compilation source manifest is empty" >&2
  exit 1
fi

cd "$repo_dir"
started=$SECONDS
printf 'rhodium-cache: compiling %s entrypoints with %s workers\n' "${#sources[@]}" "${RHODIUM_RACO_JOBS:-2}" >&2
env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  raco make -j "${RHODIUM_RACO_JOBS:-2}" "${sources[@]}"
printf 'rhodium-cache: compilation completed in %ss\n' "$((SECONDS - started))" >&2
"$repo_dir/tools/racket-artifact.sh" write
