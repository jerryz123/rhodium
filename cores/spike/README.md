<!-- Defines the standalone Spike-backed core, its transaction ABI, and current integration limits. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Spike-backed core

[`profile.rhm`](profile.rhm) defines `SpikeConfig`, the implementation-owned
configuration for the simulator-backed core. Its
`hart_description` projection uses the same implementation-neutral
[`RiscvHartDescription`](../../riscv/isa/hart.rhm) consumed by SoC and
device-tree code. The configuration derives Spike's ISA and privilege strings,
and separately owns instructions-per-cycle and PMP implementation parameters.

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

This is a standalone core boundary. It is deliberately not selectable from a
SoC yet; SoC generalization follows only after this adapter has a complete
end-to-end harness.
