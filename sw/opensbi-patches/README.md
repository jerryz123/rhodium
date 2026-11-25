<!-- Documents build-local OpenSBI transport and compiler adaptations and the ROM-DTB handoff. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# OpenSBI simulation patches

The ordered [series](series) enables firmware-owned HTIF reset in the
generic platform without adding an HTIF node to the SoC's device tree. Console
selection uses the upstream DT discovery path and the SoC's UART. The
builder applies it through the shared patched-submodule materializer; the
[`../opensbi/`](../opensbi/) submodule stays pristine.

The series also scopes an RPMI response declaration inside its switch case so
Clang accepts the upstream source in OpenSBI's C11 warning-as-error build.
It does not disable diagnostics or change the firmware's response processing.

BootROM supplies the hardware DTB in `a1`. FW_JUMP copies it to writable RAM
using `FW_JUMP_FDT_ADDR`, and normal OpenSBI fixups prepare it for S-mode.
Linux uses SBI services, not the HTIF mailboxes. The UART's DT `current-speed`
matches its reset divisor of one (6.25 Mbaud at 100 MHz) and the default
simulation receiver. No DTB is embedded in firmware.

Use `SOC=simple-spike-rva23 moon run rhodium:sim-opensbi-test` from the repository
root. The ordinary workload runner selects UART logging to capture console
bytes in its test log and validate the console marker. Interactive runs can
use the default host PTY instead. See [software development](../DEVELOPING.md) and the
[OpenSBI integration guide](../../sims/opensbi/DEVELOPING.md) for validation.
The upstream-derived patches retain OpenSBI's BSD-2-Clause license.
