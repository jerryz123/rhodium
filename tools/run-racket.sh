#!/usr/bin/env bash
# Runs Racket against an explicit root or the persistent incremental build cache.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiled_root="${PLTCOMPILEDROOTS:-}"
racket_command="${RACKET:-racket}"

if [[ "${RHODIUM_PRECOMPILED:-}" == 1 ]]; then
  "$repo_dir/tools/racket-artifact.sh" verify
  exec "$racket_command" "$@"
fi

if [[ -n "$compiled_root" ]]; then
  if [[ "$compiled_root" == *:* ]]; then
    echo "Rhodium execution requires exactly one compiled root" >&2
    exit 2
  fi
  exec env PLTCOMPILEDROOTS="$compiled_root" "$racket_command" -y "$@"
fi

exec "$repo_dir/tools/racket-build-cache.sh" run \
  env PLT_COMPILED_FILE_CHECK=exists "$racket_command" -y "$@"
