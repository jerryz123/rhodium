#!/usr/bin/env bash
# Runs one test batch in an explicit root or the persistent incremental cache.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiled_root="${PLTCOMPILEDROOTS:-}"
raco_command="${RACO:-raco}"

if [[ "${RHODIUM_PRECOMPILED:-}" == 1 ]]; then
  "$repo_dir/tools/racket-artifact.sh" verify
  exec env PLTCOLLECTS="$repo_dir": "$raco_command" test --direct "$@"
elif [[ -n "$compiled_root" ]]; then
  if [[ "$compiled_root" == *:* ]]; then
    echo "Rhodium tests require exactly one compiled root" >&2
    exit 2
  fi
  exec env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
    "$raco_command" test --make --direct "$@"
fi

"$repo_dir/tools/racket-build-cache.sh" run \
  env PLT_COMPILED_FILE_CHECK=exists PLTCOLLECTS="$repo_dir": \
  "$raco_command" test --make --direct "$@"
"$repo_dir/tools/racket-build-cache.sh" publish-dependencies
