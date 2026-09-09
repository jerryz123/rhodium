<!-- Guides contributors through implementing and validating RV5Stage translation. -->

# Developing the RV5Stage MMU

Read the MMU [README](README.md) for request flow, TLB and walker contracts,
fault ownership, Sv39 behavior, and deliberate limits. This guide owns source
placement, change workflow, and focused validation.

## Architecture and ownership

The MMU sits between virtual core requests and the physical memory hierarchy.
It owns TLB lookup/refill, serialized walking, fault correlation, fetch-result
ordering, and temporary ownership of the shared physical data port. The parent
core owns CSR sequencing, trap priority, alignment, PMA routing, cache behavior,
and final exception causes.

Reuse the public RISC-V Sv39 adapter for PTE layout, canonicality, permissions,
superpages, and physical-address construction. Keep translation state and
RV5Stage arbitration here rather than moving them into the pure RISC-V model or
the caches.

The shared data-port adapter uses gated, payload-mapped request flows and a
stateless arbiter. Ownership gates keep its sources mutually exclusive;
arbiter priority does not replace the drain or response-owner state. Response
branches use `Valid` filtering and mapping, and the walker asserts that a
routed response arrives while it is waiting.

## Implementation map

| File | Ownership |
|---|---|
| [`protocol.rhdl`](protocol.rhdl) | Translation request/result bundles, fetch-fault metadata, and walker memory interface |
| [`tlb.rhdl`](tlb.rhdl) | Fully associative demand/probe matching, permission recheck, physical-address construction, refill, and invalidation |
| [`walker.rhdl`](walker.rhdl) | Serialized three-level PTE fetch, structural and permission checks, cancellation, and completion |
| [`mmu.rhdl`](mmu.rhdl) | ITLB/DTLB composition, miss priority, fault correlation, fetch ordering, registered virtual/physical prefetch stages and cancellation, physical checks, and shared data-port ownership |
| [`../rv5stage.rhdl`](../rv5stage.rhdl) | Core, L1I, physical-router, and privileged-control integration |
| [`../../../riscv/rtl/sv39.rhdl`](../../../riscv/rtl/sv39.rhdl) | Shared Sv39 decoding, canonicality, permission, superpage, and address helpers |
| [`../tests/mmu-test.rhm`](../tests/mmu-test.rhm) | Public translation types, widths, and composition boundary |
| [`../../../tests/backend/verilog/rv5stage-mmu-replay_tb.sv`](../../../tests/backend/verilog/rv5stage-mmu-replay_tb.sv) | Cycle-level pulsed DTLB miss, three-level walk, translated replay, and prefetch latency, throughput, rejection, and cancellation |

## Change translation behavior

1. Decide whether the change is reusable Sv39 representation/policy or
   RV5Stage state and arbitration. Put only the former in `riscv/rtl`.
2. Preserve address correlation for walk completions and faults. The instruction
   request is retained in the MMU's registered queue after S0 admission; the
   core may change its payload immediately after acceptance. A blocked S1
   locally reissues its virtual read and must never duplicate a physical fire.
3. Keep page faults distinct from physical PTE access faults and suppress every
   rejected physical resolution before it reaches a cache or device. Early
   virtual SRAM reads are permitted, but cannot create a successful cache token
   without the paired physical request. Wire lookup paths directly in the parent
   composition so translation/PMA cannot feed their indices or validity.
   The ordinary `pipeline` path registers EX load/store context before sharing
   the demand DTLB in MEM. WB requests win contention. Distinguish contention
   replay from miss/uncached slow service and precise faults. Do not gate this
   path on `data_memory.drained`: buffered stores are resolved by physical-byte
   checks in L1D. `ordered_busy` separately blocks younger work behind IO.
   Forward WB authorization and readiness unchanged; WB owns squash and
   serialization. Do not reconnect drain-derived flush to store authorization.
   Never use relaxed prefetch A/D permissions or start a speculative data walk.
4. Preserve exclusive walker ownership from miss acceptance through completion,
   including the two-observation data-path drain and the single response-owner
   bit.
5. Recheck current privilege, `SUM`, `MXR`, `A`, and `D` on every TLB hit; do
   not cache a prior permission decision.
6. Keep prefetch probes non-faulting and independent of walker ownership; they
   may use Bare translation or an existing TLB entry but must not check A/D.
   Keep address, operation, and validity registered on both sides of the probe;
   cancellation is synchronous so demand squash cannot reach cache admission
   through a combinational prefetch-valid gate.
7. Keep host checks to public translation contracts. Test walk, cancellation,
   fault, and invalidation behavior in compiled simulations, then update
   [README.md](README.md) for observable changes.

## Focused validation

Run the MMU-owned host check from the repository root:

```sh
tools/run-racket-tests.sh cores/rv5stage/tests/mmu-test.rhm
FIXTURE=rv5stage-mmu-replay bash tests/backend/run-circt.sh --simulate-only
```

The wrapper creates a fresh compiled root when one is not supplied. Keep this
test limited to public translation contracts; do not add internal operation or
state snapshots. The Verilator fixture pulses one data request, checks the three
expected PTE addresses, and requires a later retry to use the filled DTLB while
preserving request metadata. It also checks two-observation draining, stalled
walker and core requests, and isolation of PTE responses from ordinary replies.
It checks prefetch latency and back-to-back
throughput, TLB selection and rejection, Bare/PMA behavior, and synchronous
cancellation at either stage on flush, invalidation, context change, and reset.
Use the parent
[`DEVELOPING.md`](../DEVELOPING.md#focused-validation) when changes span CSR
sequencing, the pipeline, physical routing, or caches.
