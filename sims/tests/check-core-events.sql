-- Checks named PC/instruction captures, scalar stage ancestry, and real pipeline latency.
WITH pcs AS (
  SELECT s.id, t.name, s.name AS mnemonic, s.ts, s.arg_set_id,
    EXTRACT_ARG(s.arg_set_id, 'debug.pc') AS pc,
    EXTRACT_ARG(s.arg_set_id, 'debug.instruction') AS instruction
  FROM slice s JOIN track t ON t.id=s.track_id
  WHERE t.name GLOB 'core.*' AND json_extract(EXTRACT_ARG(t.source_arg_set_id,'description'),'$.kind')='transfer'
    AND s.name!='stall'
), edges AS (
  SELECT a.id AS parent, b.id AS child, a.name AS src, b.name AS dst,
    a.pc AS parent_pc, b.pc AS child_pc, a.instruction AS parent_instruction,
    b.instruction AS child_instruction, b.ts-a.ts AS delay
  FROM flow JOIN pcs a ON a.id=flow.slice_out JOIN pcs b ON b.id=flow.slice_in
)
SELECT
  (SELECT count(DISTINCT name)=5 FROM pcs) AND
  (SELECT count(*)=0 FROM pcs WHERE pc IS NULL OR instruction IS NULL OR length(pc)!=18 OR length(instruction)=0) AND
  (SELECT count(*)>0 FROM pcs WHERE instruction='csrr a0, mhartid') AND
  (SELECT count(*)=0 FROM pcs WHERE mnemonic!=substr(instruction||' ',1,instr(instruction||' ',' ')-1)) AND
  (SELECT count(*)=0 FROM args WHERE key IN ('debug.payload_width','debug.payload_words_lsw_first')) AND
  (SELECT count(*)=0 FROM pcs WHERE name NOT IN ('core.s1.fetch','core.s2.decode','core.s3.execute','core.s4.memory','core.s5.wb')) AND
  (SELECT count(DISTINCT src||'->'||dst)=4 FROM edges) AND
  (SELECT count(*)=0 FROM edges WHERE
    (src||'->'||dst) NOT IN ('core.s1.fetch->core.s2.decode','core.s2.decode->core.s3.execute','core.s3.execute->core.s4.memory','core.s4.memory->core.s5.wb') OR
    parent_pc!=child_pc OR parent_instruction!=child_instruction OR delay<10 OR (src!='core.s1.fetch' AND delay!=10)) AND
  (SELECT count(*)=0 FROM pcs c WHERE name!='core.s1.fetch' AND (SELECT count(*) FROM edges WHERE child=c.id)!=1) AND
  (SELECT count(*)=0 FROM flow JOIN pcs c ON c.id=flow.slice_in WHERE c.name='core.s1.fetch') AND
  (SELECT count(*)=0 FROM (SELECT parent FROM edges GROUP BY parent HAVING count(*)>1)) AND
  (SELECT count(*)>count(DISTINCT pc) FROM pcs WHERE name='core.s1.fetch') AS ok
