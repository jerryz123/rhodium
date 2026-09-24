<!-- Routes vector configuration, sequencing, memory ownership, and storage to their validation owners. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the vector path

Read [README.md](README.md) for port timing, geometry, and caller obligations.
Architectural geometry lives in `riscv/isa/vector.rhm`; these modules own the
named core's physical chunk storage and adapters. Do not add instruction
recognition to `cores/simd-alu.rhdl` or hardware dependencies to the pure model.

## State ownership and reading order

Read the execution path in this order. Component boundaries follow state
lifetimes; they do not introduce additional pipeline stages.

| Component | Owns | Releases ownership when |
|---|---|---|
| Parent `../vector.rhdl` | Two-entry WB admission FIFO and head-only page certification | The head dispatches to execution |
| `instructions.rhdl` | Instruction IDs, pending-kind summaries, destination-row intents, progressive row frontier, and store-drain barrier | Sequencing/issue is closed and all owned slots and carry have drained |
| `sequencer.rhdl` | One descriptor, beat cursor, address setup, and authorized restart checkpoint | Final read-request transfer for ordinary compute, final feedback for serialized work |
| `operand-fetch.rhdl` | Synchronous read credits, captured context, gather return, and compression suffix | Prepared operands transfer or unauthorized preparation is flushed |
| `pipeline.rhdl` | Composition, existing private execution registers, recurrence state, and shared-service request queues | Each beat reaches compute maturity or a memory decision |
| `completion.rhdl` / `slots.rhdl` | Slot ownership metadata, direct result writes, and ordered metadata reclamation | A result writes; its metadata is reclaimed at the head; replay drops only unaccepted slots |
| `packed-memory.rhdl` | Packed request cursor, masks, store reads, and request preparation | Final packed acceptance, or replay restores the rejected cursor |
| `packed-load.rhdl` | Packed layout checkpoints, accepted responses, byte assembly, and partial-row carry | Accepted words and the final partial row drain |

`geometry.rhdl` provides focused combinational helpers for beat limits, widths,
lane counts, and VRF operand requirements. There is no schedule payload or
additional scheduling state. `bundles.rhdl` separates instruction descriptors,
operand-free `BeatControl`, operand-bearing beats, owned results, and drain
events. `BeatControl` carries only beat-specific progress, address, mask, and
destination. The read context captures its instruction descriptor once;
operand fetch derives widths, rounding modes, and decoded controls from that
captured descriptor, never from the sequencer's current instruction.
`packed-bundles.rhdl` supplies transport layouts shared by packed preparation
and assembly.

Instruction IDs and completion-slot IDs are different lifetimes, even though
both derive their width from the completion-slot capacity. Never route an old
response through the current sequencing ID. The completion owner publishes a
read-only slot snapshot for routing and hazards; it alone mutates slot state.

Keep these events distinct: WB admission, sequencing release, final issue,
irreversible acceptance, direct result write, ordered metadata reclamation, and architectural
retirement. An instruction can release sequencing while its completion slots
remain live. Independent nonstallable completion lanes must remain independent:
do not merge simultaneous responses with a lossy Valid arbiter. Flow connections
compose transfers; fixed-latency context pairing must not acquire new queues or
waiting joins during a readability change.

There is one in-order sequencer: `sequencer.rhdl` retains exactly one accepted
descriptor and never alternates among instructions. `operand-fetch.rhdl` is a
separate downstream stage that owns synchronous VRF reads, response alignment,
operand packing, dependent gather reads, compression carry, and credited result
buffering. The sequencer advances only on a Decoupled read-request transfer. An
ordinary compute tail transfer releases the descriptor and may simultaneously
accept its replacement; the replacement's first read comes from those registers
in the following cycle. There is no incoming-descriptor read bypass, prepared
successor, or second instruction slot inside the sequencer. The flow-through result queue permits
consecutive issue while credits cover all nonbackpressurable responses.
Memory retains its descriptor through the final external decision; dependent
scans and compression retain theirs through internal result maturity. Stateless
index scans release on their final read-request transfer like ordinary compute.
Reductions release their descriptor at the tail read and retain recurrence in
owner-indexed state. Stateful and packed schedules wait for older operand
preparation to drain before admission; ordinary compute can overlap it. A packed
admission coincident with an older final read retains its descriptor, but packed
VRF activity and issue wait for that read's reservation to clear. The packed-memory
schedule consumes the same accepted descriptor; it is not another sequencer. `vector.rhdl` owns a two-entry
descriptor FIFO so WB admission and head-only page-range certification can overlap
the active owner. The FIFO has empty-queue flow-through and same-cycle full
replacement; an idle sequencer can accept a WB descriptor on its admission edge.
The scalar pipeline does not reserve its space in Decode: WB
either transfers the descriptor and any floating-point scalar-result reservation
atomically, or precisely replays the instruction with no vector-side effect.
Integer scalar results are bounded by the completion-slot count. It is not a
bank of per-service instruction queues. Check-started, check-complete, and
certificate state belong to the FIFO head and reset on dispatch, never on tail
enqueue. Compute heads dispatch without a preparation cycle. Memory heads wait
for their one precheck response when eligible; false selects elementwise fallback.
Empty memory retires at dispatch without acquiring a page window. The existing
uncertified-memory retirement barrier starts at enqueue, including for a tail
entry. Per-kind enqueue/dequeue counts cover all queued loads, stores, and FP
work; transfer to execution must not create a cycle without pending ownership.
Do not use pending
descriptor state to gate the older owner's issue stream, and release a page
window only when the descriptor that acquired it finishes sequencing. An older
compute descriptor can issue its final beat after the current memory descriptor
has acquired the page window; its issue-completion pulse must not release that
window. The completion-slot-sized owner ring lets accepted work outlive sequencing
ownership. Persistent slots retain route and owner identity across descriptor
replacement. Keep front admission, sequencing release, execution allocation,
compute maturity or memory acceptance, service-result arrival, and drain distinct.

