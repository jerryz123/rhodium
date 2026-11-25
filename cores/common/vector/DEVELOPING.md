<!-- Defines shared vector ownership, extraction boundaries, and focused behavioral validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing shared vector execution

Read [README.md](README.md) for the public service contract and
[core ownership rules](../DEVELOPING.md) before changing this family.

## Architecture and ownership

This package may consume architectural `riscv/` types, shared processor
components, HardFloat contracts, and Rhodium/Flow. It must not import a named
core, its configuration, pipeline payload, or retirement policy. The
[package graph](../../../rhodium/DEVELOPING.md) is authoritative.

`VectorExecution` accepts already nonspeculative descriptors. Its opaque
`Context` travels with memory tokens; scalar retirement records do not belong
in shared bundles. Integer service tags use `../integer-execution.rhdl`, and
GPR writes use `../register-write.rhdl`. They are independent of register-file
port counts and scalar scheduling.

Keep instruction IDs, completion slots, and the sequencer cursor distinct.
Return routing uses retained slot ownership, never the live descriptor. A retry
restores the unauthorized checkpoint without deleting accepted memory ownership.
Both modes restart numbering from the rejected beat's feedback. The sequencer
independently counts authorizations to assert that feedback names the oldest
unauthorized beat.

Local ALU beats bypass the slot ring at read scheduling, issue, EX resolution,
and completion. Their tag field is unused. The registered EX owner covers the
last issue-to-maturity cycle for retirement and cancellation; destination
claims advance at maturity, never at issue. Keep masked or empty service beats
in their original route, including SIMD multiply. The `vector-local-alu-rv32`
and `vector-local-alu-rv64` fixtures exercise independent and dependent compute
while all eight accepted memory tags are live.

Each physical completion slot owns one XLEN word. Tagged results use it for
retained write data (notably a multiply-accumulate addend); non-direct packed
loads use it until byte assembly drains. An ordinary resolution and an immediate
packed hit are mutually exclusive, but a delayed packed return may write a
different slot on that same edge. Keep both write paths, the direct-row bypass,
and the separate per-slot completion and ownership state.

Each ordinary tagged beat records its destination row at resolution. Row-hazard queries
use it while a write is outstanding; delayed writeback reads the same row by
return tag. Keep immediate writes on the live result and release the row hazard
at writeback, even when an older slot delays metadata reclamation.

The sequencer uses a combinational 32-register destination mask. From the same
bounds, the owner table captures a 16-bit mask of register pairs and two flags:
omit the first pair's even register or the last pair's odd register. Interior
pairs always claim both. Owner age, progressive row release, and exact
outstanding writes still decide whether a particular read must wait.

The sequencer keeps two XLEN addresses: the speculative issue address and the
retry address. Packed mode initializes both to the
aligned word containing `vstart`. Issue advances one word per transfer;
authorization advances one word per accepted beat, even for a partial word.
For ordinary non-indexed transfers these are element bases: both step from the
scalar base to `vstart`, then advance by the element step after the last segment
field at their respective frontiers.

Packed preparation derives the byte-to-row layout from the sequencer's held
descriptor and cursor, including while mask reads, alignment, or issue stall.
On retry, the rejected slot supplies its element, field, byte, and leading-byte
position. The sequencer restores the issue address from its authorization
frontier, not the slot or a younger offer. A new descriptor admitted on tail
authorization initializes both addresses for the new mode.

Packed preparation keeps one byte mask per offered beat. The low v0 read
supplies the low row's live lanes; the high read fills only lanes outside that
row. When both reads name the same row, the second contributes nothing.
An unmasked segmented store instead uses the live lanes captured at Map.
Keep the completed mask stable through store gathering and issue stalls.

For a vector gather, extract the selected VS1 element on the VRF return and
compare the full index with VLMAX before narrowing it. Bypass that result to
the dependent VS2 read; only a stalled beat needs the saved index and range flag.

