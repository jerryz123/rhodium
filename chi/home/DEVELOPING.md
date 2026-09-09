<!-- Guides changes to CHI Home engines. -->

# Developing CHI Home engines

Read [README.md](README.md) and the parent
[CHI developer guide](../DEVELOPING.md) before changing this area.
The parent owns package-wide boundaries; this guide owns component extension and validation.

Keep the Home engines independent. Shared configuration, message policy, and target bookkeeping do not justify merging their distinct state machines or storage ownership.

Keep tests and authoring fixtures in [`../tests/`](../tests/), and behavioral
benches in [`tests/backend/`](../../tests/backend/DEVELOPING.md).
For source moves, update direct consumers, package documentation, and build/CI
paths together. Run `make check-boundaries` and the affected host and behavioral
checks with a fresh isolated compiled root. Directory boundaries do not add RTL
hierarchy or per-directory facade modules.

## Extension and focused validation

Home REQ/DAT forwarding uses immutable field replacement to retain untouched
metadata, including optional fields. `chi/home/home-common.rhdl` keeps the policy
wrappers that select downstream opcodes, early-write acknowledgement, and
coherent response state; `chi/protocol/messages.rhdl` receives those decisions explicitly.
Both Home implementations import those wrappers directly; neither implementation
imports the other. Shared configuration and identity also live in
`chi/home/home-common.rhdl`. Preserve the existing facade and coherent-Home re-exports
for callers while making new shared consumers import the owning module.
Keep LLC lookup, replacement, dirty-data ownership, and retirement in their
respective engines rather than adding modes to one shared state machine.

Both Homes instantiate `CHIHomeSnoopTargets` from its owning module. This small
child circuit shares the Home's clock/reset and replaces only the pending-mask
and expected-responder registers; it adds no snoop-payload buffer or pipeline
stage. Each Home computes its target mask, constructs the snoop, and gates
`target.ready` with its own issue phase and SNP sink readiness. That handshake
must coincide with the outgoing snoop handshake. In particular, a pending
target must not advance while the Home is processing the previous responder's
control, dirty data, or intervention write. Keep receipt masks and completion
decisions in the Home engines. Mask loading and dispatch are phase-exclusive
in both callers. Run both Home fixtures and both maintenance fixtures when
changing this bookkeeping; the shared maintenance bench checks target order,
stalled dispatch stability, and reset before and after a dispatch.

`CHIInclusiveHNF` owns `resident_lines`, indexed by LLC set/way and configured
RN-F order. Keep the absence invariant separate from LLC dirty state and from
`chi_request_allocates_coherent`, whose opcode family includes non-allocating
`WriteUniquePtl`. A successful final read-data transfer publishes a possible
cached copy before the serialized Home can accept another request; CompAck
still owns transaction completion when requested. Only complete successful
snoop responses or complete copyback may remove a responder. Track retained/error state across all
dirty packets, and keep a failed victim invalidation from replacing its entry.
Successful line installation starts with an empty directory. Never attach the
old victim's bits to the new tag. Coordinated reset clears LLC and requester
state; independent requester state surviving a Home reset is not supported.

Copyback bypasses snoop-target loading and ordinary allocation. Its saved
response state is consistent across every expected DAT packet; install dirty
data only after the complete receipt mask. Clean/Invalid late returns must
leave LLC data and replacement state untouched. The noncaching Home reserves
its single transaction until its full-line backing write completes. Once
`CompDBIDResp` transfers, it cannot report a second requester completion to
recover from a backing error; such failures are fatal in this profile.

Use `chi-inclusive-home` for residency, shared/unique grants, snapshot reads,
silent-eviction cleanup, stalled dispatch, delayed CompAck, partial dirty packets,
and response-error behavior. The maintenance bench establishes inclusive L1
copies through actual read grants, not test-only injection behind the directory.
Run `chi-maintenance-inclusive`, the I-cache coherence fixtures, and cache-level
LR/SC progress after changing target selection. Rerun SimpleSoC vvadd with
unchanged host polling and inspect `tohost` snoops and pipeline replay counts;
keep correctness and reduced traffic distinct from a cycle-count prediction.

For maintenance changes, run the `chi-cache-maintenance`,
`chi-maintenance-home`, and `chi-maintenance-inclusive` backend fixtures. The
last two share a behavioral bench with independent RN-F caches and backing
RAM, rather than using coherent reads as evidence of memory visibility.
Include `chi-coherent-home`, `chi-inclusive-home`, and `rv5stage-dcache` when
changing the data-preserving versus discard snoop policy. Shared opcode
classification stays in `chi/protocol/coherence.rhdl`; each Home retains its own SRAM,
transaction, and dirty-data lifetime. Maintain error state until completion.
