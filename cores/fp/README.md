<!-- Defines shared FP control mappings, operand execution, and register-file contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Shared floating-point components

`cores/fp/` provides reusable FP instruction/control mappings, a three-read,
two-write register file, and tagged operand execution built on HardFloat.
It does not own a core's issue policy, register hazards, retirement, or LSU.
Contributors should read [DEVELOPING.md](DEVELOPING.md).

## Get started

Import only the components needed by the caller:

```rhdl
import:
  lib("flow/main.rhdl") open
  lib("cores/fp/bundles.rhdl") open
  lib("cores/fp/execute.rhdl") open
  lib("cores/fp/types.rhdl") open
  lib("riscv/isa/fp-profile.rhm") open
  lib("riscv/isa/xlen.rhm") open

sync_circuit FpClient():
  def Request = FpExecutionRequest(XLen.X64, FloatingPointProfile.D, Bits(8))
  def Result = FpExecutionResult(XLen.X64, FloatingPointProfile.D, Bits(8))
  interface request(Decoupled(Request), ~role: consumer)
  interface result(Irrevocable(Result), ~role: producer)
  inst execution(FpExecutionService(XLen.X64, FloatingPointProfile.D, Bits(8)))
  request |> execution.request
  execution.result |> result
```

See the repository [quick start](../../README.md) for setup.

## Operand execution

[`FpExecutionService`](execute.rhdl) accepts explicit FP operands, an integer
operand, `FloatingPointControl`, an immediate index, a resolved rounding mode,
and a caller-selected `Tag` type. A `Decoupled` transfer authorizes execution.
The default `Irrevocable` result retains its unchanged tag, integer and FP
result lanes, exception flags, and flag-update enable until consumed. Only the
selected operation's result lane is meaningful.

The register-use and destination fields select numeric conversion direction;
they never name, read, reserve, or write a register. Per-operand precision tags
allow exact promotion of narrow vector operands alongside wide operands or
fused addends. FP values use FLEN-wide RISC-V NaN boxing. Execution preserves
canonical-NaN, rounding, and exception-flag semantics. Dynamic rounding-mode
resolution and accrued architectural flags belong to the caller.

Guaranteed transfer structure:

```text
decoded operands + opaque tag
            |
     fixed/variable dispatch
       /              \
 fixed arithmetic   divide/sqrt (retained terminal result)
 selected fixed delay    |
       \                /
         completion arbitration -> tagged result
```

[`FpExecutionTiming`](timing.rhdl) supplies the execution and scheduling contract:
single/half arithmetic, double arithmetic, and other fixed operations have
independently configurable delays. Defaults preserve the current two-cycle
return. Each lane has initiation interval one. These are nonstallable delays around combinational arithmetic,
not a claim of balanced arithmetic stages. Launch arbitration prevents two
different-latency operations from claiming the same return cycle.
The default service reserves `timing.maximum + 2` fixed-response slots and fairly merges
fixed and variable completions into its held result. With
`~scheduled_writeback: #true`, fixed results have no completion queue and must
be consumed on their reserved return cycle. The caller must reserve writeback
capacity before accepting the request. The scheduled output is `Decoupled`:
a fixed return can preempt an unaccepted variable offer. Aging pauses new
fixed launches so a blocked variable result can drain. Its
`fixed_admission_available` output allows a scheduled caller to reserve the
following cycle's launch without crossing that aging pause. The caller must
also reserve service ownership and the return cycle; this output alone is not
an admission grant.

Results may reorder across fixed latencies and variable paths. Use
[`fp_request_with_tag` and `fp_result_with_tag`](bundles.rhdl) with ordinary
Flow arbitration and tag-based routing to share the service. Tags remain
opaque; the service has no client count, destination scoreboard, or
architectural cancellation. Accepted work survives younger redirects.
Synchronous reset discards pending work and results. Memory operations are
not execution-service requests.

## Register file and decode

[`FloatingPointRegisterFile(profile)`](register-file.rhdl) stores 32 unreset
FPRs, exposes three combinational reads and two `Valid` writes, and forwards
same-cycle writes to reads. Write port 1 wins over port 0 if both target the
same register, matching stored-state priority. The caller owns write-port
allocation, hazards, and architectural initialization.

[`fp_load_value` and `fp_store_value`](load-store.rhdl) NaN-box raw load bits
and shape low-precision store bits. They own no memory request, reservation,
permission, or retirement behavior.

[`types.rhdl`](types.rhdl) defines precision, operation, register-use, and
numeric controls without instruction tables. [`decode.rhdl`](decode.rhdl)
maps canonical F/D/Zfhmin/Zfh/Zfa catalogs onto those types. Its partial
relations retain don't-care fields; callers compose the case lists into their
own complete decoder rather than instantiate parallel decoders.
`FloatingPointInstructionDecoder` is available for standalone users.

## Limits and integration

The extracted specialization contract remains disabled FP, RV32F, or RV64D,
with optional Zfhmin, Zfh, and Zfa. `profile.rhm` checks those component
capabilities without importing named-core configuration. This extraction does
not add RV32D or RV64F service specializations.

Architectural instruction descriptions and stateless FP helpers remain in
[`riscv/`](../../riscv/README.md); numeric primitives remain in
[`hardfloat/`](../../hardfloat/README.md). RV5Stage's
[scalar wrapper](../rv5stage/fp/README.md) owns its FPR scoreboard, scalar/vector
reservations, LSU adaptation, and retirement policy. No RV2Wide integration
is implied by these shared components.
