<!-- Defines the public behavior and integration contracts of RV5Stage floating point. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV5Stage floating point

RV5Stage's optional floating-point subsystem provides architectural FP
register state, fixed-latency arithmetic, buffered division and square root,
load/store integration, destination tracking, and completion arbitration. The
core supports RV32F or RV64D, with optional Zfhmin, Zfh, and Zfa behavior.

## Shared operand execution

[`RV5StageFpExecutionService`](execute.rhdl) accepts explicit FP operands,
an integer operand, decoded controls, an immediate index, a resolved rounding
mode, and a caller-selected `Tag` type. Its `Decoupled` request transfers
authorize execution; its `Irrevocable` result holds the unchanged tag, FP and
integer result lanes, exception flags, and a flag-update qualifier until consumed.
The selected operation determines which result lane is meaningful. Control
register-use/destination fields select numeric conversion direction; they do
not name, read, reserve, or write an architectural register.

FP operands use the existing FLEN-wide NaN-boxed representation. Results retain
the same boxing, canonical-NaN, resolved-rounding, and flag behavior as scalar
execution. A future packed vector caller must box narrow elements on entry and
extract the selected element width on return. The service does not resolve
dynamic rounding modes, accumulate architectural flags, or interpret tags.

Fixed execution accepts one request per cycle when completion credit is
available, with a two-cycle nonstallable result delay followed by reserved
buffering. This preserves the existing combinational datapath and does not
claim a physically balanced two-stage arithmetic implementation. Divide/sqrt
has independent buffered format lanes.
Completion arbitration is round-robin, including between format lanes, so
continuous fixed work cannot starve a ready divide/sqrt result. Results may
complete out of request order; callers must route them by their retained tags.
Backpressure is lossless, but a stalled output can eventually fill the shared
buffers and stop other callers. Progress requires downstream consumers to drain.

The service has no architectural cancellation input: accepted work survives
younger redirects. Synchronous reset discards pending work and results.
Memory operations remain outside its contract. Compose multiple authorized
callers with ordinary Flow request arbitration and tag-based result routing;
the service itself owns no client count or client-specific scheduling policy.

## Scalar architectural wrapper

The enabled and disabled implementations expose the same integration shape.
The enabled pipeline accepts non-speculative compute work through a
`Decoupled` issue input and retains results through an `Irrevocable` completion
output. It also provides a `Decoupled` load-reservation input, a `Valid` load
completion, a separate WB `Valid` load-hit input, one-cycle `Valid` store request/response pulses, a `Valid`
architectural-state update, the FPR busy mask, and a drained indicator. The
disabled implementation rejects FP work and reports itself drained.

`RV5StageFpPipeline` wraps one execution service. It owns the scalar FPR bank,
scoreboard, load/store bridges, and architectural completion/state updates.
Scalar context, destination kind, and register number travel through execution
only as an opaque wrapper-owned tag. Its external issue/completion interfaces
are unchanged; vector dispatch is not yet connected to this service.

Drained describes accepted execution and load reservations, not speculative
store-operand probes. Those read-only probes may repeat while Decode waits;
they do not delay an architectural trap or interrupt.

Compute requests are authorized at scalar WB; rejected attempts replay without
retirement or reservation. Accepted requests must eventually complete after
their scalar tokens retire. The subsystem retains ownership of an FP destination
until its fixed-latency, division/square-root, or load result completes. FP
load misses reserve their destination when the transaction is accepted at WB.
Load hits instead write and NaN-box directly through `load_hit` at scalar WB,
without a deferred reservation. The caller must ensure this write has a free
destination and cannot coincide with a deferred load completion. FP stores
launch a register-file read from Decode and return a one-cycle response aligned
with the store in EX. If that response is absent or belongs to another token,
the scalar pipeline replays the store instead of holding EX.

The scalar pipeline owns dispatch and memory requests; the FP subsystem owns
FPR hazards and execution after acceptance. FP loads and stores share the
ordinary scalar address, translation, PMA, cache, and uncached paths while
carrying exact precision metadata. See [`DEVELOPING.md`](DEVELOPING.md) for
source ownership and contributor validation.
