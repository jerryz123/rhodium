#!/usr/bin/env bash
# Tests C++ live/replay parity and native Trace Processor queries without Python.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
stream_test_dir="$(mktemp -d /tmp/rheg-perfetto.XXXXXX)"
trap 'rm -rf "$stream_test_dir"' EXIT
: "${TRACE_PROCESSOR:?Set TRACE_PROCESSOR to the native trace_processor_shell executable}"
cmake_options=(-DRHEG_PERFETTO_TESTS=ON '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror')
if [[ -n "${NLOHMANN_JSON_SOURCE_DIR:-}" ]]; then
  cmake_options+=("-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$NLOHMANN_JSON_SOURCE_DIR")
fi
if [[ -n "${RHEG_SPIKE_SOURCE_DIR:-}" ]]; then
  cmake_options+=("-DFETCHCONTENT_SOURCE_DIR_RHEG_SPIKE=$RHEG_SPIKE_SOURCE_DIR")
fi
cmake -S "$repo_dir/rheg/perfetto" -B "$stream_test_dir/build" "${cmake_options[@]}"
cmake --build "$stream_test_dir/build" -j 4
ctest --test-dir "$stream_test_dir/build" --output-on-failure
"$stream_test_dir/build/event-stream-test" "$stream_test_dir/snapshot.json" "$stream_test_dir/live.pftrace"
"$stream_test_dir/build/rheg-perfetto" "$stream_test_dir/snapshot.json" > "$stream_test_dir/replay.pftrace"
cmp "$stream_test_dir/live.pftrace" "$stream_test_dir/replay.pftrace"
"$stream_test_dir/build/event-stream-test" "$stream_test_dir/gzip-snapshot.json" "$stream_test_dir/live.pftrace.gz" --gzip
"$stream_test_dir/build/rheg-perfetto" --gzip "$stream_test_dir/snapshot.json" > "$stream_test_dir/replay.pftrace.gz"
for mode in live replay; do
  gzip -t "$stream_test_dir/$mode.pftrace.gz"
  gzip -dc "$stream_test_dir/$mode.pftrace.gz" | cmp - "$stream_test_dir/$mode.pftrace"
