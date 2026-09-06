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
| [`BranchResolver(width)`](branch-resolver.rhdl) | `Valid(BranchResolverRequest)` to `Valid(BranchResult)` | Combinational; output validity follows input validity, with no backpressure | Equal and signed/unsigned less-than comparison plus final `taken` selection | Encodings, target generation, PC state, and redirect timing |
| [`LoadGen(xlen, beat_bytes = 8)`](load-store.rhdl) | Address, returned beat, `MemoryWidth`, and signedness to one XLEN value | Combinational; the power-of-two beat must contain an XLEN word | Addressed scalar extraction and sign/zero extension | Beat-address alignment, access validation, protocol, and ordering |
| [`StoreGen(xlen, beat_bytes = 8)`](load-store.rhdl) | Address, XLEN value, and `MemoryWidth` to beat data and `Mask(beat_bytes)` | Combinational; the power-of-two beat must contain an XLEN word | Addressed scalar placement and byte-lane mask generation | Beat-address alignment, access validation, protocol, and ordering |
| [`IterativeMultiplier(width)`](multiplier.rhdl) | `Decoupled(MultiplierRequest)` to an `Irrevocable` double-width product | One request at a time; consumes one multiplier bit per cycle; response stays stable until accepted; may replace a response as it is consumed | Signed/unsigned magnitude handling and the complete product | Low/high/word projection and architectural destination |
| [`IterativeDivider(width)`](divider.rhdl) | `Decoupled(DividerRequest)` to an `Irrevocable(DividerResponse)` | One request at a time; resolves one quotient bit per cycle; response stays stable until accepted; may replace a response as it is consumed | Quotient, remainder, divide-by-zero, and fixed-width signed-overflow behavior | Quotient/remainder/word projection and architectural destination |

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
