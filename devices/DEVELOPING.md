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

Keep synthesizable devices independent of a particular core or SoC. Keep a
host model behind a narrow DPI boundary and pair it with synthesizable-facing
Rhodium logic; do not put DPI calls in a SoC.

## Implementation map

| Area | Owning source |
|---|---|
| Boot image and reset trampoline | [`bootrom-image.rhm`](bootrom-image.rhm) |
| CHI BootROM endpoint | [`bootrom.rhdl`](bootrom.rhdl) |
| ACLINT registers, interrupts, and CHI endpoint | [`aclint.rhdl`](aclint.rhdl) |
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

To lower and simulate only the four device fixtures through CIRCT and
Verilator, run:

```sh
FIXTURES='bootrom aclint uart16550 uart-dpi' \
  bash tests/backend/run-circt.sh --simulate-only
```

These fixtures cover transactions, registers, interrupts, serial pins, and the
DPI boundary. The backend test
[`DEVELOPING.md`](../tests/backend/DEVELOPING.md) owns runner modes, toolchain
requirements, and artifact policy.