For `vcompress`, the sequencer keeps the descriptor until the last write is
authorized. Operand fetch returns only the sequence number, source end, and
partial destination word (data, count, and destination); the sequencer issues
it under the resident descriptor without another VRF read. Keep the speculative
partial word separate from the authorized retry checkpoint. Cancellation
discards the pending write before a new descriptor can be admitted.

Aligned faultable memory beats may sequence on consecutive cycles. Their
slot-bounded metadata FIFO pairs in-order LSU decisions with the authorized
frontier; retry flushes only the unaccepted suffix. At checked-memory ingress,
assert that the compression suffix is empty and its destination is the beat's
first element. Use those values through the pipe and FIFO; only compute beats
need an independent compression checkpoint. The checked beat retains the VRF
row and one enable per destination byte, not result data: hits use decision
data, delayed loads use tagged response data, and store data travels on the
issue token. Assert that the byte enables expand to the original write mask;
compute writes can still select individual bits. Detect a misaligned element
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

The completion unit records the instruction owner at slot allocation. Packed
loads read that table by tag for live slots. Capture the head slot's owner
alongside any partial row: the slot may be reassigned before the carry flushes.
Packed-load carry retains one mask bit per byte alongside its row and word.
Reconstruct the existing bit-mask view for row assembly and VRF writes, and
assert that both merged and split-suffix masks are byte-granular before capture.
The general VRF write contract still supports individual mask bits. Owner closure
flushes an accepted partial row even after its last slot drains; write backpressure
must retain its mask, data, row claim, and owner until that write occurs.

## Implementation map

| Files | Responsibility |
|---|---|
| `decode.rhdl` | Component control relations, operand modes, configuration request formation, and operation legality |
| `bundles.rhdl`, `packed-bundles.rhdl` | Descriptor, beat, response, and packed transport contracts |
| `pipeline.rhdl` | Execution composition, shared-service scheduling, and register-port arbitration |
| `sequencer.rhdl`, `operand-fetch.rhdl` | Single descriptor cursor/checkpoint and fixed-latency operand capture |
| `instructions.rhdl`, `dependencies.rhdl` | Persistent instruction owners and register-row hazards |
| `completion.rhdl`, `slots.rhdl` | Result writeback, accepted ownership, and ordered metadata reclamation |
| `register-file.rhdl`, `packing.rhdl`, `mask.rhdl`, `execute.rhdl` | VRF with dedicated v0 read and packed SIMD operand/result shaping |
| `packed-prepare.rhdl`, `packed-load.rhdl`, `splat-load.rhdl`, `load-response.rhdl` | Packed and splat transport preparation, response assembly, and retained carry |
| `geometry.rhdl`, `precheck.rhdl` | Scheduling geometry and conservative certification footprints; captured-descriptor queries live on `VectorDescriptor` in `bundles.rhdl` |
| `backend.rhdl` | Common admission/execution composition, service forwarding, and precise fault transport; no named LSU protocol |
| `service.rhdl` | Shared arithmetic/result interfaces and lifetime summaries, independent of LSU and scalar scheduling |
| `admission.rhdl` | Descriptor credit, head certification, pinned-window release, queued-kind summaries, and registered outcomes |
| `memory-issue.rhdl` | Shared LSU issue selection, packed-load bypass, ordinary lookup register, and durable capture feedback |
| `memory-controller.rhdl` | WB decisions, split ownership, precise feedback, certified release, and arbitration of tagged data returns |
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

`pipeline.rhdl` keeps one XLEN continuation for the next dependent beat.
The first scan beat selects its seed in EX (all ones for `vfirst`, zero
otherwise); reductions take the seed from their operand. Admission must not
overwrite the word: an older tail may still need it in EX. Nonfinal integer
folds and scans advance it in issue order at compute maturity. Nonfinal FP
folds advance it at drain only for the still-active sequencing owner. Final
and canceled-owner FP returns complete their tagged slots normally without
changing the continuation. Keep the per-owner interlocks independent of the data.
Use `rv5stage-vector-reduction`, `rv5stage-vector-fp`, and `rv5stage-vector-overlap`
for these paths; the overlap fixture admits a scan alongside a delayed FP return
and repeats through owner-ring wrap. It also returns a canceled nonfinal FP
fold after its successor's first result, and checks back-to-back integer issue timing.

