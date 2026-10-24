<!-- Defines shared vector ownership, extraction boundaries, and focused behavioral validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing shared vector execution

Read [README.md](README.md) for the public service contract and
[core ownership rules](../DEVELOPING.md) before changing this family.

## Architecture and ownership

This package may consume architectural `riscv/` types, shared processor
components, HardFloat contracts, and Rhodium/Flow. It must not import a named
core, its configuration, pipeline payload, or retirement policy. The
[package graph](../../rhodium/DEVELOPING.md) is authoritative.

`VectorExecution` accepts already nonspeculative descriptors. Its opaque
`Context` travels with memory tokens; scalar retirement records do not belong
in shared bundles. Integer service tags use `../integer-execution.rhdl`, and
GPR writes use `../register-write.rhdl`. They are independent of register-file
port counts and scalar scheduling.

Keep instruction IDs, completion slots, and the sequencer cursor distinct.
Return routing uses retained slot ownership, never the live descriptor. A retry
restores the unauthorized checkpoint without deleting accepted memory ownership.

Aligned faultable memory beats may sequence on consecutive cycles. Their
slot-bounded metadata FIFO pairs in-order LSU decisions with the authorized
frontier; retry flushes only the unaccepted suffix. Detect a misaligned element
when its registered index/operands return, suppress that cycle's younger read,
and retain a split barrier until that element's tagged decision. An older
authorization must not release the barrier. Fault-only-first slot exclusivity
and the splat engine's single probe remain unchanged. Both named LSU adapters
must suppress younger attempts on recovery and commit stores only at resolution.

Keep simultaneous fixed result lanes independent; Flow composition must not
introduce buffering or lossy arbitration on a nonstallable return.
Packed store release preserves the ordered completion head and pending-carry
barrier but bypasses VRF write availability: acknowledging a store writes no
register data. Store-hit bookkeeping arrives on a separate Valid tag lane and
never arbitrates against load data. Route it by retained slot metadata, not the
live descriptor. Nonwriting slow returns bypass VRF arbitration as well; they
complete only slot metadata, independently of a simultaneous compute result.
Complete identity-mapped packed rows book the sequence-to-hit
write edge three cycles ahead in the existing calendar, rather than the ordinary
memory path's four. `VectorToken.packed` preserves this timing selection through
LSU retries. Packed loads bypass the LSU's ordinary EX register; sequencing
defers a packed load if an ordinary beat issuing now will occupy its lookup
cycle. Their payload goes directly from the LSU
return to the VRF. A direct write at the ordered head releases its slot on that
same edge, including simultaneous acceptance, when no carry barrier remains.
Other writes retain a written bit until ordered metadata reclamation. Release
and reservation clear completion/written bits ahead of same-edge arrival updates,
so a reclaimed physical slot cannot leak stale state into its next owner. The
accepted-beat trace storage describes same-edge acceptance/release as a bypass,
not as a stored enqueue/dequeue.
Clear the exact row claim at that write, not at slot release. Partial, rotated,
and segmented words and carry flushing retain their assembly grants. Neither
assembly, splat writes, nor variable element returns may consume another
operation's reserved cycle.
Trace packed VRF writes on `vector/complete` and packed metadata reclamation on
`vector/slot.release`. Never use slot release as a result-completion proxy: it
may follow assembly, include a store, or coincide with another VRF result.

Immediate packed writes follow the live accepted-hit Flow. Delayed direct writes
and assembly pieces select the accepted slot's retained owner. The row-carry
window follows actual byte occupancy, retaining a reference per byte and joining
resident contributors with the current piece only when both supply the write.
Split writes replace the old carry with the current beat's suffix on the same
edge; flushing or writing an older row must not attribute its data to the live
piece. Keep these intrinsic contracts beside the functional controls, without
new payload buffers or instruction-ID reconstruction.

## Implementation map

| Files | Responsibility |
|---|---|
| `decode.rhdl` | Component control relations, operand modes, configuration request formation, and operation legality |
| `bundles.rhdl`, `packed-bundles.rhdl` | Descriptor, beat, response, and packed transport contracts |
| `pipeline.rhdl` | Execution composition, shared-service scheduling, and register-port arbitration |
| `sequencer.rhdl`, `operand-fetch.rhdl` | Single descriptor cursor/checkpoint and fixed-latency operand capture |
| `instructions.rhdl`, `dependencies.rhdl` | Persistent instruction owners and register-row hazards |
| `completion.rhdl`, `slots.rhdl` | Result writeback, accepted ownership, and ordered metadata reclamation |
| `register-file.rhdl`, `packing.rhdl`, `mask.rhdl`, `execute.rhdl` | VRF/mask shadow and packed SIMD operand/result shaping |
| `packed-prepare.rhdl`, `packed-load.rhdl`, `splat-load.rhdl`, `load-response.rhdl` | Packed and splat transport preparation, response assembly, and retained carry |
| `geometry.rhdl`, `precheck.rhdl` | Beat geometry and conservative certification footprints |
| `backend.rhdl` | Common admission/execution composition, service forwarding, and precise fault transport; no named LSU protocol |
| `admission.rhdl` | Descriptor credit, head certification, pinned-window release, queued-kind summaries, and registered outcomes |
| `replay.rhdl` | Durable request retention/retry cursors and independent store acknowledgements |
| `instruction-metadata.rhm` | Canonical base decode domains and scalar operand/destination metadata |
| `udb.rhm` | Shared execution geometry and architectural policy for named-core UDB projections |
| `profile.rhm` | Shared scalar/vector capability validation and execution half-precision selection |
| `fp.rhdl`, `muldiv.rhdl` | Shared arithmetic operands and tagged result conversion |

