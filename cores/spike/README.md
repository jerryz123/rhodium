<!-- Defines the standalone Spike-backed core, its transaction ABI, and current integration limits. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Spike-backed core

[`profile.rhm`](profile.rhm) defines `SpikeConfig`, the implementation-owned
configuration for the simulator-backed core. Its
`hart_description` projection uses the same implementation-neutral
[`RiscvHartDescription`](../../riscv/isa/hart.rhm) consumed by SoC and
device-tree code. The configuration derives Spike's ISA, privilege, and
Bare/Sv39 MMU settings and exact vector VLEN/ELEN. The pinned runtime derives
vector geometry from its ISA string; initialization checks that it agrees with
the hart description. The adapter includes the exact VLEN's Zvl spelling when
the ISA advertises only a smaller minimum. Configuration strings are
NUL-terminated and bounded to 512 bytes for ISA and four bytes for privilege;
oversized strings are rejected, not truncated. It separately owns the maximum retired instructions per
simulated cycle and PMP implementation parameters.
`max_retired_instructions_per_cycle` defaults
to one; increasing it accelerates cached execution while memory and coherence
transactions can still yield the model before that maximum is reached.

The explicit `rva23` SoC specialization selects the shared RV64D/V architecture
with VLEN=128 and ELEN=64, without substituting a scalar profile. This preset
name is not a full RVA23 conformance claim. Simulator execution and ACT
projection are separate capabilities: the broad profile's ACT/UDB projection
is not implemented yet and still rejects that request.

The SoC specialization enables Zihpm with 29 read-only-zero HPM counters
and event selectors. Their `mcounteren`, `scounteren`, and `mcountinhibit` bits
are also read-only zero; only the base counter-control bits remain writable.

[`spike.rhdl`](spike.rhdl) exposes the same architectural inputs and independent
instruction, coherent-data, and uncached CHI ports used by a RISC-V hart. The
core contains a typed DPI boundary, RTL-owned PMA classification, and
Spike-owned CHI transaction engines. The C++ model under [`dpi/`](dpi/) runs one
Spike `processor_t` in a coroutine, models private instruction and data caches,
handles coherent snoops and victim releases, and stalls Spike while accepted
transactions await their responses.

The initial model has one blocking architectural access in flight. Coherent
normal memory may populate the software instruction and data caches;
instruction-only noncoherent regions remain executable but are read through the
uncached CHI port. Snoop service remains live while Spike is waiting on another
transaction.
LR/SC checks the complete RTL-owned physical-memory range for atomic support
and the operation's read or write permission before testing reservation state.
An SC to a faulting region therefore traps even when its reservation has failed.

[`SingleCoreSpikeSoC`](../../socs/products/single-core-spike-soc.rhdl) attaches this core
to the same coherent single-core fabric, LLC, BootROM, ACLINT, PLIC, UART, and
host interface used by the hardware implementation. The shared
[`simulation harness`](../../sims/single-core-soc-harness.rhdl) supplies external
CHI memory, and the Spike simulator build links its runtime. Normal FESVR ELF loading and
`tohost`/`fromhost` termination exercise the complete SoC path. The Spike core
and any SoC containing it remain simulation-only because the core boundary
contains DPI calls.
