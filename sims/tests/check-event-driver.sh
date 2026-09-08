#!/usr/bin/env bash
# Exercises trace-driver failures and validates a settled timeout prefix without Python.
set -euo pipefail
ulimit -c 0
: "${TRACE_PROCESSOR:?Set TRACE_PROCESSOR to the native trace_processor_shell executable}"
simulator="${1:?expected traced simulator binary}"
program="${2:?expected smoke ELF}"
driver_test_dir="$(mktemp -d /tmp/rheg-driver-test.XXXXXX)"
trap 'rm -rf "$driver_test_dir"' EXIT
expect_failure() {
  local diagnostic="$1"
  shift
  if "$@" > "$driver_test_dir/run.log" 2>&1; then
    echo "Expected failure: $diagnostic" >&2
    exit 1
  fi
  if ! rg -qF "$diagnostic" "$driver_test_dir/run.log"; then
    cat "$driver_test_dir/run.log" >&2
    exit 1
  fi
}
expect_failure '+rheg-trace=PATH is required' "$simulator" "$program"
expect_failure 'cannot open trace output' "$simulator" "+rheg-trace=$driver_test_dir/missing/trace.pftrace" "$program"
for suffix in .pftrace .pftrace.gz; do
  expect_failure 'SoC harness simulation timed out' "$simulator" "+rheg-trace=$driver_test_dir/timeout$suffix" +max-cycles=50 "$program"
  if [[ "$suffix" == *.gz ]]; then gzip -t "$driver_test_dir/timeout$suffix"; fi
  actual=$("$TRACE_PROCESSOR" query "$driver_test_dir/timeout$suffix" "SELECT (SELECT count(*) FROM slice)>0 AND count(*)=0 AS ok FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')")
  if [[ "$actual" != $'"ok"\n1' ]]; then
    printf 'Invalid timeout trace prefix: %s\n' "$actual" >&2
    exit 1
  fi
done
echo 'Trace driver missing-path, open-failure, and timeout-prefix checks passed'
