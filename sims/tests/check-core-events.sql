-- Checks named PC/instruction captures, scalar stage ancestry, and real pipeline latency.
WITH pcs AS (
  SELECT id, name, ts, arg_set_id,
    EXTRACT_ARG(arg_set_id, 'debug.pc') AS pc,
    EXTRACT_ARG(arg_set_id, 'debug.instruction') AS instruction
  FROM slice WHERE name GLOB 'core.*'
), edges AS (
  SELECT a.id AS parent, b.id AS child, a.name AS src, b.name AS dst,
    a.pc AS parent_pc, b.pc AS child_pc, a.instruction AS parent_instruction,
    b.instruction AS child_instruction, b.ts-a.ts AS delay
  FROM flow JOIN pcs a ON a.id=flow.slice_out JOIN pcs b ON b.id=flow.slice_in
)
SELECT
  (SELECT count(DISTINCT name)=5 FROM pcs) AND
  (SELECT count(*)=0 FROM pcs WHERE pc IS NULL OR instruction IS NULL OR length(pc)!=18 OR length(instruction)!=10) AND
  (SELECT count(*)=0 FROM args WHERE key IN ('debug.payload_width','debug.payload_words_lsw_first')) AND
  (SELECT count(*)=0 FROM pcs WHERE name NOT IN ('core.fetch','core.decode','core.execute','core.memory','core.wb')) AND
  (SELECT count(DISTINCT src||'->'||dst)=4 FROM edges) AND
  (SELECT count(*)=0 FROM edges WHERE
    (src||'->'||dst) NOT IN ('core.fetch->core.decode','core.decode->core.execute','core.execute->core.memory','core.memory->core.wb') OR
    parent_pc!=child_pc OR parent_instruction!=child_instruction OR delay<10 OR (src!='core.fetch' AND delay!=10)) AND
  (SELECT count(*)=0 FROM pcs c WHERE name!='core.fetch' AND (SELECT count(*) FROM edges WHERE child=c.id)!=1) AND
  (SELECT count(*)=0 FROM flow JOIN pcs c ON c.id=flow.slice_in WHERE c.name='core.fetch') AND
  (SELECT count(*)=0 FROM (SELECT parent FROM edges GROUP BY parent HAVING count(*)>1)) AND
  (SELECT count(*)>count(DISTINCT pc) FROM pcs WHERE name='core.fetch') AS ok
