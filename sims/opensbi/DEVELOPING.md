<!-- Explains ownership and validation of the simulator's OpenSBI integration. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the OpenSBI simulator integration

This package owns the simulator-specific DTB projection and qualification
payload. The target-derived layout and firmware builder live in
[`../../sw/build/opensbi.py`](../../sw/build/opensbi.py), and the unmodified
pinned source is the [`../../sw/opensbi`](../../sw/opensbi/) submodule. Architectural
memory, boot, interrupt, and device-tree descriptions remain in
[`../../socs/`](../../socs/DEVELOPING.md),
while ELF loading and completion remain in the simulator's FESVR transport.

The [`../../sw/build/opensbi.py`](../../sw/build/opensbi.py) adapter derives its FW_JUMP addresses and minimum ISA
from the selected SoC target descriptor. The firmware ELF remains linked at
zero so FESVR's standard DRAM load offset relocates it to the SoC boot address.
[`write-device-tree.rhm`](write-device-tree.rhm) resolves the selected shape
and core with a required ISA (or complete product key) through the shared
[`program-test/targets.rhm`](../program-test/targets.rhm) resolver,
derives an OpenSBI execution DTB from the canonical SoC description, and
appends only the simulator-owned `ucb,htif0` reset endpoint. FW_JUMP embeds
that DTB, copies it into the
adapter's reserved writable RAM range through `FW_JUMP_FDT_ADDR`, and passes
that RAM address to its next S-mode stage. This keeps simulator transport out
of the synthesizable SoC's BootROM DTB. The qualification firmware disables
OpenSBI boot prints so the RTL test does not spend most of its runtime
serializing a diagnostic banner.

The normal firmware artifact contains no next-stage payload. FESVR loads a
caller-selected auxiliary ELF with its existing `+payload` option while
retaining OpenSBI's sole `tohost` and `fromhost` symbols. S-mode software exits
through SBI system reset; it must not define or access an HTIF mailbox. The
package-local [`tests/smoke.S`](tests/smoke.S) image checks that ownership and
is only an end-to-end qualification fixture, not part of the OpenSBI target.

Run the adapter tests before the real firmware test:

```sh
make -C sims opensbi-adapter-test
make -C sims opensbi-setup
make -C sims opensbi-test SOC=single CORE=rv5stage ISA=rva23
make -C sims opensbi-test SOC=single CORE=spike ISA=rva23
```

Keep generated firmware, layouts, logs, and payload ELFs under
`/tmp/rhodium-software`. When changing layout policy, retain explicit checks
that firmware, the next stage, the FDT reservation, and all loaded ELF segments
fit the selected architectural RAM without overlap.