Packed multiply selection is a host choice on `VectorExecution`, forwarded to
the sequencer, operand fetch, and execute geometry. The shared
`vector_packed_multiply` predicate keeps subword bank selection consistent;
full-width operations remain singleton scalar-service requests. Keep source
rows narrow for widening multiplication: the numeric formatter selects the
product half, while operand fetch remaps that half's enables and reads a full
destination-width old-vd row. Do not widen inputs through the ALU packing path.
Normalize uncared MAC product-select fields at the numeric boundary, not in
instruction decode. Scalar returns use the same numeric formatter's lane zero.

`mask.rhdl` uses the standard `prefix_scan` for selected-bit ranks and the
word total. Widen leaves to include the all-selected count before scanning;
the caller still owns cross-word carry and its maturity-qualified update.
The `rv5stage-vector-reduction` fixture checks scan masks, iota/count results,
and compression through the production pipeline and LSU readback.

Packed multiplication reserves calendar client 2 before its synchronous reads,
with a six-cycle delay through operand capture, EX, and the four-stage numeric
pipeline. Ordinary local writes continue using that client's shorter delay;
scalar multiply and FP retain clients 0 and 1. Even empty/masked beats finish
on the reserved edge. Completion retains the slot's bit mask and qualifies the
numeric byte saturation against it. Passive cosim reports packed launch at
allocation separately from queued scalar launch; their mutually exclusive
architectural returns use the existing multiplier-return observation.
Local read admission uses `prior_claims_available` for client 2, observing
accepted scalar multiply/FP claims before promising the local return edge.

`../mmu/vector.rhdl` owns certification/probe contracts and
`../mmu/vector-window.rhdl` owns the pinned two-page window. The named MMU owns
lookups, walks, permissions context, and invalidation. `VectorBackend` composes
`VectorAdmission` and execution, including the
certified-outcome assertion and queued-kind summaries. Named wrappers connect
its memory streams to their LSU adapter and select WB policy, fault-provenance
types, drain qualification, arithmetic resources, and descriptor queue timing.
Passive execution taps are nested under the backend component; memory taps stay
on the named LSU. Keep their capture bindings aligned when changing hierarchy.
The memory adapters retain fixed lookup stages, fault interpretation, split
ownership, and cache response shaping; `VectorReplay` owns only durable request
storage and its ordered attempt/release cursors.
Its Decoupled attempt advances only on transfer, so an older registered lookup
can defer a captured packed load without another payload buffer or a lost request.
The backend installs a certified or faultable successor through the ordinary
sequencer handshake, independently of LSU drain. `memory_start_pending` remains
true until the current faultable descriptor's first memory read transfers.
While pending, the backend qualifies the existing `schedule_enable` with the
caller's `memory_start_ready`; subsequent reads retain the normal authorization
and suffix-replay protocol. Replacement clears the started bit ahead of an old
tail transfer, so the next descriptor cannot inherit permission.

The LSU promises next-cycle acceptance only with no older preparation stages,
split owner, or future durable capture. `VectorReplay.empty_after_release`
credits only an empty ring or a real final release, including same-edge capture.
The final certified WB attempt may release capacity on the first-read edge;
retry, blocked completion, and split takeover must not supply that credit.
The last registered operand return is covered by its actual durable capture,
not by a global sequencing-idle summary. Never predict a future cache outcome,
permit a scheduled read to stall later, or wait for accepted slow responses.

## Change workflow

Keep this boundary independent of RV5Stage when adding another caller. Reuse
component decode relations, but compose scalar decode columns in that caller.
Do not add compatibility re-exports through the old named-core paths.
Keep operation modifiers partial behind their consuming discriminator, including
nested scan controls. Widening and narrowing both use signed operand preparation;
do not free those fields on narrowing rows. FP rounding selection remains cared
for dynamic, fixed-RTZ, and round-to-odd operations, but not nonrounding units.
Preserve trace event labels and passive observation fields when moving code;
instrumentation consumers depend on ownership and timing, not source location.

