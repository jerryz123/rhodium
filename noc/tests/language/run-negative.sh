#!/usr/bin/env bash
# Runs invalid topology-language fixtures and checks their source-located diagnostics.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/../../.." && pwd)"

exec "$repo_dir/tools/run-racket.sh" -S "$repo_dir" "$repo_dir/noc/tests/language/run-negative.rkt"
