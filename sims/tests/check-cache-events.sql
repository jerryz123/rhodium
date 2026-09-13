-- Checks private-cache CHI schemas, direct-refill requests, and certified Home returns.
-- SPDX-License-Identifier: Apache-2.0
-- Materialize shared views so per-field checks also scale to full benchmark traces.
WITH expected(suffix, fields) AS (
  VALUES
    ('txreq','address,opcode,txn_id,src_id,tgt_id,size_or_num_req,allow_retry,exp_comp_ack'),
    ('txrsp','opcode,txn_id,src_id,tgt_id,dbid_or_group_id,resp,resp_err,pcrd_type'),
    ('rxrsp','opcode,txn_id,src_id,tgt_id,dbid_or_group_id,resp,resp_err,pcrd_type'),
    ('txdat','opcode,txn_id,src_id,tgt_id,dbid_or_mecid,data_id,resp,resp_err,byte_enable'),
    ('rxdat','opcode,txn_id,src_id,tgt_id,dbid_or_mecid,data_id,resp,resp_err,byte_enable'),
    ('rxsnp','address,opcode,txn_id,src_id,ret_to_src')
), cache_tracks AS MATERIALIZED (
  SELECT t.id, t.name, a.string_value AS schema,
         json_extract(a.string_value,'$.kind') AS kind,
         t.name AS channel
  FROM track t JOIN args a ON a.arg_set_id=t.source_arg_set_id
  WHERE t.name GLOB '[id]cache.*' AND substr(t.name,8) IN (SELECT suffix FROM expected) AND a.key='description'
), captures AS MATERIALIZED (
  SELECT t.id AS track_id, json_extract(f.value,'$.name') AS name,
         json_extract(f.value,'$.encoding') AS encoding,
         COALESCE(json_extract(f.value,'$.label'),0) AS label,
         json_extract(f.value,'$.symbols') AS symbols,
         json_extract(f.value,'$.width') AS width
  FROM cache_tracks t, json_each(t.schema,'$.fields') f
), enum_names AS MATERIALIZED (
  SELECT c.track_id, CAST(json_extract(s.value,'$.value') AS INT) AS opcode,
         json_extract(s.value,'$.name') AS name
  FROM captures c, json_each(c.symbols) s WHERE c.label=1
), events AS MATERIALIZED (
  SELECT s.id, s.arg_set_id, s.track_id, t.channel,
         CASE s.name WHEN 'stall' THEN 'stall' ELSE 'transfer' END AS kind,
         s.ts, s.name
  FROM slice s JOIN cache_tracks t ON t.id=s.track_id
)
SELECT
  -- An idle channel need not materialize a track in the importer.
  (SELECT count(*) BETWEEN 4 AND 11 AND count(DISTINCT name)=count(*) FROM cache_tracks) AND
  (SELECT count(*)=(SELECT count(*) FROM cache_tracks) FROM cache_tracks t JOIN expected e ON substr(t.channel,8)=e.suffix
   WHERE json_extract(t.schema,'$.site_id') GLOB 'SoCHarness/soc/rv5stage/event:*'
     AND length(json_extract(t.schema,'$.source_location'))>0
     AND t.kind='transfer'
     AND json_array_length(t.schema,'$.observations')=1
     AND json_extract(t.schema,'$.observations[0].kind')='stall'
     AND json_extract(t.schema,'$.observations[0].observation_of')=json_extract(t.schema,'$.site_id')
     AND json_extract(t.schema,'$.observations[0].fields')=json_extract(t.schema,'$.fields')
     AND (SELECT group_concat(json_extract(f.value,'$.name')) FROM json_each(t.schema,'$.fields') f)=e.fields) AND
  (SELECT count(*)>0 FROM events WHERE channel='icache.txreq' AND kind='transfer') AND
  (SELECT count(*)>0 FROM events WHERE channel='icache.rxdat' AND kind='transfer') AND
  (SELECT count(*)>0 FROM events WHERE channel='dcache.txreq' AND kind='transfer') AND
  (SELECT count(*)>0 FROM events WHERE channel='dcache.rxdat' AND kind='transfer') AND
  (SELECT count(*)=(SELECT count(*) FROM cache_tracks) FROM captures
   WHERE name='opcode' AND encoding='enum' AND label=1 AND json_array_length(symbols)>0) AND
  (SELECT count(*)=0 FROM captures WHERE label=1 AND name!='opcode') AND
  (SELECT count(*)=0 FROM events e JOIN captures c ON c.track_id=e.track_id AND c.label=1
   LEFT JOIN enum_names n ON n.track_id=e.track_id AND n.opcode=EXTRACT_ARG(e.arg_set_id,'debug.opcode')
   WHERE e.name!=CASE e.kind WHEN 'stall' THEN 'stall' ELSE COALESCE(n.name,printf('0x%0*x',(c.width+3)/4,EXTRACT_ARG(e.arg_set_id,'debug.opcode'))) END) AND
  (SELECT count(*)>0 FROM events WHERE channel='icache.txreq' AND kind='transfer' AND name='ReadOnce' AND EXTRACT_ARG(arg_set_id,'debug.opcode')=3) AND
  (SELECT count(*)>0 FROM events WHERE channel='icache.rxdat' AND name='CompData' AND EXTRACT_ARG(arg_set_id,'debug.opcode')=4) AND
  (SELECT count(*)=0 FROM events e JOIN captures c ON c.track_id=e.track_id
   WHERE EXTRACT_ARG(e.arg_set_id,'debug.'||c.name) IS NULL) AND
  (SELECT count(*)=0 FROM events e JOIN args a USING(arg_set_id)
   WHERE a.key NOT IN ('debug.cycle','debug.sequence','debug.ancestry_unknown')
     AND NOT EXISTS (SELECT 1 FROM captures c WHERE c.track_id=e.track_id AND a.key='debug.'||c.name)) AND
  (SELECT count(*)=0 FROM events
   WHERE EXTRACT_ARG(arg_set_id,'debug.resp_err')!=0) AND
  (SELECT count(*)=0 FROM events
   WHERE (channel GLOB '*.tx*' AND EXTRACT_ARG(arg_set_id,'debug.src_id')!=CASE substr(channel,1,6) WHEN 'icache' THEN 2 ELSE 3 END)
      OR (channel IN ('icache.rxrsp','icache.rxdat','dcache.rxrsp','dcache.rxdat')
          AND EXTRACT_ARG(arg_set_id,'debug.tgt_id')!=CASE substr(channel,1,6) WHEN 'icache' THEN 2 ELSE 3 END)) AND
  (SELECT count(*)>0 FROM events
   WHERE channel='icache.txreq' AND EXTRACT_ARG(arg_set_id,'debug.address')=printf('0x%011x',2147483648)
     AND EXTRACT_ARG(arg_set_id,'debug.size_or_num_req')=6) AND
  (SELECT count(*)=0 FROM events WHERE channel GLOB '*.rxsnp'
   AND substr(EXTRACT_ARG(arg_set_id,'debug.address'),-1) NOT IN ('0','8')) AND
  (SELECT count(*)=0 FROM flow f JOIN events p ON p.id=f.slice_out
   JOIN slice c ON c.id=f.slice_in JOIN track t ON t.id=c.track_id
   WHERE p.channel!='dcache.txreq' OR p.kind!='transfer' OR t.name NOT IN ('dcache.rxrsp','dcache.rxdat')) AND
  (SELECT count(*)=0 FROM flow f JOIN events c ON c.id=f.slice_in
   JOIN slice p ON p.id=f.slice_out JOIN track t ON t.id=p.track_id
   WHERE c.channel!='dcache.txreq' AND
     (c.channel NOT IN ('dcache.rxrsp','dcache.rxdat') OR
      t.name!='dcache.txreq')) AS ok