done
assert_query() {
  local actual
  if ! actual=$("$TRACE_PROCESSOR" query "$1" "$2" 2> "$stream_test_dir/processor.log"); then
    cat "$stream_test_dir/processor.log" >&2
    exit 1
  fi
  if [[ "$actual" != $'"ok"\n1' ]]; then
    printf 'Unexpected Trace Processor result: %s\n' "$actual" >&2
    cat "$stream_test_dir/processor.log" >&2
    exit 1
  fi
}
assert_query "$stream_test_dir/build/qualified-labels.pftrace" "WITH expected(track,label) AS (VALUES('frontend.s0.request','request'),('backend.s0.request','request'),('plain','plain'),('trailing.','trailing.')) SELECT count(*)=4 AND count(DISTINCT s.track_id)=4 AND sum(s.name=e.label AND s.dur=10)=4 AS ok FROM slice s JOIN track t ON t.id=s.track_id JOIN expected e ON e.track=t.name"
assert_query "$stream_test_dir/build/partial.pftrace" "SELECT count(*)=2 AND sum(COALESCE(EXTRACT_ARG(arg_set_id,'debug.ancestry_unknown')='true',0))=1 AND (SELECT count(*) FROM flow)=1 AS ok FROM slice"
assert_query "$stream_test_dir/build/partial.pftrace" "SELECT count(*)=1 AND min(json_extract(EXTRACT_ARG(source_arg_set_id,'description'),'$.ancestry_gaps[0].boundary'))='opaque.output' AS ok FROM track WHERE name='issued'"
assert_query "$stream_test_dir/build/partial.pftrace" "SELECT count(*)=0 AS ok FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')"
assert_query "$stream_test_dir/build/partial-stalls.pftrace" "SELECT count(*)=2 AND sum(dur)=30 AND sum(COALESCE(EXTRACT_ARG(arg_set_id,'debug.ancestry_unknown')='true',0))=1 AND sum(ts=10 AND dur=20)=1 AS ok FROM slice"
assert_query "$stream_test_dir/build/stalls.pftrace" "SELECT count(*)=4 AND sum(dur)=50 AND sum(dur=20 AND name='stall')=1 AND sum(ts=10*CAST(EXTRACT_ARG(arg_set_id,'debug.cycle') AS INT))=4 AS ok FROM slice"
assert_query "$stream_test_dir/build/stalls.pftrace" "SELECT count(*)=2 AND sum(a.name='accepted')=2 AND sum(b.name='stall')=1 AND sum(b.name='issued')=1 AS ok FROM flow JOIN slice a ON a.id=flow.slice_out JOIN slice b ON b.id=flow.slice_in"
assert_query "$stream_test_dir/build/stalls.pftrace" "SELECT count(*)=2 AND sum(json_extract(a.string_value,'$.kind')='transfer')=2 AND sum(json_extract(a.string_value,'$.observations[0].kind')='stall' AND json_extract(a.string_value,'$.observations[0].observation_of')='issued' AND json_extract(a.string_value,'$.observations[0].site')=2)=1 AS ok FROM track t JOIN args a ON a.arg_set_id=t.source_arg_set_id WHERE a.key='description'"
assert_query "$stream_test_dir/build/stalls.pftrace" "SELECT count(*)=3 AND sum(s.name='stall')=2 AND sum(s.dur)=40 AND max(s.depth)=0 AS ok FROM slice s JOIN track t ON t.id=s.track_id WHERE t.name='issued'"
assert_query "$stream_test_dir/build/stalls.pftrace" "SELECT count(*)=0 AS ok FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')"
assert_query "$stream_test_dir/build/named-stalls.pftrace" "SELECT count(*)=1 AND min(t.name)='decode' AND min(s.name)='stall' AND min(EXTRACT_ARG(s.arg_set_id,'debug.pc'))='0x0000000000001000' AND min(EXTRACT_ARG(s.arg_set_id,'debug.instruction'))='li a0, 5' AND min(EXTRACT_ARG(s.arg_set_id,'debug.reason'))=1 AND min(json_extract(EXTRACT_ARG(t.source_arg_set_id,'description'),'$.site'))=1 AND min(json_extract(EXTRACT_ARG(t.source_arg_set_id,'description'),'$.observations[0].payload_width'))=99 AND min(json_array_length(EXTRACT_ARG(t.source_arg_set_id,'description'),'$.observations[0].fields'))=5 AND min(EXTRACT_ARG(s.arg_set_id,'debug.rheg'))=1 AND min(EXTRACT_ARG(s.arg_set_id,'debug.rheg_site'))=1 AS ok FROM slice s JOIN track t ON t.id=s.track_id"
assert_query "$stream_test_dir/build/stall-runs.pftrace" "WITH expected(track,first,last) AS (VALUES('issue',1,2),('issue',3,3),('issue',4,4),('issue',5,6),('issue',8,8),('issue',10,10),('issue',11,12),('other',1,3)) SELECT count(*)=8 AND sum(s.dur=(last-first+1)*10)=8 AND max(s.depth)=0 AS ok FROM slice s JOIN track t ON t.id=s.track_id JOIN expected e ON t.name=e.track AND s.ts=e.first*10 WHERE s.name='stall'"
assert_query "$stream_test_dir/build/stall-runs.pftrace" "SELECT count(*)=12 AND sum(dur)=170 AND sum(name='stall')=8 AND (SELECT count(*) FROM flow)=6 AS ok FROM slice"
assert_query "$stream_test_dir/build/stall-runs.pftrace" "SELECT count(*)=6 AND sum(a.name='source')=5 AND sum(a.name='other')=1 AND sum(b.name='stall')=5 AND sum(b.name='issue')=1 AS ok FROM flow JOIN slice a ON a.id=flow.slice_out JOIN slice b ON b.id=flow.slice_in"
assert_query "$stream_test_dir/build/stall-runs.pftrace.prefix2" "SELECT count(*)=4 AND sum(name='stall' AND dur=-1)=2 AS ok FROM slice"
assert_query "$stream_test_dir/build/stall-runs.pftrace.prefix7" "SELECT count(*)=8 AND min(dur)=10 AND sum(dur)=120 AS ok FROM slice"
assert_query "$stream_test_dir/build/stall-runs.pftrace.maximum" "SELECT count(*)=1 AND min(name)='stall' AND min(ts)=999999999 AND min(dur)=1 AND min(EXTRACT_ARG(arg_set_id,'debug.sequence'))='18446744073709551614' AS ok FROM slice"
assert_query "$stream_test_dir/build/named-stalls.pftrace" "SELECT count(*)=7 AND sum(a.key IN ('debug.cycle','debug.sequence','debug.pc','debug.instruction','debug.reason','debug.rheg','debug.rheg_site'))=7 AS ok FROM slice s JOIN args a USING(arg_set_id)"
for suffix in '' .prefix2 .prefix7 .maximum; do
  assert_query "$stream_test_dir/build/stall-runs.pftrace$suffix" "SELECT count(*)=0 AS ok FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')"