`VectorReplay.skip` resumes attempts after an oldest owner accepted by a split
transaction; `retry` instead rewinds to that owner. Neither event deletes
accepted response ownership. Recovery suppresses attempts while still allowing
durable capture. Keep acknowledgement readiness independent of cache commit.
Preserve caller-selected pipelined versus registered descriptor credit: changing
it can introduce a combinational cycle through WB and LSU readiness.
Descriptor capacity is independently selected with `~dispatch_depth`; size the
queued-kind counters for the full inclusive occupancy range. Capacity changes
must not change faultable-memory barriers, head certification ownership, or
the single sequencer's dispatch order.

Move direct behavioral fixtures with their implementation. Shared fixtures
decode vector controls directly rather than instantiating a named core decoder.
Integrated CSR/decode, admission, MMU/cache, and software tests remain with their
named core. Existing `rv5stage-vector-*` fixture IDs are retained as runner
selectors; the corresponding direct emitters and rsim drivers now live here.
The generic host clock/response scoreboard helper lives in
`../tests/rsim/driver.hpp`; named-core memory encodings remain with their caller.

## Validation

For extraction or service-boundary changes, run the existing focused fixtures:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/vector-ctrl-test.rhm cores/rv5stage/tests/core-ctrl-test.rhm
FIXTURES='rv5stage-vector-overlap rv5stage-vector-packed rv5stage-vector-sequencer-rv32 rv5stage-vector-admission' python3 tools/testing/rsim/run.py
make check-boundaries
```

The overlap fixture covers persistent response ownership across sequencer
replacement, consecutive faultable indexed stores, middle-element retry/fault
with delayed accepted responses, and split authorization/retry/fault behind an
accepted prefix. The RV2Wide memory adapter fixtures also check consecutive
precise commits and younger-attempt suppression on retry/fault. Packed and RV32
fixtures cover transport and row geometry. The packed fixtures hold the VRF
write grant low throughout stores, checking ring
wrap, empty stores, and masked segmented retries with delayed/reordered replies.
They verify memory contents, unchanged VRF state, and complete response drain.
Full-row hit checks require same-edge writes past older pending responses and
while alignment is unavailable. Head hits and delayed head returns must release
on their write edge, including simultaneous acceptance and repeated ring wrap;
younger direct writes retain ordered metadata. Check the release-tag sequence
independently of VRF writes. Delayed full-row producers honor backpressure;
immediate hits require the promised write grant. LSU tests cover concurrent
store-hit bookkeeping and a backpressured load return, same-cycle packed-load
lookup and two-cycle hit return, registered-access priority, and retained mixed
timing retries in both XLENs. Passive cosim capture
must retain both events independently in the same settled sample.

The RV32/RV64 packed fixtures also run production event instrumentation. Their
byte-level oracle derives VRF contributors from accepted public transport
addresses and masks, checking exact write-parent sets through reordered returns,
ring reuse, masked/segmented assembly, split rows, carry flush, and replay.

The `event-vector` rsim fixture checks result-completion versus slot-release
site ownership and capture schemas alongside its dynamic sequencing oracle.
The sequencer fixtures force selected uncared decode modifiers high to exercise
their unobservability through legality, operand preparation, and writeback.
Care-mask regressions remain in `../rv5stage/tests/vector-ctrl-test.rhm`.
The RV5Stage admission fixture checks the caller boundary. Add its vector-memory
and MMU replay fixtures for translation/LSU behavior changes, FP/muldiv fixtures
for shared arithmetic changes, and cosim/event fixtures for observation changes.
Its public-port observations require indexed-store dispatch on the older compute
tail edge and first sequencing before the last arithmetic write. Its certified-
load/faultable-store case installs the successor on the older tail but holds the
first read through final memory preparation and actual release.
Use the managed Racket wrappers and keep generated HDL/native builds untracked.

For packed multiply integration, run `vector-multiply` and
`vector-multiply-rv32` for production-engine data, beat count, consecutive issue,
scalar fallback, masks, vstart, widening, overlapping service returns, and replay
checks; run `rv2wide-core-vector`
and `rv2wide-core-vector-rv32` for caller integration. Shared scalar formatter
changes also require `rv5stage-vector-muldiv`. The native vector cosim adapter
test covers atomic packed launch and same-edge old-result/tag replacement.

For descriptor-capacity changes, run the shared `vector-admission-one`,
`vector-admission-two`, `vector-admission-three`, `vector-admission-four`, and
`vector-admission-eight` rsim fixtures. Their FIFO oracle covers full registered
credit, empty bypass, simultaneous transfers, pointer wrap, immutable snapshots,
full-capacity kind summaries, reset, and the uncertified-memory outcome barrier.

For admission/replay extractions, cover both callers with
`rv2wide-core-vector`, `rv2wide-core-vector-rv32`, `rv2wide-vector-memory`,
`rv5stage-vector-admission`, `rv5stage-vector-memory`, and
`rv5stage-vector-memory-one-slot`. These preserve registered WB credit,
precise versus certified outcomes, split ownership, and depth-one ring indexing.
