<!-- Routes vector configuration, unrolling, memory ownership, and storage to their validation owners. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the vector path

Read [README.md](README.md) for port timing, geometry, and caller obligations.
Architectural geometry lives in `riscv/isa/vector.rhm`; these modules own the
named core's physical chunk storage and adapters. Do not add instruction
recognition to `cores/simd-alu.rhdl` or hardware dependencies to the pure model.

`riscv/isa/v.rhm` owns initial instruction formats and encodings, and
`riscv/rtl/vector.rhdl` materializes stateless vtype/AVL rules. Core decode owns
the vector control column; `decode/core-ctrl.rhdl` alone adds scalar source and
system controls. `vector/csr.rhdl` owns retained vector state. Its parent CSR
file gates writes on successful WB, enforces VS access, and handles traps.
The candidate configuration input is a combinational preview; a separate
`Pulse` authorizes it only after legality and exception checks. Keep preview
independent of that authorization path.
`bundles.rhdl` owns flat packed-beat/result types, context-bearing macro
requests, and lightweight issue tokens. Keep these independent of the parent
pipeline bundle definitions; scalar bundles must not contain packed vector data.
`unroller.rhdl` owns descriptor retention, synchronous-read credits, issue
position, and ordered authorization progress. `execute.rhdl` is combinational:
it adapts the beat to the shared SIMD unit and packs its result, not a separate
pipeline stage. The parent [`vector.rhdl`](../vector.rhdl) owns their composition,
the register bank, and private EX/MEM/WB storage. An atomic fork couples scalar
bookkeeping admission to private operand capture without splitting handshakes.

Configuration follows ordinary serializing system instructions through
`core.rhdl`. Integer issue tokens use its existing EX/MEM/WB boundaries while
packed data follows the vector module's parallel three-cycle path. The core
passes WB outcomes back as nonstallable feedback, never packed register writes.
The vector pipeline asserts result alignment and returns a last-completion pulse.
Only the last authorized beat updates scalar retirement/PC/NTL macro state;
vector CSR completion waits for VRF drain. Cancellation flushes speculative private validity, and the core
must exempt a vector's own last-beat prediction repair from owner cancellation.
The original vector macro crosses the scalar pipeline as a side-effect-free
launch token. Resolve its scalar, base, and stride operands through the ordinary
EX bypass selectors and capture the decoded macro in one vector-only retained
context at the accepted EX occurrence. The generic EX/MEM and MEM/WB payloads
carry only the launch token; WB combines that token with the retained context
before admitting the macro to the unroller. A launch in EX/MEM/WB blocks younger
Decode, so the context cannot be replaced and WB request readiness is reserved
without making WB elastic. Keep issue occupancy distinct from accepted memory
completion ownership.
Interrupts and vector/state observers wait for both; scalar memory admission
uses the asymmetric barriers documented in the README.
Do not turn the experimental VLEN option into a public ISA/profile claim.

`fp.rhdl` adapts singleton operands and the shared physical execution control to the
shared FP request. It imports the named FP contracts, RISC-V
boxing helpers, and HardFloat types; none of those modules imports vector
execution. The parent pipeline reserves completion slots for both memory and
FP, captures rounding at WB macro launch, and queues operands only at WB.
Fused operations reuse the third general VRF read for old `vd`; comparisons
retain a mask-destination bit beside their completion slot and write the shared
`v0` shadow through the sole ordered VRF write port. Vector-scalar FP checks the
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
The core composes `RV5StageFpScalar` and vector requests around one execution
service. Keep scalar FPR ownership separate from vector slot ownership, and
merge simultaneous architectural flag pulses without arbitration loss.
Use `rv5stage-vector-fp` and `rv5stage-vector-fp-one-slot` for vector-vector and
vector-scalar add/multiply,
divide/square-root latency, sign/minmax, fused-source topology, comparison-mask,
FPR producer forwarding, NaN-box validation, same- and mixed-width conversions,
RTZ/round-to-odd, rounding, flag, cancellation, and ordered instruction-to-memory-result coverage, alongside
the scalar FP and existing vector memory/unroller fixtures when these shared
boundaries change.