Packed multiplication reserves calendar client 2 before its synchronous reads,
with a six-cycle delay through operand capture, EX, and the four-stage numeric
pipeline. Ordinary local writes continue using that client's shorter delay;
scalar multiply and FP retain clients 0 and 1. Even empty/masked beats finish
on the reserved edge. Completion retains byte enables and a singleton FP-mask
enable rather than a full bit mask. The beat's existing mask-destination flag
and shift select the reconstructed mask; an assertion checks lossless capture.
Immediate arbitrary mask writes bypass this representation. Reconstruct the
physical mask without adding a stage or changing result arbitration, and retain
numeric byte saturation qualification against that mask. Passive cosim reports
packed launch at allocation separately from queued scalar launch; their mutually exclusive
architectural returns use the existing multiplier-return observation.
Local read admission uses `prior_claims_available` for client 2, observing
accepted scalar multiply/FP claims before promising the local return edge.

Scalar-bank results join the same atomic read admission through separate
`scalar_writeback` and `floating_writeback` claims. Both carry the computed
sequence-to-result delay; reject a conflicting claim before any operand read
or reservation advances. The backend forwards these claims without storage.
RV2Wide books the GPR and FPR calendars separately. RV5Stage accepts FPR claims
under its existing exclusive-owner policy. Keep standalone fixtures' unused
claims explicitly ready rather than accidentally stopping their sequencer.

`../mmu/vector.rhdl` owns certification/probe contracts and
`../mmu/vector-window.rhdl` owns the pinned two-page window. The named MMU owns
lookups, walks, permissions context, and invalidation. `VectorBackend` composes
`VectorAdmission` and execution, including the
certified-outcome assertion and queued-kind summaries. Named wrappers connect
its memory streams to their LSU adapter and select WB policy, fault-provenance
types, drain qualification, arithmetic resources, and descriptor queue timing.
Passive execution taps are nested under the backend component; memory taps stay
on the named LSU. Keep their capture bindings aligned when changing hierarchy.
Use the common `service` interface for arithmetic and architectural-result
forwarding rather than redeclaring its ports in each wrapper. Keep the FPR
scheduling claim separate to preserve RV5Stage's exclusive owner and the
superscalar calendar. Wrapper-wide `active` still includes each adapter's own
retained work; it is not merely a backend summary. Disabled variants terminate
the same interface without allocating execution state.

Keep `precheck.rhdl`'s fast start preparation separate from one-page range
qualification. RV5Stage registers the start/wrap proof and geometry at EX/MEM;
RV2Wide computes both helpers in EX. Neither helper owns validity, registers,
certificate context, or retirement. Validate changes through
`rv5stage-vector-memory`, `rv2wide-core-vector`, and its RV32 counterpart.
`VectorMemoryIssue` composes `VectorReplay` with certified/direct arbitration,
the ordinary lookup register, packed-load bypass, and delayed capture feedback.
Do not add buffering or flush the durable-feedback pipe during recovery. An
older registered lookup wins over a packed bypass; the latter retains ready-valid
backpressure until it can launch. Both adapters use `VectorMemoryController`,
which drives this frontend from the WB decision. Each adapter supplies its
MEM/WB occupancy, native cache admission, formatted data, and precise fault
cause and address. `vector_split_writeback` retains the accepted lookup until
the adapter converts its split reply into a WB decision. The controller reports
precise feedback on that edge and queues successful split data for completion.
Only certified hits depend on completion readiness. Keep that ready path out
of precise feedback because the feedback itself can affect write availability.
The replay ring's Decoupled attempt advances only on transfer, so an older registered lookup
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

