#!/usr/bin/env bash
# Checks real RV2Wide instruction, lane, service, and completion lineage with Perfetto.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
: "${TRACE_PROCESSOR:?Set TRACE_PROCESSOR to the native trace_processor_shell executable}"
trace_file="${1:?expected trace path}"
script_dir="$(cd "$(dirname "$0")" && pwd)"
test -s "$trace_file"
actual=$("$TRACE_PROCESSOR" query -f - "$trace_file" <<< "$(< "$script_dir/event-tracks.sql")
$(< "$script_dir/check-rv2wide-events.sql")")
if [[ "$actual" != $'"ok"\n1' ]]; then
  printf 'Unexpected RV2Wide trace result: %s\n' "$actual" >&2
  exit 1
fi
echo 'RV2Wide fetch, dual-slot pipeline, and delayed-completion ancestry passed'