`muldiv.rhdl` adapts singleton integer operands and width/result selectors to
the tagged integer service contracts. Its multiply tag separately retains
ordinary low/high/widened selection and `vsmul` rounding mode. Widening result
placement comes from the WB-owned result beat rather than changing source SEW
in that tag. For multiply-accumulate, the third general VRF read captures old
`vd`; the WB-owned completion entry retains the selected addend and add/subtract
policy rather than widening the shared multiplier tag. The completion slot
also retains fractional-multiply saturation until ordered drain can update
`vxsat`.
`../integer-execution.rhdl` owns opaque tag retention around the reusable
iterative units; scalar adapters in
`../multiply.rhdl` and `../divide.rhdl` own W-result and GPR destination policy.
The core owns separate round-robin scalar/vector arbiters for each unit. Keep
the scalar one-entry WB queue independent of vector admission: Decode's
reservation is for queue space, not an idle shared execution unit.

Run `rv5stage-vector-muldiv` and `rv5stage-vector-muldiv-one-slot` for all 39
encodings, supported source/result widths, every `vxrm` mode, fractional LMUL,
fractional saturation, scalar contention, masks, restart, empty bodies,
in-place and three-source writes, widening signedness, branch squash, and slot reuse. Retain `rv5stage-multiply`,
`rv5stage-divide`, and RV32/RV64 Zkt regressions when changing scalar adapters.
`rv5stage-integer-execution` checks opaque owner tags, result backpressure,
same-edge replacement, and reset with both services holding results.
The control fixtures cover RV32/RV64 legality and register-group alignment;
the unroller and FP fixtures cover the shared beat/completion layout.

Memory beats use their resolved data EEW and singleton element positions. Keep their
slot identifier in the `RV5StageMemoryWriteback.Vector` variant, and propagate
the complete union opaquely through the LSU.
The unroller owns full speculative and WB-authorized memory element bases.
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
Keep indexed-load destination/index groups disjoint until the unroller has a
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
`vd/vs3 + field * ceil(EMUL)`. A macro-local operation sequence,
not the architectural element, selects completion slots and ordered drain, so
several fields of one segment cannot alias the same slot. On retry, restore the
operation sequence, element, field, and address checkpoints together. Fault
reporting remains element-granular because architectural `vstart` counts whole
segments.
The private pipeline retains destination mask/shift and element range; the
profile's power-of-two `vector_completion_slots` reserved slots absorb hit and slow completions independently before ordered
VRF drain. Reserve on issue, authorize only at WB, and clear only unauthorized
slots on retry/cancel. Retry flushes younger scalar EX/MEM tokens without
redirecting fetch to the macro PC. Faults update `vstart` and keep accepted
response ownership alive through precise-trap draining.
Fault-only-first loads additionally require at most one unresolved issued
access. Element-zero faults use ordinary fault feedback; later-element faults
use truncation feedback, discard speculative younger beats, update `vl` from
the vector pipeline's private element position, and retire without entering a
trap handler. Keep the scalar stage payload to the one-bit truncation policy;
do not expose the element cursor outside the vector pipeline.
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

Slot selection is the macro-local operation sequence modulo the configured depth. Depth one
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
source reads and a fixed seed address. The parent pipeline gates reduction
issue until the preceding beat reaches WB, substitutes the authorized
accumulator for subsequent seeds at EX, and updates it only on authorization.
Widening reductions keep source addressing at SEW while the execute adapter
extends that element into the twice-SEW accumulator width. The seed and result
remain single-register scalars; do not route this form through doubled-EMUL
destination scheduling. Decode rejects SEW64 and different-EEW source aliases.
Do not move accumulation into read/issue time without a speculative checkpoint
design. Final-only VRF writes make source, seed, and mask overlap safe.
The `scalar_result` Valid output carries the existing integer register-write
type at WB; the core composes it with normal writeback through Flow. Decode
owns the scalar destination/source metadata and rejects nonzero reduction
`vstart` before the unroller can launch a read.

