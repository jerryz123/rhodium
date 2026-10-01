<!-- Describes the shared UDB-to-Sail architectural projection and environment boundary. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Shared Sail configuration

This package projects resolved RISC-V hart architecture into the pinned Sail
model. ACT and [embedded co-simulation](../cosim/README.md) share it. Contributor
ownership and validation live in [sims/DEVELOPING.md](../DEVELOPING.md#embedded-sail-reference).

## Entry point

`configuration.project_architecture(default, udb)` returns an independent model
configuration with the exact supported UDB architecture: extensions, XLEN,
VLEN/ELEN, privilege versions, translation, CSR masks, trap choices, CBO geometry,
and reservation constraints. It leaves memory regions and reference devices
untouched for the caller to replace. Unsupported mappings fail explicitly.

`product.product_architecture` checks the selected product and any separate UDB
export against canonical configuration metadata. `product.model_defaults` checks
the pinned Sail version and obtains its XLEN-specific defaults.

## Architecture versus environment

- ACT adds its synthetic reference devices, payload RAM, DTB location, test
  entry/signature files, and fault-test window in `arch-test/configure.py`.
- Co-simulation uses the real resolved Mini/Simple physical map and private
  ROM/RAM ranges in `cosim/configure.py`. Devices/time belong to the DUT
  environment. It does not inherit ACT placement or signature policy.

`reference_model_differences` records legal DUT choices Sail cannot reproduce,
including vector reserved behavior and HPM event counting. Both consumers
publish these differences without changing the product's UDB or disabling
extensions. A future comparison engine must explicitly handle or reject each
difference; configuration acceptance alone is not full-profile qualification.
