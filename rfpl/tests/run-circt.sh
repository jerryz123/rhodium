#!/usr/bin/env bash
# Verifies RFPL CIRCT lowering and its example-owned normalized Verilog output.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
circt_opt="${CIRCT_OPT:-$repo_dir/.tools/firtool-1.155.0/bin/circt-opt}"

if [[ ! -x "$circt_opt" ]]; then
  if command -v circt-opt >/dev/null 2>&1; then
    circt_opt="$(command -v circt-opt)"
  else
    echo "circt-opt not found; run 'make setup-circt' or set CIRCT_OPT" >&2
    exit 1
  fi
fi

cd "$repo_dir"
CIRCT_OPT="$circt_opt" bash tests/backend/run-circt.sh --group rfpl --golden-only