`mask.rhdl` owns decoded scan controls and a combinational parallel prefix
network. Decode selects count/first/mask/element results and prefix/index
modifiers; the datapath does not recognize instructions. The unroller reads
source masks at EEW=1 through existing ports and bounds every scan's enables
to that beat's exclusive end. Iota uses data-width beats; queries and first-bit
masks use 64-mask-bit beats. The parent retains scan carry separately from
VRF write data and serializes dependent beats through WB. Index is stateless.
Admission seeds carry, authorization advances it, and retry retains it.
Only final query results select GPR WB.

The reduction fixtures also exercise all seven scans across SEW/LMUL, source
mask aliases, bit-63/64 boundaries, all-masked/empty inputs, packed results,
nonzero index restart, retries, and partial cancellation. Control fixtures
sweep source/destination/group/mask legality. The full-core muldiv fixture
checks scalar consumers, WAW/x0/squash, vector signatures, empty queries, and
the six nonzero-vstart traps. Include the existing unroller fixtures when
changing their common result payload. `rv5stage-vector-mask-512` reuses the
transaction scoreboard with eight words per register to check SEW8 prefix
count and element-index truncation beyond 255.

Run `rv5stage-vector-reduction` and `rv5stage-vector-reduction-rv32` for
production-pipeline tests of both scalar moves and ten integer reductions.
They initialize/read storage through public LSU transactions (including a
test-only RV32 initialization transport, not an RV32 memory-ISA claim), fold
elements with an independent model, and cover SEW/LMUL, masks, aliases, tails,
empty bodies, issue stalls, initial/midstream retry, and partial cancellation.
The full-core `rv5stage-vector-muldiv` program covers GPR consumers, deferred
WAW interlocks, x0, squash, empty-body moves, and illegal reduction `vstart`.
Keep the existing unroller, control, scalar-core, and shared-FP fixtures when
changing their common result/decode payloads.

Slides retain destination-order progress while selecting source chunks from
the signed displacement in `unroller.rhdl`. Keep full XLEN offset information
until source bounds are checked; never wrap a large offset into VRF address
bits. Per-byte validity handles both negative upward prefixes and fractional
groups smaller than a physical row. Read context retains those bounds alongside
the beat. The low byte offset depends only on the retained displacement because
every issue/retry cursor is destination-chunk-aligned.

Preselect the lower chunk's high bytes and upper chunk's low bytes, then use
the existing SIMD E64 rotate-right path in `execute.rhdl`. Slide1 insertion
happens before rotation: SEW-aligned displacement preserves each byte's
element-local scalar position. Decode selects shift/rotate controls directly.
Override the ALU's physical E64 write mask with byte enables derived from the
architectural SEW, without changing ordinary SIMD shift behavior.
Increasing destination order makes legal in-place downward slides replay-safe:
committed writes cannot overwrite any later beat's needed source elements.
Upward overlap rejection belongs in decode.

The unroller fixtures compare slide writes against an architectural snapshot
across all supported SEW/LMUL geometries, offsets, masks, partial chunks,
in-place downward execution, stalls, initial/midstream replay, and cancellation.
They require consecutive WB beats on dense slide streams. Reduction fixtures
also exercise slides through the production vector pipeline, with public LSU
initialization/readback and partial cancellation followed by restart. The core
muldiv fixture covers scalar producers, nonzero vstart, branch squash, source
reads beyond VL, and reserved overlap. Run the RV32/RV64 control fixtures for
group/source/mask legality and the shared-FP fixture for common unroller changes.

Gather keeps index and destination geometry separate. Decode owns EEW16 index
EMUL, group alignment, and physical interval overlap checks. The unroller
retains the index-read beat and its mask through a second flushable Valid pipe;
this context reserves the eventual issue slot before either read. Port zero
belongs to the dependent data read in the index-response cycle, so the next
index read may overlap the preceding data response but not its request.
Retry/cancel flush both read contexts and the pending issue queue together.
Never truncate an unsigned index before comparing it with data VLMAX.

