<!-- Defines the public behavior and integration contracts of RV5Stage floating point. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RV5Stage floating point

RV5Stage's optional floating-point subsystem provides architectural FP
register state, fixed-latency arithmetic, retained division and square root,
load/store integration, destination tracking, and completion arbitration. The
core supports RV32F or RV64D, with optional Zfhmin, Zfh, and Zfa behavior.
Contributors should read [DEVELOPING.md](DEVELOPING.md) for implementation
ownership and validation.

## Shared components

Numeric execution, canonical instruction mappings, and register storage live
in [`cores/fp/`](../../fp/README.md), independently of any named core.
RV5Stage uses `FpExecutionService` in scheduled mode with reserved writeback
cycles and scalar/vector owner tags. Its scalar wrapper retains architectural
reservations and completion policy. The [shared service contract](../../fp/README.md#operand-execution)
defines timing, reordering, backpressure, rounding, and reset behavior.

## Scalar architectural wrapper

The enabled and disabled implementations expose the same architectural ports.
The enabled pipeline accepts non-speculative compute work through a
`Decoupled` issue input and forwards results through a `Decoupled` completion
output. It also provides a `Decoupled` load-reservation input, a `Valid` load
completion, a separate WB `Valid` load-hit input, one-cycle `Valid` store request/response pulses, a `Valid`
architectural-state update, the FPR busy mask, and a drained indicator. The
disabled implementation rejects FP work and reports itself drained.

The common interface also exposes a `Decoupled` vector-destination reservation
and a `Valid` vector write. A reservation marks one FPR busy before a vector
macro launches and excludes concurrent scalar FP work until the vector result
is written at authorized WB. The write clears the reservation and marks FS
dirty. It is an architectural raw-bit transfer rather than an arithmetic-service
completion, and it cannot feed its own WB disposition through a combinational
ready path.

`RV5StageFpScalar` owns the scalar FPR bank, scoreboard, load/store bridges,
and architectural completion/state updates, exposing operand execution ports.
`RV5StageFpPipeline` is its standalone composition with one execution service.
Scalar context, destination kind, and register number travel through execution
only as an opaque wrapper-owned tag. Its external issue/completion interfaces
are unchanged. The core instead connects the scalar adapter and the
[vector caller](../vector/README.md#shared-floating-point) to
one service with ordinary Flow arbitration and owner-tag routing. Scalar/vector
movement bypasses that service and uses the reservation/write pair above.

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
carrying exact precision metadata.
