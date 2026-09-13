<!-- Guides contributors through maintaining the Sky130 SRAM catalog and functional model. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing Sky130 SRAM support

Read the catalog [README](README.md) for macro geometry, pins, functional
behavior, PDK views, consumer handoff, and deliberate limits. This guide owns
catalog maintenance and focused validation.

## Architecture and ownership

This directory owns technology metadata and checked-in zero-delay functional
models. It does not own logical memory eligibility, generic adapter rendering,
design site selection, PDK installation, or physical signoff. Catalog values
must describe the exact pinned collateral consumed by downstream flows.

## Implementation map

| Path | Responsibility |
|---|---|
| [`macros.ini`](macros.ini) | Machine-readable Sky130 macro metadata and PDK-relative view paths |
| [`models/`](models/) | Checked-in zero-delay functional models named by catalog entries |
| [`../map-memories.py`](../map-memories.py) | Catalog parsing, validation, generic `openram_1rw1r` adaptation, tiling, and manifests |
| [`../tests/test_mapper.py`](../tests/test_mapper.py) | Catalog loading, wrapper shape, mask routing, functional simulation, and failures |
| [`../../vlsi/DEVELOPING.md`](../../vlsi/DEVELOPING.md) | Design/technology choices, PDK setup, assertions, and physical consumption |

## Add or replace a macro

1. Copy the exact cell/module name, geometry, interface, mask granularity,
   supply pins, and collateral paths from the pinned PDK.
2. Provide Verilog, LEF, GDS, Liberty, and SPICE paths relative to the supplied
   PDK root.
3. Use an existing generic interface token only when its adapter-visible port
   and cycle contract match exactly. Add a generic mapper adapter before
   cataloging a new pin convention.
4. Add a functional model only when it matches the adapter-visible synchronous
   behavior. Keep timing, power, analog, and physical effects out of that model.
5. Update a consumer policy or physical configuration only when that consumer
   is intended to select the new macro.
6. Update [README.md](README.md) with the public geometry, interface, PDK-view,
   handoff, and limitation changes.

The parser currently recognizes `openram_1rw1r` and collateral keys `verilog`,
`lef`, `gds`, `liberty`, and `spice`. Extending those vocabularies belongs in
the generic mapper first.

## Focused validation

Run the owning catalog and mapper regression:

```sh
make -C sram test
```

With the pinned PDK installed, validate collateral and the current direct-top
consumer manifest:

```sh
make -C vlsi mini-soc-memory-map
```

After changing functional-model behavior, run the mapped MiniSoC smoke:

```sh
make -C vlsi/sim smoke
```

These checks cover metadata consistency, wrapper behavior, consumer handoff,
and logical cycles. They are not timing, power, DRC, LVS, or silicon signoff.