The vector gather beat carries the raw source word and a rotation displacement
that places its selected element directly in its destination slot. Execute
uses the existing E64 rotate path and singleton destination mask. Scalar and
immediate gathers instead rotate to element zero, then replicate that SEW
slice into the packed destination word. They use the normal packed schedule;
their immutable source word can be reread without adding retained broadcast
state. Do not add a VRF port or a full-vector permutation datapath.

Run RV32/RV64 control/unroller fixtures for independent index/data geometry,
full-width bounds, aliases, source reads beyond VL, masks, replay, and reset
or cancellation in both read phases. The production reduction fixtures cover
gather through public LSU initialization/readback, authorized-prefix retry,
and partial cancellation followed by restart, at VLEN128/256/512. The core
muldiv fixture covers index producers, scalar hazards, vstart, squash, and
reserved overlap. Keep the shared-FP fixture as a common-read-path regression.
`rv5stage-vector-unroller-1024` checks valid scalar and EI16 indices above 255,
which smaller legal EI16 groups cannot reach.

Compression owns cross-word state in `unroller.rhdl`, not in the reusable SIMD
component. `SimdCompress` returns only a compacted 64-bit word and selected
element count. The unroller appends that word to a retained suffix and places
at most one full destination chunk on each source-read beat. If the final
source beat emits a full chunk and leaves a suffix, a backpressurable flush
beat emits the remaining partial chunk without consuming another VRF read.

Keep speculative and authorized compression checkpoints separate. Every beat
carries its post-beat suffix, count, and destination element; only ordered WB
feedback may advance the authorized copy. Retry restores the source frontier
and compression state together, including a pending final flush. Do not derive
progress from destination writes: a source beat can select no elements and
still advance the architectural source frontier. Decode owns fixed-vm,
nonzero-vstart, group alignment, and selection-mask disjointness.

The direct SIMD fixture checks the word compactor against an independent lane
oracle. The unroller fixtures cover every SEW/LMUL geometry, sparse/dense/empty
masks, full and partial final chunks, stalls, retry, cancellation, and retained
checkpoints. The production reduction fixtures initialize and read back the
vector bank through public LSU traffic and exercise the `v0` shadow through
predication, while the full-core muldiv fixture checks decode, WB-authorized
writes, tail preservation, squash, and illegal vstart.
Run the RV32/RV64 control fixtures whenever compression legality changes.

`packing.rhdl` handles runtime SEW, broadcasting, lane enables, direct unary
2x/4x/8x extension, widening halves, and destination packing. Global element
position is distinct from enabled-lane
count. Keep overflow bits until destination bounds are checked. Local `legal`
outputs are not architectural group/overlap permission or WB authorization.

Widening schedules destination-width beats in `unroller.rhdl`. Narrow-source
lower/upper pairs reread one 64-bit source row. Wide-source forms instead derive
the `vs2` row from destination-width geometry while their narrow vector/scalar
source retains lower/upper selection. Each beat independently advances the
exclusive WB frontier, making retry reconstructible without a speculative
source buffer. Decode owns doubled EMUL, SEW64 rejection, wide-source alignment
and in-place permission, and the narrow source's high-part-only overlap rule;
packing owns per-source signed extension, already-wide left selection, and half
selection.

Narrowing shifts use the same half-row schedule in the opposite direction.
Each beat reads one doubled-width `vs2` row and the corresponding half of a
narrow shift-amount row, executes through the existing SIMD shifter at twice
SEW, and writes one narrow destination half-row. Keep the architectural
destination width distinct from the ALU execution width at result packing.
Ascending issue makes low-part `vd=vs2` overlap replay-safe: an authorized
prefix can overwrite only wide source elements that have already been read.
Decode owns doubled source EMUL/alignment, low-part overlap, different-EEW
source disjointness, and masked v0 restrictions. Run the RV32/RV64 control and
unroller fixtures for all three forms, SEWs, masks, partial halves, in-place
operation, stalls, retry, and cancellation.

