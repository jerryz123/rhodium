<!-- Introduces reusable processor components and the named cores built from them. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Processor components and cores

The shared [vector engine](vector/README.md) supplies sequencing, VRF storage,
SIMD execution, and persistent completion ownership. Named cores retain
admission, architectural retirement, and LSU integration policy.

Use `cores/` for processor RTL, not for Rhodium's language internals. The
similarly named [`rhodium/core/`](../rhodium/core/README.md) owns the
frontend-independent hardware IR.

Contributors adding components or named cores should read
[`DEVELOPING.md`](DEVELOPING.md) for placement, dependency, and validation rules.

## Pick a reusable component

Reusable execution datapaths expose already-decoded physical controls. Their callers own
instruction recognition, operand selection, pipeline scheduling, and
architectural result selection.

[`operand.rhdl`](operand.rhdl) provides `OperandControl`, `OperandASelect`,
`OperandBSelect`, and `OperandImmediate`. Import
[`operand-decode.rhdl`](operand-decode.rhdl) for canonical RV32/RV64 I/M/A/B/Zicond operand
relations and partial source-use patterns. Inactive fields remain don't-cares;
each core composes its extension domains and dedicated execution-unit routing.

[`profile.rhm`](profile.rhm) projects explicitly selected capabilities into ISA
name groups and MISA bits. Its scalar, FP, memory, hint, privileged, and supervisor
helpers do not choose defaults or qualify an implementation. Named profiles
retain configuration validation and decide which guarantees they may publish.

| Component | Interface and parameters | Timing contract | Component owns | Caller owns |
|---|---|---|---|---|
| [`ALU(xlen)`](alu.rhdl) | `XLen.X32` or `XLen.X64`; `left`, `right`, and `AluControl` to `result` | Combinational; no ready/valid state | Modular arithmetic, logic, shifts/rotates, comparisons, counts, unary transforms, RV64 word shaping, and the shared Zba/Zbb/Zbs/Zicond datapaths | Decode, operand routing, and result use |
| [`SimdALU(xlen)`](simd-alu.rhdl) | Two 32/64-bit packed operands, runtime element width up to the physical word, decoded controls, fixed-point rounding mode, carry/borrow inputs, and lane masks | Combinational; no ready/valid state | Lane-isolated wrapping, saturating, averaging, and carry/borrow arithmetic; logic; shifts/rotates; fixed-point rounding; counts; reversals; comparisons; min/max; selection; and result/write-mask packing | Instruction decode, operand extraction/broadcast, clipping, vector configuration, register preservation, scheduling, and writeback |
| [`SimdWidenOperands(xlen)`](simd-alu.rhdl) | Two packed words, source elements up to half the physical width, optional already-wide left input, half selection, and element enables | Combinational; no ready/valid state | Per-source extension and enable remapping for one physical destination word | Group sequencing, scalar/immediate broadcasting, register grouping, and architectural legality |
| [`SimdExtend(xlen)`](simd-alu.rhdl) | One packed 32/64-bit source, destination element width, 2x/4x/8x ratio, source fragment, and signedness | Combinational; no ready/valid state | Direct sign/zero extension into one physical destination beat | Source EEW/EMUL scheduling, register grouping, masking, and architectural overlap legality |
| [`SimdCompress(xlen)`](simd-alu.rhdl) | One packed 32/64-bit word, runtime element width, and element-selection mask | Combinational; no ready/valid state | Stable-order compaction into consecutive low lanes and selected-element count | Cross-word accumulation, architectural register grouping, tails, restart, and writeback |
| [`BranchResolver(width)`](branch-resolver.rhdl) | `Valid(BranchResolverRequest)` to `Valid(BranchResult)` | Combinational; output validity follows input validity, with no backpressure | Equal and signed/unsigned less-than comparison plus final `taken` selection | Encodings, target generation, PC state, and redirect timing |
| [`LoadGen(xlen, beat_bytes = 8)`](load-store.rhdl) | Address, returned beat, `MemoryWidth`, and signedness to one XLEN value | Combinational; the power-of-two beat must contain an XLEN word | Addressed scalar extraction and sign/zero extension | Beat-address alignment, access validation, protocol, and ordering |
| [`StoreGen(xlen, beat_bytes = 8)`](load-store.rhdl) | Address, XLEN value, and `MemoryWidth` to beat data and `Mask(beat_bytes)` | Combinational; the power-of-two beat must contain an XLEN word | Addressed scalar placement and byte-lane mask generation | Beat-address alignment, access validation, protocol, and ordering |
| [`CachePrefetchReq(address_width)`](cache-prefetch.rhdl) | Address plus instruction, read, or write intent, transported over `Valid` | Best effort; no acceptance, completion, or fault channel | A reusable core-to-memory-hierarchy prefetch event | ISA decode, translation, permission checks, cache policy, and dropping under contention |
| [`MemoryResponseCapture(Context, Result)`](memory-response.rhdl) | Same-cycle `Valid` context and optional response, plus a local fallback | Context passes combinationally; result arrives one cycle later; payload captures even in bubbles | Response pairing and result storage, with structural lineage from both inputs | Parallel instruction/control capture, response selection policy, recovery, and retirement |
| [`IterativeMultiplier(width)`](multiplier.rhdl) | `Decoupled(MultiplierRequest)` to an `Irrevocable` double-width product | One request at a time; one magnitude-preparation cycle after capture, then one multiplier bit per cycle; response stays stable until accepted; may replace a response as it is consumed | Signed/unsigned magnitude handling and the complete product | Low/high/word projection and architectural destination |
| [`PipelinedMultiplier(width)`](multiplier.rhdl) | Power-of-two width of at least two; `Valid(MultiplierRequest)` to a `Valid` double-width product | Three feed-forward stages; accepts and advances one request per cycle with fixed latency and no backpressure | Four raw half-width products, signed corrections, carry-save reduction, and one final sum | Write-cycle reservations or elastic return credits, low/high/word projection, and architectural destination |
| [`SimdMultiplier(width, max_element_width)` / `SimdMultiplyAccumulate(width, max_element_width)`](simd-multiplier.rhdl) | Physical word width and largest element width, packed operands, independent signedness; optional addend and result controls | Three cycles for full products, four for formatted results; `Valid` input/output, one request per cycle | Shared tapered multipliers, product packing, low/high/widened projection, lane-isolated MAC, fractional rounding and saturation | Operand routing, masks, issue/return reservations, scalar sharing, and vector/core integration |
| [`IterativeDivider(width)`](divider.rhdl) | `Decoupled(DividerRequest)` to an `Irrevocable(DividerResponse)` | One request at a time; trivial operands complete directly, otherwise leading-zero quotient work is skipped before resolving one remaining bit per cycle and finalizing signs; response stays stable until accepted; may replace a response as it is consumed | Quotient, remainder, divide-by-zero, and fixed-width signed-overflow behavior | Quotient/remainder/word projection and architectural destination |

