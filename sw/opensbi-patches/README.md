<!-- Documents the build-local OpenSBI transport adaptation and ROM-DTB handoff. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# OpenSBI simulation patch

The ordered [series](series) enables firmware-owned HTIF console/reset in the
generic platform without adding an HTIF node to the SoC's device tree. The
builder applies it through the shared patched-submodule materializer; the
[`../opensbi/`](../opensbi/) submodule stays pristine.

BootROM supplies the hardware DTB in `a1`. FW_JUMP copies it to writable RAM
using `FW_JUMP_FDT_ADDR`, and normal OpenSBI fixups prepare it for S-mode.
Linux uses SBI services, not the HTIF mailboxes. No DTB is embedded in firmware.

Use the normal `make -C sims opensbi-test SOC=simple-spike-rva23` flow from the
repository root. See [software development](../DEVELOPING.md) and the
[OpenSBI integration guide](../../sims/opensbi/DEVELOPING.md) for validation.
The upstream-derived patch retains OpenSBI's BSD-2-Clause license.
