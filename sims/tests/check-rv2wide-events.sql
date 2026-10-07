-- Validates slot identity, packet ownership, fixed stage timing, and deferred writeback ancestry.
-- SPDX-License-Identifier: Apache-2.0
WITH events AS (
  SELECT s.id, t.name, s.ts, s.arg_set_id,
         EXTRACT_ARG(s.arg_set_id,'debug.pc') AS pc,
         EXTRACT_ARG(s.arg_set_id,'debug.instruction') AS instruction,
         EXTRACT_ARG(s.arg_set_id,'debug.rd') AS rd,
         CAST(EXTRACT_ARG(s.arg_set_id,'debug.cycle') AS INT) AS cycle
  FROM slice s JOIN rheg_tracks t ON t.id=s.track_id
  WHERE s.name!='stall' AND json_extract(EXTRACT_ARG(t.source_arg_set_id,'description'),'$.kind')='transfer'
), edges AS (
  SELECT a.id AS parent, b.id AS child, a.name AS src, b.name AS dst,
         a.pc AS parent_pc, b.pc AS child_pc,
         a.instruction AS parent_instruction, b.instruction AS child_instruction,
         a.rd AS parent_rd, b.rd AS child_rd, b.cycle-a.cycle AS delay
  FROM flow JOIN events a ON a.id=flow.slice_out JOIN events b ON b.id=flow.slice_in
), pipeline AS (
  SELECT * FROM events WHERE name GLOB 'core/*.slot[01]'
), deferred AS (
  SELECT * FROM events WHERE name='core/s4.wb.deferred'
)
SELECT
  (SELECT count(*)=0 FROM stats WHERE value!=0 AND (severity='error' OR name='track_event_parser_errors' OR name GLOB 'flow_*')) AND
  (SELECT count(DISTINCT name)=8 FROM pipeline) AND
  (SELECT count(*)=0 FROM pipeline WHERE name NOT IN (
    'core/s1.rr.slot0','core/s1.rr.slot1','core/s2.ex.slot0','core/s2.ex.slot1',
    'core/s3.mem.slot0','core/s3.mem.slot1','core/s4.wb.slot0','core/s4.wb.slot1')) AND
  (SELECT count(*)=0 FROM pipeline WHERE pc IS NULL OR instruction IS NULL) AND
  (SELECT count(*)>0 FROM pipeline a JOIN pipeline b ON a.cycle=b.cycle WHERE a.name='core/s1.rr.slot0' AND b.name='core/s1.rr.slot1') AND
  (SELECT count(*)=0 FROM pipeline p WHERE (SELECT count(*) FROM edges e WHERE e.child=p.id)!=1) AND
  (SELECT count(*)=0 FROM edges e WHERE e.dst GLOB 'core/s1.rr.slot[01]' AND
    (e.src!='frontend/packet' OR NOT EXISTS (
      SELECT 1 FROM events p WHERE p.id=e.parent AND
        (EXTRACT_ARG(p.arg_set_id,'debug.pc0')=e.child_pc OR
         (EXTRACT_ARG(p.arg_set_id,'debug.count')=2 AND EXTRACT_ARG(p.arg_set_id,'debug.pc1')=e.child_pc))))) AND
  (SELECT count(*)=0 FROM edges WHERE dst GLOB 'core/s2.ex.slot[01]' AND
    (src!=replace(dst,'/s2.ex.','/s1.rr.') OR delay!=1 OR parent_pc!=child_pc OR parent_instruction!=child_instruction)) AND
  (SELECT count(*)=0 FROM edges WHERE dst GLOB 'core/s3.mem.slot[01]' AND
    (src!=replace(dst,'/s3.mem.','/s2.ex.') OR delay!=1 OR parent_pc!=child_pc OR parent_instruction!=child_instruction)) AND
  (SELECT count(*)=0 FROM edges WHERE dst GLOB 'core/s4.wb.slot[01]' AND
    (src NOT IN ('core/s3.mem.slot0','core/s3.mem.slot1') OR delay<1 OR parent_pc!=child_pc OR parent_instruction!=child_instruction)) AND
  (SELECT count(*)>0 FROM deferred) AND
  (SELECT count(*)>0 FROM deferred WHERE instruction GLOB 'l[bhwd]*') AND
  (SELECT count(*)>0 FROM deferred WHERE instruction GLOB 'mul*') AND
  (SELECT count(*)>0 FROM deferred WHERE instruction GLOB 'div*' OR instruction GLOB 'rem*') AND
  (SELECT count(*)=0 FROM deferred WHERE pc IS NULL OR instruction IS NULL OR rd IS NULL) AND
  (SELECT count(*)=0 FROM deferred p WHERE (SELECT count(*) FROM edges e WHERE e.child=p.id)!=1) AND
  (SELECT count(*)=0 FROM edges WHERE dst='core/s4.wb.deferred' AND
    (parent_pc!=child_pc OR parent_instruction!=child_instruction OR
     src NOT IN ('core/s2.ex.slot0','core/s2.ex.slot1','core/s4.wb.slot0','core/s4.wb.slot1') OR
     (src GLOB 'core/s2.ex.slot[01]' AND (child_instruction NOT GLOB 'mul*' OR delay<8)) OR
     (src GLOB 'core/s4.wb.slot[01]' AND (child_instruction GLOB 'mul*' OR delay<4 OR parent_rd!=child_rd)))) AND
  (SELECT count(*)=0 FROM (SELECT parent,dst FROM edges WHERE dst GLOB 'core/*.slot[01]' AND dst NOT GLOB 'core/s1.rr.slot[01]' GROUP BY parent,dst HAVING count(*)>1)) AND
  (SELECT count(*)=0 FROM pipeline WHERE COALESCE(EXTRACT_ARG(arg_set_id,'debug.ancestry_unknown'),0)!=0) AND
  (SELECT count(*)=0 FROM events p WHERE name IN ('frontend/s1.lookup','frontend/s2.outcome') AND
    (SELECT count(*) FROM edges e WHERE e.child=p.id)!=1) AND
  (SELECT count(*)=0 FROM edges WHERE dst IN ('frontend/s1.lookup','frontend/s2.outcome') AND
    (src!=CASE dst WHEN 'frontend/s1.lookup' THEN 'frontend/s0.request' ELSE 'frontend/s1.lookup' END OR delay!=1 OR parent_pc!=child_pc)) AND
  (SELECT count(*)=0 FROM events p WHERE name='frontend/packet' AND (SELECT count(*) FROM edges e WHERE e.child=p.id) NOT BETWEEN 1 AND 2) AND
  (SELECT count(*)=0 FROM edges WHERE dst='frontend/packet' AND src!='frontend/s2.outcome') AS ok;
