#!/usr/bin/env bash
# Runs one test batch in the caller's clean bytecode root or a fresh local root.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiled_root="${PLTCOMPILEDROOTS:-}"
raco_command="${RACO:-raco}"

if [[ "${RHODIUM_PRECOMPILED:-}" == 1 ]]; then
  "$repo_dir/tools/racket-artifact.sh" verify
elif [[ -z "$compiled_root" ]]; then
  compiled_root="$(mktemp -d /tmp/rhodium-test-compiled.XXXXXX)"
  trap 'rm -rf "$compiled_root"' EXIT
fi

env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  "$raco_command" test --direct "$@"
