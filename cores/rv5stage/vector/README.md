<!-- Defines RV5Stage vector configuration, decode, storage, and execution-boundary contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# XLEN-wide vector path

The opt-in `RV5StageConfig(~vector: profile, ~vector_length: vlen)` enables one
of the standard Zve profiles or V 1.0, vector CSR state, and vector memory
operations using unit-stride, constant-stride, indexed, unit-stride segment,
constant-stride segment, indexed segment, and unit-stride fault-only-first
addressing, plus mask-register and whole-register loads and stores and
whole-register moves. The default profile is `VectorProfile.None` with a
separate default VLEN of 128 bits. Every enabled profile advertises its implied
Zve closure and cumulative `Zvl<N>b` closure through its selected VLEN. Zve32
profiles select ELEN=32; Zve64 profiles and V select ELEN=64. FP32 profiles
require scalar FP support, while Zve64d and
V require scalar D. RV32 supports Zve32x, or Zve32f with scalar F, with a 32-bit lane and
VLEN starting at 64; RV64 uses a 64-bit lane with VLEN starting at 128.
RV32 shares the scalar F execution service for vector FP32, with 32-bit operands,
results, and VRF rows; 64-bit elements still require RV64. Only V advertises `V 1.0` and `misa.V`;
it does not imply Zvbb. Selecting `VectorExtension.Zvbb` independently enables
the ratified vector basic bit-manipulation instruction set for any enabled
vector profile and advertises its required `Zvkb` subset.
Every enabled RV5Stage vector profile also advertises `Zvkt`; this is the core's
intrinsic data-independent execution-latency contract, not another
`VectorExtension` selection. The exact architectural scope and control-operand
exemptions live in [`riscv/isa/zvkt.rhm`](../../../riscv/isa/zvkt.rhm), while
the core-level guarantee and limits are documented in the
[RV5Stage contract](../README.md#vector-data-independent-timing-zvkt).
`~vector_extensions: vector_extensions(VectorExtension.Zvfhmin)` independently
adds the standard F16-to-F32 `vfwcvt.f.f.v` and F32-to-F16 `vfncvt.f.f.w`
forms; it does not enable other FP16 vector operations or scalar `Zfhmin`.
Selecting `VectorExtension.Zvfh` with scalar `Zfhmin` or `Zfh` instead enables
the complete vector FP surface at SEW=16 and the standard six SEW=8 widening
and narrowing integer-conversion forms.
The reusable arithmetic stays in [`SimdALU`](../../README.md#packed-simd-integer-alu).

## Configuration and decode

The vector core executes `vsetvli`, `vsetivli`, and `vsetvl` in EX without
treating configuration as a global vector-drain fence. Only ordered WB commits
`vl`, `vtype`, and `vstart`; scalar `rd` receives the new VL. EX computes each
configuration from the newest older configuration or architectural state and
bypasses the result to younger `vset*`, scalar consumers, and vector-state
snapshots. Each vector macro carries that EX snapshot through WB admission, so
a vector instruction can immediately follow a configuration while older macros
retain their prior snapshots. Squashed or faulting operations do not update this state.
Supported physical geometry is SEW 8/16/32 on RV32 and 8/16/32/64 on RV64, with
LMUL 1/8 through 8, with the selected profile limiting architectural ELEN to
32 or 64, subject to SEW <= LMUL * ELEN. The instruction families described
below are additionally bounded by that ELEN; widening results must fit it.
Unsupported configurations
set `vill` and zero VL. Ordinary AVL selection uses `min(AVL, VLMAX)`;
`rs1=x0,rd!=x0` selects VLMAX, while `rs1=rd=x0` preserves VL only when the
old/new types are legal and VLMAX is unchanged. Reserved keep-VL uses trap.

The CSR bank exposes `vstart`, `vxrm`, `vxsat`, `vcsr`, `vl`, `vtype`, and
`vlenb`. `vstart` retains enough low bits for VLEN-1; `vxrm` and `vxsat` alias
`vcsr`. VL/type/VLENB are read-only. Reset starts with `vill=1`, VL=0, and
`mstatus.VS=Off`; VS Off blocks vector CSR access and configuration. Successful
configuration, vector CSR writes, vector completion, fixed-point saturation, or
a vector fault mark VS Dirty; reads do not, and SD combines the FP and vector
dirty states. Software may manage VS through M/S status.

The [vector control column](../decode/vector-ctrl.rhdl) describes same-width
wrapping, reverse, saturating, averaging, and carry/borrow add/sub; logic; shifts; fixed-point scaling shifts and narrowing clips;
comparisons, min/max, compression, narrowing shifts,
and narrow-source widening plus wide-source widening add/sub using direct SIMD controls, operand selection, extension
signedness, comparison inversion, and operand swapping. Runtime group checks
cover alignment, fractional groups, doubled widening EMUL, masked data
destinations, mask-result overlap, and widening source/destination overlap.
EX checks decoded rows against the same bypassed vector-state snapshot carried
to WB as a side-effect-free launch token. VS Off, `vill`, and invalid register
groups trap before launch.
The profile-specific legality boundary follows [RVV 1.0](https://docs.riscv.org/reference/isa/unpriv/v-st-ext):
Zve32 profiles reject SEW64, integer-only profiles reject vector FP, Zve FP
profiles admit only their supported FP widths, and the Zve64 profiles reject
the EEW64 high-half and fractional multiply operations reserved for full V.

## WB allocation and autonomous execution

### Event tracing

The optional event compiler observes sequencing and beat milestones:
`vector/s1.sequence` marks an accepted compute or memory beat and its fixed downstream
resource schedule, `vector/s2.issue` records the resulting nonstallable execution
attempt after operand capture, and `vector/complete` records a mature or
authorized beat's actual completion/writeback. Packed loads sequence with their
read enables clear; packed stores sequence their data reads or prepared bytes.
Every final sequence transfer reaches `s2.issue` one cycle later unless canceled
by replay. Dependent gather-index and multi-read store preparation precede that transfer.
The `vector/s1.sequence.stall` observation records a pending beat that could
not launch. Its fields report all failing acceptance conditions in that cycle:
`setup_wait`, `vs2_wait` and `vs1_wait` for the architectural source rows,
the destination-row and gather hazards, and downstream resource `fetch_wait`.
Several fields may be true at once; they are not
priority-encoded. Idle, completed, and canceled plans do
not generate a sequencing stall.
Each issue inherits its exact sequencing occurrence, and every
completion inherits its exact issue.
Retries create fresh issue occurrences, while rejected and flushed attempts
have no completion. Masked and empty beats can complete without a VRF write.
Completion is distinct from scalar macro retirement.

The sequencer has no duration event. Sequencing captures PC and instruction on
each accepted beat; Perfetto names its slices from the decoded
instruction. Retry creates another occurrence with the same instruction bits.
Sequencing and issue capture the macro-local operation index, exclusive
element range, and last/empty flags;
the operation index can repeat on retry and is not an event identity. Completion
captures destination and VRF-write enable. Backpressure observations share
their respective sequence or issue tracks. The trace carries ownership through
existing storage; it does not infer register-data dependencies or add
per-service events. Memory attempts
continue through the shared LSU to the single cache-owned `dcache/s1.access`
checkpoint. `vector/memory.result` observes the existing registered decision
one cycle after cache resolution, retaining issue and cache ancestry. It records
hits, replay, faults, and slow-request admission, but not masked-off beats.
Arbitration/translation failures have no cache parent. Partial-mode diagnostics
remain at unmodeled boundaries.
The core supplies its selected ISA for sequencing disassembly through `~trace_isa`;
standalone vector pipelines default to the XLEN-appropriate IMAFDCV instruction set.

### Execution ownership

[`RV5StageVectorPipeline`](../vector.rhdl) contains the sequencer, a separate
synchronous operand-fetch stage, a vector bank with three general read ports
and a dedicated `v0` mask shadow, packed SIMD execution, and private
feed-forward operand/result registers.
Its `request` accepts a legal macro snapshot only at nonspeculative WB. The
sequencer drives either the local SIMD/shared-service path or its own memory
attempt pipeline. Vector micro-ops never re-enter scalar Decode, EX, MEM, or WB.
The memory path shares the scalar LSU through a fixed-cycle lookup arbiter and
a separate transaction arbiter; returned union tags retain response ownership.

`outcome: Valid(RV5StageVectorCommit(xlen, slots))` reports memory certification,
final conservative acceptance, a precise fault, or fault-only-first truncation
to scalar retirement one cycle after the local decision. Non-memory macros
retire through their ordinary scalar WB launch token and do not wait for result
drain. Empty memory bodies certify at dispatch. Contiguous unit-stride memory
macros and encoded-`rs2=x0` non-segmented strided loads can certify after a
page-level precheck of at most two 4 KiB pages. The latter check only the one
element's aligned transfer word and retain their single-read splat execution. The
one-page fast path uses the scalar ALU in EX for the first address, including
the `vstart` byte offset, and the normal DTLB in MEM. WB carries the captured
translation in the admitted descriptor and retires the macro without waiting
for the previous page-window owner. Each issued request uses that captured
translation independently of the shared fallback window. Certified ordinary
memory hands off the sequencer after its final scheduled beat; downstream
request retention handles cache retries without flushing younger vector work.
Dependent instructions still wait for actual load data to reach the VRF.
The original `rs1` base remains in its descriptor for sequencing. When this
speculative check cannot certify, WB restarts younger work and the existing
precheck or element-wise path retains precise fault ownership. The
MMU retains their translations until final non-replayable acceptance, independently
of DTLB replacement; a covering superpage needs only one translation lookup.
Certification requires natural element alignment, no address wrap, and full-page
ordinary cacheable read-idempotent PMA coverage with the required permissions.
It never accesses the vector data itself. A failed precheck keeps the original
execution path: one-read splat for encoded zero stride, element-wise for ordinary
unit stride. Larger ranges, indexed and other strided operations, and
fault-only-first operations use the element-wise path. In particular, a
conservative check of a masked-off page must not create an architectural exception.

After certification, independent scalar work can execute and retire while
the vector sequencer remains active. Vector/state observers, fences, translation
changes, and trap/interrupt entry wait for drain. Younger scalar stores wait
for vector loads or stores; younger scalar loads wait for vector stores.
Younger vector memory instructions may enter the ordered descriptor FIFO while
an older vector store is still draining, subject to certification and capacity.
Deferred scalar destinations retain their GPR/FPR scoreboard reservations.
Younger scalar FP work also waits for outstanding vector FP state updates.
Without an early certificate, successful authorization of the final element
releases independent scalar Decode on the following cycle, even if accepted
load responses and vector writes have not drained. A retry cannot release
Decode; a fault follows the registered precise-trap path. Vector retirement
remains a separate, later event.

The outcome register separates LSU admission from scalar WB selection. Internal retries
do not retire the macro or restart scalar fetch. Rejection flushes younger
unaccepted vector stages and restores the sequencer's accepted checkpoint.
Accepted requests, their destination metadata, and their responses survive.
A younger scalar redirect cannot cancel an allocated macro.
A saturating or clipping beat reports saturation when its private compute result
matures. Memory retry and fault cannot set `vxsat`.
The CSR bank ORs that pulse into sticky `vxsat`, with an explicit CSR write on
the same edge taking priority. The integrated core updates architectural vector
retirement state from scalar WB for compute and from `retire: Pulse` for memory
certification or successful conservative acceptance. `execution_done: Pulse`
reports completed execution. `active` includes accepted memory completion
ownership; `sequencing` reports the separate registered-sequencer lifetime.
Local compute results write one cycle after issue on their reserved cycle,
independently of older unrelated slots. FP, multiply, and divide requests enter
their shared services on that edge and complete when their tagged result
returns. Memory alone retains the three-cycle decision path and tagged slow
completions.

Standalone compositions leave `~multiply_latency` false and `~fixed_fp` false
when their external services can return on arbitrary cycles. Integrated fixed
services declare the multiplier's request-to-result latency and the FP service's
two-cycle contract; request acceptance then includes the VRF reservation.
`scalar_writeback` offers a one-cycle reservation that must be accepted along
with any beat producing an integer scalar result. Its consumer must guarantee
the corresponding `scalar_result` write, not queue that completed value.

A two-entry descriptor FIFO snapshots waiting vector macros in addition to the
single active sequencer instruction. When empty, it flows an accepted WB descriptor
directly into an idle sequencer on the same edge; the first read still uses the
registered sequencer descriptor in the following cycle. WB may enqueue one
descriptor per cycle; a full FIFO can replace its departing head on that same
edge. Only the head
can start page-range certification or enter the sequencer, and a blocked head
does not replay through scalar fetch. WB still replays if descriptor admission
or a required floating-point scalar-result reservation cannot succeed. Integer
scalar results reserve the deferred GPR write cycle before issue. There is no out-of-order
instruction selection. Pending status includes both waiting entries, so scalar ordering and
FP/CSR observers cannot overlook a queued instruction. An uncertified memory
macro blocks younger admission until its retirement outcome; accepting a
younger descriptor cannot overwrite the head's certificate. Pending certification
never gates beats from the older active macro. Ordinary compute can accept its queued successor on the same edge that
its final read request transfers. Every read request comes from the registered current
instruction: the replacement's first read occurs in the following cycle, never
from an incoming or speculative descriptor. Operand fetch retains each plan's
controls and owner through VRF latency and issue backpressure, independently of
sequencer replacement. Independent single-beat instructions can therefore read
and issue on consecutive cycles. The completion-slot owner ring retains older
issued work. Elementwise memory, including encoded-zero-stride loads, can also enter the sequencer while older
ordinary compute beats remain in operand fetch; those beats issue first.
MEM-certified ordinary memory uses the same final-read handoff as compute;
captured physical requests retain downstream cache-retry ownership. Uncertified
memory, dependent scans, and compression retain the descriptor through
final feedback because they carry precise replay or checkpointed cross-beat state. An
authorized final ordinary memory beat can admit the next descriptor on that same
edge. An encoded-zero-stride load likewise releases sequencing on read
capture for MEM-certified work, or cache authorization on the fallback path,
while its retained response and row writes finish independently.
Younger work waits only for conflicting destination or mask rows; another
zero-stride splat waits for the single splat engine to drain. Retry or fault
feedback cannot admit a successor. Index scans
release on their final read like ordinary compute. Reductions release at their
tail read while owner-local recurrence and completion state finish independently. A dependent
consumer waits for each needed XLEN-bit VRF row rather than the entire older
instruction. Same-width elementwise compute releases its conservative
destination-group claim row by row as results resolve; exact outstanding
writes still block reads and younger writes until actual VRF writeback. Irregular and replayable
schedules retain the conservative group claim through their final result. All
operands are captured before issue. The sequencer never alternates between
instructions or delegates replay to a service instruction queue.

`RV5StageConfig(~vector_completion_slots: n)` configures the memory completion
window independently of VLEN; `n` must be a positive power of two and defaults
to eight. Standalone `RV5StageVectorPipeline` accepts the same keyword.
Storage depth and tag width derive from this count, with a one-bit zero index
for a single slot. Slots are reserved before issue and released only after
ordered metadata reclamation after writeback; a smaller window can reduce memory throughput.
The integrated core propagates the count through every LSU adapter. Standalone
compositions must select the same count on their data interfaces and engines.

[`bundles.rhdl`](bundles.rhdl) defines an instruction/configuration snapshot,
XLEN-bit packed micro-ops, and memory acceptance/retry/fault feedback. Position
is an exclusive architectural element range, independent of masked-off lanes;
caller-defined context identifies outstanding work. Authorization is distinct
from result completion, and accepted side effects must never be retried.
[`RV5StageVectorSequencer`](sequencer.rhdl) retains one macro descriptor and its
scalar/configuration snapshot. The original macro crosses ID/EX, EX/MEM, and
MEM/WB without executing scalar side effects. EX forwarding resolves its scalar
base and stride into a per-occurrence context carried beside the launch token;
WB combines it with the then-current architectural vector state. Register
numbers are decoded from the instruction rather than copied into that context.
Vector launch tokens may follow each other through the scalar stages. WB admits
each into the two-entry descriptor FIFO or precisely replays it when full,
without making WB elastic. Older scalar instructions can finish or squash the
launch normally.
[`RV5StageVectorOperandFetch`](operand-fetch.rhdl) owns three synchronous
general VRF reads supplying `vs2` (or store `vs3`), `vs1`, and
the old destination for multiply-accumulate operations; a dedicated `v0`
shadow supplies predication concurrently. The sequencer accepts a read only
after its completion slot, execution path, and future fixed write cycle are
available. The synchronous read response reaches execution at a fixed offset;
there is no post-read operand queue. Gather performs a scheduled dependent
second read. Setup and final drain retain their latency, while independent
single-beat macros can still issue on consecutive cycles.

For unit-stride and strided element memory, the sequencer captures the full
base address and an element step. Unit-stride uses `1 << EEW`; strided forms
use the sign-extended `rs2` value, including negative and zero strides. The
sequencer initializes at the base and advances once for each skipped `vstart`
element before issue, so it uses only addition rather than an element-index
multiplier. Each completed element advances the speculative base, while WB
authorization advances a separate checkpoint. Retry restores that checkpoint,
preserving the base of the oldest unauthorized element. Masked-off elements
still advance the sequence; empty bodies neither warm up nor access memory.
For non-segmented strided loads with `rs2=x0`, the first enabled element supplies
the sole data read of a successful attempt; retries may reissue it. Once that
read is authorized, its EEW value is replicated through masked XLEN-bit VRF row
writes without later elementwise memory completions. A masked-off or empty
body performs no data read, and a register containing zero remains an ordinary
strided load with one access per active element.

Indexed loads and stores capture the scalar base but read one unsigned byte
offset from `vs2` for every active element. The encoded EEW describes that
offset, while `vtype.SEW` and LMUL describe the transferred data. Each offset
is zero-extended and added directly to the base; it is never scaled by the data
width. Ordered and unordered encodings currently share conservative
element-order issue. Retry returns to the locally accepted element and rereads its
offset. Indexed segments reuse that offset for every contiguous field at the
element, adding `field << SEW` before moving to the next index. The initial
implementation requires an indexed load's complete NFIELDS destination footprint
to be disjoint from its index group; indexed stores may overlap data and indices
only when they use the same EEW. This restriction avoids completion writes
changing offsets that have not yet been read.

Unit-stride `vlseg2-8e*.v`/`vsseg2-8e*.v` and constant-stride
`vlsseg2-8e*.v`/`vssseg2-8e*.v` walk fields inside each segment before
advancing the architectural element. A field address is the retained segment
base plus `field << EEW`. The segment base advances by `NFIELDS << EEW` for
unit stride or the captured signed `rs2` for constant stride. Thus negative,
zero, and overlapping strides need no special case, and `vstart` warm-up uses
one addition per skipped segment. Each field maps to the next EMUL-sized
register group. Decode enforces aligned field groups, `ceil(EMUL) * NFIELDS <= 8`,
and no register wrap past `v31`. Masking applies to the whole segment.
Completion slots use a persistent allocation sequence across macros, so fields at
the same element can remain outstanding and still drain in issue order. Retry
restores both element and field cursors plus the authorized address. A fault
reports the containing segment through `vstart`; fields already performed in
that segment follow RVV's implementation-defined partial-segment rule.
Indexed segments use the same field/register sequencing and retry checkpoints,
but form each segment base as `rs1 + vs2[element]`. Ordered and unordered forms
currently share conservative element-order issue; neither form promises an
order among fields within one segment.

Unit-stride `vle8/16/32/64ff.v` and `vlseg2-8e*.v` fault-only-first loads use
the same address, mask, field, and completion machinery. Only one such access
may remain unresolved: a successful element drains before the next element is
issued. A synchronous exception at element zero follows the ordinary precise
trap path and leaves `vl` unchanged. An exception at a later element suppresses
the trap, sets `vl` to that element index, clears `vstart`, marks VS Dirty,
retires the macro once, and redirects fetch to its successor. Segmented forms
truncate at the containing segment; fields completed within that segment use
RVV's implementation-defined partial-segment behavior.

Whole-register `vl1/2/4/8re8/16/32/64.v` and `vs1/2/4/8r.v` transfers use the
same singleton LSU path. Their effective length is `NREG * VLEN / EEW`,
independent of `vl` and `vtype`; `vstart` still identifies the next encoded-EEW
element. The sequencer walks one continuous register group, so its existing
XLEN-bit VRF row address naturally crosses register boundaries without another
datapath. Decode enforces NREG alignment and rejects register wrap past `v31`.
The fixed-unmasked forms leave `vl` and `vtype` unchanged, and precise faults
reuse the ordinary authorized cursor and address checkpoint.

Mask-register `vlm.v` and `vsm.v` transfers are fixed-unmasked byte streams
through one named vector register. Their effective length is `ceil(vl/8)`, and
`vstart` is a byte cursor. They require a legal `vtype` because their length
depends on `vl`, but ignore SEW and LMUL for register geometry. Loads write each
complete transferred byte through the ordinary bit-enabled VRF port; a load to
`v0` updates its dedicated mask shadow atomically. Retry, precise faults,
ordering, completion slots, and successful `vstart` clearing remain the same as
ordinary unit-stride memory.

The `vadc`/`vsbc` forms consume the `v0` shadow as one carry/borrow bit per
element and execute every body element; `v0` is data, not predication. `vmadc`
and `vmsbc` pack carry/borrow results into a mask destination and support both
carry-input and no-carry forms. The fixed carry-input data-result encodings
cannot write `v0`, and an instruction cannot also name `v0` as a SEW-wide
source. All forms reuse the SIMD ALU's lane guard bits rather than a second
adder.

The six `vzext.vf2/vf4/vf8` and `vsext.vf2/vf4/vf8` forms retain destination
SEW/LMUL scheduling while reading `vs2` at EEW `SEW/2`, `SEW/4`, or `SEW/8`.
Operand fetch selects the corresponding narrow source fragment and
[`SimdExtend`](../../simd-alu.rhdl) directly wires its elements into one XLEN-bit
destination beat. Legality rejects unsupported source EEW, source EMUL below
1/8, misaligned groups, masked `v0` conflicts, and destination overlap except
when an integral source group occupies the highest-numbered part of the
destination group.

The vector pipeline's private execution stage uses
[`RV5StageVectorExecute`](execute.rhdl) and the shared SIMD ALU. Its fixed-latency
result maturity completes on the reserved write cycle without
an external authorization round trip. An exclusive end position advances even for
masked-off elements. Ordinary compute releases the sequencer at its final read
transfer; serialized compute releases it at result maturity. Neither
releases pending result ownership. Architectural retirement follows the certification/outcome
contract above; interrupt entry waits for all macro contexts to drain.
A zero-length body or `vstart >= vl` emits one empty
completion beat, with no register write.

For serialized compute at the low-level sequencer boundary, ordered internal
maturity feedback advances cross-beat state and releases the next beat. Memory
keeps separate issued and authorized positions: retry flushes pending reads and
attempts, restarts at the authorized frontier, and preserves accepted writes and
the initial partial-chunk enable floor. Fault feedback terminates memory issue
and emits the failing element through `fault_start`; accepted memory slots remain
owned until drained.

This cut preserves inactive and tail contents, supports fractional LMUL,
in-place same-width groups, and the permitted high-part overlap for widening
destinations. `vrsub.vx` and `vrsub.vi` reuse the subtract datapath with swapped
operands. Scalar operands retain XLEN width, and
comparison and carry/borrow bits use the ordinary masked write port.

## Widening integer add and subtract

The RV32/RV64 integer path executes `vwaddu`, `vwadd`, `vwsubu`, and `vwsub`
in `.vv`/`.vx` and `.wv`/`.wx` forms for source SEW 8/16/32. The narrow-source
forms extend both operands; the wide-source forms read `vs2` at twice SEW and
extend only the vector or scalar `vs1`. Both use the shared SIMD adder at twice
SEW. Destination and wide-source EMUL are twice LMUL and must remain
representable through EMUL=8. A narrow vector source may overlap only the
architectural high part of the destination. The wide `vs2` group may equal the
destination group; misalignment and partial overlap trap before any VRF read.

One sequencer beat produces one XLEN-bit destination row. Narrow-source forms
reread the same source row for its lower and upper halves. Wide-source forms
advance the `vs2` row every beat while the narrow source still selects the
corresponding half. Each destination-width beat matures independently without
retained speculative operand state. Mask, `vstart`, tail, and empty-body behavior
use the ordinary packed-element rules. Cancellation discards only results that
have not matured.

## Narrowing integer shifts

The RV32/RV64 integer path executes `vnsrl` and `vnsra` in `.wv`, `.wx`, and
`.wi` forms for destination SEW 8/16/32. `vs2` uses twice SEW and twice LMUL;
the vector shift-amount source and destination use the configured SEW/LMUL.
Shift amounts are reduced modulo twice SEW. Logical forms zero-fill and
arithmetic forms sign-fill before the low SEW result is retained.

One XLEN-bit wide-source row produces half of an XLEN-bit destination row. The
operand adapter zero-extends the selected narrow shift amounts into the
existing SIMD shifter's doubled-width lanes, and result packing places the
narrow halves into the correct destination half-row. No second shifter or VRF
port is added. Ascending execution permits `vd=vs2`: every wide source row is
captured before its low-part destination bytes can overwrite it. Other overlap
with the wide source is rejected, as is overlap between vector `vs1` and `vs2`
at their different EEWs.

Masks, `vstart`, tails, empty bodies, result maturity, and cancellation use the
ordinary packed-integer rules. An in-place destination half-row cannot write
until the wide source row it overlaps has been captured. Fixed-point scaling
shifts and narrowing clips reuse the same execution rules.

## Moves, merge, and mask logic

The reusable RV32/RV64 integer path supports `vmv.v.v`, `vmv.v.x`, and
`vmv.v.i`, plus `vmerge.vvm`, `vmerge.vxm`, and `vmerge.vim`, at every supported
SEW/LMUL. Moves copy or broadcast their sole source; the reserved `vs2` field
must be zero. Scalar and signed immediate broadcasts use the ordinary captured
operand path. Merge uses each `v0` bit to select between sources, not to
suppress the destination write. Both zero and one mask bits write their
selected value within the body. Merge cannot target `v0`; unmasked moves can.
Merge vector data sources also cannot overlap `v0`, since an instruction may
not read the same register at both mask EEW=1 and data SEW. Scalar `x0` and an
immediate zero remain valid merge inputs.

Whole-register `vmv1r.v`, `vmv2r.v`, `vmv4r.v`, and `vmv8r.v` copy one
aligned register group through the same XLEN-bit packed datapath. Their effective
length is `NREG * VLEN / SEW`, independent of `vl` and LMUL but still dependent
on a legal `vtype`; `vstart` identifies the first SEW-wide element to copy.
Decode rejects misaligned or wrapping source and destination groups. Equal
source and destination groups are a legal no-op. The sequencer naturally crosses
VRF row and register boundaries, preserves the pre-`vstart` prefix, and keeps
the ordinary result-maturity, cancellation, and `v0`-shadow rules.

`vmandn.mm`, `vmand.mm`, `vmor.mm`, `vmxor.mm`, `vmorn.mm`, `vmnand.mm`,
`vmnor.mm`, and `vmxnor.mm` operate on packed one-bit elements. Each operand
names one register independent of LMUL. They are always unmasked, may write
`v0`, and support in-place source/destination overlap. The sequencer processes
up to XLEN mask bits per beat through the existing logic datapath and VRF write
port; it does not expand mask bits into SEW-sized data elements.

All these operations preserve pre-`vstart` and tail contents, including partial
mask words. Preserving mask tails is a permitted choice for tail-agnostic mask
results. Empty bodies perform no write but still retire once and clear `vstart`.
Writes occur only for mature results; cancellation suppresses future writes.

## Element moves and integer reductions

The reusable RV32/RV64 path also executes `vmv.x.s`, `vmv.s.x`, and
`vredsum.vs`, `vredand.vs`, `vredor.vs`, `vredxor.vs`, `vredminu.vs`,
`vredmin.vs`, `vredmaxu.vs`, and `vredmax.vs` at SEW8/16/32/64, plus
`vwredsumu.vs` and `vwredsum.vs` at source SEW8/16/32.
Element moves ignore LMUL grouping. Extraction sign-extends or truncates to
XLEN and executes even when VL is zero or `vstart >= vl`; insertion does not
write when `vstart >= vl`. Both clear `vstart` on successful retirement.
The vector pipeline's `scalar_result: Valid(RegisterFileWrite(xlen))` is
aligned with the internal result-maturity event, not a deferred completion. The core
routes it to normal GPR writeback; scalar dependencies and older deferred WAW
hazards remain subject to the existing interlocks.

Reductions start from `vs1[0]`, fold active `vs2` elements, and write only
`vd[0]`. The seed and destination name single registers regardless of LMUL;
the source group must be aligned. The destination may overlap sources or v0.
Masked reductions cannot also use v0 as a SEW-sized data source.
All-masked nonempty reductions copy the seed; VL zero leaves the destination
unchanged. Nonzero `vstart` traps before issue. Destination tails are preserved.
Widening sums read and write the seed/result at twice SEW, zero- or sign-extend
each narrow source element, and reject SEW64. Their wide scalar operands retain
EMUL=1; a widening seed cannot alias the narrow source group because that would
read one register at two EEWs.

This implementation reuses the SIMD ALU. Integer reduction beats can sequence
on consecutive cycles when downstream resources are available: each beat
consumes the owner accumulator at EX after the preceding beat has matured.
The accumulator advances at private result maturity, and the non-replayable
compute path carries owner and completion tag with every result. FP reductions
remain serialized through their shared-service result.
The tail hands the sequencer to a younger macro while the older completion slot
retains its final result. Cancellation cannot expose a partial reduction in the
VRF. This is not a packed-per-cycle reduction tree;
ordinary packed integer throughput is unchanged. The three general read ports
remain available independently of the dedicated `v0` mask read.

## Mask queries and prefix/index generation

The reusable RV32/RV64 path executes `vcpop.m`, `vfirst.m`, `vmsbf.m`,
`vmsif.m`, `vmsof.m`, `viota.m`, and `vid.v`. Queries count active source mask
bits or return the first active set-bit index at compute maturity through the scalar
result interface. VL zero still writes a scalar result: zero for population
count and -1 for first-set. Only the final mature beat writes the GPR.

The three first-bit mask generators preserve inactive and tail bits and write
before, through, or only at the first active set bit. Iota writes the count
of preceding active set bits into each enabled SEW-sized element. Index writes
the architectural element number, independent of masking. Counts and indices
truncate to SEW. All six source-scanning instructions require `vstart=0`;
`vid.v` permits nonzero `vstart` and preserves earlier elements.

Mask sources and mask destinations name single registers regardless of LMUL.
Prefix-mask destinations cannot overlap their source or, when masked, v0.
Iota's aligned data group cannot overlap its source mask or, when masked, v0;
index has no source group. Queries permit any source mask, including v0.

The packed scan network processes up to XLEN mask bits per query/prefix-mask
beat or XLEN/SEW elements per iota beat. One dependent scan beat is in flight;
its carry advances only when the private result matures. Cancellation preserves
already-written prefix results while suppressing future writes
and unfinished scalar answers. Index needs no carry dependency, releases the
sequencer on its final read, and finishes issue on its final issued beat. The
bank retains three general read ports plus
the dedicated `v0` mask read.

## Packed integer slides

The reusable RV32/RV64 datapath supports `vslideup.vx/vi`,
`vslidedown.vx/vi`, `vslide1up.vx`, and `vslide1down.vx` at every supported
SEW/LMUL. Ordinary slide offsets are unsigned XLEN values or unsigned five-bit
immediates, not SEW-truncated shift amounts. Slide1 inserts a scalar at element
zero or VL-1, sign-extending it when SEW exceeds XLEN.

Upward slides preserve elements below the offset and require disjoint source
and destination groups, including at VL zero. Downward slides permit in-place
execution and read source elements up to VLMAX, even beyond VL; out-of-range
source elements produce zero. Masking applies to destination elements. A masked
slide cannot use v0 as either its data source or destination. Pre-vstart,
inactive, and tail elements are preserved; an empty body performs no write.
These rules follow the [RVV slide specification](https://github.com/riscv/riscv-v-spec/blob/master/v-spec.adoc#vector-slide-instructions).

Slides read two adjacent source chunks through general ports while the `v0`
shadow supplies predication. Byte muxes form one input for the SIMD ALU's
existing XLEN-bit rotate slot; no separate slide barrel shifter or full-vector
crossbar is instantiated.
The rotation operates at the full physical word width while write enables retain architectural SEW.
The packed schedule supplies XLEN/SEW elements per beat, with one result per
cycle in an unstalled stream after setup. Result maturity authorizes writes,
and cancellation suppresses only future writes.

## Register gather

The reusable RV32/RV64 datapath supports `vrgather.vv`, `vrgatherei16.vv`,
`vrgather.vx`, and `vrgather.vi`. Data uses SEW/LMUL; `vrgatherei16.vv`
uses unsigned 16-bit indices with EMUL = LMUL * 16 / SEW. Other vector
indices use unsigned SEW. Scalar indices retain all unsigned XLEN bits;
immediates are unsigned five-bit indices. Indices below VLMAX can read beyond
VL; indices at or above VLMAX produce zero, without wrapping into another
register. These follow the [RVV gather specification](https://github.com/riscv/riscv-v-spec/blob/master/v-spec.adoc#vector-register-gather-instructions).

Destination groups cannot overlap either vector source. Equal-EEW data/index
sources may overlap each other; mixed-EEW source overlap is reserved. Masked
data and index sources cannot also read v0 at another EEW. Index EMUL and
alignment are checked independently, even for an empty body. Predication,
nonzero vstart, and inactive/tail preservation follow ordinary integer writes.

Vector-index gathers use two dependent synchronous general reads: index, then
the addressed data word; the `v0` shadow supplies predication alongside them.
The bank supplies one element every
two cycles in an unstalled stream after setup. Scalar/immediate forms read
their selected source word for each destination chunk and broadcast packed
XLEN/SEW-element beats, one per cycle. Both use the existing SIMD XLEN-bit rotate
slot, with no extra slide shifter or full-vector crossbar. Only result maturity
authorizes writes; cancellation flushes both read contexts and speculative results.

## Vector compression

The reusable RV32/RV64 datapath supports `vcompress.vm`. The fixed `vm=1`
encoding uses `vs1` as an unmasked selection register and packs selected
`vs2` elements, in source order, into consecutive destination elements
starting at zero. The remainder of the destination group is preserved as the
tail-policy choice. Nonzero `vstart` traps. Destination and data-source groups
must be aligned and disjoint; the single-register selection mask must be
disjoint from both data groups, including when VL is zero.

The sequencer reads the data and selection-mask chunks through general ports.
[`SimdCompress`](../../simd-alu.rhdl) compacts each XLEN-bit word without
owning architectural state. A retained suffix joins the next compacted word;
each issued beat carries its post-beat suffix, element count, and destination
position as a speculative checkpoint. Result maturity advances the committed
checkpoint, and cancellation discards only speculative state. A final flush
beat writes a partial retained suffix when necessary. Every VRF write follows
result maturity, and no extra read or write port is added. Source chunks can
sequence every cycle; a final suffix uses the common S1/S2 path with no reads
and issues two cycles after its generating source beat.

## Shared integer multiply/divide

RV32 and RV64 vectors execute `vmul`, `vmulh`, `vmulhu`, `vmulhsu`,
`vsmul`, `vdiv`, `vdivu`, `vrem`, and `vremu` in `.vv` and `.vx` forms at
SEW8/16/32/64. `vwmulu`, `vwmulsu`, and `vwmul` execute at SEW8/16/32 and
produce 2*SEW destinations. These are singleton operations, not packed SIMD
operations. VX captures its EX-forwarded scalar operand at WB launch. Signed operands
extend from source SEW before execution; high multiplication selects bits
`[SEW, 2*SEW)`, while widening multiplication retains all `2*SEW` product bits.
`vsmul` rounds the signed double-width product after shifting it right by
`SEW - 1`, using the `vxrm` value captured with the macro, then saturates to
signed SEW. Division truncates toward zero and preserves the architectural
divide-by-zero and overflow results.

The same service executes `vmacc`, `vnmsac`, `vmadd`, and `vnmsub` in `.vv`
and `.vx` forms. A third general read captures old `vd` in parallel with the
two source reads. `vmacc`/`vnmsac` use it as the addend; `vmadd`/`vnmsub` use it
as a multiplicand and retain `vs2` as the addend. `vwmaccu`, `vwmacc`, and
`vwmaccsu` support `.vv` and `.vx`; `vwmaccus` supports its architectural `.vx`
form. Their old destination is read at 2*SEW and their signedness follows each
mnemonic. The completion slot retains the selected addend and add/subtract
policy until the tagged product returns, so the shared multiplier interface
does not carry a wide accumulator.

Widening multiplication doubles destination EMUL and rejects SEW64 or EMUL16.
Its destination and narrow vector sources use the same alignment and
high-part-only overlap policy as widening add/sub, including fractional LMUL.
The source SEW remains in the multiplier tag while the WB-owned destination
metadata independently carries doubled EEW, address, and mask placement.

Scalar and vector clients share the profile-selected multiplier and one
iterative divider. Vector requests have priority at the multiplier; the
divider uses round-robin arbitration. The vector multiplier request queue
bypasses when empty, attempting admission two cycles after sequencing when
the multiplier has capacity. The iterative multiplier retains one request;
the five-stage pipelined multiplier advances every launched request without
backpressure. The integrated service reserves its destination write cycle
before launch and pipelines the opaque owner tag beside the operands, without
completed-result storage. The standalone elastic adapter retains result credits
for callers without a schedule. Each scalar adapter has one reserved WB request slot, so
vector contention cannot steal an ID admission reservation. Scalar GPR
completion still uses the ordinary deferred writeback arbiter.

Vector elements reserve completion slots before issue. Local results mature and
FP, multiply, or divide requests enter their request queues directly from the
private execute stage. Fixed operations reserve the single masked VRF write
port before launch; variable services hold their response until an available
cycle. Independent results can write out of order. Row-level RAW/WAW checks
preserve dependencies. Backpressure stops earlier issue; MEM/WB remains feed-forward.
Cancellation discards only speculative work, never accepted requests or their
response ownership. The multiply completion tag retains the `vsmul` rounding
mode and result selection; its result reports saturation at actual writeback, when
`vxsat` is pulsed exactly once. Masked and empty elements complete without
execution.
These services do not promise one element per cycle. RV32 executes integer
mul/div through SEW32; widening operations require a destination no wider than XLEN.

## Shared floating point

The RV64D vector path executes same-width vector-vector and
vector-floating-scalar add/subtract, multiply/divide, sign injection, min/max,
classification, reciprocal estimates, reciprocal-square-root estimates,
comparisons, and all eight fused multiply-add/subtract forms at SEW32 or SEW64;
vector-vector also includes square root. Same-width conversions cover signed
and unsigned integer-to-float, dynamic-rounding float-to-integer, and fixed-RTZ
float-to-integer forms. All fifteen widening/narrowing conversion forms execute
at SEW32, crossing between 32- and 64-bit integer or FP elements; narrowing
FP-to-FP additionally supports its fixed round-to-odd form. Comparisons produce
packed mask destinations, while fused operations consume old `vd` through the
third general VRF port. Widening FP arithmetic includes all `vfwadd`, `vfwsub`,
`vfwmul`, `vfwmacc`, `vfwnmacc`, `vfwmsac`, and `vfwnmsac` vector-vector and
vector-scalar forms at SEW32. The `.wv` and `.wf` forms retain a wide `vs2`;
the other arithmetic forms exactly promote narrow operands before one FP64
operation, including a wide old-`vd` fused addend. FS and VS must be enabled.
Operations that round require a supported `frm`, which
the macro captures at WB launch; exact sign, min/max, and comparison operations
do not depend on `frm`; fixed-RTZ conversions also ignore it. `Zvfhmin`
restricts SEW16 to its two FP-to-FP conversions; full `Zvfh` admits same-width
FP16 arithmetic, comparisons, reductions, moves, slides, and all applicable
widening/narrowing forms. The RV32Max product selects F/Zve32f only;
its native-width FP32 operations share the scalar F service.
`vfredusum.vs`, `vfredosum.vs`,
`vfredmin.vs`, and `vfredmax.vs` fold FP32 or FP64 elements through the shared
service in element order. `vfwredusum.vs` and `vfwredosum.vs` exactly promote
FP32 inputs and fold them into an FP64 seed. Only the final accumulator writes
element zero; masked elements do not execute, and an all-masked nonzero body
copies the seed exactly without raising flags. At `vl=0`, the destination is
unchanged. Width-changing conversion at SEW64 is illegal because this profile
does not provide 128-bit elements.

The sequencer reads one element from each vector source through general ports;
fused operations additionally read old `vd`, while square root and `.vf` leave
`vs1` free. The dedicated `v0` shadow supplies predication. A `.vf` instruction
waits for its scalar FPR producer in Decode and snapshots the forwarded
architectural value at nonspeculative WB launch. That snapshot is broadcast to
each element; invalid SEW32 NaN boxes become canonical NaNs at the shared scalar
datapath boundary. Narrow vector elements are NaN-boxed
only at the shared execution-service boundary; VRF storage remains packed.
Conversion controls explicitly select floating or integer source and result
domains, source and result widths, and dynamic, RTZ, or round-to-odd policy.
Integer elements use the service's integer operand/result path, while FP results
return through the boxed FP path before their destination-width bits are packed.
Widening doubles destination EMUL; narrowing doubles source EMUL. Width-changing
operations retain singleton execution. Each FP request carries per-operand
precision so a widening operation can combine a wide `vs2` or old `vd` with a
narrow vector or scalar source without inventing a vector-only arithmetic lane.
With `Zvfhmin` or `Zvfh`, the same adapter NaN-boxes F16 elements into the
shared FP service and carries Half/Single precision through tagged vector
completion. Full `Zvfh` also selects exact 8- and 16-bit integer converter
widths and promotes widening half arithmetic into the shared FP32 lane.
Active elements queue for execution only when their private operands mature.
Masked, tail, and pre-vstart elements never execute or contribute flags. Empty
bodies still complete once. Floating-point reductions additionally hold the
next element behind the prior service result, preserving the ordered-fold
implementation used for both ordered and unordered sums. Every active fold
contributes its exception flags, while inactive folds retain the accumulator
without entering the service.

Scalar/vector movement supports `vfmv.v.f`, `vfmerge.vfm`, `vfmv.f.s`,
`vfmv.s.f`, `vfslide1up.vf`, and `vfslide1down.vf`. Broadcast and merge reuse
the packed operand path, while the slide forms reuse the ordinary XLEN-bit slide
datapath; none enters the FP arithmetic service or updates `fflags`.
An SEW32 scalar FPR source is NaN-box checked before its payload enters the
vector path, so an invalid box supplies the canonical FP32 NaN. Vector-to-FPR
movement preserves the selected element's raw bits and applies the required
SEW32 NaN box only at architectural writeback. `vfmv.f.s` reads element zero
even when `vl=0`; `vfmv.s.f` and the FP slide forms leave the destination
unchanged when their body is empty.

The scalar FP wrapper reserves a vector-to-FPR destination before the vector
macro launches. Only a mature vector result emits the corresponding architectural
write; redirect and cancellation cannot alter the FPR. Until that write completes,
scalar FP issue is held behind the reservation.

The core composes scalar and vector requests around one FP execution service
using round-robin arbitration and an owner-tagged union. Scalar FPR state
remains in its architectural adapter; only the WB `.vf` source snapshot crosses
into the vector descriptor.
Each vector element reserves a completion slot before issue. A bounded
locally accepted request queue absorbs service backpressure and bypasses an
empty queue directly into the shared service, while slot exhaustion stops
earlier issue, keeping MEM/WB feed-forward. Fixed operations reserve their
two-cycle write opportunity atomically with service acceptance. Variable
results wait at the arithmetic producer. Results write directly, with
completion-time exception-flag updates; slot metadata is reclaimed separately.
Scalar and vector flag updates on the same cycle are ORed together.

Cancellation discards speculative slots and private pipeline validity, but
accepted service requests and their result ownership survive until drained. CSR
observers and interrupts wait for this tail. Subsequent vector instructions
wait for issue ownership and actual register dependencies instead.
Final completion clears `vstart`; inactive and tail bits remain undisturbed.

## Unit-stride memory

The vector path executes naturally aligned `vle8/16/32.v` and
`vse8/16/32.v` on both XLENs, plus the EEW64 forms on RV64. Encoded EEW determines both the
address increment and EMUL (`LMUL * EEW / SEW`); legality checks the effective
group and rejects masked load overlap with `v0`. Masks suppress accesses and faults, and nonzero `vstart` preserves the
prefix. Empty bodies still complete exactly one macro without memory effects.

Certified contiguous accesses use aligned XLEN-sized LSU beats, with byte
enables preserving masks, tails, and the pre-`vstart` prefix. This includes
unit-stride segments, whole-register transfers, and packed-mask transfers.
The certificate covers the aligned transport envelope within ordinary,
idempotent, cacheable memory. Uncertified, strided, indexed, and fault-only-first
operations retain elementwise execution and precise element fault reporting.
This does not enable architecturally misaligned elements.

The private address/lookup/acceptance stages arbitrate for the scalar LSU.
Unmasked contiguous streams can offer one aligned word per cycle when read
credits, completion slots, and the LSU permit it. The existing SIMD full-word rotator
aligns memory words with XLEN-bit VRF rows; a masked carry merges boundary fragments.
Segments additionally transpose memory bytes into their separate field groups.
Masked and segmented store preparation can require multiple VRF read cycles.
Stores cannot mutate the
cache or devices before WB. Misses, translation misses, and uncached accesses
use the ordinary authorized LSU service. Each accepted slow request carries a
`RV5StageMemoryWriteback(n).Vector(slot)` identifying one of `n` reserved vector
slots, distinct from integer and FP writeback variants. MMU, router, cache, and
uncached service preserve the union unchanged.

Slots are reserved before issue. A hit and delayed response can complete
different slots on the same edge; the single VRF write port drains completed
slots in request order. Raw data is buffered before alignment, so delayed
responses may arrive out of order. A slot can release into the partial-row carry
without waiting for its successor; a one-slot configuration therefore progresses.
Execution completion waits for the last partial-row write. Accepted entries and
the partial-row carry retain their ownership after the sequencer is released.
Younger vector reads and writes wait for older pending writes by row,
without blocking sequencer admission itself.
The slot scoreboard distinguishes reservation, acceptance, and ordered release.
A local replay rewinds only the unauthorized frontier,
without refetching the macro or reissuing accepted effects. Cancellation drops
speculative slots but never erases accepted response ownership. Ordinary LSU
faults are reported before acceptance, as in the scalar protocol; this does
not introduce asynchronous ordinary-load error handling.

Tracing uses `vector/s2.issue` and `vector/complete` for both execution paths.
The `packed` field identifies packed transport; its events also report
`memory_bytes`, `store`, `byte_mask`, and `slot`. A completion marks a
micro-op/beat, not completion of the whole vector instruction.

A fault records its element in `vstart`, stops younger elements, and waits for
older accepted data/VRF work before entering the precise trap at the macro PC.
Successful certification clears `vstart` early; conservative execution clears
it on final completion. Independent scalar work may pass certified sequencing
as well as a vector load's completion tail, but scalar stores and vector/CSR
state observers retain their ordering barriers. Younger scalar loads and stores wait for an older vector
store's ordered LSU drain. Interrupt entry waits for vector completion.

## Register bank

`RV5StageVectorRegisterFile(xlen :: XLen, vlen :: VectorLength)` has exactly
`32 * VLEN / XLEN` general entries of `Bits(XLEN)`, with no reset value. VLEN is a host
power of two from twice XLEN through 65536 bits. The flat address is
`register_number * (VLEN / XLEN) + chunk_number`; chunk zero holds the lowest bits.
`v0` is writable, not a hardwired zero register. A physical shadow of its
`VLEN / XLEN` chunks supplies the dedicated mask-read port; it is not separate
architectural state.

- Three independent `Valid(Address)` reads return `Valid(Bits(XLEN))` exactly one
  cycle later for arbitrary vector rows.
- One independent `Valid(MaskAddress)` read addresses a chunk within the `v0`
  shadow and returns `Valid(Bits(XLEN))` exactly one cycle later. It cannot name
  another vector register. There is no read backpressure; the caller must have
  space for every requested result.
- One `Valid(VectorRegisterWrite(xlen, vlen))` write carries an address, XLEN-bit data,
  and **XLEN individual bit enables**. Ordinary byte enables are expanded by the
  result adapter. Mask results update individual bits through the same port.
  Writes to `v0` update its general row and shadow atomically.
- A read and write sampled at the same edge return the post-write value:
  enabled bits forward new data, disabled bits retain their prior values. This
  applies identically to general and mask-shadow reads. Later writes cannot
  change an already captured read response.
- Synchronous reset clears response validity and suppresses writes. It does
  not initialize or erase architectural storage.

The implementation is a flat register array with registered read results, not
a promise of an inferred technology SRAM. This gives precise collision and
masked-write semantics without depending on an unspecified SRAM collision mode.

## Packing boundary

`RV5StageVectorOperands(xlen, vlen)` takes two source chunks, one `v0` shadow chunk, a
scalar, a six-bit immediate container, and `VectorPackingControl(vlen)`. It
emits SIMD operands, element enables, and the first output element. Two-source
operations therefore leave the third general read port free, and masked
three-source operations can consume all three general ports without a
mask-capture phase.

`first_element` denotes the start of an aligned **source** chunk, not the next
enabled element. The caller supplies the mask word containing that element
(`v0` chunk `first_element / XLEN`). Enables intersect `vstart <= i < vl`,
`i < vlmax`, and the architectural mask. Nonzero `vstart` suppresses lanes rather
than shifting their positions. Scalar/immediate broadcast uses the low SEW
bits; `VectorImmediateKind` distinguishes signed five-bit, unsigned five-bit,
and unsigned six-bit values. The six-bit form supplies `vror.vi`; other vector
immediates retain their architectural five-bit interpretation.

Widening reuses `SimdWidenOperands`: each invocation sign- or zero-extends
either half of an 8/16/32-bit source chunk into one XLEN-bit output chunk. The
returned first element and width describe that destination chunk. Both halves
must use the appropriate source snapshot. Narrowing reuses the same adapter to
pass one wide `vs2` row while zero-extending half of the narrow shift-amount
row; the result adapter packs the low halves into one destination half-row.
For `vnclipu` and `vnclip`, the shared SIMD datapath rounds the doubled-width
source according to the captured `vxrm` value before this adapter clips each
enabled lane to its unsigned or signed destination range. Disabled lanes never
contribute saturation. The result carries the per-beat saturation indication
to result maturity rather than mutating CSR state in the combinational adapter. Cross-beat
reduction and permutation scheduling are not supplied here; element-local Zvbb
reversals use the ordinary packed execution schedule.

The fixed-point execution slice implements saturating `vsaddu`, `vsadd`,
`vssubu`, and `vssub`; averaging `vaaddu`, `vaadd`, `vasubu`, and `vasub`;
scaling shifts `vssrl` and `vssra`; narrowing clips `vnclipu` and `vnclip`; and
signed fractional multiply `vsmul` in their architectural forms. `vxrm` is
captured with the macro descriptor, so later CSR changes cannot alter admitted
work. Saturating operations report their result through accepted completion
state before producing the sticky `vxsat` update.

`RV5StageVectorResult(xlen, vlen)` converts a SIMD result into the bank write payload.
Data destinations use the returned output element width; comparisons and
carry/borrow results place one bit per element into the correct destination
mask chunk. Disabled bytes/bits receive no write enable, preserving inactive
and tail contents.

Both adapters expose `legal` for their local chunk/alignment/bank bounds. The
caller must gate writes with it and separately validate architectural register
groups, EEW/EMUL, `vtype`, overlap, and instruction-specific restrictions. The
pure [`VectorConfig`](../../../riscv/README.md#vector-geometry) supplies host
geometry and ordinary data-overlap rules, not an instruction decoder.

Writes are accepted inputs. The bank and packing adapters do not decide macro
allocation, compute maturity, or memory replay; the vector execution owner
supplies that policy after scalar WB has allocated the macro.

See [DEVELOPING.md](DEVELOPING.md) for ownership and focused validation.