Fixed-point scaling and averaging reuse the SIMD tapered shifter and
lane-isolated adder. Saturating add/sub also reports overflow from that physical
adder. Keep `vxrm` in the admitted macro snapshot, not as a live execution
input. Narrowing clip policy belongs in the result packer because it sees the
rounded doubled-width lane and the destination width together. Carry a
per-beat saturation bit from either arithmetic saturation or clipping to WB;
only authorized WB may pulse the CSR bank, and explicit CSR writes take
priority over the sticky set.

Keep execution enables separate from `select_right`: merge consumes `v0` as
data while both selected alternatives remain writable. Move rows describe only
their real source and select the existing SIMD right-input path. Mask-logic rows
select a 64-mask-bit schedule in `unroller.rhdl`, with a retained
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

The RV32/RV64 unroller fixtures cover all fourteen move/merge/mask encodings,
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

Run from the repository root with one fresh compiled root:

```sh
export PLTCOMPILEDROOTS="$(mktemp -d)"
tools/run-racket-tests.sh riscv/tests/vector-test.rhm
FIXTURE=rv5stage-vector bash tests/backend/run-circt.sh --simulate-only
make check-boundaries
```

For configuration, run `riscv/tests/vector-isa-test.rhm`,
`riscv/tests/csr-test.rhm`, `cores/rv5stage/tests/vector-ctrl-test.rhm`,
and the `rv5stage-vector-control`,
`rv5stage-vector-control-rv32`, and `rv5stage-vector-config` backend fixtures.
The first two compose real decode/CSR state with independent RV32/RV64 SV
checks. The last executes configuration, dependent scalar results, CSR reads,
branch squash, integer writes, macro-only `minstret`, VS Dirty, empty-body
retirement, and illegal register groups through the real core/frontend. Its
SV observer binds only to the reusable VRF's public write port, never storage.
This exercises the production vector pipeline's issue/commit alignment and
last-beat feedback through the scalar core, not a replacement execution model.
Two full LMUL=8 streams require sixteen consecutive WB writes each, with exact
row/data/mask checks across private EX/MEM/WB overlap and in-place reuse.
The signature-memory model rejects each store once, then retains readiness
until acceptance, exercising replay without periodic readiness/retry phase lock.
The `rv5stage-vector-unroller` and `rv5stage-vector-unroller-rv32` fixtures
compose real decode/VRF/execute with a flushable WB boundary. An independent
element model checks all decoded packed integer operations, fixed-point averaging,
saturation, rounding, and clipping across every `vxrm` mode, SEW/LMUL, partial bodies,
mask writes, in-place operations, randomized issue stalls, authorized-prefix
retry, and cancellation. Keep the retry test's downstream flush explicit.
Whole-register move coverage must include every NREG and SEW, independence from
`vl` and LMUL, nonzero `vstart`, register-boundary crossing, equal source and
destination groups, retry, cancellation, and writes to the `v0` shadow.
The production reduction fixture uses guaranteed-overflow clips to prove that
authorized beats pulse saturation once, replay does not duplicate the pulse,
and cancellation exposes only its already-authorized prefix.
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
warm-hit throughput, hits completing ahead of a delayed miss, scalar-load
overlap with a vector-load tail, and both asymmetric scalar/store barriers. Keep ordinary
scalar and RV32F/RV64D core regressions when shared LSU metadata changes.
The control fixtures sweep EEW/SEW/EMUL and destination alignment independently.
The `rv5stage-vector-memory-one-slot` and
`rv5stage-vector-memory-sixteen-slots` specializations reuse that architectural
scoreboard to cover zero-index head wrap and four-bit slot reuse. Only the
single-slot run omits multi-slot throughput/overlap requirements; all variants
retain signature, replay, ordering, mask, and precise-fault restart checks.
The IO-MSHR fixture additionally returns slot fifteen through the physical
router and shared RN-I engine under contention and cancellation; this guards
against an adapter silently retaining the former three-bit slot width.
