#!/usr/bin/env bash
# Runs one test batch in the caller's clean bytecode root or a fresh local root.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiled_root="${PLTCOMPILEDROOTS:-}"
raco_command="${RACO:-raco}"
owns_compiled_root=false
cache_seeded=false

if [[ "${RHODIUM_PRECOMPILED:-}" == 1 ]]; then
  "$repo_dir/tools/racket-artifact.sh" verify
elif [[ -z "$compiled_root" ]]; then
  compiled_root="$(mktemp -d /tmp/rhodium-test-compiled.XXXXXX)"
  trap 'rm -rf "$compiled_root"' EXIT
  owns_compiled_root=true
  if "$repo_dir/tools/racket-dependency-cache.sh" seed "$compiled_root"; then
    cache_seeded=true
  fi
fi

test_options=(--direct)
if [[ "$owns_compiled_root" == true && "$cache_seeded" == false ]]; then
  echo "Preparing the local Racket dependency cache; later test runs will reuse it." >&2
  test_options=(--make --direct)
fi

env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  "$raco_command" test "${test_options[@]}" "$@"

if [[ "$owns_compiled_root" == true && "$cache_seeded" == false ]]; then
  "$repo_dir/tools/racket-dependency-cache.sh" publish "$compiled_root"
fi
