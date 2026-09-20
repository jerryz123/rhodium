-- Checks direct S1-to-S2 ownership, sibling WB alignment, and admitted cache stages.
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
  SELECT e.*, a.id AS access, e.ts-a.ts AS latency, a.arg_set_id AS access_args,
         w.ts AS wb_ts, w.arg_set_id AS wb_args
  FROM events e
  JOIN edges access ON access.child=e.id AND access.src='dcache/s1.access'
  JOIN edges mem ON mem.child=access.parent AND mem.src='core/s4.memory'
  JOIN edges wb ON wb.parent=mem.parent AND wb.dst='core/s5.wb'
  JOIN events w ON w.id=wb.child
  JOIN events a ON a.id=access.parent
  WHERE e.name='dcache/s2.resp'
)
SELECT
  (SELECT count(DISTINCT name)=4 FROM events WHERE name GLOB 'dcache/s[1-4].*') AND
  (SELECT count(*)=0 FROM events e WHERE name GLOB 'dcache/s[1-4].*'
    AND (SELECT count(*) FROM edges WHERE child=e.id)!=1) AND
  (SELECT count(*)=0 FROM edges WHERE
    (dst='dcache/s1.access' AND (src!='core/s4.memory' OR delay!=0)) OR
    (dst='dcache/s2.resp' AND (src!='dcache/s1.access' OR delay!=10)) OR
    (dst='dcache/s3.lookup' AND (src NOT IN ('dcache/s2.resp','mmu/pte.request','dcache/prefetch') OR delay<10)) OR
    (dst='dcache/s4.resolve' AND (src!='dcache/s3.lookup' OR delay!=10))) AND
  (SELECT count(*)=0 FROM edges WHERE dst IN ('dcache/s1.access','dcache/s2.resp') AND
    (EXTRACT_ARG(parent_args,'debug.pc')!=EXTRACT_ARG(child_args,'debug.pc') OR
     EXTRACT_ARG(parent_args,'debug.instruction')!=EXTRACT_ARG(child_args,'debug.instruction'))) AND
  (SELECT count(*) FROM responses)=(SELECT count(*) FROM events WHERE name='dcache/s2.resp') AND
  (SELECT count(*)=0 FROM responses WHERE latency!=10 OR
    wb_ts!=ts OR
    EXTRACT_ARG(wb_args,'debug.pc')!=EXTRACT_ARG(arg_set_id,'debug.pc') OR
    EXTRACT_ARG(wb_args,'debug.instruction')!=EXTRACT_ARG(arg_set_id,'debug.instruction') OR
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
  (SELECT count(*)>0 FROM edges WHERE src='dcache/s4.resolve' AND dst='dcache/refill') AND
  (SELECT count(*)=0 FROM edges WHERE src='dcache/s4.resolve' AND dst='dcache/refill' AND
    (delay!=0 OR EXTRACT_ARG(parent_args,'debug.refill_accepted')!=1 OR
     EXTRACT_ARG(parent_args,'debug.refill_opcode')!=EXTRACT_ARG(child_args,'debug.opcode') OR
     ltrim(substr(EXTRACT_ARG(parent_args,'debug.refill_address'),3),'0')!=ltrim(substr(EXTRACT_ARG(child_args,'debug.address'),3),'0'))) AND
  (SELECT count(*)=0 FROM edges WHERE dst='dcache/chi.txreq' AND
    (src NOT IN ('dcache/refill','dcache/writeback') OR delay<10 OR
     (src='dcache/refill' AND EXTRACT_ARG(parent_args,'debug.opcode')!=EXTRACT_ARG(child_args,'debug.opcode')) OR
     (src='dcache/writeback' AND EXTRACT_ARG(child_args,'debug.opcode')!=27) OR
     ltrim(substr(EXTRACT_ARG(parent_args,'debug.address'),3),'0')!=ltrim(substr(EXTRACT_ARG(child_args,'debug.address'),3),'0'))) AND
  (SELECT count(*)=0 FROM events e WHERE name='dcache/chi.txreq' AND
    ((SELECT count(*) FROM edges WHERE child=e.id)>1 OR
     ((SELECT count(*) FROM edges WHERE child=e.id)=0 AND
      COALESCE(EXTRACT_ARG(arg_set_id,'debug.ancestry_unknown'),'false')!='true') OR
     ((SELECT count(*) FROM edges WHERE child=e.id)=1 AND
      COALESCE(EXTRACT_ARG(arg_set_id,'debug.ancestry_unknown'),'false')!='false'))) AND
  (SELECT count(*)=0 FROM events e WHERE name='dcache/s4.resolve' AND EXTRACT_ARG(arg_set_id,'debug.refill_accepted')=1 AND
    NOT EXISTS (SELECT 1 FROM edges r JOIN edges tx ON tx.parent=r.child
                WHERE r.parent=e.id AND r.dst='dcache/refill' AND tx.dst='dcache/chi.txreq')) AND
  (SELECT count(*)=0 FROM edges WHERE dst='dcache/prefetch' OR
    (dst='mmu/pte.request' AND (src!='mmu/walk' OR delay<10))) AS ok