Every ordinary beat, including immediate integer results, reserves a slot in
one persistent ring. Packed beats share its allocation/acceptance/drain frontier;
their layout and partial-row carry retain independent completion ownership.
Replay restores only the current unauthorized suffix. Result routing uses the
slot's route, never the current descriptor's route, and no launch resets the ring.
All accepted operands are captured, so older issued instructions have no unread
VRF sources. Older pending writes block reads by 64-bit row; `dependencies.rhdl`
computes conservative destination groups while pending writes have not yet
resolved to individual rows. For monotonic same-width elementwise compute,
`instructions.rhdl` advances an owner-local row frontier when the last beat for a
row resolves. The result's exact completion slot already owns that row until
its actual write, so this handoff needs no speculative issue-time write address
or operand-fetch reservation. Irregular and replayable schedules keep their
whole-group claim until final resolution. The sequencer checks the next
destination row as well as source rows, and packed loads check their mapped
destination rows before issue. These checks preserve WAW order without a
macro-level sequencer-admission gate. Gather's
dependent second read waits for older writes before starting its nonstallable
read pair. There is no renaming, out-of-order instruction selection, or
interleaved instruction sequencing.

Certified contiguous macros select `packed-memory.rhdl` after the page check,
before execution allocation. Its byte/field cursor maps aligned XLEN requests
onto 64-bit VRF rows without an element-address multiplier. Ordinary unmasked
stores pipeline two contributing VRF reads into a credited, replay-flushed
queue that retains the unaligned words until the shared SIMD slice is free;
masked and segmented stores retain each read response through the explicit
row-gather alignment step.
Loads capture raw hit/delayed data in reserved slots and align only at ordered
drain. A completed prefix and carry suffix can update on the same edge.
Segment mapping remains explicit byte routing, separate from the rotator.
The existing `execute.rhdl` instance shares its SIMD E64 rotate slice between
ordinary execution and packed alignment. Fixed-cycle ordinary execution has
priority over buffered store preparation, then buffered load alignment.
Scheduled ordinary returns and final carry flush arbitrate the sole VRF write port
without lossy Valid arbitration; there is no second barrel shifter or VRF.

Packed retry restores the rejected beat's complete byte/field/address cursor,
drops younger read preparation and reservations, and retains accepted responses
and the partial-row carry. The aligned transport envelope must remain inside
the MMU certificate. A false certificate selects the original elementwise
sequencer; never treat a failed precheck as an architectural fault.
The `rv5stage-vector-packed` and `rv5stage-vector-packed-rv32` fixtures
check byte-accurate loads/stores, every legal head offset, masks, segments,
whole/mask transfers, replay, reordered returns, and sustained common-path issue.
The real MMU/cache vector-memory fixture remains the integration boundary.
`rv5stage-vector-admission` drives the production parent pipeline to check two
waiting entries, consecutive admission, full replacement, head-only precheck
under backpressure, both certificate results, empty bodies, tail pending flags,
captured instruction operands, delayed older load responses, final packed-beat
replay, and reset with queued work.

## Event ownership

`pipeline.rhdl` keeps the admitted descriptor's retained-storage contract
before the schedule fork, without emitting an admission or residency event.
The scope uses the existing sequencer occupancy and mode-specific sequencing release:
final read-request transfer for ordinary compute, final feedback for serialized
operations. A read transferred on the replacement edge still belongs to the old
descriptor; the following cycle's read belongs to its replacement.
Accepted slots and packed carry can outlive sequencing. The parent `vector.rhdl`
emits no launch checkpoint. The original scalar WB identity follows the existing
admission queue into each schedule's retained Flow.
The sequencer offers each pending read request independently of its address-setup
and older-write hazards. Its internal atomic fork requires setup, source
availability, and operand fetch to accept together, including in the standalone
sequencer fixture. The sequencer-owned `vector/s1.sequence` checkpoint records the
actual transfer and captures the instruction for slice naming; its stall
companion captures setup, `vs2_wait`/`vs1_wait` source-row, and aggregate
operand-fetch readiness failures without changing launch timing.
Operand fetch carries that occurrence through the VRF response and credited queue to
elementwise issue. Packed memory has no equivalent elementwise read request.
Elementwise issue remains in `pipeline.rhdl`; direct completion is owned by
`completion.rhdl`.
Both paths use the stable `vector/s2.issue` and `vector/complete` labels, with a
`packed` field distinguishing their beat geometry. Packed events additionally
carry transport byte count, store direction, byte mask, and slot metadata.
Site identity includes the instance path; consumers must not assume a label
uniquely identifies a site. These are micro-op/beat milestones, not whole-vector
instruction completion.
In `packed-memory.rhdl`, retained macro ownership reaches each offer. The fixed
attempt pipe reaches acceptance; `packed-load.rhdl` owns the accepted FIFO
contract through ordered beat release. A completion denotes
transfer into the masked row carry/write path, not raw response arrival.
`sequencer.rhdl` declares the retained descriptor's request-to-read-request
relation. Operand fetch carries that lineage through its fixed read-context
pipe, optional gather pipe, and credited result queue. Compression's optional
suffix retains its generating read context. These paths preserve lineage through
stalls and explicit flush. Capture on macro acceptance, preserve across retry,
and release only on the actual occupied-to-idle conditions. Never use the PC or
the replayable operation index as an occurrence identity.

Every ordinary local-compute completion follows the one-stage private execute
path and writes on its reserved cycle. FP and multiply reserve at shared-service
acceptance; variable services and ordinary slow loads retain their response
until an unreserved write opportunity. Memory hits reserve the fixed decision
cycle. Completed result data is not retained in the slot ring: only routing,
ownership, pending write rows, and multiply addends outlive execution.
Metadata is reclaimed in allocation order independently of actual write order.
Slow-memory trace ownership is retained per slot, allowing tagged returns in
any order; compute and service lineage follows the actual producing path.
Speculative reservations are not captures, and retry preserves accepted owners.
The completion checkpoint denotes actual completion/writeback, not metadata
reclamation. Packed assembly keeps its separate ordered transport storage.

