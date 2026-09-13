-- Checks fetch-attempt stages and retained/live S2-to-instruction ancestry in the native importer.
-- SPDX-License-Identifier: Apache-2.0
WITH events AS MATERIALIZED (
  SELECT s.id, t.name, s.ts, s.arg_set_id, EXTRACT_ARG(s.arg_set_id,'debug.pc') AS pc
  FROM slice s JOIN rheg_tracks t ON t.id=s.track_id
  WHERE (t.name GLOB 'frontend/*' OR t.name='core/s2.decode') AND s.name!='stall'
), edges AS MATERIALIZED (
  SELECT a.id AS parent, b.id AS child, a.name AS src, b.name AS dst,
         a.pc AS parent_pc, b.pc AS child_pc, b.ts-a.ts AS delay,
         EXTRACT_ARG(a.arg_set_id,'debug.replay') AS replay,
         EXTRACT_ARG(a.arg_set_id,'debug.admitted') AS admitted
  FROM flow f JOIN events a ON a.id=f.slice_out JOIN events b ON b.id=f.slice_in
)
SELECT
  (SELECT count(DISTINCT name)=4 FROM events) AND
  (SELECT count(*)=0 FROM events WHERE pc IS NULL OR length(pc)!=18) AND
  -- The unmodeled PC source is unknown; its checkpoints supply definite parents.
  (SELECT count(*)=0 FROM events WHERE
    COALESCE(EXTRACT_ARG(arg_set_id,'debug.ancestry_unknown'),'false') !=
      CASE WHEN name='frontend/s0.request' THEN 'true' ELSE 'false' END) AND
  (SELECT count(DISTINCT src||'->'||dst)=3 FROM edges) AND
  (SELECT count(*)=0 FROM edges WHERE
    (src||'->'||dst) NOT IN ('frontend/s0.request->frontend/s1.lookup',
      'frontend/s1.lookup->frontend/s2.outcome','frontend/s2.outcome->core/s2.decode') OR
    (src IN ('frontend/s0.request','frontend/s1.lookup') AND (delay!=10 OR parent_pc!=child_pc)) OR
    (src='frontend/s2.outcome' AND (delay<0 OR replay IS NOT 0 OR admitted IS NOT 1))) AND
  (SELECT count(*)=0 FROM events e WHERE name IN ('frontend/s1.lookup','frontend/s2.outcome')
    AND (SELECT count(*) FROM flow WHERE slice_in=e.id)!=1) AND
  (SELECT count(*)=0 FROM events e WHERE name='core/s2.decode'
    AND (SELECT count(*) FROM edges WHERE child=e.id) NOT BETWEEN 1 AND 2) AND
  (SELECT count(*)=0 FROM flow f JOIN events e ON e.id=f.slice_in WHERE e.name='frontend/s0.request') AS ok
