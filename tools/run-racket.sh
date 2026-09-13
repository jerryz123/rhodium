#!/usr/bin/env bash
# Runs Racket with source rebuilding unless an exact CI bytecode artifact is verified.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiled_root="${PLTCOMPILEDROOTS:-}"
racket_command="${RACKET:-racket}"

if [[ "${RHODIUM_PRECOMPILED:-}" == 1 ]]; then
  "$repo_dir/tools/racket-artifact.sh" verify
  exec "$racket_command" "$@"
fi

if [[ -z "$compiled_root" ]]; then
  compiled_root="$(mktemp -d /tmp/rhodium-racket-compiled.XXXXXX)"
  trap 'rm -rf "$compiled_root"' EXIT
  env PLTCOMPILEDROOTS="$compiled_root" "$racket_command" -y "$@"
  exit
fi

exec "$racket_command" -y "$@"