`memory.rhdl` explicitly forks the lookup/context, request/decision, and
feedback/outcome branches. The private execute boundary supplies compute
maturity or a shared-service request directly.
Pair the optional LSU response with the same-cycle lookup context before the
existing decision pipe; a context-owned
fallback preserves absent responses and no join may add a wait. Observe its
registered result as `vector/memory.result`, qualified for enabled memory beats.
The shared cache owns `dcache/s1.access`; do not duplicate it in this adapter.

Run `event-vector` for exact public-transfer lineage,
retries, fault/truncation, no-write completions, stalled issue, direct completion,
slot reuse, and pending reset. The multi-slot case returns younger responses
first. Keep `rv5stage-vector-reduction`, `rv5stage-vector-config`, and the vector
memory/FP/muldiv fixtures as functional regressions for the affected paths.
`rv5stage-vector-overlap` checks consecutive issue for independent single-beat
compute macros and independently delays FP and memory responses to check
registered read-tail replacement, FP-to-store row chaining, route changes with
old responses outstanding, final memory-beat replay, overlapping-destination
admission and row-level write ordering, independent completion ahead of a held
FP result, persistent slot wrap, and a canceled packed prefix
whose partial-row carry still needs writeback.

## Implementation ownership

The shared [`memory-arbiter.rhdl`](../memory-arbiter.rhdl) has separate Valid
lookup and Decoupled transaction arbiters. A losing lookup gets an explicit
Replay result in the response cycle. The winner's identity accompanies its
retained store candidate through the commit cycle; later arbitration must not
select that owner. Tagged delayed responses route independently of current
requests. `rv5stage-memory-arbiter` checks this cycle-visible contract; use
`rv5stage-vector-memory` for integrated MMU/cache rejection, accepted-tail
ownership, simultaneous hit/response completion, and precise restart.

`riscv/isa/v.rhm` owns initial instruction formats and encodings, and
`riscv/rtl/vector.rhdl` materializes stateless vtype/AVL rules. Core decode owns
the vector control column; `decode/core-ctrl.rhdl` alone adds scalar source and
system controls. `vector/csr.rhdl` owns retained vector state. Its parent CSR
file gates writes on successful WB, enforces VS access, and handles traps.
The candidate configuration input is a combinational preview; a separate
`Pulse` authorizes it only after legality and exception checks. Keep preview
independent of that authorization path.
`bundles.rhdl` owns phase-specific beat-control/beat/result types, context-bearing macro
requests, and lightweight issue tokens. Keep these independent of the parent
pipeline bundle definitions; scalar bundles must not contain packed vector data.
`sequencer.rhdl` owns descriptor retention, read generation, the sequencing cursor,
internal maturity progress, and memory authorization progress. `bundles.rhdl`
owns the read-request boundary. `operand-fetch.rhdl` owns VRF ports, per-beat
context alignment, packing, gather, compression checkpoints, and response credits. Its downstream beat storage
survives ordinary sequencer replacement; it never selects or retains a successor
instruction. `pipeline.rhdl` attaches an owner to each ordinary read request,
allocates completion slots from that carried owner, and routes VRF returns by
the sampled read-port owner, not the current descriptor. Sequencing release,
final issue, direct result write, and ordered metadata reclamation are separate events.
`execute.rhdl` is combinational:
it adapts the beat to the shared SIMD unit and packs its result, not a separate
pipeline stage. The parent [`vector.rhdl`](../vector.rhdl) composes the execution
engine, memory attempt pipeline, macro ownership, and retirement outcome.
[`precheck.rhdl`](precheck.rhdl) computes a conservative contiguous byte footprint
using shifts and constant field-count sums, never an element-address multiplier.
It includes whole-register and packed-mask geometry, `vstart`, segment fields,
pointer normalization, alignment, and overflow. The parent freezes attempts
while the MMU's page certificate is pending. A false certificate is fallback,
not a fault: element masking and exact first-fault semantics remain in memory
execution. See [MMU ownership](../mmu/DEVELOPING.md) for pinned translations.
[`pipeline.rhdl`](pipeline.rhdl) owns the sequencer/VRF/SIMD composition and shared
service operands. An atomic fork couples local attempt admission to operand
capture. Compute has one fixed execute stage before local maturity or service
request acceptance. Memory has that execute stage plus two private stages to
its external decision. A rejected memory decision flushes younger attempts;
accepted results and service requests retain their ownership.
[`memory.rhdl`](memory.rhdl) owns address/lookup, result classification, and
transaction acceptance. Its replay flushes younger attempts and operand results,
then restores the sequencer checkpoint. It never replays an accepted transaction.
[`completion.rhdl`](completion.rhdl) owns direct writeback and ordered ownership
reclamation. Its [`slots.rhdl`](slots.rhdl) separates reserved slots from accepted
owners: replay releases only the former. Register-row hazards belong to
[`instructions.rhdl`](instructions.rhdl), not the slot tracker.
[`load-response.rhdl`](load-response.rhdl)
keeps packed hits and delayed responses independent for genuine byte assembly.
Ordinary hits own their reserved write cycle; delayed ordinary responses wait
at the memory producer until that port is free.