Run `vector-completion-row-rv32` and `vector-completion-row-rv64` when changing
completion-slot row ownership. They check writeback under backpressure, an
older return on the edge a younger row resolves, out-of-order hazard release,
and reuse of both physical slots for different rows.

For VRF storage or forwarding changes, run `vector-register-file-rv32` and
`vector-register-file-rv64`. Their independent word-array oracle covers all
four simultaneous reads, independent valids, partial-write forwarding, v0
versus same-chunk aliases in other registers, immutable read snapshots, and reset.
Run `rv5stage-vector` for VLEN128/256/512 packing and in-place writes, plus
`vector-masked-load-rv32`, `vector-masked-load-rv64`, and
`rv5stage-vector-overlap` for production execution and delayed completion.

For extraction or service-boundary changes, run the existing focused fixtures:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/vector-ctrl-test.rhm cores/rv5stage/tests/core-ctrl-test.rhm
FIXTURES='rv5stage-vector-overlap rv5stage-vector-packed rv5stage-vector-sequencer-rv32 rv5stage-vector-admission' python3 tools/testing/rsim/run.py
moon run rhodium:check-boundaries
```

The overlap fixture covers persistent response ownership across sequencer
replacement, consecutive faultable indexed stores, middle-element retry/fault
with delayed accepted responses, partial-word compression after a middle-beat
retry, and split authorization/retry/fault behind an accepted prefix.
It compares the owner and sequencer claims at admission and checks that a
five-field load to `v27`–`v31` blocks a read of `v31` but leaves `v26` available.
The `vector-masked-load-rv32` and `vector-masked-load-rv64` fixtures retry
an enabled byte, complete a younger byte first, then read back the whole row
to check that the delayed write preserves its neighbors.
The RV2Wide memory adapter fixtures also check consecutive
precise commits and younger-attempt suppression on retry/fault. Packed and RV32
fixtures cover transport and row geometry. The `vector-memory-element-step-rv32`
and `vector-memory-element-step-rv64` fixtures check the issue and retry addresses
across segment fields, strides, nonzero `vstart`, and backpressure.
The `vector-issue-address-rv32` and `vector-issue-address-rv64` fixtures switch
modes at tail issue and tail authorization, stall a retry with younger beats
outstanding, and check packed replay on both sides of XLEN address wraparound.
The `vector-gather-index-rv32` and `vector-gather-index-rv64` fixtures check the
dependent read on the VRF return cycle and after a stall, including high-index
aliases for both ordinary and EI16 gather.
The sequencer fixtures also stall a final compression write, cancel as its port
reopens, and immediately read the old destination with the next instruction.
They separately retry the final write and check that the authorized byte is restored.

The packed fixtures hold the VRF write grant low throughout stores, checking ring
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

Run `vector-packed-carry-rv32` and `vector-packed-carry-rv64` for partial-row
storage changes. Their public-port oracle stalls partial assembly and closed-owner
flushes, checks foreign-row claims across owner handoff and slot reuse, then
writes overlapping bytes under the next owner without stale enables. Every
split position also exercises same-edge prefix write/suffix capture and a stalled
merge that finishes the next row. Keep both instrumented packed fixtures and
`rv5stage-vector-overlap` for integration and accepted-byte lineage.
The `vector-packed-carry-reuse-rv32` and `vector-packed-carry-reuse-rv64`
variants force the successor to reuse the same physical slot while the old
carry is stalled; check both owners' row hazards before and after the flush.

The RV32/RV64 packed fixtures also run production event instrumentation. Their
byte-level oracle derives VRF contributors from accepted public transport
addresses and masks, checking exact write-parent sets through reordered returns,
ring reuse, masked/segmented assembly, split rows, carry flush, and replay.

The `event-vector` rsim fixture checks result-completion versus slot-release
site ownership and capture schemas alongside its dynamic sequencing oracle.
The sequencer fixtures force selected uncared decode modifiers high to exercise
their unobservability through legality, operand preparation, and writeback.
Care-mask regressions remain in `../../rv5stage/tests/vector-ctrl-test.rhm`.
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
