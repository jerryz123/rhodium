<!-- Describes the embedded Sail reference boundary and its currently qualified scope. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Embedded Sail reference

This is the first building block for passive architectural co-simulation: an
in-process reference hart with private memory. It is not yet connected to RTL
retirement or to Mini/Simple harnesses. See the parent
[contributor guide](../DEVELOPING.md#embedded-sail-reference) for maintenance.

## Get started

Use the repository's [simulation prerequisites](../README.md), plus CMake,
GMP development headers, and the pinned Sail 0.20.2 compiler. On supported
Linux hosts the installer downloads the compiler; elsewhere set
`SAIL_COMPILER` to its executable.

```sh
make -C sims setup arch-test-sail-setup
make -C sims sail-cosim-test
```

These targets do not require a SoC or ISA selection. They qualify the library
boundary, not any product configuration. The tests use explicit RV32 and RV64
Sail configurations and execute scalar programs.

## Adapter contract

[`sail-reference.h`](sail-reference.h) is the generated-type-free API.
Construct `SailReference` with complete Sail configuration JSON, a reset PC,
and physical ranges backed by private ROM/RAM. Initialization validates the
supplied configuration; it does not silently substitute an ISA profile. The
caller must disable Sail's built-in CLINT and simple interrupt generator.
Shared product-to-Sail configuration projection is a later integration step.

The backing map is independent of PMA classification: ROM can be `IOMemory`
without being a replayed device. `load()` initializes backing memory, including
read-only ROM. Uninitialized backing bytes read as zero. Architectural writes
still obey Sail's PMA/PMP and translation checks.

`step()` executes exactly one instruction attempt or pending-interrupt
transition. It reports the original/next PC, privilege, fetched instruction,
retirement or waiting status, trap, register-write callbacks, and physical
memory transfers. A trap does not also execute the first handler instruction.
Fetches and page-table transfers are marked/reported at the physical boundary;
these are not an already-normalized DUT retirement record. CSR callbacks are
not a complete CSR-state delta, and repeated writes are retained in order.

`StepInputs` supplies machine/supervisor external interrupt levels, explicit
cycle ticks, architectural time, wait release, and an ordered list of raw device-read bytes. Device
reads are injected before load sign extension and other instruction semantics.
Device writes are reported but never sent to a second device implementation.
Missing, mismatched, or unused read replay fails the step; a failed step poisons
the instance because architectural state may already have changed.

FESVR remains the sole loader/host-service implementation. The adapter neither
creates HTIF nor reads an ELF on its own. A future harness must mirror loading
and host writes into private reference memory at defined boundaries.

## Current limits

- One live instance on one host thread, because the pinned Sail runtime and
  configuration are process-global. Destroying and reconstructing resets it.
- Qualified: RV32/RV64 scalar arithmetic, branches, loads/stores, synchronous
  traps, machine external interrupts, host time, WFI, strict MMIO replay, and coexistence
  with the existing FESVR transport.
- The callback API carries FP/vector writes, but FP/vector, atomics/reservation
  behavior, virtualized privilege, full product profiles, and supervisor
  interrupt delivery are not qualified by this first cut.
- No RTL observations, deferred-effect assembler, comparison engine, or CI
  software-suite co-simulation is enabled. Timer/software interrupt inputs and
  nondeterministic CSR replay remain future environment work.

The pinned model and optional host hooks live in the existing
[Sail patch series](../../riscv/sail-riscv-patches/series), not in a dirty
submodule. The same package still supplies the standalone ACT executable.
