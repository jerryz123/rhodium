-- Checks selected fetch causes, including joined retry ancestry, and retained/live S2 instruction ownership.
-- SPDX-License-Identifier: Apache-2.0
WITH all_events AS MATERIALIZED (
  SELECT s.id, t.name, s.ts, s.arg_set_id, EXTRACT_ARG(s.arg_set_id,'debug.pc') AS pc
  FROM slice s JOIN rheg_tracks t ON t.id=s.track_id
  WHERE s.name!='stall'
), events AS MATERIALIZED (
  SELECT * FROM all_events WHERE name GLOB 'frontend/*' OR name='core/s2.decode'
), edges AS MATERIALIZED (
  SELECT a.id AS parent, b.id AS child, a.name AS src, b.name AS dst,
         a.pc AS parent_pc, b.pc AS child_pc, b.ts-a.ts AS delay,
         EXTRACT_ARG(a.arg_set_id,'debug.replay') AS replay,
         EXTRACT_ARG(a.arg_set_id,'debug.admitted') AS admitted
  FROM flow f JOIN all_events a ON a.id=f.slice_out JOIN events b ON b.id=f.slice_in
)
SELECT
  (SELECT count(DISTINCT name)=4 FROM events) AND
  (SELECT count(*)=0 FROM events WHERE pc IS NULL OR length(pc)!=18) AND
  -- Unknown markers identify only unmodeled incoming causes, not every S0 request.
  (SELECT count(*)=0 FROM events e WHERE name='frontend/s0.request' AND
    ((SELECT count(*) FROM edges WHERE child=e.id)>2 OR
     COALESCE(EXTRACT_ARG(arg_set_id,'debug.ancestry_unknown'),'false') !=
       CASE WHEN (SELECT count(*) FROM edges WHERE child=e.id)=0 THEN 'true' ELSE 'false' END)) AND
  -- A retry may carry MEM plus its paired LSU result instead of a non-fired WB.
  (SELECT count(*)=0 FROM events e WHERE name='frontend/s0.request' AND
    (SELECT count(*) FROM edges WHERE child=e.id)=2 AND
    ((SELECT count(*) FROM edges WHERE child=e.id AND src='core/s4.memory')!=1 OR
     (SELECT count(*) FROM edges WHERE child=e.id AND src IN ('core/s3.execute','dcache/s1.access'))!=1)) AND
  (SELECT count(*)=0 FROM edges result JOIN edges mem ON mem.child=result.child AND mem.src='core/s4.memory'
    WHERE result.dst='frontend/s0.request' AND result.src IN ('core/s3.execute','dcache/s1.access') AND
      NOT EXISTS (SELECT 1 FROM flow m JOIN all_events ex ON ex.id=m.slice_out
                  WHERE m.slice_in=mem.parent AND ex.name='core/s3.execute' AND
                    (ex.id=result.parent OR EXISTS (SELECT 1 FROM flow cache WHERE cache.slice_out=ex.id AND cache.slice_in=result.parent)))) AND
  (SELECT count(*)=0 FROM events WHERE name!='frontend/s0.request' AND
    COALESCE(EXTRACT_ARG(arg_set_id,'debug.ancestry_unknown'),'false')!='false') AND
  (SELECT count(*)>0 FROM edges WHERE src='frontend/s0.request' AND dst='frontend/s0.request') AND
  (SELECT count(*)>0 FROM edges WHERE src='frontend/s2.outcome' AND dst='frontend/s0.request') AND
  (SELECT count(*)>0 FROM edges WHERE src='core/s4.memory' AND dst='frontend/s0.request') AND
  (SELECT count(*)=0 FROM edges WHERE
    (src||'->'||dst) NOT IN ('frontend/s0.request->frontend/s1.lookup',
      'frontend/s1.lookup->frontend/s2.outcome','frontend/s2.outcome->core/s2.decode',
      'frontend/s0.request->frontend/s0.request','frontend/s2.outcome->frontend/s0.request',
      'core/s4.memory->frontend/s0.request','core/s3.execute->frontend/s0.request','dcache/s1.access->frontend/s0.request') OR
    (dst IN ('frontend/s1.lookup','frontend/s2.outcome') AND (delay!=10 OR parent_pc!=child_pc)) OR
    (dst='core/s2.decode' AND (delay<0 OR replay IS NOT 0 OR admitted IS NOT 1)) OR
    (dst='frontend/s0.request' AND (delay<0 OR
      (src='frontend/s0.request' AND delay<10) OR
      (src='frontend/s2.outcome' AND NOT (
        (replay IS 1 AND parent_pc=child_pc) OR
        (replay IS 0 AND admitted IS 1 AND delay>=10)))))) AND
  (SELECT count(*)=0 FROM events e WHERE name IN ('frontend/s1.lookup','frontend/s2.outcome')
    AND (SELECT count(*) FROM flow WHERE slice_in=e.id)!=1) AND
  (SELECT count(*)=0 FROM events e WHERE name='core/s2.decode'
    AND (SELECT count(*) FROM edges WHERE child=e.id) NOT BETWEEN 1 AND 2) AS ok
