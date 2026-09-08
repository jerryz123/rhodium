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
  assert_query "$file" "SELECT count(*)=$nodes AND sum(dur=10 AND ts=CASE name WHEN 'source' THEN 0 WHEN 'left' THEN 10 ELSE 20 END)=$nodes AS ok FROM slice"
  assert_query "$file" "SELECT count(*)=$nodes AND sum(t.name=s.name)=$nodes AS ok FROM slice s JOIN track t ON t.id=s.track_id"
  assert_query "$file" "SELECT count(*)=0 AS ok FROM slice s JOIN thread_track t ON t.id=s.track_id"
  assert_query "$file" "SELECT count(*)=$edges AS ok FROM flow"
  assert_query "$file" "SELECT count(*)=0 AS ok FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')"
  # Argument sets can now be shared by same-cycle events on different tracks.
  assert_query "$file" "SELECT count(*)=$nodes AND min(string_value)='9007199254740993' AND max(string_value)='9007199254740993' AS ok FROM slice JOIN args USING(arg_set_id) WHERE key='debug.sequence'"
  assert_query "$file" "SELECT count(*)=1 AND min(EXTRACT_ARG(arg_set_id,'debug.pc'))='0xfedcba9876543210' AND min(EXTRACT_ARG(arg_set_id,'debug.instruction'))='0x89abcdef' AND min(EXTRACT_ARG(arg_set_id,'debug.fault'))=1 AND min(EXTRACT_ARG(arg_set_id,'debug.count'))=19 AND min(EXTRACT_ARG(arg_set_id,'debug.delta'))=-7 AND min(EXTRACT_ARG(arg_set_id,'debug.wide'))='36893488147419103231' AS ok FROM slice WHERE name='source'"
  assert_query "$file" "SELECT count(*)=0 AS ok FROM args WHERE key IN ('debug.payload_width','debug.payload_words_lsw_first')"
  assert_query "$file" "SELECT count(*)=0 AS ok FROM slice s JOIN args a USING(arg_set_id) WHERE a.key NOT IN ('debug.sequence','debug.cycle','debug.pc','debug.instruction','debug.fault','debug.count','debug.delta','debug.wide')"
  assert_query "$file" "SELECT count(*)=1 AND min(str_value)='100000000' AS ok FROM metadata WHERE name='cr-rheg.clock_frequency_hz'"
  assert_query "$file" "SELECT count(*)=1 AND min(str_value)='9' AS ok FROM metadata WHERE name='cr-rheg.epoch_id'"
  assert_query "$file" "SELECT count(*)=1 AND min(json_extract(a.string_value,'$.site_id'))='top/source' AND min(json_extract(a.string_value,'$.payload_width'))=172 AND min(json_array_length(a.string_value,'$.fields'))=6 AS ok FROM track t JOIN args a ON a.arg_set_id=t.source_arg_set_id WHERE t.name='source' AND a.key='description'"
done
assert_query "$stream_test_dir/live.pftrace.prefix1" "SELECT count(*)=1 AND min(a.name)='source' AND min(b.name)='left' AS ok FROM flow JOIN slice a ON a.id=flow.slice_out JOIN slice b ON b.id=flow.slice_in"
assert_query "$stream_test_dir/live.pftrace" "SELECT count(*)=4 AND count(DISTINCT a.name||'->'||b.name)=4 AND sum((a.name||'->'||b.name) IN ('source->left','source->right','left->join','right->join'))=4 AS ok FROM flow JOIN slice a ON a.id=flow.slice_out JOIN slice b ON b.id=flow.slice_in"
assert_query "$stream_test_dir/build/precision.pftrace" "SELECT count(*)=3 AND min(ts)=0 AND max(ts)=6 AND sum(ts)=9 AND min(dur)=3 AND max(dur)=4 AND sum(dur)=10 AND max(depth)=0 AS ok FROM slice"
assert_query "$stream_test_dir/build/precision.pftrace" "SELECT count(*)=1 AND min(str_value)='18446744073709551615' AS ok FROM metadata WHERE name='cr-rheg.epoch_id'"
assert_query "$stream_test_dir/build/precision.pftrace" "SELECT count(*)=0 AS ok FROM slice s JOIN args a USING(arg_set_id) WHERE a.key IN ('debug.site','debug.site_id','debug.source_location','debug.epoch_id','debug.clock_frequency_hz','debug.payload_width')"
assert_query "$stream_test_dir/build/precision.pftrace" "SELECT count(*)=1 AND min(json_extract(a.string_value,'$.site_id'))='root/accepted' AND min(json_extract(a.string_value,'$.source_location'))='fixture.rhdl:17' AND min(json_extract(a.string_value,'$.payload_width'))=8 AS ok FROM track t JOIN args a ON a.arg_set_id=t.source_arg_set_id WHERE t.name='accepted' AND a.key='description'"
assert_query "$stream_test_dir/build/empty.pftrace" "SELECT count(*)=0 AS ok FROM slice"
assert_query "$stream_test_dir/build/empty.pftrace" "SELECT count(*)=1 AND min(str_value)='1' AS ok FROM metadata WHERE name='cr-rheg.clock_frequency_hz'"
assert_query "$stream_test_dir/build/last-cycle.pftrace" "SELECT count(*)=1 AND min(ts)=1000000000 AND min(dur)=0 AS ok FROM slice"
assert_query "$stream_test_dir/build/repeated-label.pftrace" "SELECT count(*)=3 AND count(DISTINCT track_id)=2 AND sum(name='accepted')=3 AS ok FROM slice"
if "$stream_test_dir/build/rheg-perfetto" "$stream_test_dir/nonexistent.json" > "$stream_test_dir/invalid.pftrace" 2> "$stream_test_dir/invalid.log"; then
  echo 'Converter accepted missing input' >&2
  exit 1
fi
test ! -s "$stream_test_dir/invalid.pftrace"
"$TRACE_PROCESSOR" --version
echo 'C++ live/replay and native Perfetto interoperability tests passed'
