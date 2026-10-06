<!-- Describes the shared CSR/trap service and its caller-owned authorization boundary. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Shared RISC-V CSR and trap state

`RiscvCsrFile` supplies M/S/U CSR state, privilege transitions, traps,
interrupts, counters, and optional FP, vector, and guest banks. It has no
named-core configuration or pipeline payload dependency. See
[DEVELOPING.md](DEVELOPING.md) for implementation and test ownership.

Import `config.rhm`, `protocol.rhdl`, and `file.rhdl`. Construct
`RiscvCsrConfig(isa_profile, ...)` from architectural feature choices, then
instantiate `RiscvCsrFile(config)` in the caller's clock/reset domain.
The supplied `RiscvIsaProfile` determines XLEN and MISA; optional bank choices
are explicit. The caller must keep those choices consistent with its ISA.

## Authorization contract

```text
core's precise boundary -> Valid(RiscvCsrCommand) -> CSR/trap bank
core's retirement count (0, 1, or 2) ------------> counters
core's interrupt boundary + next PC ------------> interrupt entry
                         <- result, redirect, live architectural state
```

- `commit` is a one-cycle, irrevocably authorized command, not a speculative
  stage or a ready/valid request. It carries CSR intent, an architectural
  action, resolved operands, raw instruction, destination, and precise fault
  context. Raw instruction bits preserve write intent and trap values.
- `command_success` reports a valid command without a synchronous exception.
  `writeback_valid/value` report legal CSR/configuration results for nonzero
  `rd`. `wfi` reports an authorized, legal WFI; sleeping remains caller policy.
  `wfi_wake` exposes locally enabled pending interrupts.
- `retire_count: Bits(2)` independently counts zero, one, or two architectural
  retirements in `minstret` and the optional retirement HPM event. Ordinary instructions need not send
  commands. Command success does not itself increment counters. Counter CSR
  writes retain priority over increments.
- The caller asserts `interrupt_boundary` with precise `interrupt_pc` only
  when entry is safe. Synchronous exceptions outrank interrupts. Do not
  authorize a successful state-changing command on an interrupt-taking
  boundary: the core must order completion and boundary interrupt entry.
- Redirects, translation-flush notifications, pointer-mask changes, permissions,
  and live translation context are outputs. Draining older work, squashing
  younger work, cache/TLB invalidation, and resumption remain core policy.
  `trap_event: RiscvTrapDecision` exposes the selected synchronous or interrupt
  cause, value, and destination privilege on the trap-entry edge.
- FP/vector completion inputs update flags/status and vector progress. Drain
  outstanding updates before changing the host/guest context owning them.

## Scope and navigation

The bank preserves the existing RV32 Bare and RV64 Bare/Sv39 behavior,
including optional RV64 hypervisor state. Trap vectors are direct. Retirement
accounting accepts at most two instructions per cycle from one execution context.
The integrating core must serialize privilege transitions. There is no instruction decoder,
page walker, cache engine, or issue/retirement queue here.

CSR IDs, masks, WARL helpers, and architectural semantics remain in
[`riscv/`](../../../riscv/README.md). The RV5Stage
[adapter](../../rv5stage/csr.rhdl) projects configuration and decode controls;
the core retains WB authorization and retirement policy.
