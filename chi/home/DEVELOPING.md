<!-- Guides changes to CHI Home engines. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing CHI Home engines

Read [README.md](README.md) and the parent
[CHI developer guide](../DEVELOPING.md) before changing this area.
The parent owns package-wide boundaries; this guide owns component extension and validation.

Keep the Home engines independent. Shared configuration, message policy, and target bookkeeping do not justify merging their distinct state machines or storage ownership.

Keep tests and authoring fixtures in [`../tests/`](../tests/), and behavioral
benches in [`../tests/circt/`](../tests/circt/).
For source moves, update direct consumers, package documentation, and build/CI
paths together. Run `make check-boundaries` and the affected host and behavioral
checks through the persistent isolated build cache. Directory boundaries do not add RTL
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

The noncaching Home instantiates `CHIHomeSnoopTargets` from its owning module.
The inclusive Home keeps pending and outstanding masks plus per-responder DAT
receipt state in each transaction slot, then arbitrates one outgoing snoop at a
time. Its 12-bit snoop transaction identity is the configured RN-F index times
the Home slot count plus the slot index. This permits consecutive dispatch to
different residents and out-of-order completion while preserving direct slot
routing. The configured RN-F count times the slot count must fit the snoop
transaction-ID space. Neither Home form adds a snoop-payload buffer or pipeline
stage. Each Home computes its target mask, constructs the snoop, and gates
`target.ready` with its own issue phase and SNP sink readiness. That handshake
must coincide with the outgoing snoop handshake. In particular, a pending
target must advance only when the corresponding outgoing snoop transfers.
Control and data responses clear only their encoded outstanding target, and a
slot leaves snoop processing only after both target masks are empty. Keep
receipt masks and completion decisions in the Home engines. Mask loading and
dispatch are phase-exclusive in both callers. Run both Home fixtures and both
maintenance fixtures when changing this bookkeeping; the shared maintenance
bench checks target order, stalled dispatch stability, and reset before and
after a dispatch.

`CHIInclusiveHNF` owns `resident_lines` and `may_write_lines`, indexed by LLC
set/way and configured RN-F order. Possible writers are a subset of possible
residents; a clean `Unique` snoop response still permits a silent later write.
Keep these permission invariants separate from LLC dirty state and from
`chi_request_allocates_coherent`, whose opcode family includes non-allocating
`WriteUniquePtl`. Its bounded transaction slots retain the request, selected
set/way, line data, snoop state, fill/writeback masks, errors, and subordinate
DBID. The slot index is the Home TxnID/DBID on subordinate and snoop traffic.
One shared SRAM lookup port accepts at most one lookup per cycle. Different
sets may overlap, but an admitted transaction owns its set until retirement;
same-set requests remain backpressured so directory and replacement updates
cannot conflict. Lookup issue, autonomous progress, and requester response DAT
use independent rotating slot selectors. Requester DAT/RSP and subordinate
DAT/RSP enter non-pipelined two-entry buffers before they decode with
victim-writeback completion onto one typed, slot-targeted ingress event. The
buffers prevent autonomous output readiness from propagating to external
ingress readiness. A retained autonomous output continues presenting the same
owner and payload while a stalled handshake permits that ingress event to
advance; an available autonomous handshake wins before another ingress event
so only one autonomous phase transition commits per cycle. Lookup response
remains the nonstallable shared-resource priority. A non-final subordinate fill
beat may update its private slot buffer in the same cycle that another slot
returns DAT, and a lookup may issue alongside a non-final fill. A final fill,
merge, or intervention update reserves the single LLC array port instead. Requester DAT
and each selected shared RSP, SNP, or subordinate REQ/DAT output retain their
owner while stalled; do not let another slot or incoming channel change a
payload before handshake. A successful final read-data transfer publishes a possible
cached copy before releasing its transaction slot.
Reads with `ExpCompAck` reserve a `CHIHomeCompAckTable` slot at admission and
carry its DBID on every response DAT beat. Final DAT publishes the slot and
releases the transaction slot; the granted set remains reserved until `CompAck`
confirms receipt, so its resident cannot be probed or replaced early. The later
`CompAck` validates source, target, and DBID against only that table entry.
Table capacity may backpressure another
acknowledgement-bearing request, but must not block a request that does not need
`CompAck`. Only complete successful snoop responses or complete copyback may
remove a responder. A complete SharedClean response removes possible write
permission but not residency; an Invalid response removes both. Track
retained/error state across all dirty packets, and keep a failed victim
invalidation from replacing its entry.
Successful line installation starts with an empty directory. Never attach the
old victim's bits to the new tag. `ReadOnce` `MemAttr.Allocate` controls only
miss installation: hits retain normal directory snooping, while nonallocating
misses bypass victim selection and leave tags, data, directory, dirty state,
and replacement state unchanged. Coordinated reset clears LLC and requester
state; independent requester state surviving a Home reset is not supported.
The LLC stores one `PLRUState(ways)` per set. Only successful fills and ordinary
resident read or write lookups touch it; failed fills, maintenance, and
copyback leave recency unchanged. Invalid-first selection remains owned by the
shared standard-library policy.