### Tagged integer services

[`integer-service.rhdl`](integer-service.rhdl) carries opaque caller tags through
the shared multiplier and divider. `TaggedPipelinedMultiplier` is feed-forward
Valid input/output with no completion storage. `IterativeMultiplyService` and
`DivideService` retain an accepted tag until their held result is consumed.
`PipelinedMultiplyService` explicitly selects buffered returns or scheduled
writeback, where every result must be consumed on its return cycle.
Callers retain launch arbitration, scalar authorization, result projection,
and destination/write-port scheduling.

[`integer-format.rhdl`](integer-format.rhdl) provides combinational scalar
M-extension formatting: `integer_word_operand` normalizes signed/unsigned RV64
word-division inputs, `integer_multiply_result` selects the product half or MULW
result, and `integer_divide_result` selects quotient/remainder with word-result
sign extension. `integer_word_result` exposes that sign-extension rule directly.
RV32 values remain XLEN-wide; none of these helpers adds storage or arbitration.

### Reservation waiting

[`ReservationWaitMode`](reservation-wait.rhdl) distinguishes `None`,
`NoTimeout`, and `ShortTimeout`. [`ReservationWaitCases`](reservation-wait-decode.rhdl)
maps canonical WRS instructions to those modes for composition into a core's
partial control relation; it does not impose serialization or trap policy.

[`ReservationWait`](reservation-wait.rhdl) owns the saturating 12-bit WRS timer.
`capture` restarts it; `active` refers to a caller-retained instruction. Loss of
`reservation_valid` or `interrupt_wake` completes normally, ahead of timeout.
A short wait completes normally at timeout; an unbounded wait times out only
when the caller enables `trap_on_timeout` or `virtual_trap_on_timeout`. The former
wins if both apply. `completion` is a Valid level until the caller releases its
active owner, with separate illegal/virtual-instruction flags. The caller owns
privilege qualification, instruction retention, trap values, and retirement.

