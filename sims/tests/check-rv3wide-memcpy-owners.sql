-- Checks dynamic instruction and data parents in a distributed vec-memcpy trace.
-- SPDX-License-Identifier: Apache-2.0
WITH events AS MATERIALIZED (
  SELECT s.id, s.ts, t.name, extract_arg(s.arg_set_id,'debug.pc') pc,
         extract_arg(s.arg_set_id,'debug.owner') owner,
         coalesce(extract_arg(s.arg_set_id,'debug.ancestry_unknown'),'false') unknown
  FROM slice s JOIN rheg_tracks t ON t.id=s.track_id WHERE s.name!='stall'
), roots AS (
  SELECT *, lag(owner) OVER (PARTITION BY name,pc ORDER BY ts) previous
  FROM events WHERE name IN ('vector/load.agen','vector/store.sequence')
), beats AS (
  -- Each of these two in-order cursors changes owner for each new copy-loop
  -- instruction. Number its runs independently of both the WB graph and PC reuse.
  SELECT *, sum(previous IS NOT owner) OVER (PARTITION BY name,pc ORDER BY ts) occurrence
  FROM roots
), wb AS (
  SELECT *, row_number() OVER (PARTITION BY pc ORDER BY ts) occurrence
  FROM events WHERE name GLOB 'core/s4.wb.slot*'
), parents AS MATERIALIZED (
  SELECT f.slice_in child, e.* FROM flow f JOIN events e ON e.id=f.slice_out
)
SELECT
  (SELECT count(DISTINCT name)=2 FROM beats) AND
  NOT EXISTS (SELECT 1 FROM beats GROUP BY name,pc HAVING max(occurrence)<=count(DISTINCT owner)) AND
  NOT EXISTS (
    SELECT 1 FROM beats b LEFT JOIN wb w ON w.pc=b.pc AND w.occurrence=b.occurrence
    WHERE b.unknown!='false' OR w.id IS NULL OR w.ts>b.ts OR
          (SELECT count(*) FROM parents p WHERE p.child=b.id)!=1 OR
          NOT EXISTS (SELECT 1 FROM parents p WHERE p.child=b.id AND p.id=w.id)
  ) AND NOT EXISTS (
    SELECT 1 FROM events e WHERE
      (e.name='vector/store.agen' AND NOT EXISTS (
        SELECT 1 FROM parents p WHERE p.child=e.id AND p.name='vector/store.sequence' AND p.owner=e.owner
      )) OR (e.name='vector/load.return' AND (
        NOT EXISTS (SELECT 1 FROM parents p WHERE p.child=e.id AND p.name='vector/load.agen' AND p.owner=e.owner) OR
        NOT EXISTS (SELECT 1 FROM parents p WHERE p.child=e.id AND p.name IN ('dcache/s4.resolve','dcache/refill.data'))
      )) OR (e.name='vector/load.sequence' AND NOT EXISTS (
        SELECT 1 FROM parents p WHERE p.child=e.id AND p.name='vector/load.return' AND p.owner=e.owner
      ))
  ) AS ok;
