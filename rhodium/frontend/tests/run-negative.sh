#!/usr/bin/env bash
# Runs isolated #lang rhodium programs and checks their required frontend diagnostics.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/../../.." && pwd)"

exec "$repo_dir/tools/run-racket.sh" -S "$repo_dir" "$repo_dir/tools/testing/run-negative.rkt" \
  "$repo_dir/rhodium/frontend/tests/invalid" \
  "$repo_dir/rhodium/frontend/tests/run-negative-cases.rktd"