Configuration commits in order at WB through `core.rhdl`, but is not a global
vector-drain fence. EX computes configuration from the newest older in-flight
configuration or committed state, bypasses its VL result through the scalar
pipeline, and attaches the resulting `vl`/`vtype`/`vstart` snapshot to younger
vector launches. `RV5StageVectorOperationLegality` owns every state-dependent
non-configuration rule, including the operation-specific `vstart` restrictions,
and consumes that same snapshot. This allows a vector macro or another `vset*`
to follow in the next cycle while older admitted macros retain their own state.
Vector micro-ops do not traverse scalar EX/MEM/WB. The
private pipeline supplies its own nonstallable feedback and asserts result alignment.
Compute retires from its ordinary scalar WB launch token. Memory certification
updates scalar retirement/PC/NTL macro state once for an early retired macro;
the conservative memory path still uses its final external acceptance.
The macro outcome crosses one register before scalar retirement selection, so
LSU fault/admission cannot feed back into scalar request formation. Local retry
feedback remains same-cycle. Certified execution cannot later fault; assert that
only authorization or retry feedback occurs. Retain execution ownership until
VRF/shared-service/memory drain independently of architectural retirement.
Vector CSR observers wait for this drain. An allocated macro is older than
subsequent scalar redirects and must not be canceled by them.
The original vector macro crosses the scalar pipeline as a side-effect-free
launch token plus a per-occurrence vector context. Resolve its scalar base and
stride operands through the ordinary EX bypass selectors and carry that resolved
context through EX/MEM and MEM/WB; do not restore a singleton side register.
Register numbers remain canonical instruction fields and are decoded from the
retained instruction instead of being copied into the context. Descriptor
capacity does not stall Decode: WB atomically enqueues and reserves scalar
destinations or replays without side effects. Pre-admission dependency and
certification hazards still account for older EX/MEM/WB launch tokens. Once WB
hands the descriptor to the vector path, sequencing ownership remains distinct from
accepted completion ownership.
Younger scalar exceptions are retained until all active macro contexts drain,
not merely the currently occupied completion slots. Interrupts and vector/state observers wait for both; scalar memory admission
uses the asymmetric barriers documented in the README.
Scalar vector results join the scheduled deferred GPR completion arbiter and
reserve their destination at WB allocation; do not merge a late result with
normal scalar WB using a lossy Valid selector. FPR results keep their existing
reservation path. Keep vector FP/CSR observers behind pending flag updates.
The vector issue fork reserves its one-cycle GPR write atomically with issue;
there is no queued completed scalar result.
Keep every public `VectorProfile` claim coupled to its ELEN/FP legality, implied
Zve closure, selected VLEN, UDB parameters, and SoC architectural description.
Only full V may set `misa.V`.
Keep orthogonal `VectorExtension` claims equally coupled to legality and shared
service specialization. `Zvfhmin` must remain independent of scalar `Zfhmin`
and must not admit any SEW=16 FP instruction beyond its two conversion forms.
Full `Zvfh` requires scalar `Zfhmin` or `Zfh`, admits the standard vector FP
surface at SEW=16, and admits only its six defined integer conversions at
SEW=8. `Zvbb` remains independently selected, adds only its extension-specific
decode rows, and reuses the packed integer controls. Keep those legality classes
explicit in decode and never merge extension instructions into base V.

`fp.rhdl` adapts singleton operands and the shared physical execution control to the
shared FP request. It imports the named FP contracts, RISC-V
boxing helpers, and HardFloat types; none of those modules imports vector
execution. The parent pipeline reserves completion slots for both memory and
FP. Dynamic FP rounding is part of each admitted descriptor and issued beat;
the engine queues operands directly from the private execute stage.
Fused operations reuse the third general VRF read for old `vd`; comparisons
retain a mask-destination bit beside their completion slot and write the shared
`v0` shadow through the sole scheduled VRF write port. Vector-scalar FP checks the
FPR scoreboard in Decode, selects the scalar source on the FP adapter's first
read port at WB launch, and retains the forwarded 64-bit value in the admitted
descriptor. It must not consume a general VRF read port. Do not add vector-only
FP datapaths or reinterpret scalar register controls as vector source metadata.
Conversion rows carry explicit source/result domains, source/result element
widths, and dynamic, RTZ, or round-to-odd selection. Width-changing FP remains
singleton: reuse the register-group overlap helpers, but do not route it through
the packed integer half-word schedule. Keep the result domain and destination
shift/mask in the completion entry; selecting integer versus boxed FP data or
reconstructing destination width from an opcode at drain would make ownership
implicit.
Widening arithmetic also remains singleton. Carry each operand's precision in
the shared request: `.wv/.wf` reads `vs2` at the result width, ordinary widening
reads both multiplicands at source width, and fused widening reads old `vd` at
result width. Exact promotion and signaling-NaN flags belong to the shared FP
datapath before its FP64 add, multiply, or fused operation; do not duplicate
those numeric units in the vector package.
The core composes `RV5StageFpScalar` and vector requests around one execution
service. Keep scalar FPR ownership separate from vector slot ownership, and
merge simultaneous architectural flag pulses without arbitration loss.
Use `rv5stage-vector-fp` for vector-vector and vector-scalar add/multiply,
divide/square-root latency, sign/minmax, fused-source topology, comparison-mask,
FPR producer forwarding, NaN-box validation, same- and mixed-width conversions,
widening arithmetic source topologies, RTZ/round-to-odd, rounding, flag,
cancellation, and ordered instruction-to-memory-result coverage, alongside
the scalar FP and existing vector memory/sequencer fixtures when these shared
boundaries change.

