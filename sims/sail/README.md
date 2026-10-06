<!-- Describes the shared UDB-to-Sail architectural projection and environment boundary. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Shared Sail configuration

This package projects resolved RISC-V hart architecture into the pinned Sail
model. ACT and [embedded co-simulation](../cosim/README.md) share it. Contributor
ownership and validation live in [sims/DEVELOPING.md](../DEVELOPING.md#embedded-sail-reference).
Contributors follow the [source documentation requirements](../../AGENTS.md#source-documentation),
including the `tests/` exemption.

[`SailModelConfig.cmake`](SailModelConfig.cmake) also defines the installed
`Sail::Model`, `Sail::Runtime`, and `Sail::SoftFloat` CMake targets. The shared
Sail installer packages it with the pinned libraries; consumers do not need
the model build directory.

## Entry point

`configuration.project_architecture(default, udb, csr_warl)` returns an independent model
configuration with the exact supported UDB architecture: extensions, XLEN,
VLEN/ELEN, privilege versions, translation, CSR masks, trap choices, CBO geometry,
and reservation constraints. It leaves memory regions and reference devices
untouched for the caller to replace. Unsupported mappings fail explicitly.
The projection preserves `mcountinhibit` presence and its exact writable mask.
Resolved config metadata supplies implementation-owned `medeleg`/`hedeleg`
writable masks and unsupported PMM-write behavior in `csr_warl`. The shared
RTL CSR bank exports its specialization's masks; Spike exports the pinned
implementation's different mask. These are static parameters, never DUT state.
Both ACT and cosim apply the same policy. Unsupported HPM event IDs become
zero according to UDB's `HPM_EVENTS`, preserving overflow and filter fields;
absent counters and their selectors remain read-only zero. This models selector
WARL behavior only, not HPM event counting or generated overflow.
Our no-trigger harts set `base.tselect_present` false so optional selector accesses
trap, including OpenSBI's trigger probe. The patched model retains its legacy
placeholder by default; this control does not implement or advertise Sdtrig.
For H profiles, the current supported transformed-instruction policy is the
UDB's always-zero choice; other policies fail instead of inheriting Sail defaults.

`soc_config.config_architecture` checks the selected config and any separate UDB
export against canonical configuration metadata. `soc_config.model_defaults` checks
the pinned Sail release and required configuration controls, then obtains its
XLEN-specific defaults. The release string alone cannot distinguish the current
master pin from the older 0.14.1 tag.

## Architecture versus environment

- ACT adds its synthetic reference devices, payload RAM, DTB location, test
  entry/signature files, and fault-test window in `arch-test/configure.py`.
- Co-simulation uses the real resolved Mini/Simple physical map and private
  ROM/RAM ranges in `cosim/runtime/configure.py`. Devices/time belong to the DUT
  environment. It does not inherit ACT placement or signature policy.

Sstc compares `stimecmp` and `vstimecmp` with architectural time, including the
existing embedded host-time provider. It does not require Sail's optional
CLINT device; machine timer/software interrupt pins remain independent.
When STCE enables the supervisor comparator, its result replaces legacy STIP
pins without overwriting the independently stored software-pending bit.

`reference_model_differences` records legal DUT choices Sail cannot reproduce,
including vector reserved behavior and HPM event counting. Both consumers
publish these differences without changing the config's UDB or disabling
extensions. A future comparison engine must explicitly handle or reject each
difference; configuration acceptance alone is not full-profile validation.