### Writeback calendar

[`WritebackCalendar(clients, latency, ~queries: 1)`](writeback-calendar.rhdl)
reserves future cycles of one write port without storing result payloads.
Each `reserve[client]` is a Decoupled delay; lower-index clients win same-cycle
collisions. Delays are `Bits(index_width(latency + 1))`; valid reservations
must name cycles 1 through `latency`, or assert. `occupied` reports registered
bookings, with bit zero naming the current cycle. Every clock ages bookings,
including while `pause` blocks new reservations; synchronous reset clears them.

The `query_client` and `query_delay` inputs are vectors of `queries` entries.
Each entry has two distinct combinational answers:

| Output | Bookings it observes | Effect of `pause` |
|---|---|---|
| `registered_free[q]` | Only registered occupancy; no same-cycle claim from any client | None |
| `prior_claims_available[q]` | Registered occupancy plus accepted same-cycle claims from clients before `query_client[q]` | False while paused |

The ordered answer excludes the selected client's own claim and all later
clients. The registered answer ignores `query_client`. Queries may inspect
cycle zero through `latency`; out-of-range delays return false for both
answers, and an out-of-range client returns false for the ordered answer.
Queries never reserve a cycle or consume a grant.

Use registered queries when admission must not depend on other live claims;
the caller must then resolve same-cycle conflicts separately and honor the
real reservation handshake. Use ordered queries only when dependencies on
earlier clients are safe. Neither query alone grants instruction admission,
launch, authorization, or retirement.

### Shared memory decode

`memory-decode.rhdl` supplies `scalar_memory_cases(Control, ~xlen: xlen)`,
`atomic_memory_cases(Control, ~xlen: xlen)`, and
`cache_block_memory_cases(Control)`. The caller-shaped control bundle supplies
`access`, `atomic`, `width`, and `unsigned` fields as needed. The returned partial
relations leave irrelevant controls unconstrained; callers add validity fields,
inactive-domain coverage, and pipeline policy before composing their decoder.
`RiscvGuestMemoryInstructions` maps guest loads/stores to ordinary LSU operations
and distinguishes HLVX execute-read permission.

## Packed SIMD integer ALU

`SimdALU(xlen)` takes `xlen :: XLen` (`XLen.X32` or `XLen.X64`); its physical word width is `xlen.width`.
The 32-bit specialization operates on 4, 2, or 1 independent E8/E16/E32
elements; the 64-bit specialization adds E64 and doubles those lane counts.
The caller must select an element width that fits the physical word. Element
zero occupies the least significant bits. `enabled` and `select_right` are
`Mask(xlen.width / 8)` values indexed by
element, not byte; unused high mask bits are ignored at wider element sizes.

`SimdAluControl` chooses an adder, logic, shift, comparison, min/max, select,
permutation, or count result. `SimdArithmeticMode` makes add/subtract wrap or
saturate independently at element width, or compute an infinite-precision
average before rounding and truncation. Shifts use the low
log2(element-width) bits of each element's right operand; arithmetic fill
applies only to nonrotating right shifts. `rotate` changes the shared shift path
to element-local rotation and ignores `arithmetic_shift`; direction still comes
from `shift_right`. `invert_right` complements the right logic operand, supporting
AND-NOT (and OR-NOT/XNOR) without changing arithmetic operands.
Comparisons support equality, less-than, and
less-or-equal with signed or unsigned ordering. Min/max uses the same ordering;
select chooses the right operand when that element's `select_right` bit is set.
`carry_in` supplies one low carry or borrow bit per active element. Add consumes
that bit directly; subtract computes `left - right - carry_in`. The same
guard-bit adder exposes the corresponding unsigned carry or borrow bits.

For a right shift, `rounding` selects RVV fixed-point rounding after the shared
tapered shifter and before the existing lane-isolated adder. `SimdRoundingMode`
uses the architectural `vxrm` order: nearest-up, nearest-even, down, and odd.
Each lane derives its increment from its own discarded bits; a zero shift never
increments. Averaging arithmetic uses the same rounding modes with one
discarded bit. Saturating arithmetic returns an enabled-lane reduction in
`SimdAluResult.saturated`; the ALU does not clip or retain architectural state.

