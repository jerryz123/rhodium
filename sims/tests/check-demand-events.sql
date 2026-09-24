-- Checks sibling retirement/result ownership from MEM, shared cache capture, and admitted stages.
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
  SELECT e.*, mem.parent AS mem_parent,
    (EXTRACT_ARG(e.arg_set_id,'debug.instruction') GLOB 'cbo.clean *' OR
     EXTRACT_ARG(e.arg_set_id,'debug.instruction') GLOB 'cbo.inval *' OR
     EXTRACT_ARG(e.arg_set_id,'debug.instruction') GLOB 'cbo.flush *') AS retained
  FROM events e
  JOIN edges mem ON mem.child=e.id AND mem.src='core/s4.memory'
  WHERE e.name='dcache/s2.resp'
)
SELECT
  (SELECT count(DISTINCT name)=4 FROM events WHERE name GLOB 'dcache/s[1-4].*') AND
  (SELECT count(*)=0 FROM events e WHERE name IN ('dcache/s1.access','dcache/s3.lookup','dcache/s4.resolve')
    AND (SELECT count(*) FROM edges WHERE child=e.id)!=1) AND
  (SELECT count(*)=0 FROM edges WHERE
    (dst='dcache/s1.access' AND (src!='core/s3.execute' OR delay!=10)) OR
    (dst='dcache/s2.resp' AND NOT ((src IN ('core/s4.memory','dcache/s1.access') AND delay=10) OR (src='core/s3.execute' AND delay=20))) OR
    (src='dcache/s1.access' AND (dst NOT IN ('core/s5.wb','dcache/s2.resp','frontend/s0.request') OR delay!=10)) OR
    (dst='dcache/s3.lookup' AND (src NOT IN ('dcache/s2.resp','mmu/pte.request','dcache/prefetch') OR delay<10)) OR
    (dst='dcache/s4.resolve' AND (src!='dcache/s3.lookup' OR delay!=10))) AND
  (SELECT count(*)=0 FROM edges WHERE dst='dcache/s2.resp' AND src GLOB 'core/*' AND
    (EXTRACT_ARG(parent_args,'debug.pc')!=EXTRACT_ARG(child_args,'debug.pc') OR
     EXTRACT_ARG(parent_args,'debug.instruction')!=EXTRACT_ARG(child_args,'debug.instruction'))) AND
  (SELECT count(*) FROM responses)=(SELECT count(*) FROM events WHERE name='dcache/s2.resp') AND
  -- Retirement and response are siblings of the exact same MEM occurrence.
  (SELECT count(*)=0 FROM responses r WHERE NOT retained AND
    (SELECT count(*) FROM edges wb WHERE wb.parent=r.mem_parent AND wb.dst='core/s5.wb') !=
      CASE WHEN EXTRACT_ARG(arg_set_id,'debug.fault')=1 OR EXTRACT_ARG(arg_set_id,'debug.replay')=1 THEN 0 ELSE 1 END) AND
  (SELECT count(*)=0 FROM responses r JOIN edges wb ON wb.parent=r.mem_parent AND wb.dst='core/s5.wb'
    JOIN events w ON w.id=wb.child WHERE EXTRACT_ARG(r.arg_set_id,'debug.fault')=1 OR
      EXTRACT_ARG(r.arg_set_id,'debug.replay')=1 OR w.ts<r.ts OR (NOT retained AND w.ts!=r.ts) OR
      EXTRACT_ARG(w.arg_set_id,'debug.pc')!=EXTRACT_ARG(r.arg_set_id,'debug.pc') OR
      EXTRACT_ARG(w.arg_set_id,'debug.instruction')!=EXTRACT_ARG(r.arg_set_id,'debug.instruction')) AND
  (SELECT count(*)=0 FROM responses r WHERE EXTRACT_ARG(arg_set_id,'debug.outcome') IN (1,2) AND
    NOT EXISTS (SELECT 1 FROM edges cache WHERE cache.child=r.id AND cache.src='dcache/s1.access')) AND
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
  -- Perfetto projects residency admission one cycle later, unlike transfers.
  (SELECT count(*)=0 FROM edges WHERE src='dcache/s4.resolve' AND dst='dcache/refill' AND
    (delay!=10 OR EXTRACT_ARG(parent_args,'debug.refill_accepted')!=1 OR
     EXTRACT_ARG(parent_args,'debug.refill_opcode')!=EXTRACT_ARG(child_args,'debug.opcode') OR
     ltrim(substr(EXTRACT_ARG(parent_args,'debug.refill_address'),3),'0')!=ltrim(substr(EXTRACT_ARG(child_args,'debug.address'),3),'0'))) AND
  (SELECT count(*)=0 FROM edges WHERE dst='dcache/chi.txreq' AND
    (src NOT IN ('dcache/refill','dcache/writeback') OR delay<0 OR
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