done
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
  assert_query "$file" "SELECT count(*)=$nodes AND count(DISTINCT p.id)=1 AND sum(p.name='StreamTest' AND p.parent_id IS NULL AND EXTRACT_ARG(p.source_arg_set_id,'child_ordering')='lexicographic')=$nodes AS ok FROM slice s JOIN track t ON t.id=s.track_id JOIN track p ON p.id=t.parent_id"
  assert_query "$file" "SELECT count(*)=0 AS ok FROM slice s JOIN thread_track t ON t.id=s.track_id"
  assert_query "$file" "SELECT count(*)=0 AS ok FROM process_track"
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
for mode in live replay; do
  assert_query "$stream_test_dir/$mode.pftrace.gz" "SELECT (SELECT count(*) FROM slice)=4 AND (SELECT count(*) FROM flow)=4 AND count(*)=0 AS ok FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')"
done
assert_query "$stream_test_dir/build/compressed.pftrace.gz" "SELECT count(*)=10000 AND min(ts)=30 AND max(ts)=100020 AND sum(dur=10)=10000 AS ok FROM slice"
assert_query "$stream_test_dir/build/compressed.pftrace.gz" "SELECT count(*)=0 AS ok FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')"
assert_query "$stream_test_dir/build/enums.pftrace" "SELECT count(*)=12 AND sum(s.dur=10 AND s.ts=10*CAST(EXTRACT_ARG(s.arg_set_id,'debug.cycle') AS INT))=12 AND sum(s.name=CASE t.name WHEN 'cache.txreq' THEN CASE EXTRACT_ARG(s.arg_set_id,'debug.opcode') WHEN 1 THEN 'ReadShared' WHEN 2 THEN 'ReadClean' ELSE '0x7f' END WHEN 'cache.txrsp' THEN CASE EXTRACT_ARG(s.arg_set_id,'debug.operation') WHEN 1 THEN 'SnpResp' WHEN 2 THEN 'CompAck' ELSE '0x7f' END WHEN 'unselected' THEN 'unselected' ELSE 'All' END)=12 AS ok FROM slice s JOIN track t ON t.id=s.track_id"
assert_query "$stream_test_dir/build/enums.pftrace" "SELECT count(*)=3 AND sum(EXTRACT_ARG(s.arg_set_id,'debug.opcode')='18446744073709551615')=3 AS ok FROM slice s JOIN track t ON t.id=s.track_id WHERE t.name='wide'"
assert_query "$stream_test_dir/build/enums.pftrace" "SELECT count(*)=4 AND sum(json_array_length(a.string_value,'$.fields[0].symbols')>0)=4 AND sum(COALESCE(json_extract(a.string_value,'$.fields[0].label'),0))=3 AS ok FROM track t JOIN args a ON a.arg_set_id=t.source_arg_set_id WHERE a.key='description'"
assert_query "$stream_test_dir/build/enums.pftrace" "SELECT count(*)=0 AS ok FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')"
assert_query "$stream_test_dir/build/enums.pftrace.override" "SELECT count(*)=1 AND min(s.name)='Explicit' AND min(t.name)='decode' AND min(EXTRACT_ARG(s.arg_set_id,'debug.opcode'))='li a0, 5' AND min(EXTRACT_ARG(s.arg_set_id,'debug.instruction'))=5244179 AS ok FROM slice s JOIN track t ON t.id=s.track_id"
assert_query "$stream_test_dir/build/riscv64.pftrace" "WITH expected(seq,asm) AS (VALUES('0','li a0, 5'),('1','csrr a0, mhartid'),('2','j 0x1008'),('3','j 0x2008'),('4','ld a0, 0(a0)'),('5','fadd.s fa0, fa0, fa1'),('6','c.nop'),('7','0xffffffff'),('8','j 0xfffffffffffffffc'),('9','auipc a0, 0x0')) SELECT count(*)=10 AND sum(EXTRACT_ARG(s.arg_set_id,'debug.opcode')=e.asm)=10 AND sum(length(EXTRACT_ARG(s.arg_set_id,'debug.instruction'))=10)=10 AS ok FROM slice s JOIN expected e ON e.seq=EXTRACT_ARG(s.arg_set_id,'debug.sequence')"
assert_query "$stream_test_dir/build/riscv32.pftrace" "WITH expected(seq,asm) AS (VALUES('4','0x00053503'),('5','0x00b50553'),('6','0x00000001'),('8','j 0xfffffffc')) SELECT count(*)=4 AND sum(EXTRACT_ARG(s.arg_set_id,'debug.opcode')=e.asm)=4 AS ok FROM slice s JOIN expected e ON e.seq=EXTRACT_ARG(s.arg_set_id,'debug.sequence')"
assert_query "$stream_test_dir/build/riscv16.pftrace" "SELECT count(*)=2 AND sum(EXTRACT_ARG(arg_set_id,'debug.opcode')='c.nop')=1 AND sum(EXTRACT_ARG(arg_set_id,'debug.opcode')='c.unimp')=1 AS ok FROM slice"
assert_query "$stream_test_dir/build/interning.pftrace" "SELECT count(*)=5002 AND sum(name='li' AND EXTRACT_ARG(arg_set_id,'debug.opcode')='li a0, 5' AND EXTRACT_ARG(arg_set_id,'debug.instruction')='0x00500513' AND EXTRACT_ARG(arg_set_id,'debug.address')=printf('0x%016x',4096+4*CASE CAST(EXTRACT_ARG(arg_set_id,'debug.sequence') AS INT) WHEN 5000 THEN 0 WHEN 5001 THEN 4999 ELSE CAST(EXTRACT_ARG(arg_set_id,'debug.sequence') AS INT) END))=5002 AS ok FROM slice"
assert_query "$stream_test_dir/build/interning.pftrace" "SELECT count(*)=0 AS ok FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')"
assert_query "$stream_test_dir/build/terminal.pftrace" "SELECT count(*)=1 AND min(a.name)='accepted' AND min(b.name)='issued' AND min(a.ts)=0 AND min(b.ts)=6 AS ok FROM flow JOIN slice a ON a.id=flow.slice_out JOIN slice b ON b.id=flow.slice_in"
for fixture in riscv64 riscv64-properties riscv32 riscv16; do
  assert_query "$stream_test_dir/build/$fixture.pftrace" "WITH decoded AS (SELECT s.*, EXTRACT_ARG(s.arg_set_id,'debug.opcode')||' ' AS assembly FROM slice s) SELECT count(*)>0 AND sum(s.name=substr(s.assembly,1,instr(s.assembly,' ')-1) AND t.name='decode' AND s.dur=10 AND s.ts=10*CAST(EXTRACT_ARG(s.arg_set_id,'debug.cycle') AS INT))=count(*) AS ok FROM decoded s JOIN track t ON t.id=s.track_id"
