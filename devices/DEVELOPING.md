<!-- Guides contributors through implementing and validating reusable platform devices. -->

# Developing platform devices

Read the package [README](README.md) for public device interfaces, register
behavior, configuration constraints, simulation boundaries, and deliberate
limits. This guide owns source placement, extension workflow, and validation.

## Architecture and ownership

Device modules own accepted CHI operations, transfer sizes, register offsets,
window constraints, and device-local parameter validation. SoCs own NodeIDs,
address placement, PMA and Home routing, clock/tick policy, and interrupt
wiring. Simulators own terminal processes, executable harnesses, and other
host policy.

Import CHI contracts and the required engine from their defining modules, as
described in the [CHI import guide](../chi/README.md#package-boundary-and-import).
Device configuration uses generic address and transfer types from
`rhodium/std/interconnect.rhdl`; it does not require the all-CHI facade, NoC,
Home engines, or either backing-memory implementation.

Boot-address, ACLINT, PLIC, and UART16550 delegate their single-beat CHI
transaction lifetime to [`CHISingleBeatSubordinate`](../chi/subordinate/single-beat-subordinate.rhdl).
Devices retain decode, read snapshots, request-acceptance side effects, and
DAT-acceptance writes. PLIC claims and UART FIFO/read-to-clear effects must not
move to response acceptance. UART TX space drives the engine's write readiness;
mask policies retain their original gating versus assertion-only behavior.
BootROM remains a separate read-only multibeat endpoint. See the
[CHI developer guide](../chi/DEVELOPING.md) for the engine boundary and tests.

Use `expand_mask` from [`rhodium/std/bits.rhdl`](../rhodium/std/bits.rhdl) for
lane-enable expansion instead of device-specific byte-mask builders. ACLINT
owns its physical-lane shift and boot-address owns its low-eight-lane slice;
the generic helper only expands the resulting enables for `masked_merge`.

Keep synthesizable devices independent of a particular core or SoC. Keep a
host model behind a narrow DPI boundary and pair it with synthesizable-facing
Rhodium logic; do not put DPI calls in a SoC.

## Implementation map

| Area | Owning source |
|---|---|
| Boot image and reset trampoline | [`bootrom-image.rhm`](bootrom-image.rhm) |
| CHI boot-address register | [`boot-address.rhdl`](boot-address.rhdl) |
| CHI BootROM endpoint | [`bootrom.rhdl`](bootrom.rhdl) |
| ACLINT registers, interrupts, and CHI endpoint | [`aclint.rhdl`](aclint.rhdl) |
| PLIC priorities, gateways, contexts, and CHI endpoint | [`plic.rhdl`](plic.rhdl) |
| 8-N-1 serial engines | [`uart.rhdl`](uart.rhdl) |
| 16550-style registers, FIFOs, and CHI endpoint | [`uart16550.rhdl`](uart16550.rhdl) |
| Rhodium PTY adapter | [`uart-dpi.rhdl`](uart-dpi.rhdl) |
| PTY ABI and host implementation | [`dpi/uart_dpi.h`](dpi/uart_dpi.h), [`dpi/uart_dpi.cc`](dpi/uart_dpi.cc) |
| Host image, configuration, parameter, and ABI checks | [`tests/`](tests/) |
| CIRCT emitters and Verilator benches | [`../tests/backend/`](../tests/backend/DEVELOPING.md#fixture-and-artifact-ownership) |

## Add or change a device

1. Define the reusable hardware boundary without choosing a system address or
   processor-specific interrupt route.
2. Validate configuration at elaboration and make unsupported accesses or
   register modes fail at the narrowest owning boundary.
3. Keep register effects, response timing, interrupt generation, and serial or
   timer protocols explicit in the public README contract.
4. If host interaction is required, define a narrow ABI and test its C++ model
   independently before integrating the Rhodium DPI adapter.
5. Use a host test for image construction, parameters, or ABI validation. Test
   register effects, interrupts, serial timing, and transactions in a
   CIRCT/Verilator fixture; do not add a separate internal-shape snapshot.
6. Update [`../socs/`](../socs/README.md) only when a concrete platform adopts
   the device or changes its address, NodeID, PMA, Home, or interrupt policy.

## Focused validation

Run device host contracts and the standalone C++ PTY test from the repository
root:

```sh
make device-test
```

The target runs package-boundary checks, every `devices/tests/*-test.rhm`, and
[`run-uart-dpi-cpp.sh`](tests/run-uart-dpi-cpp.sh). The Rhombus test wrapper
creates a fresh compiled root when the caller has not supplied one.

To lower and simulate only the device fixtures through CIRCT and
Verilator, run:

```sh
FIXTURES='bootrom boot-address aclint plic uart16550 uart-dpi' \
  bash tests/backend/run-circt.sh --simulate-only
```

These fixtures cover transactions, registers, interrupts, serial pins, and the
DPI boundary. The backend test
[`DEVELOPING.md`](../tests/backend/DEVELOPING.md) owns runner modes, toolchain
requirements, and artifact policy.
