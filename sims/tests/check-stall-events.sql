-- Checks shared-track stall durations, exact hazard flags, and non-advancing lineage.
-- SPDX-License-Identifier: Apache-2.0
WITH tracks AS MATERIALIZED (
  SELECT t.id, t.name, EXTRACT_ARG(t.source_arg_set_id,'description') AS schema
  FROM rheg_tracks t WHERE t.name GLOB 'core/*' OR t.name GLOB '[id]cache/*' OR t.name GLOB 'frontend/*'
), sites AS MATERIALIZED (
  SELECT t.id AS track_id, t.schema FROM tracks t
  UNION ALL
  SELECT t.id, o.value FROM tracks t, json_each(t.schema,'$.observations') o
), events AS MATERIALIZED (
  SELECT s.id, s.ts, s.dur, s.name AS mnemonic, s.arg_set_id, t.name, s.track_id,
         CASE s.name WHEN 'stall' THEN 'stall' ELSE 'transfer' END AS kind,
         EXTRACT_ARG(s.arg_set_id,'debug.pc') AS pc,
         EXTRACT_ARG(s.arg_set_id,'debug.instruction') AS instruction
  FROM slice s JOIN tracks t ON t.id=s.track_id
), edges AS MATERIALIZED (
  SELECT a.id AS parent, b.id AS child, a.name AS src, b.name AS dst,
         a.kind AS parent_kind, b.kind AS child_kind,
         a.pc AS parent_pc, b.pc AS child_pc, a.instruction AS parent_instruction,
         b.instruction AS child_instruction, b.ts-a.ts AS delay, b.dur AS child_duration
  FROM flow JOIN events a ON a.id=flow.slice_out JOIN events b ON b.id=flow.slice_in
), hazards(name) AS (
  VALUES ('deferred_dependency'),('raw_hazard'),('waw_hazard'),('fp_source_hazard'),
         ('fp_destination_hazard'),('resource_hazard'),('pause'),('serialization'),
         ('wfi'),('interrupt'),('exception')
), reasons AS MATERIALIZED (
  SELECT e.id, e.kind, count(*) AS fields,
         sum(EXTRACT_ARG(e.arg_set_id,'debug.'||h.name) IN (0,1)) AS booleans,
         sum(EXTRACT_ARG(e.arg_set_id,'debug.'||h.name)) AS active
  FROM events e CROSS JOIN hazards h
  WHERE e.name='core/s2.decode' GROUP BY e.id, e.kind
)
SELECT
  (SELECT count(*) FROM events)=(SELECT count(*) FROM slice s JOIN tracks t ON t.id=s.track_id) AND
  (SELECT count(*)=0 FROM tracks WHERE name GLOB '*.stall') AND
  (SELECT count(*)=0 FROM (SELECT track_id,ts FROM events GROUP BY track_id,ts HAVING count(*)>1)) AND
  (SELECT count(*)>0 FROM events WHERE name='core/s2.decode' AND kind='stall') AND
  (SELECT count(*)=0 FROM events WHERE kind='stall' AND (dur<10 OR dur%10!=0)) AND
  (SELECT count(*)>0 FROM events WHERE kind='stall' AND dur>10) AND
  (SELECT count(*)=0 FROM events WHERE kind='transfer' AND dur!=10) AND
  (SELECT count(*)=0 FROM (SELECT ts,LAG(ts+dur) OVER (PARTITION BY track_id ORDER BY ts) AS previous_end FROM events)
   WHERE previous_end>ts) AND
  (SELECT count(*)=0 FROM events WHERE kind='stall' AND name GLOB 'core/*'
   AND (name!='core/s2.decode' OR pc IS NULL OR instruction IS NULL
        OR length(pc)!=18 OR length(instruction)=0)) AND
  (SELECT count(*)=1 FROM tracks t, json_each(t.schema,'$.observations') o
   WHERE t.name GLOB 'core/*' AND json_extract(o.value,'$.kind')='stall'
     AND json_extract(o.value,'$.observation_of')=json_extract(t.schema,'$.site_id')
     AND json_extract(o.value,'$.fields')=json_extract(t.schema,'$.fields')) AND
  (SELECT count(*)=0 FROM flow JOIN events s ON s.id=flow.slice_out WHERE s.kind='stall') AND
  (SELECT count(*)=0 FROM events s WHERE s.name='core/s2.decode' AND s.kind='stall'
   AND (SELECT count(*) FROM flow WHERE slice_in=s.id) NOT BETWEEN 1 AND 2) AND
  (SELECT count(*)=0 FROM edges WHERE dst='core/s2.decode' AND child_kind='stall'
   AND (src!='frontend/s2.outcome' OR parent_kind!='transfer' OR delay<0)) AND
  (SELECT count(*)=0 FROM flow f JOIN events s ON s.id=f.slice_in JOIN events p ON p.id=f.slice_out WHERE s.name='core/s2.decode' AND s.kind='stall' AND (p.name!='frontend/s2.outcome' OR EXTRACT_ARG(p.arg_set_id,'debug.admitted') IS NOT 1)) AND
  -- Match both parent and instruction PC: two compressed instructions can
  -- share one word parent. Surviving instructions follow their own stalls.
  (SELECT count(*)>0 FROM edges s JOIN edges issued ON issued.parent=s.parent AND issued.child_pc=s.child_pc
   WHERE s.dst='core/s2.decode' AND s.child_kind='stall' AND issued.dst='core/s2.decode' AND issued.child_kind='transfer') AND
  (SELECT count(*)=0 FROM edges s JOIN edges issued ON issued.parent=s.parent AND issued.child_pc=s.child_pc
   WHERE s.dst='core/s2.decode' AND s.child_kind='stall' AND issued.dst='core/s2.decode' AND issued.child_kind='transfer' AND s.delay+s.child_duration>issued.delay) AND
  (SELECT count(*)>0 AND sum(fields=11 AND booleans=11 AND
     CASE kind WHEN 'stall' THEN active>0 ELSE active=0 END)=count(*) FROM reasons) AND
  (SELECT count(*)=2 FROM sites site JOIN tracks t ON t.id=site.track_id WHERE t.name='core/s2.decode'
   AND (SELECT count(*) FROM json_each(site.schema,'$.fields') f JOIN hazards h
        ON h.name=json_extract(f.value,'$.name')
        WHERE json_extract(f.value,'$.encoding')='bool' AND json_extract(f.value,'$.width')=1)=11) AS ok