done
assert_query "$stream_test_dir/build/multiple-instructions.pftrace" "SELECT count(*)=10 AND sum(s.name='decode' AND t.name='decode' AND EXTRACT_ARG(s.arg_set_id,'debug.opcode')=EXTRACT_ARG(s.arg_set_id,'debug.instruction'))=10 AS ok FROM slice s JOIN track t ON t.id=s.track_id"
assert_query "$stream_test_dir/build/riscv64.pftrace" "SELECT count(*)=1 AND min(json_extract(a.string_value,'$.fields[1].isa'))='rv64imafdc_zicsr' AND min(json_extract(a.string_value,'$.fields[1].pc'))='address' AS ok FROM track t JOIN args a ON a.arg_set_id=t.source_arg_set_id WHERE t.name='decode' AND a.key='description'"
assert_query "$stream_test_dir/build/riscv64-properties.pftrace" "SELECT count(*)=1 AND min(json_extract(a.string_value,'$.fields[1].isa'))='rv64imafdcb_za64rs_zba_zbb_zbs_zcmop_zic64b_zicbop_zicboz_zawrs_zihintpause_zihintntl_zicntr_zicond_zicsr_zifencei_zihpm_zimop_zkt' AS ok FROM track t JOIN args a ON a.arg_set_id=t.source_arg_set_id WHERE t.name='decode' AND a.key='description'"
assert_query "$stream_test_dir/build/riscv64-properties.pftrace" "SELECT count(*)=10 AND sum(name='li' AND EXTRACT_ARG(arg_set_id,'debug.opcode')='li a0, 5')=1 AND sum(name='fadd.s')=1 AND sum(name='c.nop')=1 AND sum(name='0xffffffff')=1 AS ok FROM slice"
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
if "$stream_test_dir/build/rheg-perfetto" --unknown "$stream_test_dir/snapshot.json" > "$stream_test_dir/invalid.pftrace" 2> "$stream_test_dir/invalid.log"; then
  echo 'Converter accepted unknown option' >&2
  exit 1
fi
test ! -s "$stream_test_dir/invalid.pftrace"
"$TRACE_PROCESSOR" --version
echo 'C++ live/replay and native Perfetto interoperability tests passed'
