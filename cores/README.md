<!-- Defines ownership and dependency boundaries for reusable processor components and named cores. -->

# Processor components and cores

Use `cores/` for processor RTL, not for Rhodium's language internals. The
similarly named [`rhodium/core/`](../rhodium/core/README.md) owns the
frontend-independent hardware IR.

Contributors adding components or named cores should read
[`DEVELOPING.md`](DEVELOPING.md).

## Choose the right home

Contributor placement and dependency rules are documented in
[`DEVELOPING.md`](DEVELOPING.md#choose-the-right-home).

## Pick a reusable component

All reusable blocks expose already-decoded physical controls. Their callers own
instruction recognition, operand selection, pipeline scheduling, and
architectural result selection.

| Component | Interface and parameters | Timing contract | Component owns | Caller owns |
|---|---|---|---|---|
| [`ALU(xlen)`](alu.rhdl) | `XLen.X32` or `XLen.X64`; `left`, `right`, and `AluControl` to `result` | Combinational; no ready/valid state | Modular arithmetic, logic, shifts/rotates, comparisons, counts, unary transforms, RV64 word shaping, and the shared Zba/Zbb/Zbs/Zicond datapaths | Decode, operand routing, and result use |
| [`SimdALU()`](simd-alu.rhdl) | Two 64-bit packed operands, runtime 8/16/32/64-bit elements, decoded controls, and lane masks | Combinational; no ready/valid state | Lane-isolated arithmetic, logic, shifts/rotates, counts, reversals, comparisons, min/max, selection, and result/write-mask packing | Instruction decode, operand extraction/broadcast, vector configuration, register preservation, scheduling, and writeback |
| [`SimdWidenOperands()`](simd-alu.rhdl) | Two packed 64-bit source operands, 8/16/32-bit source elements, half selection, and element enables | Combinational; no ready/valid state | Zero-extension and enable remapping for one 64-bit destination group | Group sequencing, scalar/immediate broadcasting, register grouping, and architectural legality |
| [`BranchResolver(width)`](branch-resolver.rhdl) | `Valid(BranchResolverRequest)` to `Valid(BranchResult)` | Combinational; output validity follows input validity, with no backpressure | Equal and signed/unsigned less-than comparison plus final `taken` selection | Encodings, target generation, PC state, and redirect timing |
| [`LoadGen(xlen, beat_bytes = 8)`](load-store.rhdl) | Address, returned beat, `MemoryWidth`, and signedness to one XLEN value | Combinational; the power-of-two beat must contain an XLEN word | Addressed scalar extraction and sign/zero extension | Beat-address alignment, access validation, protocol, and ordering |
| [`StoreGen(xlen, beat_bytes = 8)`](load-store.rhdl) | Address, XLEN value, and `MemoryWidth` to beat data and `Mask(beat_bytes)` | Combinational; the power-of-two beat must contain an XLEN word | Addressed scalar placement and byte-lane mask generation | Beat-address alignment, access validation, protocol, and ordering |
| [`CachePrefetchReq(address_width)`](cache-prefetch.rhdl) | Address plus instruction, read, or write intent, transported over `Valid` | Best effort; no acceptance, completion, or fault channel | A reusable core-to-memory-hierarchy prefetch event | ISA decode, translation, permission checks, cache policy, and dropping under contention |
| [`IterativeMultiplier(width)`](multiplier.rhdl) | `Decoupled(MultiplierRequest)` to an `Irrevocable` double-width product | One request at a time; one magnitude-preparation cycle after capture, then one multiplier bit per cycle; response stays stable until accepted; may replace a response as it is consumed | Signed/unsigned magnitude handling and the complete product | Low/high/word projection and architectural destination |
| [`IterativeDivider(width)`](divider.rhdl) | `Decoupled(DividerRequest)` to an `Irrevocable(DividerResponse)` | One request at a time; resolves one quotient bit per cycle, then finalizes signs for one cycle; response stays stable until accepted; may replace a response as it is consumed | Quotient, remainder, divide-by-zero, and fixed-width signed-overflow behavior | Quotient/remainder/word projection and architectural destination |

### Packed SIMD integer ALU

`SimdALU()` operates on 8, 4, 2, or 1 independent elements within 64 bits,
selected by `SimdElementWidth.E8/E16/E32/E64`. Element zero occupies the least
significant bits. `enabled` and `select_right` are `Mask(8)` values indexed by
element, not byte; unused high mask bits are ignored at wider element sizes.

`SimdAluControl` chooses an adder, logic, shift, comparison, min/max, select,
permutation, or count
result. Add/subtract wrap independently at element width. Shifts use the low
log2(element-width) bits of each element's right operand; arithmetic fill
applies only to nonrotating right shifts. `rotate` changes the shared shift path
to element-local rotation and ignores `arithmetic_shift`; direction still comes
from `shift_right`. `invert_right` complements the right logic operand, supporting
AND-NOT (and OR-NOT/XNOR) without changing arithmetic operands.
Comparisons support equality, less-than, and
less-or-equal with signed or unsigned ordering. Min/max uses the same ordering;
select chooses the right operand when that element's `select_right` bit is set.

`SimdPermutationSelect` chooses `ReverseBits` within each element,
`ReverseBitsInBytes` independently within each byte, or `ReverseBytes` within
each element. `SimdCountSelect` chooses `LeadingZeros`, `TrailingZeros`, or
`Population`, returning one unsigned count per element. Zero inputs have a
leading/trailing-zero count equal to the element width. Counts and permutations
operate on the left operand and ignore the right operand.

`SimdAluResult.data` contains the selected packed values (comparison results are
zero or one per element). `comparison` independently reports the configured
predicate as one packed bit per enabled element, regardless of result selection.
`write_mask` expands enabled elements to their destination byte enables.
Disabled elements produce zero data and comparison bits and no byte enables.
The caller uses those enables to preserve old register contents; the ALU has
no architectural state or tail policy. Mask-register destinations use the
element enables rather than the data byte mask.

`SimdWidenOperands()` takes `SimdWidenElementWidth.E8/E16/E32`, two source
words, `upper_half`, and source-element `enabled` bits. Its `operands` output
contains zero-extended `left`/`right` values, destination `element_width`, and
destination-element `enabled`. The lower or upper 32 source bits become four
16-bit, two 32-bit, or one 64-bit destination elements. Enables are selected
from the corresponding source elements; unused high output enable bits are
zero. Prepared data is not masked. Two invocations cover a complete source
word, and the caller owns which group to issue and where to write it.

Feeding these operands into an ordinary logical left shift implements the
datapath for `vwsll`: sources are zero-extended before shifting, and shift
amounts are reduced modulo the destination width (twice the source width).
Together with the ALU's logic, rotation, reversal, and count paths, this provides
the execution operations required by [Zvbb](https://docs.riscv.org/reference/isa/v20250508/unpriv/vector-crypto.html).
Instruction forms, vector masks/tails, `vl`/`vstart`, register-group legality,
and scheduling remain caller policy; this is not architectural Zvbb support
or an advertised ISA profile.

The block is independent of RISC-V profiles and is not integrated into RV5Stage.
It does not promise a clock frequency or provide a registered pipeline.

### Scalar memory and iterative engines

`MemoryWidth.is_aligned(address)` checks the same byte, halfword, word, or
doubleword size contract used by the load/store generators. The generators do
not suppress misaligned requests; invoke the helper or perform an equivalent
check before issuing one.

For both iterative engines, a request transfers only when `request.fire()` is
true. The `Irrevocable` response may be backpressured and must be consumed with
`response.fire()`. This interface deliberately leaves queueing, cancellation,
destination tracking, and writeback policy outside the reusable block.

## Add or inspect a named core

A named core owns its decode, datapath, architectural state, pipeline policy,
integration adapters, and public system boundary. Named cores may reuse the
components above without changing those components' caller-owned policy.

RV5Stage is the current named core. Its default profile is integer-only;
supported optional profiles are RV32F on `XLen.X32` and RV64D on `XLen.X64`,
with the D profile also implementing F. RV32D and an RV64F-only specialization
are rejected. Compressed instructions use composable Zc selections with Zca
and the FP-profile-dependent C composition as presets and Zcb as an orthogonal
extension. See [`rv5stage/README.md`](rv5stage/README.md) for the owned
instruction families, pipeline and completion contracts, FP state and
execution, memory hierarchy, CHI boundary, generator parameters, ports, tests,
and deliberate limits.

## Preserve dependency direction

The enforced implementation dependency graph moved to
[`DEVELOPING.md`](DEVELOPING.md#dependency-direction). This heading remains for
existing links.

## Verify a change

Contributor test selection and boundary checking are documented in
[`DEVELOPING.md`](DEVELOPING.md#focused-validation).
