-- Checks retained Home ownership and the network round trip to D-cache return events.
-- SPDX-License-Identifier: Apache-2.0
WITH events AS MATERIALIZED (
  SELECT s.id, s.ts, s.arg_set_id, t.name, s.name AS opcode
  FROM slice s JOIN track t ON t.id=s.track_id WHERE s.name!='stall'
), edges AS MATERIALIZED (
  SELECT p.id AS parent, c.id AS child, p.name AS src, c.name AS dst,
         c.ts-p.ts AS delay, p.arg_set_id AS parent_args, c.arg_set_id AS child_args
  FROM flow f JOIN events p ON p.id=f.slice_out JOIN events c ON c.id=f.slice_in
)
SELECT
  (SELECT count(*)>0 FROM edges WHERE src='dcache.txreq' AND dst='home.request') AND
  (SELECT count(*)>0 FROM edges WHERE src='home.data' AND dst='dcache.rxdat') AND
  (SELECT count(*)=0 FROM events e WHERE name IN ('home.response','home.data','dcache.rxrsp','dcache.rxdat')
    AND (SELECT count(*) FROM edges WHERE child=e.id)!=1) AND
  (SELECT count(*)=0 FROM edges WHERE
    (dst IN ('home.response','home.data') AND (src!='home.request' OR delay<10 OR
      EXTRACT_ARG(parent_args,'debug.txn_id')!=EXTRACT_ARG(child_args,'debug.txn_id') OR
      EXTRACT_ARG(parent_args,'debug.src_id')!=EXTRACT_ARG(child_args,'debug.tgt_id'))) OR
    (dst IN ('dcache.rxrsp','dcache.rxdat') AND
      (src!=CASE dst WHEN 'dcache.rxrsp' THEN 'home.response' ELSE 'home.data' END OR delay<10 OR
       EXTRACT_ARG(parent_args,'debug.txn_id')!=EXTRACT_ARG(child_args,'debug.txn_id') OR
       EXTRACT_ARG(parent_args,'debug.tgt_id')!=EXTRACT_ARG(child_args,'debug.tgt_id') OR
       EXTRACT_ARG(parent_args,'debug.opcode')!=EXTRACT_ARG(child_args,'debug.opcode'))) OR
    (dst='home.request' AND src IN ('icache.txreq','dcache.txreq') AND (delay<10 OR
      EXTRACT_ARG(parent_args,'debug.address')!=EXTRACT_ARG(child_args,'debug.address') OR
      EXTRACT_ARG(parent_args,'debug.txn_id')!=EXTRACT_ARG(child_args,'debug.txn_id') OR
      EXTRACT_ARG(parent_args,'debug.src_id')!=EXTRACT_ARG(child_args,'debug.src_id')))) AND
  (SELECT count(*)=0 FROM edges WHERE dst='dcache.rxdat' AND
    EXTRACT_ARG(parent_args,'debug.data_id')!=EXTRACT_ARG(child_args,'debug.data_id')) AND
  (SELECT count(*)=0 FROM events d WHERE d.name='dcache.rxdat' AND NOT EXISTS (
    SELECT 1 FROM edges back JOIN edges owned ON owned.child=back.parent
      JOIN edges outward ON outward.child=owned.parent
    WHERE back.child=d.id AND outward.src='dcache.txreq')) AS ok
