<!-- Describes passive architectural hooks, ordered collection, and the embedded Sail reference. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Architectural co-simulation

This package provides a model-independent architectural event collector and an
in-process Sail reference hart with private memory. The comparison runtime
connects them to Mini/Simple RV5Stage simulators through compile-target instrumentation.
See [DEVELOPING.md](DEVELOPING.md) for ownership and maintenance.

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
boundary, not any configuration. The tests use explicit RV32 and RV64
Sail configurations and retain compact scalar/vector checker regressions.

Build and run the first end-to-end scalar workload through the normal FESVR loader:

```sh
make -C sims cosim-smoke SOC=mini-rv5stage-rv64imacb
make -C sims cosim-smoke SOC=simple-rv5stage-rv64imacb
make -C sims isa-smoke SOC=mini-rv5stage-rv64imacb COSIM=1 PROGRAM_BUILD_ROOT=/tmp/rhodium-cosim-programs
make -C sims isa-smoke SOC=simple-rv5stage-rv64imacb COSIM=1 PROGRAM_BUILD_ROOT=/tmp/rhodium-cosim-programs
make -C sims run SOC=mini-rv5stage-rv64imacb COSIM=1 BINARY=/absolute/path/to/program.elf
```

`COSIM=1` selects the observation pass and native checker, not an HDL-generator
parameter. Builds live in separate `-cosim` directories; Simple can also select
`TRACE=1` with its required `TRACE_FILE`, producing a `-trace-cosim` build.
Ordinary simulators contain neither the observer nor the Sail runtime.
The executable embeds its exact config, boot ROM/DTB, and hart
descriptor, so running it does not require the build directory's JSON files.

### Capture and ordered ownership

RV5Stage observation has a stateless RTL capture layer and a native hart adapter:
core-declared taps → simulation-owned DPI capture → RV5Stage C++ normalization → generic ordered
collector → Sail checker. The hart adapter assigns one instruction identity to
scalar, FP, and vector events. It retains delayed owners, split requests,
translated addresses, FP flag contributions, and vector effect counts in host memory.
Capture adds no bookkeeping registers or queues and never drives the core.
All callbacks in a cycle are resolved together; their invocation order does not
determine ownership. Host reset epochs discard that hart's old observation state.

### Simulator checking

Cosim can be selected for every Mini/Simple RV5Stage ISA profile, including
`rv64max` and `rva23`. Integer and FP effects retain their architectural
widths; vector collection uses the selected VLEN, including RV32's VLEN64.
This is bounded execution support, not full qualification of
the selected ISA. Sail executes guest privilege transitions and two-stage translation
independently; the checker compares privilege including virtualization and trap
outcomes. The exact profile is preserved; FESVR remains the only loader.

The checker compares instruction PC/encoding/length, privilege and trap transitions,
integer/FP/vector writes, FP flag effects, and physical store effects. RV5Stage does
not export privileged or vector CSR-bank snapshots. Sail evolves deterministic
CSR state itself; software CSR reads are checked through their register results.
The generic hook API still accepts explicitly authored CSR effects, but does not
require them for deterministic internal state.
Sail has private RAM and independent page-table walks. Device reads and explicitly
registered externally mutable memory reads receive physical bytes from the DUT
environment, before Sail's load semantics. Writes are never repeated against real
devices. Neither registers nor private memory are repaired using DUT results.

Successful FESVR loading and host writes are mirrored into reference memory.
Host writes precede records at or after their completion sample. FESVR registers
the ELF's eight-byte `tohost` and `fromhost` mailboxes before publishing the boot
entry. A load admitted before a host write completes may observe the old or new
mailbox bytes; replay makes that observation an explicit environmental input.
Mailboxes retain their normal RAM PMAs and checked stores. Address, width, and
complete replay consumption are checked; Sail still translates and checks access
permissions, extends the load, and computes its destination. Instruction fetch
and page walks never use mailbox replay. Ordinary RAM remains independently
checked; other host/hart memory races need an explicit policy before use.
This mailbox policy does not independently verify HTIF/coherence ordering or the
host's returned data. The real BootROM/DTB is embedded with the exact configuration.

