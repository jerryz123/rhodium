<!-- Explains ownership and validation of the simulator's OpenSBI integration. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the OpenSBI simulator integration

This package owns the simulator's OpenSBI targets and validation
payload. Read [Run OpenSBI](../README.md#run-opensbi) for usage.
The target-derived layout and firmware builder live in
[`../../sw/build/opensbi.py`](../../sw/build/opensbi.py), and the unmodified
pinned source is the [`../../sw/opensbi`](../../sw/opensbi/) submodule. Architectural
memory, boot, interrupt, and device-tree descriptions remain in
[`../../socs/`](../../socs/DEVELOPING.md),
while ELF loading and completion remain in the simulator's FESVR transport.

The [`../../sw/build/opensbi.py`](../../sw/build/opensbi.py) adapter derives its FW_JUMP addresses and minimum ISA
from the selected SoC target descriptor. The firmware ELF remains linked at
zero so FESVR's standard DRAM load offset relocates it to the SoC boot address.
The SoC emits the only hardware DTB into BootROM, which supplies its address
in `a1`. FW_JUMP copies that DTB into the reserved writable RAM range through
`FW_JUMP_FDT_ADDR`, applies its normal FDT fixups, and passes the RAM address
to S-mode. Do not embed another DTB with `FW_FDT_PATH` or add transport nodes
to the hardware tree. The current 8 KiB BootROM window and OpenSBI's 4 KiB
FDT expansion fit within the 64 KiB reservation; revisit this bound if either grows.

[`../../sw/opensbi-patches/series`](../../sw/opensbi-patches/series) enables
`PLATFORM_GENERIC_HTIF_RESET` in a build-local generic-platform copy. It initializes
OpenSBI's existing HTIF reset device directly, without an `/htif` node. Console
selection retains upstream FDT discovery of the SoC UART. Its `current-speed`
selects divisor one, matching the default simulation UART receiver.
The source submodule remains pristine; patch and materializer bytes
participate in firmware build identity.
Linux build/run targets live in `../linux/Makefile.inc`, while their target
software owner is [`../../sw/buildroot-riscv/`](../../sw/buildroot-riscv/README.md).
Linux smoke boot arguments belong in its kernel configuration, not the DTB.
The validation firmware disables
OpenSBI boot prints so the RTL test does not spend most of its runtime
serializing a diagnostic banner.

The normal firmware artifact contains no next-stage payload. FESVR loads a
caller-selected auxiliary ELF with its existing `+payload` option while
retaining OpenSBI's sole `tohost` and `fromhost` symbols. S-mode software exits
through SBI system reset; it must not define or access an HTIF mailbox. The
package-local [`tests/smoke.S`](tests/smoke.S) image checks the RAM DTB pointer
and magic, SBI facilities, console output, and reset. It checks that ownership and
is only an end-to-end validation fixture, not part of the OpenSBI target.

Run the adapter tests before the real firmware test:

```sh
make -C sims opensbi-adapter-test
make -C sims opensbi-setup
make -C sims opensbi-test SOC=simple CORE=rv5stage ISA=rva23
make -C sims opensbi-test SOC=simple CORE=spike ISA=rva23
```

The generic workload runner selects `+uart-log=-`, capturing UART console bytes
in its ordinary process log before checking the complete marker. Check the
final newline as well as HTIF exit when changing console or shutdown timing;
host logging cannot recover bytes still inside the UART at simulation exit.

Keep generated firmware, layouts, logs, and payload ELFs under
`/tmp/rhodium-software`. When changing layout policy, retain explicit checks
that firmware, the next stage, the FDT reservation, and all loaded ELF segments
fit the selected architectural RAM without overlap.

`opensbi-elfs` builds firmware and the validation payload and writes the
shared manifest. CI supplies `PREBUILT_PROGRAM_TARGET` from its planning
artifact, so firmware compilation does not elaborate Rhodium. The common
archive includes both ELFs; the common binder checks the target-derived ISA,
boot layout, image hashes, and relocated firmware/payload ranges without
compiler tools. Compatible configs share firmware and use their own ROM DTB at boot.
`opensbi-smoke-run` runs that bound manifest through the ordinary workload
runner with bounded process-group execution and JSON/JUnit diagnostics.
`opensbi-test` composes build and run locally. `opensbi-run` remains the
caller-selected next-stage path but no longer builds firmware implicitly;
build `opensbi-firmware` first. Firmware build
identity stays in `sw/build/opensbi.py`.
