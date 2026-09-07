-- Checks RV64 scalar stage ancestry, canonical packed PCs, and real pipeline latency.
WITH raw AS (
  SELECT id, name, ts, arg_set_id,
    CAST(EXTRACT_ARG(arg_set_id, 'debug.payload_width') AS INT)-64 AS pc_offset,
    EXTRACT_ARG(arg_set_id, 'debug.payload_words_lsw_first') AS words
  FROM slice WHERE name GLOB 'core.*'
), pcs AS (
  SELECT *,
    (CAST(json_extract(words, '$['||(pc_offset/32)||']') AS INT) >> (pc_offset%32)) |
    (CAST(json_extract(words, '$['||(pc_offset/32+1)||']') AS INT) << (32-pc_offset%32)) |
    CASE WHEN pc_offset%32=0 THEN 0 ELSE
      COALESCE(CAST(json_extract(words, '$['||(pc_offset/32+2)||']') AS INT),0) << (64-pc_offset%32)
    END AS pc
  FROM raw
), edges AS (
  SELECT a.id AS parent, b.id AS child, a.name AS src, b.name AS dst,
    a.pc AS parent_pc, b.pc AS child_pc, b.ts-a.ts AS delay
  FROM flow JOIN pcs a ON a.id=flow.slice_out JOIN pcs b ON b.id=flow.slice_in
)
SELECT
  (SELECT count(DISTINCT name)=5 FROM pcs) AND
  (SELECT count(*)=0 FROM pcs WHERE pc IS NULL OR pc_offset<0) AND
  (SELECT count(*)=0 FROM pcs WHERE name NOT IN ('core.fetch','core.decode','core.execute','core.memory','core.wb')) AND
  (SELECT count(DISTINCT src||'->'||dst)=4 FROM edges) AND
  (SELECT count(*)=0 FROM edges WHERE
    (src||'->'||dst) NOT IN ('core.fetch->core.decode','core.decode->core.execute','core.execute->core.memory','core.memory->core.wb') OR
    parent_pc!=child_pc OR delay<10 OR (src!='core.fetch' AND delay!=10)) AND
  (SELECT count(*)=0 FROM pcs c WHERE name!='core.fetch' AND (SELECT count(*) FROM edges WHERE child=c.id)!=1) AND
  (SELECT count(*)=0 FROM flow JOIN pcs c ON c.id=flow.slice_in WHERE c.name='core.fetch') AND
  (SELECT count(*)=0 FROM (SELECT parent FROM edges GROUP BY parent HAVING count(*)>1)) AND
  (SELECT count(*)>count(DISTINCT pc) FROM pcs WHERE name='core.fetch') AS ok
