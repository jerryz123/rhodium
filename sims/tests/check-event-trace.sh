#!/usr/bin/env bash
# Checks SimpleSoC memory traffic and scalar pipeline lineage with the native importer.
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
assert_query "SELECT count(*)>0 AND sum(name='memory-request')>0 AND sum(name='memory-accept')>0 AND sum(name NOT IN ('memory-request','memory-accept','memory-response','soc-response','core.fetch','core.decode','core.execute','core.memory','core.wb'))=0 AND sum(dur!=10)=0 AND max(depth)=0 AS ok FROM slice"
assert_query "SELECT count(*)>0 AND sum(t.name=s.name)=count(*) AS ok FROM slice s JOIN track t ON t.id=s.track_id"
assert_query "SELECT count(*)=0 AS ok FROM slice s JOIN thread_track t ON t.id=s.track_id"
assert_query "SELECT count(*)=0 AS ok FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')"
assert_query "SELECT count(*)>0 AND count(*)*2=(SELECT count(*) FROM slice WHERE name NOT GLOB 'core.*') AND sum(a.ts!=b.ts)=0 AND sum((a.name||'->'||b.name) NOT IN ('memory-request->memory-accept','memory-response->soc-response'))=0 AS ok FROM flow JOIN slice a ON a.id=flow.slice_out JOIN slice b ON b.id=flow.slice_in WHERE a.name NOT GLOB 'core.*'"
assert_query "SELECT count(*)>0 AND sum(ts!=10*CAST(EXTRACT_ARG(arg_set_id,'debug.cycle') AS INT))=0 AND sum(EXTRACT_ARG(arg_set_id,'debug.clock_frequency_hz')!='100000000')=0 AND sum(EXTRACT_ARG(arg_set_id,'debug.epoch_id')!='0')=0 AS ok FROM slice"
assert_query "SELECT count(*)>0 AND sum(EXTRACT_ARG(a.arg_set_id,'debug.payload_words_lsw_first')!=EXTRACT_ARG(b.arg_set_id,'debug.payload_words_lsw_first'))=0 AND sum(EXTRACT_ARG(a.arg_set_id,'debug.sequence')!=EXTRACT_ARG(b.arg_set_id,'debug.sequence'))=0 AS ok FROM flow JOIN slice a ON a.id=flow.slice_out JOIN slice b ON b.id=flow.slice_in WHERE a.name NOT GLOB 'core.*'"
assert_query "$(< "$script_dir/check-core-events.sql")"
echo 'SimpleSoC memory events and scalar pipeline ancestry, PCs, and timing passed'