`muldiv.rhdl` adapts singleton integer operands and width/result selectors to
the tagged integer service contracts. Its multiply tag separately retains
ordinary low/high/widened selection and `vsmul` rounding mode. Widening result
placement comes from the accepted result beat rather than changing source SEW
in that tag. For multiply-accumulate, the third general VRF read captures old
`vd`; the accepted completion entry retains the selected addend and add/subtract
policy rather than widening the shared multiplier tag. Fractional-multiply
saturation updates `vxsat` on the scheduled product return.
`../integer-execution.rhdl` owns opaque tag retention around the reusable
iterative units; scalar adapters in
`../multiply.rhdl` and `../divide.rhdl` own W-result and GPR destination policy.
The core gives vector requests fixed priority over scalar requests at the
shared multiplier; the divider remains round-robin. The vector multiplier
request queue bypasses when empty, so an uncontended request attempts service
admission in the compute-maturity cycle, two cycles after sequencing. Keep
the scalar one-entry WB queue independent of vector admission: Decode's
reservation is for queue space, not an idle shared execution unit.
Before shared-service acceptance, fixed multiplication and FP reserve their
actual return cycle. Private compute and memory decisions reserve at S2 issue.
An older unrelated memory, divide, or FP operation does not block a fixed
product merely because its metadata is at the ring head. Variable returns
yield to occupied cycles, and packed assembly consumes only leftover cycles.
The integrated multiplier and FP services do not retain completed fixed data.

Run `rv5stage-vector-muldiv` for all 39
encodings, supported source/result widths, every `vxrm` mode, fractional LMUL,
fractional saturation, scalar contention, masks, restart, empty bodies,
in-place and three-source writes, widening signedness, branch squash, and slot reuse. Retain `rv5stage-multiply`,
`rv5stage-divide`, and RV32/RV64 Zkt regressions when changing scalar adapters.
`rv5stage-integer-execution` checks opaque owner tags, result backpressure,
same-edge replacement, and reset with both services holding results.
The control fixtures cover RV32/RV64 legality and register-group alignment;
the sequencer and FP fixtures cover the shared beat/completion layout.

Elementwise memory beats use their resolved data EEW and singleton element positions; certified
packed beats carry an aligned transport width and an explicit byte mask. Keep their
slot identifier in the `RV5StageMemoryWriteback.Vector` variant, and propagate
the complete union opaquely through the LSU.
The sequencer owns full attempted and locally accepted memory element bases.
Capture the base and element step once, warm the base through `vstart` with one
addition per skipped element or segment, advance on final-field issue and
authorization, and restore the authorized base on retry. Do not reintroduce an
element-index multiplier or reconstruct the address in the scalar pipeline.
Unit-stride and signed strided accesses share this sequencer; masking changes
the memory operation, not address progression. Indexed memory instead reads one `vs2`
offset through the ordinary second VRF port. Retain index EEW separately from
data EEW: the instruction encodes the former, while `vtype` supplies the latter.
Zero-extend the offset, add it to the captured base, and reread it after retry
from the authorized element cursor. Ordered and unordered forms may share
element-order issue until an explicitly unordered scheduler is introduced.
Keep indexed-load destination/index groups disjoint until the sequencer has a
source-preservation strategy for legal overlap. Indexed segments reuse the
same offset while the field cursor adds `field << SEW`; do not add another
address register or advance the element cursor before the final field.
Unit- and constant-stride segments retain separate speculative and authorized
field cursors. Form each access as `element_base + (field << EEW)`, and advance
the element base only after its final field. Unit stride steps the base by
`NFIELDS << EEW`; constant stride uses the captured signed `rs2`, including
negative, zero, and overlapping strides. Indexed segments instead retain the
scalar base and reread the current element's index for each field. Warm-up
skips one whole segment per addition for nonindexed forms. Destination/source field groups start at
`vd/vs3 + field * ceil(EMUL)`. A persistent ring, independent of the macro-local
operation sequence or architectural element, selects completion slots and
ordered metadata reclamation. Several fields and outstanding macros cannot alias a live slot.
On retry, restore the unauthorized allocation frontier alongside the operation
sequence, element, field, and address checkpoints. Fault
reporting remains element-granular because architectural `vstart` counts whole
segments.
The private pipeline retains destination mask/shift and element range; the
profile's power-of-two `vector_completion_slots` retain destination ownership
until actual writeback, followed by ordered reclamation. Reserve on issue,
accept at the local outcome, and clear only unaccepted
slots on retry/cancel. Retry flushes younger vector attempt/result stages without
redirecting fetch to the macro PC. Faults update `vstart` and keep accepted
response ownership alive through precise-trap draining.
Fault-only-first loads additionally require at most one unresolved issued
access. Element-zero faults use ordinary fault feedback; later-element faults
use truncation feedback, discard speculative younger beats, update `vl` from
the vector pipeline's private element position, and retire without entering a
trap handler. Keep the element cursor and truncation policy inside the vector
pipeline; scalar retirement receives only its final outcome.
Whole-register transfers remain a distinct memory mode, not a segment or an
ordinary unit-stride special case. Derive EVL from NREG, VLEN, and encoded EEW;
do not consult `vl` or decoded `vtype` geometry. Keep the global encoded-EEW
cursor continuous across the aligned register group so the existing VRF row
calculation selects successive registers. Unit-stride address setup, authorized
retry checkpoints, LSU completion slots, and precise `vstart` faults remain
shared with ordinary vector memory. Do not apply fault-only-first serialization
or truncation to this mode.
Mask-register transfers also remain a distinct memory mode. Iterate
`ceil(vl/8)` byte operations in one register, interpret `vstart` in bytes, and
keep their geometry independent of SEW/LMUL while still rejecting `vill`.
Reuse singleton E8 address, extraction, completion, and restart machinery; do
not expand the transfer into one LSU operation per mask bit. Loads into `v0`
must pass through the sole bit-enabled write port so the general row and mask
shadow stay atomic.

Slot selection is the persistent allocation frontier modulo the configured depth. Depth one
must explicitly produce zero and hold the drain head at zero; `index_width(1)`
still represents a one-bit hardware value. Keep the count on public token,
completion, and data-protocol types, not just on the private register arrays.

