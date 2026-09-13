-- Exposes full annotation labels for queries independently of visual track grouping.
-- SPDX-License-Identifier: Apache-2.0
CREATE PERFETTO VIEW rheg_tracks AS
SELECT id, parent_id, source_arg_set_id, name AS leaf_name,
       json_extract(EXTRACT_ARG(source_arg_set_id,'description'),'$.label') AS name
FROM track
WHERE json_extract(EXTRACT_ARG(source_arg_set_id,'description'),'$.kind')='transfer';
