<!-- Guides contributors through implementing and validating reusable platform devices. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing platform devices

Read the package [README](README.md) for public device interfaces, register
behavior, configuration constraints, simulation boundaries, and deliberate
limits. This guide owns source placement, extension workflow, and validation.

## Architecture and ownership

Device modules own accepted CHI operations, transfer sizes, register offsets,
window constraints, and device-local parameter validation. ACLINT additionally
owns its fixed architectural base. SoCs own NodeIDs, other device address
placement, PMA and Home routing, clock/tick policy, and interrupt
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
[CHI subordinate developer guide](../chi/subordinate/DEVELOPING.md) for the engine boundary and tests.

Use `expand_mask` from [`rhodium/std/bits.rhdl`](../rhodium/std/bits.rhdl) for
lane-enable expansion instead of device-specific byte-mask builders. ACLINT
owns its physical-lane shift and boot-address owns its low-eight-lane slice;
the generic helper only expands the resulting enables for `masked_merge`.

Keep synthesizable devices independent of a particular core or SoC. Keep a
host model behind a narrow DPI boundary and pair it with synthesizable-facing
Rhodium logic; do not put DPI calls in a SoC.

## Implementation map

Production sources are grouped by device family under `boot/`, `interrupt/`,
`uart/`, and `display/`. Tests remain together under `tests/`; the UART host
model stays beside its Rhodium adapter under `uart/dpi/`.

| Area | Owning source |
|---|---|
| Boot image and reset trampoline | [`boot/bootrom-image.rhm`](boot/bootrom-image.rhm) |
| Fixed ACLINT address and register layout | [`interrupt/aclint-layout.rhm`](interrupt/aclint-layout.rhm) |
| CHI boot-address register | [`boot/boot-address.rhdl`](boot/boot-address.rhdl) |
| CHI BootROM endpoint | [`boot/bootrom.rhdl`](boot/bootrom.rhdl) |
| ACLINT registers, interrupts, and CHI endpoint | [`interrupt/aclint.rhdl`](interrupt/aclint.rhdl) |
| PLIC priorities, gateways, contexts, and CHI endpoint | [`interrupt/plic.rhdl`](interrupt/plic.rhdl) |
| 8-N-1 serial engines | [`uart/uart.rhdl`](uart/uart.rhdl) |
| 16550-style registers, FIFOs, and CHI endpoint | [`uart/uart16550.rhdl`](uart/uart16550.rhdl) |
| Fixed HDMI timing, framebuffer contract, and CHI frame reader | [`display/hdmi.rhdl`](display/hdmi.rhdl) |
| Synchronous row SRAM, pixel unpacking, video timing, and CHI scanout composition | [`display/hdmi-scanout.rhdl`](display/hdmi-scanout.rhdl) |
| Independent TMDS channel disparity and three-channel video encoding | [`display/tmds.rhdl`](display/tmds.rhdl) |
| Rhodium PTY adapter | [`uart/uart-dpi.rhdl`](uart/uart-dpi.rhdl) |
| PTY ABI and host implementation | [`uart/dpi/uart_dpi.h`](uart/dpi/uart_dpi.h), [`uart/dpi/uart_dpi.cc`](uart/dpi/uart_dpi.cc) |
| Host image, configuration, parameter, and ABI checks | [`tests/`](tests/) |
| CIRCT emitters and Verilator benches | [`tests/circt/`](tests/circt/) |

## Add or change a device

1. Define the reusable hardware boundary without choosing a system address or
   processor-specific interrupt route, except for ACLINT's fixed architectural
   placement.
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
selects the persistent worktree-specific compiled root when none is supplied.

To lower and simulate only the device fixtures through CIRCT and
Verilator, run:

```sh
FIXTURES='bootrom boot-address aclint plic uart16550 uart-dpi hdmi-frame-reader hdmi-scanout hdmi-tmds' \
  bash tools/testing/circt/run.sh --simulate-only
```

These fixtures cover transactions, registers, interrupts, serial pins, and the
DPI boundary. The backend test
[`DEVELOPING.md`](../tools/testing/circt/DEVELOPING.md) owns runner modes, toolchain
requirements, and artifact policy.

The `hdmi-scanout` fixture tests the composed reader and row buffers against
a CHI response model and an independent video-position scoreboard. It checks
SRAM bank reuse across five-row frames, RGB byte order, sync polarities,
continuous and gapped pixel enables, deadline failure, completions delayed
across restart, poison/errors, sticky status, and disabled black output.
Its encoder connection also checks per-lane symbols and the added clock of
latency across active pixels, blanking, and pixel-enable gaps.

The `hdmi-tmds` fixture discovers every reachable disparity state in an
independent integer reference model, then drives a reference prefix and all
256 byte values from each state. It also covers all four control symbols,
invalid-cycle state retention, reset during active data, decoding, and long
streams. The shared test-only `tmds-reference.svh` updates disparity by
counting transmitted bits instead of repeating the RTL's arithmetic.
