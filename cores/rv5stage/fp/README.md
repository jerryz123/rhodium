<!-- Defines the public behavior and integration contracts of RV5Stage floating point. -->

# RV5Stage floating point

RV5Stage's optional floating-point subsystem provides architectural FP
register state, fixed-latency arithmetic, buffered division and square root,
load/store integration, destination tracking, and completion arbitration. The
core supports RV32F or RV64D, with optional Zfhmin, Zfh, and Zfa behavior.

The enabled and disabled implementations expose the same integration shape.
The enabled pipeline accepts non-speculative compute work through a
`Decoupled` issue input and retains results through an `Irrevocable` completion
output. It also provides a `Decoupled` load-reservation input, a `Valid` load
completion, one-cycle `Valid` store request/response pulses, a `Valid`
architectural-state update, the FPR busy mask, and a drained indicator. The
disabled implementation rejects FP work and reports itself drained.

Drained describes accepted execution and load reservations, not speculative
store-operand probes. Those read-only probes may repeat while Decode waits;
they do not delay an architectural trap or interrupt.

Compute requests are authorized at scalar WB; rejected attempts replay without
retirement or reservation. Accepted requests must eventually complete after
their scalar tokens retire. The subsystem retains ownership of an FP destination
until its fixed-latency, division/square-root, or load result completes. FP
loads reserve their destination when the memory request is accepted at WB. FP stores
launch a register-file read from Decode and return a one-cycle response aligned
with the store in EX. If that response is absent or belongs to another token,
the scalar pipeline replays the store instead of holding EX.

The scalar pipeline owns dispatch and memory requests; the FP subsystem owns
FPR hazards and execution after acceptance. FP loads and stores share the
ordinary scalar address, translation, PMA, cache, and uncached paths while
carrying exact precision metadata. See [`DEVELOPING.md`](DEVELOPING.md) for
source ownership and contributor validation.
