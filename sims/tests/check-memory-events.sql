-- Checks accepted external-memory transfers, named CHI captures, and memory-relative DAT directions.
-- SPDX-License-Identifier: Apache-2.0
WITH expected(name, fields) AS (
  VALUES
    ('memory/chi.req','address,opcode,txn_id,src_id,tgt_id,size_or_num_req'),
    ('memory/chi.rsp','opcode,txn_id,src_id,tgt_id,dbid_or_group_id,resp,resp_err'),
    ('memory/chi.rxdat','opcode,txn_id,src_id,tgt_id,dbid_or_mecid,data_id,resp,resp_err,byte_enable'),
    ('memory/chi.txdat','opcode,txn_id,src_id,tgt_id,dbid_or_mecid,data_id,resp,resp_err,byte_enable')
), channels AS MATERIALIZED (
  SELECT t.id, t.name, EXTRACT_ARG(t.source_arg_set_id,'description') AS schema
  FROM rheg_tracks t WHERE t.name GLOB 'memory/*'
), captures AS MATERIALIZED (
  SELECT c.id, json_extract(f.value,'$.name') AS name, f.value AS field
  FROM channels c, json_each(c.schema,'$.fields') f
), events AS MATERIALIZED (
  SELECT s.*, c.name AS channel FROM slice s JOIN channels c ON c.id=s.track_id
)
SELECT
  -- An idle channel may be absent, but each observed channel has one site.
  (SELECT count(*) BETWEEN 2 AND 4 AND count(*)=count(DISTINCT name) FROM channels) AND
  (SELECT count(*)=(SELECT count(*) FROM channels) FROM channels c JOIN expected e USING(name)
    WHERE json_extract(c.schema,'$.kind')='transfer'
      AND json_extract(c.schema,'$.site_id') GLOB 'SoCHarness/event:*'
      AND (SELECT group_concat(json_extract(f.value,'$.name')) FROM json_each(c.schema,'$.fields') f)=e.fields) AND
  (SELECT count(*)>0 FROM events WHERE channel='memory/chi.req') AND
  (SELECT count(*)>0 FROM events WHERE channel='memory/chi.txdat') AND
  (SELECT count(*)=0 FROM events WHERE dur!=10 OR depth!=0 OR ts!=10*CAST(EXTRACT_ARG(arg_set_id,'debug.cycle') AS INT)) AND
  (SELECT count(*)=(SELECT count(*) FROM channels) FROM captures
    WHERE name='opcode' AND json_extract(field,'$.encoding')='enum'
      AND json_extract(field,'$.label')=1 AND json_array_length(field,'$.symbols')>0) AND
  (SELECT count(*)=0 FROM events e WHERE NOT EXISTS (
    SELECT 1 FROM captures c, json_each(c.field,'$.symbols') symbol
    WHERE c.id=e.track_id AND c.name='opcode'
      AND json_extract(symbol.value,'$.name')=e.name
      AND EXTRACT_ARG(e.arg_set_id,'debug.opcode')=e.name)) AND
  (SELECT count(*)=0 FROM events e JOIN captures c ON c.id=e.track_id
    WHERE EXTRACT_ARG(e.arg_set_id,'debug.'||c.name) IS NULL) AND
  (SELECT count(*)=0 FROM events e JOIN args a USING(arg_set_id)
    WHERE a.key NOT IN ('debug.cycle','debug.sequence','debug.ancestry_unknown')
      AND NOT EXISTS (SELECT 1 FROM captures c WHERE c.id=e.track_id AND a.key='debug.'||c.name)) AND
  -- The concrete SingleCoreSoC memory endpoint is node 9; reads return CompData.
  (SELECT count(*)=0 FROM events WHERE
    EXTRACT_ARG(arg_set_id,CASE WHEN channel IN ('memory/chi.req','memory/chi.rxdat') THEN 'debug.tgt_id' ELSE 'debug.src_id' END)!=9
    OR EXTRACT_ARG(arg_set_id,'debug.resp_err')!=0
    OR (channel='memory/chi.txdat' AND name!='CompData')
    OR (channel='memory/chi.rxdat' AND name!='NonCopyBackWriteData')) AND
  -- No accepted channel transfer is duplicated at the same cycle.
  (SELECT count(*)=0 FROM (SELECT track_id,ts FROM events GROUP BY track_id,ts HAVING count(*)!=1)) AND
  -- Every Home-originated transfer retains its causing transaction, including
  -- host-originated transactions whose own earlier ancestry can be unknown.
  (SELECT count(*)=0 FROM events e WHERE channel IN ('memory/chi.req','memory/chi.rxdat') AND
    ((SELECT count(*) FROM flow WHERE slice_in=e.id)!=1 OR
     COALESCE(EXTRACT_ARG(e.arg_set_id,'debug.ancestry_unknown'),'false')!='false' OR
     NOT EXISTS (SELECT 1 FROM flow f JOIN slice p ON p.id=f.slice_out JOIN rheg_tracks t ON t.id=p.track_id
       WHERE f.slice_in=e.id AND t.name GLOB 'home/transaction*' AND p.ts<=e.ts))) AND
  (SELECT count(*)=0 FROM events e WHERE NOT EXISTS (SELECT 1 FROM flow WHERE slice_in=e.id)
    AND COALESCE(EXTRACT_ARG(e.arg_set_id,'debug.ancestry_unknown'),'false')!='true') AS ok
