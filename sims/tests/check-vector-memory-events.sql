-- Checks unified vector sequencing, shared cache resolution, and exact memory-result ownership.
-- SPDX-License-Identifier: Apache-2.0
WITH events AS MATERIALIZED (
  SELECT s.id, s.ts, s.arg_set_id, t.name, t.id AS track_id
  FROM slice s JOIN rheg_tracks t ON t.id=s.track_id
  WHERE s.name!='stall'
), edges AS MATERIALIZED (
  SELECT p.id AS parent, c.id AS child, p.name AS src, c.name AS dst,
         c.ts-p.ts AS delay, p.arg_set_id AS parent_args, c.arg_set_id AS child_args
  FROM flow JOIN events p ON p.id=flow.slice_out JOIN events c ON c.id=flow.slice_in
), results AS MATERIALIZED (
  SELECT * FROM events WHERE name='vector/memory.result'
)
SELECT
  (SELECT count(*)>0 FROM edges WHERE src='vector/s1.sequence' AND dst='vector/s2.issue') AND
  (SELECT count(*)=0 FROM events e WHERE name='vector/s2.issue' AND
    ((SELECT count(*) FROM edges WHERE child=e.id AND src='vector/s1.sequence' AND delay=10)!=1 OR
     (SELECT count(*) FROM edges WHERE child=e.id)!=1 OR
     COALESCE(EXTRACT_ARG(arg_set_id,'debug.ancestry_unknown'),'false')!='false')) AND
  (SELECT count(*)=0 FROM edges WHERE src='vector/s1.sequence' AND dst='vector/s2.issue' AND
    (EXTRACT_ARG(parent_args,'debug.packed') IS NOT EXTRACT_ARG(child_args,'debug.packed') OR
     EXTRACT_ARG(parent_args,'debug.op_index') IS NOT EXTRACT_ARG(child_args,'debug.op_index'))) AND
  (SELECT count(DISTINCT track_id)=1 FROM events WHERE name='dcache/s1.access') AND
  (SELECT count(*)>0 FROM edges WHERE src='core/s3.execute' AND dst='dcache/s1.access') AND
  (SELECT count(*)>0 FROM edges WHERE src='vector/s2.issue' AND dst='dcache/s1.access') AND
  (SELECT count(*)=0 FROM edges WHERE dst='dcache/s1.access' AND
    NOT ((src='core/s3.execute' AND delay=10) OR (src='vector/s2.issue' AND delay>=20))) AND
  (SELECT count(*)=0 FROM events e WHERE name='dcache/s1.access' AND
    ((SELECT count(*) FROM edges WHERE child=e.id)!=1 OR
     COALESCE(EXTRACT_ARG(arg_set_id,'debug.ancestry_unknown'),'false')!='false')) AND
  (SELECT count(*)>0 FROM results WHERE EXTRACT_ARG(arg_set_id,'debug.outcome')='LoadHit') AND
  (SELECT count(*)>0 FROM results WHERE EXTRACT_ARG(arg_set_id,'debug.outcome')='StoreHit') AND
  (SELECT count(*)=0 FROM results r WHERE
    (SELECT count(*) FROM edges WHERE child=r.id AND src='vector/s2.issue' AND delay>=30)!=1 OR
    COALESCE(EXTRACT_ARG(arg_set_id,'debug.ancestry_unknown'),'false')!='false') AND
  (SELECT count(*)=0 FROM edges WHERE dst='vector/memory.result' AND
    NOT ((src='vector/s2.issue' AND delay>=30) OR (src='dcache/s1.access' AND delay=10))) AND
  (SELECT count(*)=0 FROM results r WHERE EXTRACT_ARG(arg_set_id,'debug.outcome') IN ('LoadHit','StoreHit') AND
    (SELECT count(*) FROM edges WHERE child=r.id AND src='dcache/s1.access')!=1) AND
  -- Retained requests may retry after the minimum pipeline latency without
  -- resequencing. The cache and caller must name the same issue occurrence, even on retries
  -- with identical PC/address/slot captures. Payload equality cannot establish this.
  (SELECT count(*)=0 FROM edges cache JOIN edges issue ON issue.child=cache.parent
    WHERE cache.dst='vector/memory.result' AND cache.src='dcache/s1.access' AND
      NOT EXISTS (SELECT 1 FROM edges direct WHERE direct.child=cache.child AND direct.parent=issue.parent)) AND
  (SELECT count(*)>0 FROM edges WHERE src='vector/memory.result' AND dst='dcache/s3.lookup') AND
  (SELECT count(*)=0 FROM edges WHERE src='vector/memory.result' AND dst='dcache/s3.lookup' AND
    (delay<10 OR EXTRACT_ARG(parent_args,'debug.admitted')!=1)) AND
  (SELECT count(*)=0 FROM results WHERE EXTRACT_ARG(arg_set_id,'debug.admitted')=1 AND
    (EXTRACT_ARG(arg_set_id,'debug.fault')!=0 OR EXTRACT_ARG(arg_set_id,'debug.replay')!=0)) AS ok
