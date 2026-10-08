<!-- Introduces reusable processor components and the named cores built from them. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Processor components and cores

Use `cores/` for processor RTL, not for Rhodium's language internals. The
similarly named [`rhodium/core/`](../rhodium/core/README.md) owns the
frontend-independent hardware IR.

Contributors adding components or named cores should read
[`DEVELOPING.md`](DEVELOPING.md) for placement, dependency, and validation rules.

## Pick a reusable component

Reusable execution datapaths expose already-decoded physical controls. Their callers own
instruction recognition, operand selection, pipeline scheduling, and
architectural result selection.

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
| [`IterativeDivider(width)`](divider.rhdl) | `Decoupled(DividerRequest)` to an `Irrevocable(DividerResponse)` | One request at a time; trivial operands complete directly, otherwise leading-zero quotient work is skipped before resolving one remaining bit per cycle and finalizing signs; response stays stable until accepted; may replace a response as it is consumed | Quotient, remainder, divide-by-zero, and fixed-width signed-overflow behavior | Quotient/remainder/word projection and architectural destination |

### Shared memory decode

`memory-decode.rhdl` supplies `scalar_memory_cases(Control, ~double: enabled)`,
`atomic_memory_cases(Control, ~double: enabled)`, and
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

[`cosim-source.rhm`](cosim-source.rhm) declares versioned hart contracts and
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
