<!-- Guides changes to CHI network integration. -->

# Developing CHI network integration

Read [README.md](README.md) and the parent
[CHI developer guide](../DEVELOPING.md) before changing this area.
The parent owns package-wide boundaries; this guide owns component extension and validation.

Keep noc-authoring.rhm independent of Rhodium and CIRCT. Generic topology, routing analysis, and router hardware remain in the root noc/ package.

Keep tests and authoring fixtures in [`../tests/`](../tests/), and behavioral
benches in [`tests/backend/`](../../tests/backend/DEVELOPING.md).
For source moves, update direct consumers, package documentation, and build/CI
paths together. Run `make check-boundaries` and the affected host and behavioral
checks with a fresh isolated compiled root. Directory boundaries do not add RTL
hierarchy or per-directory facade modules.

## Extension and focused validation

Attachment compilation builds typed local endpoint plans before deriving the
channel closure indices from them. Preserve category order and each category's
column order; those indices determine which physical router slots are attached.
The mixed-role `chi/tests/router-attachment-plan-test.rhm` covers all six roles,
nonlexical column ordering, and empty attachments. Family adapter lookup methods
return their concrete route/ejection decision bundles, so consumers need no
repeated result annotation.

NoC adapters share injection wiring and flow-stage bookkeeping in
`chi/noc/noc-adapter.rhdl`, and reuse generic envelope-removal binding from
`noc/rtl/route-adapter.rhdl`. Keep the typed channel circuits and fixed
versus family-site factories explicit. REQ/RSP/DAT select `tgt_id`; SNP selects
`CHISnoopDispatch.target_id` and transports only its flit. Ejection checks remain
channel-owned because SNP has no target field. These helpers add no hierarchy,
buffering, or route policy beyond the existing `noc/rtl` injector/ejector.
The `chi-noc-adapter` backend fixture covers all sixteen variants, complete
payloads, stalls, and invalid routes, targets, and family sites. Its host test
checks the transform kinds, fixed NodeID properties, and implementation
associations consumed by diagram/event tooling. Run the SNP, subordinate,
family NoC, and router-composition integration fixtures alongside it.

Endpoint attachment policy lives in `CHINoCPlane` in `chi/noc/noc-adapter.rhdl`.
Both fixed connection helpers and `CHIRouter`'s family attachments use its
injection and queued-ejection methods; keep the RN/HN/SN field mappings and
fixed versus family adapter choices explicit at their callers. The one-entry
queue precedes the ejection adapter, and router availability tracks its input
readiness, not the final sink. `CHINoCPorts` groups existing plane endpoints for
fixed-router callers without introducing circuit parameters, ports, or hierarchy.
Generic physical-link binding remains in `noc/rtl`, outside this CHI queue policy.
The SN fixture exercises fixed attachments in both directions under stalls;
the family fixture fills, stalls, and drains an asymmetric three-router path
with complete-packet ordering checks. Validate MiniSoC and SimpleSoC for fixed
RN-F/HN attachments and TiledSoC for coherent family attachments.
