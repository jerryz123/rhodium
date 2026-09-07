#!/usr/bin/env bash
# Tests C++ live/replay parity and native Trace Processor queries without Python.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
stream_test_dir="$(mktemp -d /tmp/rheg-perfetto.XXXXXX)"
trap 'rm -rf "$stream_test_dir"' EXIT
: "${TRACE_PROCESSOR:?Set TRACE_PROCESSOR to the native trace_processor_shell executable}"
cmake_options=(-DRHEG_PERFETTO_TESTS=ON '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror')
if [[ -n "${NLOHMANN_JSON_SOURCE_DIR:-}" ]]; then
  cmake_options+=("-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$NLOHMANN_JSON_SOURCE_DIR")
fi
cmake -S "$repo_dir/rheg/perfetto" -B "$stream_test_dir/build" "${cmake_options[@]}"
cmake --build "$stream_test_dir/build" -j 4
ctest --test-dir "$stream_test_dir/build" --output-on-failure
"$stream_test_dir/build/event-stream-test" "$stream_test_dir/snapshot.json" "$stream_test_dir/live.pftrace"
"$stream_test_dir/build/rheg-perfetto" "$stream_test_dir/snapshot.json" > "$stream_test_dir/replay.pftrace"
cmp "$stream_test_dir/live.pftrace" "$stream_test_dir/replay.pftrace"
assert_query() {
  local actual
  actual=$("$TRACE_PROCESSOR" query "$1" "$2" 2> "$stream_test_dir/processor.log")
  if [[ "$actual" != $'"ok"\n1' ]]; then
    printf 'Unexpected Trace Processor result: %s\n' "$actual" >&2
    cat "$stream_test_dir/processor.log" >&2
    exit 1
  fi
}
for index in 0 1 2; do
  file="$stream_test_dir/live.pftrace.prefix$index"
  bytes=$(wc -c < "$file")
  head -c "$bytes" "$stream_test_dir/live.pftrace" | cmp - "$file"
  case "$index" in
    0) nodes=1; edges=0 ;;
    1) nodes=2; edges=1 ;;
    2) nodes=4; edges=4 ;;
  esac
  assert_query "$file" "SELECT count(*)=$nodes AND sum(dur=0 AND ts=CASE name WHEN 'source' THEN 0 WHEN 'left' THEN 10 ELSE 20 END)=$nodes AS ok FROM slice"
  assert_query "$file" "SELECT count(*)=$edges AS ok FROM flow"
  assert_query "$file" "SELECT count(*)=0 AS ok FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')"
  assert_query "$file" "SELECT count(*)=$nodes AND min(string_value)='9007199254740993' AND max(string_value)='9007199254740993' AS ok FROM args WHERE key='debug.sequence'"
done
assert_query "$stream_test_dir/live.pftrace.prefix1" "SELECT count(*)=1 AND min(a.name)='source' AND min(b.name)='left' AS ok FROM flow JOIN slice a ON a.id=flow.slice_out JOIN slice b ON b.id=flow.slice_in"
assert_query "$stream_test_dir/live.pftrace" "SELECT count(*)=4 AND count(DISTINCT a.name||'->'||b.name)=4 AND sum((a.name||'->'||b.name) IN ('source->left','source->right','left->join','right->join'))=4 AS ok FROM flow JOIN slice a ON a.id=flow.slice_out JOIN slice b ON b.id=flow.slice_in"
assert_query "$stream_test_dir/build/precision.pftrace" "SELECT count(*)=3 AND min(ts)=0 AND max(ts)=10 AND sum(ts)=13 AS ok FROM slice"
assert_query "$stream_test_dir/build/precision.pftrace" "SELECT count(*)=3 AND min(string_value)='18446744073709551615' AND max(string_value)='18446744073709551615' AS ok FROM args WHERE key='debug.epoch_id'"
if "$stream_test_dir/build/rheg-perfetto" "$stream_test_dir/nonexistent.json" > "$stream_test_dir/invalid.pftrace" 2> "$stream_test_dir/invalid.log"; then
  echo 'Converter accepted missing input' >&2
  exit 1
fi
test ! -s "$stream_test_dir/invalid.pftrace"
"$TRACE_PROCESSOR" --version
echo 'C++ live/replay and native Perfetto interoperability tests passed'
