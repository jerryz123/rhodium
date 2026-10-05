<!-- Describes passive architectural hooks, ordered collection, and the embedded Sail reference. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Architectural co-simulation

This package provides a model-independent architectural event collector and an
in-process Sail reference hart with private memory. The scalar comparison runtime
connects them to Mini/Simple RV5Stage simulators through compile-target instrumentation.
See the parent
[contributor guide](../DEVELOPING.md#embedded-sail-reference) for maintenance and
the [source documentation requirements](../../AGENTS.md#source-documentation),
including the `tests/` exemption.

## Get started

The hook collector needs only CMake and a C++20 compiler:

```sh
make -C sims cosim-hooks-test
```

Use the repository's [simulation prerequisites](../README.md), plus CMake,
GMP development headers, and the pinned Sail 0.20.3 compiler. On supported
Linux hosts the installer downloads the compiler; elsewhere set
`SAIL_COMPILER` to its executable.

```sh
make -C sims setup arch-test-sail-setup
make -C sims sail-cosim-test
```

These targets do not require a SoC or ISA selection. They validate the library
boundary, not any product configuration. The tests use explicit RV32 and RV64
Sail configurations and execute scalar programs.

Build and run the first end-to-end scalar workload through the normal FESVR loader:

```sh
make -C sims cosim-smoke SOC=mini-rv5stage-rv64imacb
make -C sims cosim-smoke SOC=simple-rv5stage-rv64imacb
make -C sims run SOC=mini-rv5stage-rv64imacb COSIM=1 BINARY=/absolute/path/to/program.elf
```

`COSIM=1` selects the observation pass and native checker, not an HDL-generator
parameter. Builds live in separate `-cosim` directories; Simple can also select
`TRACE=1` with its required `TRACE_FILE`, producing a `-trace-cosim` build.
Ordinary simulators contain neither the observer nor the Sail runtime.
The executable embeds its exact product configuration, boot ROM/DTB, and hart
descriptor, so running it does not require the build directory's JSON files.

### Scalar execution boundary

The current simulator runtime accepts only `mini-rv5stage-rv64imacb` and
`simple-rv5stage-rv64imacb`. This is a bounded bring-up workload, **not full
validation of the selected ISA**. The real profile is preserved; no scalar
substitute configuration or second ELF loader is used.

```text
actual ROM + successful FESVR writes -> private Sail memory
RTL hooks -> ordered collector -> scalar checker -> mismatch / checked record
                             device-read bytes -> Sail device input
```

The checker compares PC, instruction bits/length, privilege transitions, integer
writeback, scalar memory bytes, synchronous traps, and observed masked CSR deltas.
Private RAM loads are independently computed. Only device-read bytes are replayed,
before Sail's load semantics; Sail never repeats writes to real devices.
FESVR mirrors successful image loading, clearing, and later host writes. Host writes
are applied before records at or after their completion sample. This ordering does
not yet validate races between host writes and in-flight hart accesses.

Each evaluated rising edge is bracketed by host sample barriers. Checking runs
after all callbacks settle, and termination rejects incomplete collected records.
These barriers use the driver's feature-independent native runtime; the runtime
owns cosim configuration and counters, alongside optional tracing.
The driver supports its initial reset, not a later warm reset. A mismatch fails the
simulation and identifies the architectural order and PC. `cosim-smoke` also runs
a deliberate GPR-corruption probe to verify that a bad observation is rejected.

Interrupt delivery, translated or fragmented accesses, atomics, cache operations,
FP/vector/H, and complete CSR/counter-state comparison remain outside this runtime's
validated scope. Unsupported event kinds fail rather than being skipped. Do not
enable general software-suite CI with this runtime yet.

## Hook and collector contract

### Compile-target instrumentation

Select architectural observation independently of the HDL generator:

```rhombus
import:
  lib("rhodium/compile/program.rhm").compile_program
  lib("rhodium/compile/pipeline.rhm").rtl_pipeline_target
  lib("rhodium/backend/circt-target.rhm").circt_target
  lib("sims/cosim/pass.rhm").cosim_pass

def compiled = compile_program(program, rtl_pipeline_target(circt_target, [cosim_pass()]))
```

The direct `verilog_target` is also supported. Add `event_trace_pass()` to the
same list for Flow DPI tracing; both pass orders preserve annotations and prior
observers. `cosim_pass(~harts: [["core"]])` selects exact declared hart paths
relative to the compilation top. The default selects all declared harts.
Missing selections and unsupported selected profiles fail compilation, never
silently produce incomplete checking.

The pass preserves functional top ports and existing instance paths. It returns
`cosim.json` with deterministic occurrence IDs and paths alongside the RTL.
Only selected harts get observer state/DPI. Ordinary functional DPI is retained
in every mode. Source programs remain reusable across compilation variants.

The host binds descriptor IDs to architectural harts using `Collector::reset`
before the first reset edge and advances the epoch before each later reset.
The generated observer samples `rhodium_cosim_epoch` while reset is asserted;
the collector remains the authority for epoch generation. No epoch/instance
ports are added to the functional design. At least one reset edge is required
before execution. Sample barriers and environment snapshots remain host-owned.

This pass emits observations; it does not enable Sail comparison or change
SoC/ISA selection. The current RV5Stage recipe supports scalar RV32/RV64;
FP/vector and hypervisor observation are explicitly rejected when selected.

### Producer and receiver

The producer API is [`CosimHart`](../../cores/riscv/cosim.rhdl), imported with
`lib("cores/riscv/cosim.rhdl")`. Construct it with an explicit `Clock` (or
`#false` for the ambient `sync_circuit` clock), a 64-bit simulation-instance ID,
and an optional host Boolean `enabled` (default true). Callers suppress reset events.
`enabled = #false` specializes away every callback. It has no ready signal, result register,
or influence on execution. Gate each method with the actual semantic event's
`Bool`, not stage occupancy. Each takes a `CosimInstructionId` containing
64-bit `epoch` and `order` fields, followed by its typed payload:

| Method | Meaning |
|---|---|
| `instruction(valid, id, value)` | Allocate an architectural position; carries PC, encoding, privilege, and producer bitmap |
| `retire(valid, id, value)` | Successful architectural retirement, with next PC and privilege |
| `exception(valid, id, value)` | Synchronous exception, with cause, EPC, TVAL, target, and optional guest information |
| `interrupt(valid, id, value, producers)` | Actual interrupt entry at a separate ordered position, not an instruction |
| `reg_write(valid, id, effect_id, value)` | Owned integer/FP/vector writeback value, not a later register snapshot |
| `csr_update(valid, id, effect_id, value)` | This instruction's masked assignment, set-bits, or clear-bits contribution |
| `memory(valid, id, effect_id, value)` | Architectural memory access fragment, not a refill or speculative attempt |
| `seal(valid, id, producer, count)` | Exact final effect count from one producer |

`CosimEffectId` contains a six-bit producer and 64-bit index. The instruction
or interrupt's 64-bit bitmap declares its producers. Each emits indices
`0..count-1` exactly once and seals exactly once, including zero-effect producers.
The seal can arrive before effect callbacks. Producer identities are local to
an instruction and mean nothing to the collector; they need not name stages.
Trap `cause` carries the numeric cause code; the interrupt event variant, not
an XLEN-dependent high bit in that code, identifies an interrupt.

Orders start at zero after reset and include interrupts; no gaps or wrap are
allowed. Assign them at nonspeculative acceptance in architectural order, and
retain them through replay and delayed execution. Speculative/squashed attempts
emit nothing. Each instruction gets exactly one retirement or exception outcome.
Retirement can precede delayed effects. A vector exception can retain partial
register and memory effects. A fetch exception may carry an incomplete encoding
and zero instruction length; successful retirement requires the full 2- or
4-byte encoding. Wider instruction encodings are not supported in this ABI.

Register writes use at most 64 bits with a bit mask and an architectural bit
offset. The mask and value are relative to that offset, independent of physical
VRF layout. Integer x0 writes are omitted; FP values use architectural encoding,
including applicable NaN boxing. RV32 writes mask only their architectural bits.
CSR `SetBits`/`ClearBits` affect `value & mask`; `AssignMasked` replaces the
masked bits. Preserve contributions such as fflags/vxsat instead of observing a
whole CSR containing younger instructions' contributions. Effects within a
producer are ordered by index. Distinct producers must have disjoint effects
or commuting updates; producer number does not establish architectural priority.
The collector retains contributions rather than interpreting ISA semantics.

Memory effects carry access ID, fragment offset, kind, virtual address, optional
physical address, byte mask, optional read/write data, and success/fault/SC-failure
result. Each fragment is at most eight bytes, with mask/data relative to its
reported address. Vector accesses have distinct access IDs; split portions of
one access share an ID. Record a store's architectural acceptance, not a later
cache writeback. No cross-hart memory order or coherence checker is implied.

[`observation.h`](observation.h) is the independent C++ API. Its guaranteed flow is:

```text
header + outcome + effects + producer seals (arbitrary callback order)
  -> per-hart pending records
  -> end-of-sample validation
  -> contiguous complete architectural records
```

The driver registers/reset harts with `Collector::reset(instance, epoch, state)`.
Architectural hart ID is separate from simulation-instance ID. Reset explicitly
abandons pending records, advances the epoch, and resets order to zero. Late
old-epoch callbacks are errors, never reassigned to a reused execution slot.
Reset state describes XLEN/VLEN, initial PC and privilege; it is not a register
write event or a complete initialization image for the reference model.

Before an evaluated edge, set per-hart `environment` (interrupt-input levels,
time and ticks), then `begin_sample`. After evaluation has settled, call
`DpiBinding::check()` and `Collector::end_sample()`. Never implement this barrier
as another unordered posedge callback. Headers capture the environment at their
architectural boundary, not at late completion. Time/input meaning is owned by
the driver and future reference integration; interrupt input levels are distinct
from observed interrupt entry. `end_sample` returns records in order **per
hart**, not a global multi-hart execution order. Effects/outcomes may precede
their header within a sample, but not across samples.

`finish()` rejects an open sample, missing effects/seals/outcomes, and order gaps.
Published IDs cannot receive more callbacks. Pending order distance and effects
per record have configurable bounds (defaults 4096 and 65536); exceeding them
fails instead of dropping observations. Protocol failures poison the collector.
The DPI binding catches errors instead of unwinding C++ exceptions through SV;
the driver must check its sticky error after every evaluated edge and before
finishing. One binding owns all registered harts on one simulation thread.

## Sail adapter contract

[`sail-reference.h`](sail-reference.h) is the generated-type-free API.
Construct `SailReference` with complete Sail configuration JSON, a reset PC,
and physical ranges backed by private ROM/RAM. Initialization validates the
supplied configuration; it does not silently substitute an ISA profile. The
caller must disable Sail's built-in CLINT and simple interrupt generator.
The [shared projection](../sail/README.md) consumes the resolved product's UDB;
ACT and co-simulation use identical hart settings with separate environments.

Generate and initialize a real product configuration with:

```sh
make -C sims sail-cosim-config-test SOC=mini-rv5stage-rv32int
make -C sims sail-cosim-config-test SOC=simple-rv5stage-rva23
```

`sail-cosim-config` generates artifacts without building the adapter. Both targets
require an explicit ISA, accept Mini/Simple with RV5Stage or Spike, and use the
same product resolver as the simulator. `COSIM_CONFIG_DIR` defaults to the
selected simulator build directory's `cosim/`; `COSIM_PYTHON` defaults to the
existing ACT Python environment (pyjson5 and ruamel.yaml).

The export retains full product metadata, UDB, configuration fingerprint, and
the actual PMA map, reset address, ROM/RAM backing, and clock/timebase frequencies.
`sail.json` contains the reference configuration; `manifest.json` binds its
fingerprint and environment to that product and records known model differences.
The initialization test supplies a tiny ROM probe, not the real firmware image.
It also checks the exact eight-byte UART aperture: an in-range read is replayed,
and its immediate unmapped neighbor faults. It validates configuration and
environment boundaries, not complete execution of that profile.

Sail's synthetic CLINT and interrupt generator are disabled. Device regions,
including the boot-address register, remain externally replayed; only ROM and
RAM are privately backed. No synthetic ACT fault window or reference-generated
DTB is substituted. Time and device values must eventually come from the DUT
environment, not a second device model.

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
creates HTIF nor reads an ELF on its own. The simulator runtime mirrors successful
loading and host writes into private reference memory as described above.

## Current limits

- One live instance on one host thread, because the pinned Sail runtime and
  configuration are process-global. Destroying and reconstructing resets it.
- Adapter-library validation (distinct from the narrower simulator runtime):
  RV32/RV64 scalar arithmetic, branches, loads/stores, synchronous
  traps, machine external interrupts, host time, WFI, strict MMIO replay, and coexistence
  with the existing FESVR transport.
- The callback API carries FP/vector writes, but FP/vector, atomics/reservation
  behavior, virtualized privilege, full-profile execution, and supervisor
  interrupt delivery are not validated by this first cut.
- Typed RTL hooks and the deferred-effect collector also have an optional
  [RV5Stage scalar producer](../../cores/rv5stage/README.md#optional-scalar-architectural-observation).
  The scalar comparison runtime is opt-in; software-suite co-simulation is not
  enabled in CI. Timer/software interrupt inputs into Sail and
  nondeterministic CSR replay remain future environment work.

The pinned model and optional host hooks live in the existing
[Sail patch series](../../riscv/sail-riscv-patches/series), not in a dirty
submodule. The same package still supplies the standalone ACT executable.