`SimdPermutationSelect` chooses `ReverseBits` within each element,
`ReverseBitsInBytes` independently within each byte, or `ReverseBytes` within
each element. `SimdCountSelect` chooses `LeadingZeros`, `TrailingZeros`, or
`Population`, returning one unsigned count per element. Zero inputs have a
leading/trailing-zero count equal to the element width. Counts and permutations
operate on the left operand and ignore the right operand.

`SimdAluResult.data` contains the selected packed values (comparison results are
zero or one per element). `mask_result` reports either the configured comparison
predicate or the adder's carry/borrow result, selected by `mask_result_select`,
as one packed bit per enabled element regardless of ordinary data selection.
`write_mask` expands enabled elements to their destination byte enables, and
`saturated` reports whether an enabled lane saturated.
Disabled elements produce zero data and mask-result bits and no byte enables.
The caller uses those enables to preserve old register contents; the ALU has
no architectural state or tail policy. Mask-register destinations use the
element enables rather than the data byte mask.

`SimdWidenOperands(xlen)` takes `SimdWidenElementWidth.E8/E16/E32`, two source
words, independent `left_signed`/`right_signed` controls, `upper_half`, and
source-element `enabled` bits. `left_wide` passes an already destination-width
left word through while the right input is extended from the selected source
half. Its `operands` output contains the prepared `left`/`right` values,
destination `element_width`, and
destination-element `enabled`. Each invocation expands the lower or upper
half of the source word. The caller must select a doubled element width that
fits the physical word: E8/E16 sources on 32-bit hardware, or E8/E16/E32 on
64-bit hardware. Enables are selected
from the corresponding source elements; unused high output enable bits are
zero. Prepared data is not masked. Two invocations cover a complete source
word, and the caller owns which group to issue and where to write it. Independent
signedness and the already-wide selector support heterogeneous source widths
without ISA recognition in the reusable adapter.

