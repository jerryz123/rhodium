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

`configuration.project_architecture(default, udb)` returns an independent model
configuration with the exact supported UDB architecture: extensions, XLEN,
VLEN/ELEN, privilege versions, translation, CSR masks, trap choices, CBO geometry,
and reservation constraints. It leaves memory regions and reference devices
untouched for the caller to replace. Unsupported mappings fail explicitly.
The projection preserves `mcountinhibit` presence and its exact writable mask.
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

`reference_model_differences` records legal DUT choices Sail cannot reproduce,
including vector reserved behavior and HPM event counting. Both consumers
publish these differences without changing the config's UDB or disabling
extensions. A future comparison engine must explicitly handle or reject each
difference; configuration acceptance alone is not full-profile validation.
