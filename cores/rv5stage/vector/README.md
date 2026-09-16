<!-- Defines experimental vector configuration, decode, storage, and execution-boundary contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Experimental vector path

The opt-in `RVCoreProfile(~experimental_vector: vlen)` enables configuration,
vector CSR state, the decoded packed-integer subset, and RV64 vector memory
operations using unit-stride, constant-stride, indexed, unit-stride segment,
constant-stride segment, and indexed segment addressing. The default is `#false`. Neither setting advertises `V`,
Zve, or Zvbb; the remaining vector instruction families are not implemented.
The reusable arithmetic stays in [`SimdALU`](../../README.md#packed-simd-integer-alu).

## Configuration and decode

The experimental core executes `vsetvli`, `vsetivli`, and `vsetvl` through the
existing serializing system-instruction path. Only WB updates `vl`, `vtype`,
and `vstart`; scalar `rd` receives the new VL. Squashed or faulting operations
do not update this state. Supported geometry is ELEN=64, SEW 8/16/32/64,
LMUL 1/8 through 8, subject to SEW <= LMUL * ELEN. Unsupported configurations
set `vill` and zero VL. Ordinary AVL selection uses `min(AVL, VLMAX)`;
`rs1=x0,rd!=x0` selects VLMAX, while `rs1=rd=x0` preserves VL only when the
old/new types are legal and VLMAX is unchanged. Reserved keep-VL uses trap.

The CSR bank exposes `vstart`, `vxrm`, `vxsat`, `vcsr`, `vl`, `vtype`, and
`vlenb`. `vstart` retains enough low bits for VLEN-1; `vxrm` and `vxsat` alias
`vcsr`. VL/type/VLENB are read-only. Reset starts with `vill=1`, VL=0, and
`mstatus.VS=Off`; VS Off blocks vector CSR access and configuration. Successful
configuration, vector CSR writes, vector completion, fixed-point saturation, or a vector fault mark VS Dirty;
reads do not, and SD combines
the FP and vector dirty states. Software may manage VS through M/S status.

The [vector control column](../decode/vector-ctrl.rhdl) describes same-width
wrapping, reverse, saturating, averaging, and carry/borrow add/sub; logic; shifts; fixed-point scaling shifts and narrowing clips;
comparisons, min/max, compression, narrowing shifts,
and narrow-source widening plus wide-source widening add/sub using direct SIMD controls, operand selection, extension
signedness, comparison inversion, and operand swapping. Runtime group checks
cover alignment, fractional groups, doubled widening EMUL, masked data
destinations, mask-result overlap, and widening source/destination overlap.
Legal rows reach WB as side-effect-free launch tokens, then execute through the
integer unroller. VS Off, `vill`, and invalid register groups trap before launch.
This is an initial subset of [RVV 1.0](https://docs.riscv.org/reference/isa/unpriv/v-st-ext),
not a complete vector ISA implementation.

## Integer pipeline and unroller boundary

[`RV5StageVectorPipeline`](../vector.rhdl) contains the unroller, a vector bank
with three general read ports and a dedicated `v0` mask shadow, packed SIMD
execution, and private EX/MEM/WB data registers.
Its `request` accepts a legal macro snapshot only at nonspeculative WB. Each
accepted `issue` emits
the caller's context, a `last` marker, and scalar-LSU memory metadata when
applicable, atomically capturing that beat's
operands in the private pipeline. Scalar stages carry bookkeeping and singleton
LSU operands, not packed vector operands or register-write payloads.

The nonstallable `commit: Valid(RV5StageVectorCommit(xlen))` supplies the macro
PC, authorization/retry/fault outcome, and hit data exactly three cycles after
issue. No authorization
means no write. `cancel: Pulse` discards speculative work and flushes private
stage validity; it does not undo a live older WB authorization on that edge.
The caller must suppress authorizations for squashed tokens. A saturating or
clipping beat reports saturation with its private result, but only an authorized WB commit
emits `saturate: Pulse`; retry, fault, and cancellation cannot set `vxsat`.
The CSR bank ORs that pulse into sticky `vxsat`, with an explicit CSR write on
the same edge taking priority. `retire: Pulse`
reports the completed last beat. `active` includes accepted memory completion
ownership; `unrolling` reports the separate issue/authorization lifetime.
Integer results use fixed-cycle pairing; slow memory uses tagged completions.

`RVCoreProfile(~vector_completion_slots: n)` configures the memory completion
window independently of VLEN; `n` must be a positive power of two and defaults
to eight. Standalone `RV5StageVectorPipeline` accepts the same keyword.
Storage depth and tag width derive from this count, with a one-bit zero index
for a single slot. Slots are reserved before issue and released only after
ordered completion drain; a smaller window can reduce memory throughput.
The integrated core propagates the count through every LSU adapter. Standalone
compositions must select the same count on their data interfaces and engines.

[`bundles.rhdl`](bundles.rhdl) defines an instruction/configuration snapshot,
64-bit packed micro-ops, and WB authorization/retry/fault feedback. Position
is an exclusive architectural element range, independent of masked-off lanes;
caller-defined context identifies outstanding work. Authorization is distinct
from result completion, and accepted side effects must never be retried.
[`RV5StageVectorUnroller`](unroller.rhdl) retains one macro descriptor and its
scalar/configuration snapshot. The original macro crosses ID/EX, EX/MEM, and
MEM/WB without executing scalar side effects; EX forwarding resolves its scalar
base and stride before WB launches the unroller. Younger instructions wait in
Decode from launch admission until the last WB beat, while older scalar
instructions can finish or squash the launch normally.
Three synchronous general VRF reads supply `vs2` (or store `vs3`), `vs1`, and
the old destination for multiply-accumulate operations; a dedicated `v0`
shadow supplies predication concurrently. A two-slot credit
window reserves space before every read, covering read latency and buffered
beats even when issue stalls. Once filled, it supplies one packed 64-bit beat
per cycle. A macro has setup/drain latency; this is not single-cycle vector
instruction issue.

For unit-stride and strided element memory, the unroller captures the full
base address and an element step. Unit-stride uses `1 << EEW`; strided forms
use the sign-extended `rs2` value, including negative and zero strides. The
sequencer initializes at the base and advances once for each skipped `vstart`
element before issue, so it uses only addition rather than an element-index
multiplier. Each completed element advances the speculative base, while WB
authorization advances a separate checkpoint. Retry restores that checkpoint,
preserving the base of the oldest unauthorized element. Masked-off elements
still advance the sequence; empty bodies neither warm up nor access memory.

Indexed loads and stores capture the scalar base but read one unsigned byte
offset from `vs2` for every active element. The encoded EEW describes that
offset, while `vtype.SEW` and LMUL describe the transferred data. Each offset
is zero-extended and added directly to the base; it is never scaled by the data
width. Ordered and unordered encodings currently share conservative
element-order issue. Retry returns to the WB-authorized element and rereads its
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
Completion slots use a separate macro-local operation sequence, so fields at
the same element can remain outstanding and still drain in issue order. Retry
restores both element and field cursors plus the authorized address. A fault
reports the containing segment through `vstart`; fields already performed in
that segment follow RVV's implementation-defined partial-segment rule.
Indexed segments use the same field/register sequencing and retry checkpoints,
but form each segment base as `rs1 + vs2[element]`. Ordered and unordered forms
currently share conservative element-order issue; neither form promises an
order among fields within one segment.

The `vadc`/`vsbc` forms consume the `v0` shadow as one carry/borrow bit per
element and execute every body element; `v0` is data, not predication. `vmadc`
and `vmsbc` pack carry/borrow results into a mask destination and support both
carry-input and no-carry forms. The fixed carry-input data-result encodings
cannot write `v0`, and an instruction cannot also name `v0` as a SEW-wide
source. All forms reuse the SIMD ALU's lane guard bits rather than a second
adder.

The six `vzext.vf2/vf4/vf8` and `vsext.vf2/vf4/vf8` forms retain destination
SEW/LMUL scheduling while reading `vs2` at EEW `SEW/2`, `SEW/4`, or `SEW/8`.
The unroller selects the corresponding narrow source fragment and
[`SimdExtend`](../../simd-alu.rhdl) directly wires its elements into one 64-bit
destination beat. Legality rejects unsupported source EEW, source EMUL below
1/8, misaligned groups, masked `v0` conflicts, and destination overlap except
when an integral source group occupies the highest-numbered part of the
destination group.

The vector pipeline's private EX stage uses
[`RV5StageVectorExecute`](execute.rhdl) and the shared SIMD ALU. Its MEM/WB
registers retain the packed result; scalar WB authorization permits the VRF
write. An exclusive end position advances even for masked-off elements. The
final authorized beat retires the macro, advances architectural PC, and consumes
an NTL hint. Integer retirement clears `vstart` and marks VS Dirty immediately;
memory waits for its final ordered completion. Interrupt entry waits for the
macro to drain. A zero-length body or `vstart >= vl` emits one empty
completion beat, with no register write.

At the low-level unroller boundary, issued and authorized positions are
separate. Ordered WB feedback
identifies the oldest unauthorized beat and the macro PC. Retry flushes all
pending reads/issue beats and restarts at the authorized frontier, preserving
already committed writes and the initial partial-chunk enable floor. The
caller must discard younger downstream beats on retry/cancellation; the
unroller does not own scalar EX/MEM/WB. The composed vector pipeline forwards
WB feedback to the unroller and flushes its private speculative pipeline.
Fault feedback terminates issue and emits the failing element through
`fault_start`; accepted memory slots remain owned until drained.

This cut preserves inactive and tail contents, supports fractional LMUL,
in-place same-width groups, and the permitted high-part overlap for widening
destinations. `vrsub.vx` and `vrsub.vi` reuse the subtract datapath with swapped
operands. RV32 VX operands are sign-extended before SEW64 broadcast, and
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

One unroller beat produces one 64-bit destination row. Narrow-source forms
reread the same source row for its lower and upper halves. Wide-source forms
advance the `vs2` row every beat while the narrow source still selects the
corresponding half. Each destination-width beat remains its own WB authorization
and retry boundary without retained speculative operand state. Mask, `vstart`,
tail, and empty-body behavior use the ordinary packed-element rules. Writes
remain WB-authorized, and retry resumes at the oldest unauthorized half-row.

## Narrowing integer shifts

The RV32/RV64 integer path executes `vnsrl` and `vnsra` in `.wv`, `.wx`, and
`.wi` forms for destination SEW 8/16/32. `vs2` uses twice SEW and twice LMUL;
the vector shift-amount source and destination use the configured SEW/LMUL.
Shift amounts are reduced modulo twice SEW. Logical forms zero-fill and
arithmetic forms sign-fill before the low SEW result is retained.

One 64-bit wide-source row produces half of a 64-bit destination row. The
operand adapter zero-extends the selected narrow shift amounts into the
existing SIMD shifter's doubled-width lanes, and result packing places the
narrow halves into the correct destination half-row. No second shifter or VRF
port is added. Ascending execution permits `vd=vs2`: every wide source row is
captured before its low-part destination bytes can overwrite it. Other overlap
with the wide source is rejected, as is overlap between vector `vs1` and `vs2`
at their different EEWs.

Masks, `vstart`, tails, empty bodies, WB authorization, cancellation, and retry
use the ordinary packed-integer rules. Retry resumes at the oldest unauthorized
half-row; an authorized in-place prefix cannot overwrite a source element that
the suffix still needs. Fixed-point scaling shifts and narrowing clips reuse
the same execution and recovery rules.

## Moves, merge, and mask logic

The experimental RV32/RV64 integer path supports `vmv.v.v`, `vmv.v.x`, and
`vmv.v.i`, plus `vmerge.vvm`, `vmerge.vxm`, and `vmerge.vim`, at every supported
SEW/LMUL. Moves copy or broadcast their sole source; the reserved `vs2` field
must be zero. Scalar and signed immediate broadcasts use the ordinary captured
operand path. Merge uses each `v0` bit to select between sources, not to
suppress the destination write. Both zero and one mask bits write their
selected value within the body. Merge cannot target `v0`; unmasked moves can.
Merge vector data sources also cannot overlap `v0`, since an instruction may
not read the same register at both mask EEW=1 and data SEW. Scalar `x0` and an
immediate zero remain valid merge inputs.

`vmandn.mm`, `vmand.mm`, `vmor.mm`, `vmxor.mm`, `vmorn.mm`, `vmnand.mm`,
`vmnor.mm`, and `vmxnor.mm` operate on packed one-bit elements. Each operand
names one register independent of LMUL. They are always unmasked, may write
`v0`, and support in-place source/destination overlap. The unroller processes
up to 64 mask bits per beat through the existing logic datapath and VRF write
port; it does not expand mask bits into SEW-sized data elements.

All these operations preserve pre-`vstart` and tail contents, including partial
mask words. Preserving mask tails is a permitted choice for tail-agnostic mask
results. Empty bodies perform no write but still retire once and clear `vstart`.
Writes remain WB-authorized; retry resumes at the authorized frontier and
cancellation suppresses speculative writes. This is still partial V coverage.

## Element moves and integer reductions

The experimental RV32/RV64 path also executes `vmv.x.s`, `vmv.s.x`, and
`vredsum.vs`, `vredand.vs`, `vredor.vs`, `vredxor.vs`, `vredminu.vs`,
`vredmin.vs`, `vredmaxu.vs`, and `vredmax.vs` at SEW8/16/32/64, plus
`vwredsumu.vs` and `vwredsum.vs` at source SEW8/16/32.
Element moves ignore LMUL grouping. Extraction sign-extends or truncates to
XLEN and executes even when VL is zero or `vstart >= vl`; insertion does not
write when `vstart >= vl`. Both clear `vstart` on successful retirement.
The vector pipeline's `scalar_result: Valid(RegisterFileWrite(xlen))` is
aligned with the authorizing WB event, not a deferred completion. The core
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

This first implementation reuses the SIMD ALU with one reduction element in
flight. Its accumulator advances only with WB authorization, so retries retain
the authorized prefix without double counting. Cancellation cannot expose a
partial reduction in the VRF. This is not a packed-per-cycle reduction tree;
ordinary packed integer throughput is unchanged. The three general read ports
remain available independently of the dedicated `v0` mask read.

## Mask queries and prefix/index generation

The experimental RV32/RV64 path executes `vcpop.m`, `vfirst.m`, `vmsbf.m`,
`vmsif.m`, `vmsof.m`, `viota.m`, and `vid.v`. Queries count active source mask
bits or return the first active set-bit index through the WB-aligned scalar
result interface. VL zero still writes a scalar result: zero for population
count and -1 for first-set. Only the final authorized beat writes the GPR.

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

The packed scan network processes up to 64 mask bits per query/prefix-mask
beat or 8/4/2/1 elements per iota beat. One dependent scan beat is in flight;
its carry advances only at WB, and retries resume from the authorized frontier.
Cancellation preserves authorized prefix writes while suppressing future writes
and unfinished scalar answers. Index needs no carry dependency and retains the
ordinary packed issue schedule. The bank retains three general read ports plus
the dedicated `v0` mask read, and V remains unadvertised.

## Packed integer slides

The experimental RV32/RV64 path supports `vslideup.vx/vi`,
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
existing 64-bit rotate slot; no separate slide barrel shifter or full-vector
crossbar is instantiated.
The rotation operates as E64 while write enables retain architectural SEW.
The packed schedule supplies 8/4/2/1 elements per beat, with one result per
cycle in an unstalled stream after setup. WB alone authorizes writes. Retry
resumes at the authorized destination frontier, and cancellation suppresses
only speculative writes. FP scalar-insertion slides remain outside this cut;
V remains unadvertised.

## Register gather

The experimental RV32/RV64 path supports `vrgather.vv`, `vrgatherei16.vv`,
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
8/4/2/1-element beats, one per cycle. Both use the existing SIMD 64-bit rotate
slot, with no extra slide shifter or full-vector crossbar. Only WB authorizes
writes; retry restarts at the authorized destination frontier, and cancellation
flushes both read contexts and speculative results. V remains unadvertised.

## Vector compression

The experimental RV32/RV64 path supports `vcompress.vm`. The fixed `vm=1`
encoding uses `vs1` as an unmasked selection register and packs selected
`vs2` elements, in source order, into consecutive destination elements
starting at zero. The remainder of the destination group is preserved as the
tail-policy choice. Nonzero `vstart` traps. Destination and data-source groups
must be aligned and disjoint; the single-register selection mask must be
disjoint from both data groups, including when VL is zero.

The unroller reads the data and selection-mask chunks through general ports.
[`SimdCompress`](../../simd-alu.rhdl) compacts each 64-bit word without
owning architectural state. A retained suffix joins the next compacted word;
each issued beat carries its post-beat suffix, element count, and destination
position as a speculative checkpoint. WB authorization advances the committed
checkpoint, retry restores it, and cancellation discards only speculative
state. A final flush beat writes a partial retained suffix when necessary.
Every VRF write remains WB-authorized, and no extra read or write port is added.
V remains unadvertised.

## Shared integer multiply/divide

RV64 experimental vectors execute `vmul`, `vmulh`, `vmulhu`, `vmulhsu`,
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

Scalar and vector clients share one iterative multiplier and one iterative
divider through independent round-robin request arbiters. An opaque owner tag
routes each held result back to its client. Each scalar adapter has one reserved
WB request slot, so vector contention cannot steal an ID admission reservation.
Scalar GPR completion still uses the ordinary deferred writeback arbiter.

Vector elements reserve completion slots before issue, enter request queues
only when WB authorizes them, and drain through the single masked VRF write port
in element order. Backpressure stops earlier issue; MEM/WB remains feed-forward.
Cancellation discards only speculative work, never accepted requests or their
response ownership. The multiply completion tag retains the `vsmul` rounding
mode and result selection; its slot retains saturation until ordered drain, when
`vxsat` is pulsed exactly once. Masked and empty elements complete without
execution.
These iterative services do not promise one element per cycle.
RV32 vector mul/div and V advertisement remain outside this cut.

## Shared floating point

The experimental RV64D path executes same-width `vfadd.vv`, `vfsub.vv`, and
`vfmul.vv` at SEW32 or SEW64. This is a subset, not an advertised V extension.
FS and VS must be enabled and `frm` must select a supported rounding mode;
the macro captures `frm` at WB launch. FP16, RV32 vector FP, scalar-FP vector
operands, widening, and fused operations are outside this cut.

The unroller reads one element from each vector source through general ports;
the dedicated `v0` shadow supplies predication. Narrow elements are NaN-boxed
only at the shared execution-service boundary; VRF storage remains packed.
Active elements queue for execution only when scalar WB authorizes them.
Masked, tail, and pre-vstart elements never execute or contribute flags. Empty
bodies still complete once.

The core composes scalar and vector requests around one FP execution service
using round-robin arbitration and an owner-tagged union. Scalar FPR state
remains in its architectural adapter; vector operands never pass through it.
Each vector element reserves a completion slot before issue. A bounded
WB-authorized request queue absorbs service backpressure, while slot exhaustion
stops earlier issue, keeping MEM/WB feed-forward. Results can return out of
order; masked VRF writes and exception-flag updates drain in element order.
Scalar and vector flag updates on the same cycle are ORed together.

Cancellation discards speculative slots and private pipeline validity, but
authorized requests and their result ownership survive until drained. CSR
observers, subsequent vector instructions, and interrupts wait for this tail.
Final completion clears `vstart`; inactive and tail bits remain undisturbed.

## Unit-stride memory

The RV64 experimental path executes naturally aligned `vle8/16/32/64.v` and
`vse8/16/32/64.v`, one element per micro-op. Encoded EEW determines both the
address increment and EMUL (`LMUL * EEW / SEW`); legality checks the effective
group and rejects masked load overlap with `v0`. RV32 memory execution is not
enabled. Masks suppress accesses and faults, and nonzero `vstart` preserves the
prefix. Empty bodies still complete exactly one macro without memory effects.

Elements use the scalar EX/MEM speculative lookup and WB authorization paths.
Warm loads can complete at one element per cycle. Stores cannot mutate the
cache or devices before WB. Misses, translation misses, and uncached accesses
use the ordinary authorized LSU service. Each accepted slow request carries a
`RV5StageMemoryWriteback(n).Vector(slot)` identifying one of `n` reserved vector
slots, distinct from integer and FP writeback variants. MMU, router, cache, and
uncached service preserve the union unchanged.

Slots are reserved before issue. A hit and delayed response can complete
different slots on the same edge; the single VRF write port drains completed
slots in element order. A local replay rewinds only the unauthorized frontier,
without refetching the macro or reissuing accepted effects. Cancellation drops
speculative slots but never erases accepted response ownership. Ordinary LSU
faults are reported before acceptance, as in the scalar protocol; this does
not introduce asynchronous ordinary-load error handling.

A fault records its element in `vstart`, stops younger elements, and waits for
older accepted data/VRF work before entering the precise trap at the macro PC.
Successful final completion clears `vstart`. Younger independent scalar work
may pass a vector load's completion tail, but scalar stores and vector/CSR
state observers wait. Younger scalar loads and stores wait for an older vector
store's ordered LSU drain. Interrupt entry waits for vector completion.

## Register bank

`RV5StageVectorRegisterFile(vlen :: VectorLength)` has exactly
`32 * VLEN / 64` general entries of `Bits(64)`, with no reset value. VLEN is a host
power of two from 128 through 65536 bits. The flat address is
`register_number * (VLEN / 64) + chunk_number`; chunk zero holds the lowest bits.
`v0` is writable, not a hardwired zero register. A physical shadow of its
`VLEN / 64` chunks supplies the dedicated mask-read port; it is not separate
architectural state.

- Three independent `Valid(Address)` reads return `Valid(Bits(64))` exactly one
  cycle later for arbitrary vector rows.
- One independent `Valid(MaskAddress)` read addresses a chunk within the `v0`
  shadow and returns `Valid(Bits(64))` exactly one cycle later. It cannot name
  another vector register. There is no read backpressure; the caller must have
  space for every requested result.
- One `Valid(VectorRegisterWrite(vlen))` write carries an address, 64-bit data,
  and **64 individual bit enables**. Ordinary byte enables are expanded by the
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

`RV5StageVectorOperands(vlen)` takes two source chunks, one `v0` shadow chunk, a
scalar, a five-bit immediate, and `VectorPackingControl(vlen)`. It emits SIMD
operands, element enables, and the first output element. Two-source operations
therefore leave the third general read port free, and masked three-source
operations can consume all three general ports without a mask-capture phase.

`first_element` denotes the start of an aligned **source** chunk, not the next
enabled element. The caller supplies the mask word containing that element
(`v0` chunk `first_element / 64`). Enables intersect `vstart <= i < vl`,
`i < vlmax`, and the architectural mask. Nonzero `vstart` suppresses lanes rather
than shifting their positions. Scalar/immediate broadcast uses the low SEW
bits; immediates select signed or unsigned extension explicitly.

Widening reuses `SimdWidenOperands`: each invocation sign- or zero-extends
either half of an 8/16/32-bit source chunk into one 64-bit output chunk. The
returned first element and width describe that destination chunk. Both halves
must use the appropriate source snapshot. Narrowing reuses the same adapter to
pass one wide `vs2` row while zero-extending half of the narrow shift-amount
row; the result adapter packs the low halves into one destination half-row.
For `vnclipu` and `vnclip`, the shared SIMD datapath rounds the doubled-width
source according to the captured `vxrm` value before this adapter clips each
enabled lane to its unsigned or signed destination range. Disabled lanes never
contribute saturation. The result carries the per-beat saturation indication
to WB rather than mutating CSR state in the combinational adapter. Reduction
and permutation scheduling are not supplied here.

The fixed-point execution slice implements saturating `vsaddu`, `vsadd`,
`vssubu`, and `vssub`; averaging `vaaddu`, `vaadd`, `vasubu`, and `vasub`;
scaling shifts `vssrl` and `vssra`; narrowing clips `vnclipu` and `vnclip`; and
signed fractional multiply `vsmul` in their architectural forms. `vxrm` is
captured with the macro descriptor, so later CSR changes cannot alter admitted
work. Saturating operations report their result through WB-owned completion
state before producing the sticky `vxsat` update.

`RV5StageVectorResult(vlen)` converts a SIMD result into the bank write payload.
Data destinations use the returned output element width; comparisons and
carry/borrow results place one bit per element into the correct destination
mask chunk. Disabled bytes/bits receive no write enable, preserving inactive
and tail contents.

Both adapters expose `legal` for their local chunk/alignment/bank bounds. The
caller must gate writes with it and separately validate architectural register
groups, EEW/EMUL, `vtype`, overlap, and instruction-specific restrictions. The
pure [`VectorConfig`](../../../riscv/README.md#vector-geometry) supplies host
geometry and ordinary data-overlap rules, not an instruction decoder.

Writes are authorized inputs. The bank and packing adapters do not decide WB
permission or cancel accepted work; the core's WB boundary owns that decision.

See [DEVELOPING.md](DEVELOPING.md) for ownership and focused validation.