`register-file.rhdl` stores a flat `Vec(32 * VLEN / 64, Bits(64))` behind three
general read ports and mirrors the `VLEN / 64` physical chunks of `v0` into a
dedicated address-only mask shadow. Both paths use Flow
`map_valid`/`valid_pipe` to snapshot each read. Forward the bit-merged value at
the read edge, not a live write mux after the response register. Otherwise a
later completion could alter a prior read or create an in-place ALU loop. The
sole write port updates general `v0` storage and its shadow atomically using the
same bit mask. It uses row-local masked hold feedback, never a destination
snapshot captured by an earlier micro-op or an indexed read port for merging.

Element moves use a one-token schedule independent of VL; insertion separately
checks its architectural empty-body condition. Reduction rows use singleton
source reads and a fixed seed address. The parent pipeline gates each owner's
reduction issue until that owner's preceding beat matures, substitutes the
owner's accumulator for subsequent seeds at EX, and updates integer reductions
only at maturity. The tail read hands off the sequencer; its completion
slot and owner-scoped recurrence state retain the older macro independently.
Floating-point reductions retain their owner-local gate until an active fold
drains from the shared FP service, then advance that owner's accumulator from
the ordered result. Inactive folds bypass the service and retain the accumulator;
active intermediate folds update `fflags` but only the final fold carries a VRF
write mask. This implements both sum variants as the permitted ordered fold and
gives an all-masked nonzero body an exact seed copy with no FP exception
activity. A zero-length body remains the ordinary no-write vector case.
Widening reductions keep source addressing at SEW while the execute adapter
extends that element into the twice-SEW accumulator width. The seed and result
remain single-register scalars; do not route this form through doubled-EMUL
destination scheduling. Decode rejects SEW64 and different-EEW source aliases.
Do not move accumulation into read/issue time. The compute route is
non-replayable, and final-only VRF writes make source, seed, and mask overlap
safe.
The `scalar_result` Valid output carries the existing integer register-write
type at compute maturity; the core composes it with normal writeback through Flow. Decode
owns the scalar destination/source metadata and rejects nonzero reduction
`vstart` before the sequencer can launch a read.
The `floating_result` Valid output analogously carries a
`FloatingPointRegisterWrite` for `vfmv.f.s`. The scalar FP wrapper reserves the
destination before vector launch and consumes this output only for a mature
private result. Keep raw vector element bits through the sequencer and
NaN-box an SEW32 result only at this architectural boundary. Conversely,
validate an SEW32 scalar FPR box before packing its payload for `vfmv.v.f`,
`vfmerge.vfm`, `vfmv.s.f`, or the FP slide forms. These movement operations
reuse packed merge/slide hardware and must not allocate a shared FP-service
completion slot or contribute `fflags`.

`mask.rhdl` owns decoded scan controls and a combinational parallel prefix
network. Decode selects count/first/mask/element results and prefix/index
modifiers; the datapath does not recognize instructions. The sequencer reads
source masks at EEW=1 through existing ports and bounds every scan's enables
to that beat's exclusive end. Iota uses data-width beats; queries and first-bit
masks use 64-mask-bit beats. The parent retains scan carry separately from
VRF write data and serializes dependent beats through result maturity. Index is
stateless: its final read releases sequencing, its final issue ends issue
ownership, and it may enter while older operand preparation remains active.
Admission seeds carry for dependent scans, and maturity advances it.
Only final query results select GPR WB.

The reduction fixtures also exercise all seven scans across SEW/LMUL, source
mask aliases, bit-63/64 boundaries, all-masked/empty inputs, packed results,
nonzero index restart, and partial cancellation. Control fixtures
sweep source/destination/group/mask legality. The full-core muldiv fixture
checks scalar consumers, WAW/x0/squash, vector signatures, empty queries, and
the six nonzero-vstart traps. Include the existing sequencer fixtures when
changing their common result payload. `rv5stage-vector-mask-512` reuses the
transaction scoreboard with eight words per register to check SEW8 prefix
count and element-index truncation beyond 255.

Run `rv5stage-vector-reduction` for production-pipeline tests of both scalar
moves and ten integer reductions. It initializes and reads storage through
public LSU transactions, folds elements with an independent model, and covers
SEW/LMUL, masks, aliases, tails,
empty bodies, issue stalls, result maturity, and partial cancellation.
The full-core `rv5stage-vector-fp` program covers all six FP reductions,
unary classification and seven-bit reciprocal/reciprocal-square-root estimates,
their active-element exception behavior, masking, restart, replay, and squash,
same-width and widening folds, masks, empty vectors, exact seed retention, and
per-fold exception accumulation. It also covers the six scalar/vector FP
movement forms, merge masks, slide boundaries, invalid source NaN boxes, raw
NaN payload preservation, immediate scalar consumers, empty bodies, and
branch-squashed FPR writes. The full-core `rv5stage-vector-muldiv` program covers GPR consumers, deferred
WAW interlocks, x0, squash, empty-body moves, and illegal reduction `vstart`.
Keep the existing sequencer, control, scalar-core, and shared-FP fixtures when
changing their common result/decode payloads.

Slides retain destination-order progress while selecting source chunks from
the signed displacement in `sequencer.rhdl`. Keep full XLEN offset information
until source bounds are checked; never wrap a large offset into VRF address
bits. Per-byte validity handles both negative upward prefixes and fractional
groups smaller than a physical row. Read context retains those bounds alongside
the beat. The low byte offset depends only on the retained displacement because
every issue cursor is destination-chunk-aligned.

Preselect the lower chunk's high bytes and upper chunk's low bytes, then use
the existing SIMD E64 rotate-right path in `execute.rhdl`. Slide1 insertion
happens before rotation: SEW-aligned displacement preserves each byte's
element-local scalar position. Decode selects shift/rotate controls directly.
Override the ALU's physical E64 write mask with byte enables derived from the
architectural SEW, without changing ordinary SIMD shift behavior.
Increasing destination order makes legal in-place downward slides overlap-safe:
mature writes cannot overwrite any later beat's needed source elements.
Upward overlap rejection belongs in decode.