Checking runs after all callbacks for an edge settle. HTIF exit freezes the
admitted instruction boundary, then the simulator clocks until its delayed
effects complete. Younger instructions are outside that frozen window.
Incomplete records, mismatches, and cycle-limit exhaustion fail the run.
Only initial reset is integrated; later warm reset is not supported.

Interrupt pins, architectural time, and active cycles are sampled at admission.
Sail independently determines interrupt eligibility, priority, delegation, and
target at the observed arbitration boundary; this does not check interrupt
latency. Sail evolves instret, software counter writes, FCSR, and rounding itself.
Their effects are checked when they affect instruction outcomes, not through
separate CSR snapshots. FP results include NaN boxing; reported FP flag
contributions are folded into architectural `fflags`. A contribution to an
already-set sticky flag is not separately compared with Sail.

Bare/Sv32/Sv39 and guest translation currently use software-managed A/D bits. Split accesses
are sequential: successful store prefixes survive a second-page fault, and a
faulting load leaves its destination unchanged. Store comparison preserves
those effects independently of transfer size. Fetch encoding and fault outcomes
are checked, but a separate fetch physical address is not observed.

Ordinary RAM loads are checked through register results, not a reference access
trace. RAM stores compare touched physical addresses and final byte values;
device writes preserve byte order and multiplicity. Device reads are replayed
by physical address and width. Access-attempt identities, fault-fragment coverage,
and ordinary load addresses/data are not independently compared. An incorrect
load returning the same architectural value or an omitted already-set FP flag
can therefore escape this checker; targeted RTL tests own those details.

AMO results and LR/SC reservations are computed independently. An observed SC
failure may withhold reference success, never force it. This permitted spurious
failure does not establish LR/SC progress, coherence correctness, or cross-hart
ordering. Cache-block zero is checked as ordinary physical zero stores. Clean,
flush, and invalidate have no architectural byte effect; Sail checks their
permissions and trap/retirement outcomes, not cache residency or writeback timing.
Device and external-memory atomics, hardware A/D updates, HPM event/filter
behavior remain outside runtime support.

`cosim-smoke` also injects deliberate GPR corruption to confirm mismatch detection.
CI enables cosim on every enrolled Mini/Simple RV5Stage and RV2Wide config, including the
existing direct-SystemVerilog variant; the experimental Rsim backend is not
instrumented. They retain their ordinary shape/ISA-selected
workloads, including native software, ACT, and OpenSBI where selected. There are
no additional cosim configs, duplicate simulator builds, or test exclusions.
Enablement is not a claim that all workloads pass; incomplete comparison paths
remain visible as failures in those existing lanes.

### Dual-issue RV2Wide checking

`mini-rv2wide-rv64imacb` and `simple-rv2wide-rv64imacb` reuse the same Sail
configuration, collector, and FESVR lifecycle. Enable a local checked build with
`make -C sims boot-test isa-smoke SOC=mini-rv2wide-rv64imacb COSIM=1`.
The observer admits both successful WB slots in age order. Accepted load,
multiply, and divide owners retain their identities until their actual RF
writes; completion does not retire the instruction again. Replays and
speculative multiply launch create no records. Traps retain successful older
retirement, and interrupts enter only at the core's drained boundary.
Misaligned stores report completed physical prefixes even if a later fragment
faults. No observation state, backpressure, or reference-state repair is added
to the functional core. The existing RV2Wide CI rows remain ISA-smoke-only.

### Vector checking

The observer associates actual VRF writes, shared-service returns, and memory
fragments with the instruction admitted at WB. Owners survive queued dispatch,
replay, and overlapping execution. Records close at final execution drain, not
sequencer release; the v0 shadow does not produce duplicate architectural writes.
Vector-to-scalar integer and FP writes use that same vector owner; they do not
create another scalar retirement record.

Vector compute has no opcode qualification whitelist. The checker applies the
reported bit-masked writes to the independently matched pre-state and compares
the entire VRF with Sail's post-state. Bounds, overlapping fragments, changed
values, and missing state changes are checked without reproducing each opcode's
operand geometry. A write of the already-held value is architecturally a no-op.
Both models currently retain inactive/tail data; differing legal agnostic
policies need an explicit comparison policy, never copying DUT values into Sail.
Programs must initialize vector operands before reading them; arbitrary power-on
VRF contents are not modeled as symbolic values.

