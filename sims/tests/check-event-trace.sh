#!/usr/bin/env bash
# Checks SimpleSoC outer-memory/cache transfers and scalar pipeline lineage with the native importer.
set -euo pipefail
: "${TRACE_PROCESSOR:?Set TRACE_PROCESSOR to the native trace_processor_shell executable}"
trace_file="${1:?expected trace path}"
script_dir="$(cd "$(dirname "$0")" && pwd)"
test -s "$trace_file"
assert_query() {
  local actual
  actual=$("$TRACE_PROCESSOR" query -f - "$trace_file" <<< "$1")
  if [[ "$actual" != $'"ok"\n1' ]]; then
    printf 'Unexpected SimpleSoC trace result: %s\n' "$actual" >&2
    exit 1
  fi
}
assert_query "SELECT count(*)>0 AND sum(t.name='memory-request')>0 AND sum(t.name='memory-accept')>0 AND sum(t.name NOT IN ('memory-request','memory-accept','memory-response','soc-response','core.s1.fetch','core.s2.decode','core.s3.execute','core.s4.memory','core.s5.wb') AND t.name NOT GLOB '[id]cache.*')=0 AND sum(s.dur!=10)=0 AND max(s.depth)=0 AS ok FROM slice s JOIN track t ON t.id=s.track_id"
assert_query "SELECT count(*)>0 AND sum(t.name=s.name)=count(*) AS ok FROM slice s JOIN track t ON t.id=s.track_id WHERE t.name NOT GLOB 'core.*' AND t.name NOT GLOB '[id]cache.*'"
assert_query "SELECT count(*)=(SELECT count(*) FROM slice) AND count(DISTINCT p.id)=1 AND sum(p.name='SoCHarness' AND p.parent_id IS NULL AND EXTRACT_ARG(p.source_arg_set_id,'child_ordering')='lexicographic')=count(*) AS ok FROM slice s JOIN track t ON t.id=s.track_id JOIN track p ON p.id=t.parent_id"
assert_query "SELECT count(*)=0 AS ok FROM slice s JOIN thread_track t ON t.id=s.track_id"
assert_query "SELECT count(*)=0 AS ok FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')"
assert_query "SELECT count(*)>0 AND count(*)*2=(SELECT count(*) FROM slice s JOIN track t ON t.id=s.track_id WHERE t.name IN ('memory-request','memory-accept','memory-response','soc-response')) AND sum(a.ts!=b.ts)=0 AND sum((a.name||'->'||b.name) NOT IN ('memory-request->memory-accept','memory-response->soc-response'))=0 AS ok FROM flow JOIN slice a ON a.id=flow.slice_out JOIN slice b ON b.id=flow.slice_in JOIN track t ON t.id=a.track_id WHERE t.name NOT GLOB 'core.*'"
assert_query "SELECT count(*)>0 AND sum(ts!=10*CAST(EXTRACT_ARG(arg_set_id,'debug.cycle') AS INT))=0 AS ok FROM slice"
assert_query "SELECT count(*)=1 AND min(str_value)='100000000' AS ok FROM metadata WHERE name='cr-rheg.clock_frequency_hz'"
assert_query "SELECT count(*)=1 AND min(str_value)='0' AS ok FROM metadata WHERE name='cr-rheg.epoch_id'"
assert_query "SELECT count(*)=0 AS ok FROM slice s JOIN args a USING(arg_set_id) JOIN track t ON t.id=s.track_id WHERE t.name NOT GLOB '[id]cache.*' AND a.key NOT IN ('debug.sequence','debug.cycle','debug.pc','debug.instruction','debug.raw')"
assert_query "SELECT count(*)=5 AND sum(json_extract(a.string_value,'$.payload_width')=96)=5 AND sum(json_extract(a.string_value,'$.site_id') GLOB 'SoCHarness/soc/rv5stage/core/event:*')=5 AND sum(length(json_extract(a.string_value,'$.source_location'))>0)=5 AS ok FROM track t JOIN args a ON a.arg_set_id=t.source_arg_set_id WHERE t.name GLOB 'core.*' AND a.key='description'"
assert_query "SELECT count(*)=5 AND sum(json_extract(a.string_value,'$.fields[1].encoding')='riscv' AND json_extract(a.string_value,'$.fields[1].isa') GLOB 'rv64i*' AND json_extract(a.string_value,'$.fields[1].pc')='pc')=5 AS ok FROM track t JOIN args a ON a.arg_set_id=t.source_arg_set_id WHERE t.name GLOB 'core.*' AND a.key='description'"
assert_query "SELECT count(*)>0 AND sum(EXTRACT_ARG(a.arg_set_id,'debug.raw')!=EXTRACT_ARG(b.arg_set_id,'debug.raw'))=0 AND sum(EXTRACT_ARG(a.arg_set_id,'debug.sequence')!=EXTRACT_ARG(b.arg_set_id,'debug.sequence'))=0 AS ok FROM flow JOIN slice a ON a.id=flow.slice_out JOIN slice b ON b.id=flow.slice_in JOIN track t ON t.id=a.track_id WHERE t.name NOT GLOB 'core.*'"
assert_query "$(< "$script_dir/check-core-events.sql")"
assert_query "$(< "$script_dir/check-cache-events.sql")"
echo 'SimpleSoC memory/cache events and scalar pipeline ancestry, PCs, and timing passed'