The sequencer fixtures compare slide writes against an architectural snapshot
across all supported SEW/LMUL geometries, offsets, masks, partial chunks,
in-place downward execution, stalls, initial/midstream replay, and cancellation.
They require consecutive accepted beats on dense slide streams. Reduction fixtures
also exercise slides through the production execution engine, with local
initialization/readback and partial cancellation followed by restart. The core
muldiv fixture covers scalar producers, nonzero vstart, branch squash, source
reads beyond VL, and reserved overlap. Run the RV32/RV64 control fixtures for
group/source/mask legality and the shared-FP fixture for common sequencer changes.

Gather keeps index and destination geometry separate. Decode owns EEW16 index
EMUL, group alignment, and physical interval overlap checks. The sequencer
retains the index-read beat and its mask through a second flushable Valid pipe;
this context reserves the eventual issue slot before either read. Port zero
belongs to the dependent data read in the index-response cycle, so the next
index read may overlap the preceding data response but not its request.
Cancellation flushes both read contexts and the pending issue queue together.
Never truncate an unsigned index before comparing it with data VLMAX.

The vector gather beat carries the raw source word and a rotation displacement
that places its selected element directly in its destination slot. Execute
uses the existing E64 rotate path and singleton destination mask. Scalar and
immediate gathers instead rotate to element zero, then replicate that SEW
slice into the packed destination word. They use the normal packed schedule;
their immutable source word can be reread without adding retained broadcast
state. Do not add a VRF port or a full-vector permutation datapath.

Run RV32/RV64 control/sequencer fixtures for independent index/data geometry,
full-width bounds, aliases, source reads beyond VL, masks, replay, and reset
or cancellation in both read phases. The production reduction fixtures cover
gather through public LSU initialization/readback and partial cancellation
followed by restart, at VLEN128/256/512. The core
muldiv fixture covers index producers, scalar hazards, vstart, squash, and
reserved overlap. Keep the shared-FP fixture as a common-read-path regression.
`rv5stage-vector-sequencer-1024` checks valid scalar and EI16 indices above 255,
which smaller legal EI16 groups cannot reach.

Compression owns cross-word state in `sequencer.rhdl`, not in the reusable SIMD
component. `SimdCompress` returns only a compacted 64-bit word and selected
element count. The sequencer appends that word to a retained suffix and places
at most one full destination chunk on each source-read beat. If the final
source beat emits a full chunk and leaves a suffix, a backpressurable flush
beat emits the remaining partial chunk without consuming another VRF read.

Keep speculative and mature compression checkpoints separate. Every beat
carries its post-beat suffix, count, and destination element; only ordered
compute maturity may advance the committed copy. Cancellation drops the
speculative source frontier and compression state, including a pending final flush. Do not derive
progress from destination writes: a source beat can select no elements and
still advance the architectural source frontier. Decode owns fixed-vm,
nonzero-vstart, group alignment, and selection-mask disjointness.

The direct SIMD fixture checks the word compactor against an independent lane
oracle. The sequencer fixtures cover every SEW/LMUL geometry, sparse/dense/empty
masks, full and partial final chunks, stalls, cancellation, and retained
checkpoints. The production reduction fixtures initialize and read back the
vector bank through public LSU traffic and exercise the `v0` shadow through
predication, while the full-core muldiv fixture checks decode, locally accepted
writes, tail preservation, squash, and illegal vstart.
Run the RV32/RV64 control fixtures whenever compression legality changes.

`packing.rhdl` handles runtime SEW, broadcasting, lane enables, direct unary
2x/4x/8x extension, widening halves, and destination packing. Global element
position is distinct from enabled-lane
count. Keep overflow bits until destination bounds are checked. Local `legal`
outputs are not architectural group/overlap permission or result maturity.

Widening schedules destination-width beats in `sequencer.rhdl`. Narrow-source
lower/upper pairs reread one 64-bit source row. Wide-source forms instead derive
the `vs2` row from destination-width geometry while their narrow vector/scalar
source retains lower/upper selection. Each beat independently advances the
exclusive maturity frontier without a speculative source buffer. Decode owns
doubled EMUL, SEW64 rejection, wide-source alignment
and in-place permission, and the narrow source's high-part-only overlap rule;
packing owns per-source signed extension, already-wide left selection, and half
selection.

Narrowing shifts use the same half-row schedule in the opposite direction.
Each beat reads one doubled-width `vs2` row and the corresponding half of a
narrow shift-amount row, executes through the existing SIMD shifter at twice
SEW, and writes one narrow destination half-row. Keep the architectural
destination width distinct from the ALU execution width at result packing.
Ascending issue makes low-part `vd=vs2` overlap safe: a mature prefix can
overwrite only wide source elements that have already been read.
Decode owns doubled source EMUL/alignment, low-part overlap, different-EEW
source disjointness, and masked v0 restrictions. Run the RV32/RV64 control and
sequencer fixtures for all three forms, SEWs, masks, partial halves, in-place
operation, stalls, and cancellation.

Fixed-point scaling and averaging reuse the SIMD tapered shifter and
lane-isolated adder. Saturating add/sub also reports overflow from that physical
adder. Keep `vxrm` in the admitted macro snapshot, not as a live execution
input. Narrowing clip policy belongs in the result packer because it sees the
rounded doubled-width lane and the destination width together. Carry a
per-beat saturation bit from either arithmetic saturation or clipping to WB;
only result maturity may pulse the CSR bank, and explicit CSR writes take
priority over the sticky set.

Keep execution enables separate from `select_right`: merge consumes `v0` as
data while both selected alternatives remain writable. Move rows describe only
their real source and select the existing SIMD right-input path. Mask-logic rows
select a 64-mask-bit schedule in `sequencer.rhdl`, with a retained
bit-enable mask for the partial first/last word. `execute.rhdl` reuses the SIMD
logic network and writes its packed data directly instead of comparison bits.
The single-register legality rule belongs in decode, not generic VRF geometry.
Do not make memory/FP scheduling depend on integer-only don't-care controls.