Vector memory uses the same physical store comparison and full VRF post-state
comparison. Sail independently determines trap values, `vstart`, and
FOF-shortened `vl`. Partial segment-load and optional FOF suffix divergence is
tracked, not repaired. No Sail access-attempt callbacks are needed. Generic
compute after such divergence currently fails explicitly; memory may consume
only known predicate, index, and store-data bits.

Scalar/vector flags are attributed to the issuing instruction and folded into
architectural sticky state before comparison. Other uncaptured effect families
still need integration; absence of an opcode
whitelist does not imply that every observation path is complete.

Instruction coverage comes from the existing ISA/ACT/software suites with cosim
enabled, not separate opcode matrices. Focused tests protect observation,
ownership, passivity, and mismatch detection. See the
[validation strategy](DEVELOPING.md#validation).

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
The default adapter map handles `rv5stage.v1`. To support another declared
contract, supply `~adapters: {"my-hart.v1": adapter}`. This replaces the map;
list every contract needed by the selected harts. Each adapter receives detached
configuration, tap types, component parameters, and an occurrence ID, and returns
an elaborated observer with no outputs. The core declares metadata only; adapters
and their eligibility checks belong to the simulation package.
Missing selections, unknown contracts, and unsupported selected profiles fail compilation, never
silently produce incomplete checking.

The pass preserves functional top ports and existing instance paths. It returns
`cosim.json` with deterministic occurrence IDs and paths alongside the RTL.
Only selected harts get passive observer DPI. Ordinary functional DPI is retained
in every mode. Source programs remain reusable across compilation variants.
Integration may bind sibling-component taps to a child hart using
`describe_cosim_context` from `cores/riscv/cosim-source.rhm`. The pass routes
these read-only taps only when that hart is selected, requiring the same clock
and reset domain; the uninstrumented design has no additional ports.

The host binds descriptor IDs to architectural harts using `Collector::reset`
before the first reset edge and advances the epoch before each later reset.
The RV5Stage native adapter obtains the current epoch directly from the collector
when it captures an event; it does not retain a DPI-result register in RTL.
No epoch/instance ports are added to the functional design. At least one reset
edge is required before execution. Sample barriers and environment snapshots
remain host-owned.

This pass emits observations; it does not enable Sail comparison or change
SoC/ISA selection. The RV5Stage adapter captures scalar RV32/RV64, scalar F/D,
and vector effects described above. H execution uses the same ordered stream;
no extra DUT CSR-state extraction or reference-state repair is required.

### Producer and receiver

The producer API is [`CosimHart`](events/hooks.rhdl), imported with
`lib("sims/cosim/events/hooks.rhdl")`. Construct it with an explicit `Clock` (or
`#false` for the ambient `sync_circuit` clock), a 64-bit simulation-instance ID,
and an optional host Boolean `enabled` (default true). Callers suppress reset events.
`enabled = #false` specializes away every callback. It has no ready signal, result register,
or influence on execution. Gate each method with the actual semantic event's
`Bool`, not stage occupancy. Each takes a `CosimInstructionId` containing
64-bit `epoch` and `order` fields, followed by its typed payload:

| Method | Meaning |
|---|---|
| `environment(valid, value)` | Pre-edge interrupt pins, time, active-cycle position, and interrupt-check boundary |
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
one access share an ID. A fault result records a failed access; the instruction's
separate outcome determines whether it trapped (a fault-only-first load can
suppress that fault and retire). Record a store's architectural acceptance, not a later
cache writeback. No cross-hart memory order or coherence checker is implied.

[`Collector`](events/collector.h) is the independent C++ API. Its guaranteed flow is:

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
At a settled termination boundary, `drain()` freezes each hart's admitted order
prefix and reports whether it is empty. Continue normal samples until it returns
true. Headers and effects for newer orders are outside the frozen window;
delayed callbacks for admitted orders retain every normal validation rule.
Drain is idempotent, does not reset state, and does not weaken `finish()`.
Published IDs cannot receive more callbacks. Pending order distance and effects
per record have configurable bounds (defaults 4096 and 65536); exceeding them
fails instead of dropping observations. Protocol failures poison the collector.
The DPI binding catches errors instead of unwinding C++ exceptions through SV;
the driver must check its sticky error after every evaluated edge and before
finishing. One binding owns all registered harts on one simulation thread.

## Sail adapter contract

[`SailReference`](sail/reference.h) is the generated-type-free API.
Construct `SailReference` with complete Sail configuration JSON, a reset PC,
and physical ranges backed by private ROM/RAM. Initialization validates the
supplied configuration; it does not silently substitute an ISA profile. The
caller must disable Sail's built-in CLINT and simple interrupt generator.
The [shared projection](../sail/README.md) consumes the resolved config's UDB;
ACT and co-simulation use identical hart settings with separate environments.

Generate and initialize a real config with:

```sh
make -C sims sail-cosim-config-test SOC=mini-rv5stage-rv32int
make -C sims sail-cosim-config-test SOC=simple-rv5stage-rva23
```

`sail-cosim-config` generates artifacts without building the adapter. Both targets
require an explicit ISA, accept Mini/Simple with RV5Stage or Spike, and use the
same config resolver as the simulator. `COSIM_CONFIG_DIR` defaults to the
selected simulator build directory's `cosim/`; `COSIM_PYTHON` defaults to the
existing ACT Python environment (pyjson5 and ruamel.yaml).

The export retains full config metadata, UDB, configuration fingerprint, and
the actual PMA map, reset address, ROM/RAM backing, and clock/timebase frequencies.
`sail.json` contains the reference configuration; `manifest.json` binds its
fingerprint and environment to that config and records known model differences.
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
transition. It reports the original/next PC, privilege and virtualization, fetched instruction,
retirement or waiting status, trap, register-write callbacks, and physical
memory transfers. A trap does not also execute the first handler instruction.
Fetches and page-table transfers are marked/reported at the physical boundary;
these are not an already-normalized DUT retirement record. CSR callbacks are
not a complete CSR-state delta, and repeated writes are retained in order.

`StepInputs` supplies machine/supervisor external interrupt levels, explicit
cycle ticks, architectural time, wait release, and an ordered list of physical
addresses and raw device-read bytes. Device
reads are injected before load sign extension and other instruction semantics.
Device writes are reported but never sent to a second device implementation.
Missing, mismatched, or unused read replay fails the step; a failed step poisons
the instance because architectural state may already have changed.

Implemented programmable HPM counters (3–31) are hardware-supplied raw 64-bit
inputs. Sail samples them before an authorized counter CSR read, including
read-modify-write operations with `rd=x0`; it still checks access permissions,
read-only aliases, RV32 halves, CSR operations, and selector WARL rules.
Unimplemented counters remain strictly zero. Hardware supplies overflow events,
not selector or pending-CSR snapshots: Sail maintains sticky OF, `scountovf`,
LCOFIP, software clears, and interrupt arbitration. RV5Stage currently supplies
only implemented counter 3. Cycle, time, and instret policies are unchanged.
PMU event selection, counting, inhibition, and wrap detection are checked by
the RTL component tests, not independently reproduced by cosimulation.

FESVR remains the sole loader/host-service implementation. The adapter neither
creates HTIF nor reads an ELF on its own. The simulator runtime mirrors successful
loading and host writes into private reference memory as described above.

## Current limits

- One live instance on one host thread, because the pinned Sail runtime and
  configuration are process-global. Destroying and reconstructing resets it.
- The embedding and simulator initialization support RV32/RV64.
  Hardware A/D effects remain a gap. RV64 vector-profile workloads are enabled but not
  yet established as passing end to end.
- Reservation hooks use the existing Sail platform interface; no atomic
  semantics are patched into the upstream model.
- Typed RTL hooks and the deferred-effect collector also have an optional
  [RV5Stage producer](../../cores/rv5stage/README.md#optional-architectural-observation).
  Local runtime selection is opt-in; all existing Mini/Simple RV5Stage CI
  artifacts have it enabled. Close remaining observation gaps through those
  existing workloads, not more custom ISA suites.

The pinned model and optional host hooks live in the existing
[Sail patch series](../../riscv/sail-riscv-patches/series), not in a dirty
submodule. The same package still supplies the standalone ACT executable.