Feeding these operands into an ordinary logical left shift implements the
datapath for `vwsll`: sources are zero-extended before shifting, and shift
amounts are reduced modulo the destination width (twice the source width).
Together with the ALU's logic, rotation, reversal, and count paths, this provides
the execution operations required by [Zvbb](https://docs.riscv.org/reference/isa/v20250508/unpriv/vector-crypto.html).
Instruction forms, vector masks/tails, `vl`/`vstart`, register-group legality,
and scheduling remain caller policy. The reusable block does not advertise an
ISA; RV5Stage owns its architectural Zvbb integration and profile selection.

The block remains independent of RISC-V profiles: RV5Stage supplies the decode,
legality, and scheduling around it. It does not promise a clock frequency or
provide a registered pipeline.

`SimdExtend(xlen)` uses the same physical word width for source and destination.
Its `legal` output checks the extension ratio, fragment, and destination width,
including rejection of E64 on 32-bit hardware. No 64-bit execution datapath is
retained inside the 32-bit specialization.

`SimdCompress(xlen)` retains the input order of selected elements, places them in
consecutive low lanes, clears unused output lanes, and reports a count from zero
through the number of physical lanes. Selection bits above the active lane count
are ignored. It is a stateless word-local primitive; a vector implementation
must retain cross-word remnants and authorize destination writes itself.

### Packed SIMD multiplication

Import `lib("cores/simd-multiplier.rhdl")`. Instantiate
`SimdMultiplier(width, max_element_width)` for raw products, or
`SimdMultiplyAccumulate(width, max_element_width)` for a complete numerical
result pipeline. These physical dimensions are independent of core XLEN.
Both can be used standalone. RV2Wide's shared vector engine uses the formatted
pipeline for subword multiplication and retains scalar full-width fallback;
see [packed vector multiplication](vector/README.md#packed-integer-multiplication).

| Parameters | Multiplier banks | Products per cycle |
|---|---|---|
| `(32, 16)` | Two 16/8 banks | 4x8 or 2x16 |
| `(64, 32)` | Two 32/16/8/8 banks | 8x8, 4x16, or 2x32 |
| `(64, 64)` | One 64/32/16/16/8/8/8/8 bank | 8x8, 4x16, 2x32, or 1x64 |

`width` is a power-of-two physical word size of at least eight bits;
`max_element_width` is one of 8, 16, 32, or 64 and cannot exceed `width`.
All power-of-two element widths from eight through that limit are supported.
The word contains repeated banks, so wider physical words do not require wider
elements. Choosing a smaller element limit leaves larger elements to another
datapath; choosing 64 permits self-contained 64-bit multiplication. Scalar
sharing is caller policy, not a property inferred from XLEN.

An asserted raw request with an unsupported width fails the component assertion.
Each accepted word produces `2*width` bits: product lane zero is at the least-significant end,
and each product occupies twice its source element width. Signedness applies
independently to the two operands. Element width is retained with the products.

The guaranteed timing is:

```text
Valid operands -> partial products -> carry-save reduction -> final products
                                                            | three cycles
                                   result processing -> registered result
                                                        | four cycles total
```

There is no backpressure, operand-dependent timing, or completion storage.
Successive requests may change widths and controls. Synchronous reset clears
all in-flight validity, not otherwise-unobservable payloads.

`SimdMultiplyAccumulateRequest` supplies `operands` and a `context` containing
one physical word of `addend` and `SimdMultiplyControl`. `select` chooses `Low`, `High`,
`Wide`, or `Fractional`. `Wide` selects one half of the packed full products
using `upper_half`, giving one physical destination beat with doubled element
width. The doubled element must fit the physical word and be at most 64 bits;
invalid widening requests fail the component assertion. Two requests with
different halves cover a full source word; sequencing
and destination selection remain outside the component. When `accumulate` is
true, the selected result is added to or subtracted from `addend`, according to
`subtract`, modulo each destination element width. Carries never cross lanes.
The caller supplies the actual multiplicands/addend for each instruction form.

`Fractional` requires signed multiplicands, rounds the product divided by
2^(SEW-1) according to `SimdRoundingMode`, then clips to the signed SEW range.
It ignores `accumulate` and `subtract`. Other selections ignore rounding;
only `Wide` observes `upper_half`. The result's `saturated` mask replicates
each element's saturation onto its destination bytes; it is zero for other
selections. The caller qualifies this mask with active byte enables before
updating architectural saturation state. No mask, tail, CSR, or instruction
state is maintained here.

`simd_multiply_result(width, products, context)` exposes the combinational
formatter independently, including for products supplied by a scalar multiplier.
Its host `~max_element_width` defaults to `min(width, 64)`; the pipeline passes
its configured limit to omit unused projection and rounding logic. The helper
creates no multiplier, and its caller must satisfy the same source-width and
widening constraints. The component does not promise a synthesized frequency or area.

### Scalar memory and multicycle engines

`MemoryWidth.is_aligned(address)` checks the same byte, halfword, word, or
doubleword size contract used by the load/store generators. The generators do
not suppress misaligned requests; invoke the helper or perform an equivalent
check before issuing one.
`memory_byte_mask(xlen, address, width)` returns the corresponding byte enables
within the containing XLEN word. It does not split misaligned accesses.

The iterative multiplier and divider transfer requests on `request.fire()` and
hold their `Irrevocable` responses until `response.fire()`. The pipelined
multiplier instead consumes every asserted `Valid` request and produces the
corresponding `Valid` response exactly three cycles later, independently of
operand values and signedness. The stages compute raw half-width products,
compress their aligned terms and signed corrections without full-width carry
propagation, then perform one final addition. It has no readiness path: a
caller must reserve its write cycle or elastic return capacity before launch.

## Map RISC-V instructions onto components

Import [`alu-decode.rhdl`](alu-decode.rhdl),
[`branch-decode.rhdl`](branch-decode.rhdl),
[`multiply-decode.rhdl`](multiply-decode.rhdl), or
[`divide-decode.rhdl`](divide-decode.rhdl) for reusable relations from the pure
RV32/RV64 instruction catalogs to the component controls above. Named cores
compose those case lists into their complete decode relation and select their
own extension sets; the mappings do not impose a pipeline policy.

## Shared state and integration

The package is organized by component, not by ISA namespace:

| Location | Shared responsibility |
|---|---|
| Root datapaths and `*-decode.rhdl` | Execution blocks and their component-control relations |
| [`cache/`](cache/README.md) | Physical L1I/L1D, cache protocols, and cache-side CHI engines |
| [`csr/`](csr/README.md) | Core-neutral CSR/trap state behind authorized commands |
| [`mmu/`](mmu/README.md) | Host/guest TLBs, page-table walking, and translation contracts |
| [`fp/`](fp/README.md) | FP instruction/control mappings, opaque-tag arithmetic service, and 3R2W FPR storage |
| [`bpred/`](bpred/README.md) | Core-neutral BTB, speculative/resolved RAS, prediction payloads, and call/return hints |
| Root integration helpers | CHI placement, split accesses, vector row layout, and passive observations |
| `rv5stage/`, `rv2wide/`, `spike/` | Named-core configuration, pipeline policy, and integration |

These shared components may be RISC-V-specific. Architectural encodings and
stateless architectural helpers remain in [`../riscv/`](../riscv/README.md);
storage and transaction ownership stay in `cores/`.

### CHI hart attachment

[`RiscvHartCHIConfig`](chi-hart.rhdl) derives physical-memory and CHI Home maps
from one nonempty region list. `RiscvHartCHIParams` validates distinct requester
and Home NodeIDs; `RiscvHartCHIAttachment` describes RN-I instruction/uncached
and RN-F data endpoints. `RiscvHartCHIIdentity` carries placement IDs into an
instance. Named cores retain their transaction engines and capabilities.

### Misaligned ordinary accesses

[`RiscvMisalignedEngine(xlen, Context)`](misaligned-access.rhdl) retains an
ordinary load/store and emits one or two independently authorized aligned word
fragments. `Context` is opaque caller metadata. `RiscvSplitAccess` provides a
Decoupled original request and one final Valid `RiscvSplitResult`.

The guaranteed sequence is capture → first request/response → optional second
request/response → final outcome. Callers translate and check each fragment and
retain retirement ownership. First-fragment faults report the original VA;
second-fragment faults report the next aligned word. Accepted store prefixes
survive later faults: there is no two-page preflight or prefix rollback/replay.
Loads assemble and sign/zero-extend the natural-width result. Callers must
restrict widened fragments to memory where full-word accesses are safe.

### Vector row layout

[`VectorRegisterLayout(vlen, row_bits)`](vector-layout.rhm) maps architectural
elements and mask bits into physical rows. Row width must divide VLEN and an
element must fit within a row. The layout exposes `rows_per_register`, `depth`
for all 32 registers, and row/bit locations. With VLEN=64, 32-bit rows give a
64-row bank; 64-bit rows give a 32-row bank. This pure host description does
not prescribe SRAM ports or scheduling.

### Passive architectural observations

[`cosim-source.rhm`](cosim-source.rhm) declares hart adapter contracts and
read-only semantic taps without capture hardware. The optional
[simulation pass](../sims/cosim/README.md#compile-target-instrumentation) selects
adapters and adds DPI capture at compilation. Functional cores import only
declarations; simulation owns ordered event reconstruction and checking.

## Add or inspect a named core

A named core owns its decode, datapath, architectural state, pipeline policy,
integration adapters, and public system boundary. Named cores may reuse the
components above without changing those components' caller-owned policy.

RV5Stage is the full RTL named core. Its default profile is integer-only;
supported optional profiles are RV32F on `XLen.X32` and RV64D on `XLen.X64`,
with the D profile also implementing F. RV32D and an RV64F-only specialization
are rejected. Compressed instructions use composable Zc selections with Zca
and the FP-profile-dependent C composition as presets and Zcb as an orthogonal
extension. See [`rv5stage/README.md`](rv5stage/README.md) for the owned
instruction families, pipeline and completion contracts, FP state and
execution, memory hierarchy, CHI boundary, generator parameters, ports, tests,
and deliberate limits.

[`RV2Wide`](rv2wide/README.md) is an in-progress dual-issue in-order core. Its
current RR-through-WB slice reuses the shared ALU, branch resolver, load/store
datapaths, and scoreboard. It supports dual retirement, speculative hit lookup,
WB-authorized memory, and separately owned deferred completions. Its cached
composition fetches 64-bit instruction blocks through the shared L1I and uses
the [shared physical L1D](cache/README.md), both also used by RV5Stage. It has
shared CSR/trap and translation machinery, precise faults, and Mini/Simple SoC
selection. See its [README](rv2wide/README.md) for the current ISA, memory,
retirement, cosim, and tracing contracts.

[`spike/spike.rhdl`](spike/spike.rhdl) is the standalone simulator-backed named
core. It projects the shared architectural hart description, runs Spike through
a typed DPI transaction boundary, keeps PMA classification in RTL, and exposes
independent instruction, coherent-data, and uncached CHI ports.
The [single-core SoC](../socs/README.md) composes that boundary with the shared
coherent platform through its explicit core selection. See
[`spike/README.md`](spike/README.md) for the adapter, private-cache, and
simulation contracts.
