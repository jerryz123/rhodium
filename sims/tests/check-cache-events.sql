-- Checks the private-cache CHI channel schema, transferred flits, and explicit lineage boundary.
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
  SELECT t.id, t.name, a.string_value AS schema
  FROM track t JOIN args a ON a.arg_set_id=t.source_arg_set_id
  WHERE t.name GLOB '[id]cache.*' AND a.key='description'
), captures AS MATERIALIZED (
  SELECT t.id AS track_id, json_extract(f.value,'$.name') AS name
  FROM cache_tracks t, json_each(t.schema,'$.fields') f
), events AS MATERIALIZED (
  SELECT s.id, s.arg_set_id, s.track_id, t.name AS channel, s.ts
  FROM slice s JOIN cache_tracks t ON t.id=s.track_id
)
SELECT
  -- The importer materializes tracks only when a channel transfers a flit.
  (SELECT count(*) BETWEEN 4 AND 12 AND count(DISTINCT name)=count(*) FROM cache_tracks) AND
  (SELECT count(*)=(SELECT count(*) FROM cache_tracks) FROM cache_tracks t JOIN expected e ON substr(t.name,8)=e.suffix
   WHERE json_extract(t.schema,'$.site_id') GLOB 'SoCHarness/soc/rv5stage/event:*'
     AND length(json_extract(t.schema,'$.source_location'))>0
     AND (SELECT group_concat(json_extract(f.value,'$.name')) FROM json_each(t.schema,'$.fields') f)=e.fields) AND
  (SELECT count(*)>0 FROM events WHERE channel='icache.txreq') AND
  (SELECT count(*)>0 FROM events WHERE channel='icache.rxdat') AND
  (SELECT count(*)>0 FROM events WHERE channel='dcache.txreq') AND
  (SELECT count(*)>0 FROM events WHERE channel='dcache.rxdat') AND
  (SELECT count(*)=0 FROM events e JOIN captures c ON c.track_id=e.track_id
   WHERE EXTRACT_ARG(e.arg_set_id,'debug.'||c.name) IS NULL) AND
  (SELECT count(*)=0 FROM events e JOIN args a USING(arg_set_id)
   WHERE a.key NOT IN ('debug.cycle','debug.sequence')
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
  (SELECT count(*)=0 FROM flow
   WHERE slice_out IN (SELECT id FROM events) OR slice_in IN (SELECT id FROM events)) AS ok