Carry/borrow operations also consume the dedicated `v0` shadow as data. Their
beat enables cover the complete body instead of applying predication, and the
SIMD guard-bit adder injects the selected `v0` bit independently at each element
boundary. Keep the architectural carry/borrow bit in `RV5StageVectorBeat`, not
as hidden ALU state. `vmadc`/`vmsbc` select the ALU mask result and use ordinary
mask write packing; `vadc`/`vsbc` select packed data. Decode must reject fixed
carry-input data results that target `v0` and SEW-wide sources that also name
`v0`. Retry rereads the retained macro's shadow data with the restarted beat.

The RV32/RV64 sequencer fixtures cover all fourteen move/merge/mask encodings,
legal SEW/LMUL combinations, single-register mask addressing, overlaps, partial
words, stalls, retries, cancellation, and empty bodies. The full-core
`rv5stage-vector-muldiv` program additionally checks their memory signatures,
scalar-source capture, branch squash, and `vstart` clearing alongside shared
execution regression. The control fixtures reject reserved vm/vs2 encodings
and merge into/from vector v0 while accepting scalar x0, immediate zero, and
unaligned single mask registers at large LMUL.

The composed `tests/vector-fixture.rhdl` captures request controls alongside
the bank read, executes the actual SIMD ALU, and optionally commits its result
through the same masked port used for initialization. Its independent SV
scoreboard checks public read/write behavior at VLEN 128, 256, and 512,
including in-place operations, register boundaries, bit-granular forwarding,
partial bodies, broadcasts, comparison/carry packing, widening, and reset. Do not
replace this with internal register-shape assertions or elaboration-only tests.

Run from the repository root through the persistent worktree-specific cache:

```sh
tools/run-racket-tests.sh riscv/tests/vector-test.rhm
FIXTURE=rv5stage-vector bash tools/testing/circt/run.sh --simulate-only
make check-boundaries
```

For configuration, run `riscv/tests/vector-isa-test.rhm`,
`riscv/tests/csr-test.rhm`, `cores/rv5stage/tests/vector-ctrl-test.rhm`,
and the `rv5stage-vector-control` and `rv5stage-vector-config` backend fixtures.
The first composes real RV64V decode and CSR state. The last executes
configuration, dependent scalar results, CSR reads,
branch squash, integer writes, macro-only `minstret`, VS Dirty, empty-body
retirement, and illegal register groups through the real core/frontend. Its
SV observer binds only to the reusable VRF's public write port, never storage.
This exercises the production vector pipeline's separate compute-maturity and
memory-decision alignment and last-beat feedback through the scalar core, not a
replacement execution model. Two full LMUL=8 streams require sixteen
consecutive VRF writes each, with exact row/data/mask checks across private
execute, scheduled completion, and in-place reuse.
The signature-memory model rejects each store once, then retains readiness
until acceptance, exercising replay without periodic readiness/retry phase lock.
The `rv5stage-vector-sequencer` and `rv5stage-vector-sequencer-rv32` fixtures
compose real decode/VRF/execute with a flushable result boundary. An independent
element model checks all decoded packed integer operations, fixed-point averaging,
saturation, rounding, and clipping across every `vxrm` mode, SEW/LMUL, partial bodies,
mask writes, in-place operations, randomized issue stalls, result maturity,
and cancellation.
Whole-register move coverage must include every NREG and SEW, independence from
`vl` and LMUL, nonzero `vstart`, register-boundary crossing, equal source and
destination groups, cancellation, and writes to the `v0` shadow.
The production reduction fixture uses guaranteed-overflow clips to prove that
mature beats pulse saturation once, and cancellation exposes only its
already-written prefix.
Changes to shared CSR payloads also require `rv5stage-csr` and the RV32/RV64
`rv5stage-zihpm-*` fixtures. These fixtures belong to `cores-execution`.

For vector memory, run `rv5stage-vector-memory` through the shared real
core/MMU/router/L1D fixture, plus `rv5stage-vector-config` for packed integer
regression. The memory bench covers all four EEWs, an EEW/SEW mismatch,
mask-register byte-count boundaries and `v0` shadow use,
whole-register transfer across a register boundary with `vl` independence and
an Sv39 fault repaired and restarted at the next register,
positive/negative/zero stride, indexed and three-field unit-stride,
constant-stride, and indexed segment operations, field/register mapping, additive segment-aware
`vstart` warm-up, masks, empty bodies, in-order device stores, request/CHI
backpressure, ordinary and segmented fault-only-first truncation, an
element-zero fault-only-first precise trap, and an indexed segmented Sv39
page-boundary fault repaired and restarted from `vstart`. It also requires
warm-hit throughput, hits completing ahead of a delayed miss, a scalar hit
during certified vector sequencing, scalar-load overlap with a vector-load tail,
and both asymmetric scalar/store barriers. The configuration bench checks
scalar WB before the last packed vector beat, younger precise exceptions,
and exact traced WB-to-sequencer ownership. Use `rv5stage-mmu-replay` for pinned
split-page/superpage translations, permission failure, and DTLB replacement.
Keep ordinary
scalar and RV32F/RV64D core regressions when shared LSU metadata changes.
The control fixtures sweep EEW/SEW/EMUL and destination alignment independently.
The `rv5stage-vector-memory-one-slot` specialization reuses that architectural
scoreboard to cover zero-index head wrap while omitting multi-slot
throughput/overlap requirements. It retains signature, replay, ordering, mask,
and precise-fault restart checks. The IO-MSHR fixture returns slot fifteen
through the physical router and shared RN-I engine under contention and
cancellation; this independently guards the four-bit slot path against an
adapter silently retaining the former three-bit width.