`CHIInclusiveVictimWritebackBuffer` owns replacement write REQ/DBID/DAT/Comp
state after the parent slot has resolved every victim snoop. Its entries use
subordinate transaction IDs immediately above the demand-slot range, retain
the complete post-snoop line, and return completion plus rollback data to the
parent slot. Track `DBIDResp`, `Comp`, and the final write-data transfer as
independent events: separate write responses may arrive in either order and
`Comp` may precede write data. `DBIDResp` carries no error; a completion error
is retained from `Comp`, including when it arrives first. The parent continues
to own its set and selected way. It may start the refill after enqueue, but it
installs the new tag and line only after both the fill and writeback succeed. A
writeback error remains buffered while an already-issued fill drains, then
rewrites the old line without changing its tag, valid, dirty, or surviving
resident metadata. Keep maintenance writebacks on their existing serialized
slot path until they deliberately adopt the same ownership transfer.

Copyback bypasses snoop-target loading and ordinary allocation. Its saved
response state is consistent across every expected DAT packet; install dirty
data only after the complete receipt mask. Clean/Invalid late returns must
leave LLC data and replacement state untouched. The noncaching Home reserves
its single transaction until its full-line backing write completes. Once
`CompDBIDResp` transfers, it cannot report a second requester completion to
recover from a backing error; such failures are fatal in this profile.

Use `chi-read-once-home` for the focused `ReadOnce` allocation matrix: LLC-only
hits, clean and dirty RN-F intervention, allocating fills, nonallocating bypass,
backpressure, response errors, delayed `CompAck`, and streaming preservation of
unrelated LLC lines. Pair it with `chi-read-once` for requester-side retry and
Protocol Credit behavior and with `rv5stage-icache-coherence` for CPU-write and
`FENCE.I` visibility through real L1 caches. Keep `chi-inclusive-home` as the
broader residency, shared/unique grant, silent-eviction, partial-dirty-packet,
and copyback regression. The maintenance bench establishes inclusive L1 copies
through actual read grants, not test-only injection behind the directory. Run
`chi-maintenance-inclusive`, the I-cache coherence fixtures, and cache-level
LR/SC progress after changing target selection. Rerun SingleCoreRV5StageSoC vvadd with
unchanged host polling and inspect `tohost` snoops and pipeline replay counts;
keep correctness and reduced traffic distinct from a cycle-count prediction.

One-slot inclusive-Home tracing uses an intrinsic `describe_interface_contract` from
requester REQ to requester RSP/DAT with the named `request` retained scope.
Keep visible checkpoints in callers, not the Home. Capture on accepted requester
REQ; keep ownership throughout the real FSM lifetime, releasing on copyback finish, terminal completion,
or final data. Do not release on the first data beat, DBID response, subordinate
response, or delayed acknowledgement; `CompAck` has no requester output and is
owned by the separate DBID table after final DAT.
Both requester output channels share this lifetime; Flow infers network transit.
`chi/tests/home-trace-fixture.rhdl` supplies test-only boundary checkpoints.
The retained-event model does not yet express dynamically selected owners, so
this fixture deliberately constructs a one-slot Home while the behavioral Home
fixture uses two slots.
Run `event-home` for exact per-cycle graph comparison against public transfers,
including hit/miss data, repeated IDs, backpressure, and pending reset, then
the SingleCoreRV5StageSoC trace smoke for the composed router/queue paths.

For maintenance changes, run the `chi-cache-maintenance`,
`chi-maintenance-home`, and `chi-maintenance-inclusive` backend fixtures. The
last two share a behavioral bench with independent RN-F caches and backing
RAM, rather than using coherent reads as evidence of memory visibility.
Include `chi-coherent-home`, `chi-inclusive-home`, and `rv5stage-dcache` when
changing the data-preserving versus discard snoop policy. Shared opcode
classification stays in `chi/protocol/coherence.rhdl`; each Home retains its own SRAM,
transaction, and dirty-data lifetime. Maintain error state until completion.
