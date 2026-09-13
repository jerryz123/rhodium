#!/usr/bin/env bash
# Runs isolated invalid CHI programs and checks their package-owned diagnostics.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"

exec "$repo_dir/tools/run-racket.sh" -S "$repo_dir" "$repo_dir/tests/support/run-negative.rkt" \
  "$repo_dir/chi/tests/invalid" \
  "$repo_dir/chi/tests/run-negative-cases.rktd"
