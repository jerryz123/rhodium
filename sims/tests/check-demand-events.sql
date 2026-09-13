-- Checks four D-cache stages: core-aligned access/response and admitted lookup/resolution.
-- SPDX-License-Identifier: Apache-2.0
WITH events AS MATERIALIZED (
  SELECT s.id, s.ts, s.arg_set_id, t.name
  FROM slice s JOIN rheg_tracks t ON t.id=s.track_id
  WHERE s.name!='stall'
), edges AS MATERIALIZED (
  SELECT p.id AS parent, c.id AS child, p.name AS src, c.name AS dst,
         c.ts-p.ts AS delay, p.arg_set_id AS parent_args, c.arg_set_id AS child_args
  FROM flow JOIN events p ON p.id=flow.slice_out JOIN events c ON c.id=flow.slice_in
), responses AS MATERIALIZED (
  SELECT e.*, a.id AS access, e.ts-a.ts AS latency, a.arg_set_id AS access_args
  FROM events e
  JOIN edges wb ON wb.child=e.id AND wb.src='core/s5.wb'
  JOIN edges mem ON mem.child=wb.parent AND mem.src='core/s4.memory'
  JOIN edges access ON access.parent=mem.parent AND access.dst='dcache/s1.access'
  JOIN events a ON a.id=access.child
  WHERE e.name='dcache/s2.resp'
)
SELECT
  (SELECT count(DISTINCT name)=4 FROM events WHERE name GLOB 'dcache/s[1-4].*') AND
  (SELECT count(*)=0 FROM events e WHERE name GLOB 'dcache/s[1-4].*'
    AND (SELECT count(*) FROM edges WHERE child=e.id)!=1) AND
  (SELECT count(*)=0 FROM edges WHERE
    (dst='dcache/s1.access' AND (src!='core/s4.memory' OR delay!=0)) OR
    (dst='dcache/s2.resp' AND (src!='core/s5.wb' OR delay!=0)) OR
    (dst='dcache/s3.lookup' AND (src NOT IN ('dcache/s2.resp','mmu/pte.request','dcache/prefetch') OR delay<10)) OR
    (dst='dcache/s4.resolve' AND (src!='dcache/s3.lookup' OR delay!=10))) AND
  (SELECT count(*)=0 FROM edges WHERE dst IN ('dcache/s1.access','dcache/s2.resp') AND
    (EXTRACT_ARG(parent_args,'debug.pc')!=EXTRACT_ARG(child_args,'debug.pc') OR
     EXTRACT_ARG(parent_args,'debug.instruction')!=EXTRACT_ARG(child_args,'debug.instruction'))) AND
  (SELECT count(*) FROM responses)=(SELECT count(*) FROM events WHERE name='dcache/s2.resp') AND
  (SELECT count(*)=0 FROM responses WHERE latency!=10 OR
    EXTRACT_ARG(access_args,'debug.pc')!=EXTRACT_ARG(arg_set_id,'debug.pc') OR
    EXTRACT_ARG(access_args,'debug.address')!=EXTRACT_ARG(arg_set_id,'debug.address')) AND
  (SELECT count(*)>0 FROM responses WHERE EXTRACT_ARG(arg_set_id,'debug.admitted')=1) AND
  (SELECT count(*)=0 FROM responses WHERE EXTRACT_ARG(arg_set_id,'debug.admitted')=1 AND
    (EXTRACT_ARG(arg_set_id,'debug.fault')!=0 OR EXTRACT_ARG(arg_set_id,'debug.replay')!=0)) AND
  (SELECT count(*)>0 FROM edges WHERE src='dcache/s2.resp' AND dst='dcache/s3.lookup') AND
  (SELECT count(*)=0 FROM edges WHERE dst='dcache/s3.lookup' AND
    (EXTRACT_ARG(child_args,'debug.prefetch')!=(src='dcache/prefetch') OR
     (src='dcache/s2.resp' AND EXTRACT_ARG(parent_args,'debug.admitted')!=1))) AND
  (SELECT count(*)=0 FROM edges WHERE dst='dcache/s4.resolve' AND
    (EXTRACT_ARG(parent_args,'debug.address')!=EXTRACT_ARG(child_args,'debug.address') OR
     EXTRACT_ARG(parent_args,'debug.prefetch')!=EXTRACT_ARG(child_args,'debug.prefetch'))) AND
  (SELECT count(*)>0 FROM events WHERE name='dcache/s4.resolve' AND EXTRACT_ARG(arg_set_id,'debug.refill_accepted')=1) AND
  (SELECT count(*)>0 FROM edges WHERE src='dcache/s4.resolve' AND dst='dcache/chi.txreq') AND
  (SELECT count(*)=0 FROM edges WHERE dst='dcache/chi.txreq' AND
    (src!='dcache/s4.resolve' OR delay<10 OR EXTRACT_ARG(parent_args,'debug.refill_accepted')!=1 OR
     EXTRACT_ARG(parent_args,'debug.refill_opcode')!=EXTRACT_ARG(child_args,'debug.opcode') OR
     ltrim(substr(EXTRACT_ARG(parent_args,'debug.refill_address'),3),'0')!=ltrim(substr(EXTRACT_ARG(child_args,'debug.address'),3),'0'))) AND
  (SELECT count(*)=0 FROM events e WHERE name='dcache/chi.txreq' AND
    (SELECT count(*) FROM edges WHERE child=e.id)>1) AND
  (SELECT count(*)=0 FROM events e WHERE name='dcache/s4.resolve' AND EXTRACT_ARG(arg_set_id,'debug.refill_accepted')=1 AND
    NOT EXISTS (SELECT 1 FROM edges WHERE parent=e.id AND dst='dcache/chi.txreq')) AND
  (SELECT count(*)=0 FROM edges WHERE dst IN ('mmu/pte.request','dcache/prefetch')) AS ok
