-- Checks direct request ancestry across the unannotated Home and network return path.
-- SPDX-License-Identifier: Apache-2.0
WITH events AS MATERIALIZED (
  SELECT s.id, s.ts, s.arg_set_id, t.name, s.name AS opcode
  FROM slice s JOIN rheg_tracks t ON t.id=s.track_id WHERE s.name!='stall'
), edges AS MATERIALIZED (
  SELECT p.id AS parent, c.id AS child, p.name AS src, c.name AS dst,
         c.ts-p.ts AS delay, p.arg_set_id AS parent_args, c.arg_set_id AS child_args
  FROM flow f JOIN events p ON p.id=f.slice_out JOIN events c ON c.id=f.slice_in
)
SELECT
  (SELECT count(*)>0 FROM edges WHERE src='dcache/chi.txreq' AND dst='dcache/chi.rxdat') AND
  -- Opaque branches may lack parents, but must explicitly report that gap.
  (SELECT count(*)=0 FROM events e WHERE name IN ('dcache/chi.rxrsp','dcache/chi.rxdat')
    AND ((SELECT count(*) FROM edges WHERE child=e.id)>1 OR
      ((SELECT count(*) FROM edges WHERE child=e.id)=0 AND
       COALESCE(EXTRACT_ARG(e.arg_set_id,'debug.ancestry_unknown'),'false')!='true'))) AND
  (SELECT count(*)=0 FROM edges WHERE
    (dst IN ('dcache/chi.rxrsp','dcache/chi.rxdat') AND
      (src!='dcache/chi.txreq' OR delay<10 OR
       EXTRACT_ARG(parent_args,'debug.txn_id')!=EXTRACT_ARG(child_args,'debug.txn_id') OR
       EXTRACT_ARG(parent_args,'debug.src_id')!=EXTRACT_ARG(child_args,'debug.tgt_id')))) AS ok
