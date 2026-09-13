<!-- Guides contributors through implementing and validating NoC hardware from certified plans. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing NoC RTL

Read the [NoC RTL guide](README.md) for public component and integration
contracts, and the parent [`DEVELOPING.md`](../DEVELOPING.md) for pure-model,
proof, and planning ownership.

## Architecture and ownership

This directory is the only NoC layer that may construct hardware. It consumes
`RouterPlan` or `RouterFamilyPlan` values produced by the pure stack and may
use public `#lang rhodium` and standard-library APIs. It
must not import Rhodium core, frontend, backend, or CIRCT implementation
modules, construct proof certificates, or accept unchecked routing relations.

The system or tile that instantiates a router owns hierarchy and physical-link
wiring. NoC RTL owns only reusable realization of the supplied plan.
[`../check-boundaries.sh`](../check-boundaries.sh) enforces the package split.

## Implementation map

| Concern | Owner |
|---|---|
| Public facade | [`main.rhdl`](main.rhdl) |
| Validated route decoding | [`route-computer.rhdl`](route-computer.rhdl) |
| Protocol-neutral metadata boundaries | [`route-adapter.rhdl`](route-adapter.rhdl) |
| Fallback-aware allocation | [`allocator.rhdl`](allocator.rhdl) |
| Single-beat and uniform-family routers, physical-slot binding, and unused-local closure | [`router.rhdl`](router.rhdl) |
| Wormhole reservation and switching | [`wormhole-router.rhdl`](wormhole-router.rhdl) |
| Backend fixture designs | [`tests/`](tests/) |
| CIRCT runner and Verilator benches | [`../../tests/backend/`](../../tests/backend/DEVELOPING.md#fixture-and-artifact-ownership) |

## Change a hardware realization

1. Start from the narrowest opaque plan that contains the required certified
   facts. Do not repeat topology or proof analysis in hardware code.
2. Keep route, origin, target, and site encodings derived from the plan; do not
   infer stable meaning from local array positions outside the owning plan.
3. Preserve the proof regime's runtime obligation. Escape-certified routers
   must continuously offer fallback choices when adaptive resources are not
   immediately available.
4. Keep protocol payloads behind the route-adapter boundary and reuse public
   ready-valid, VC, queue, matcher, and crossbar components.
5. Test route, arbitration, reservation, backpressure, and packet behavior in a
   compiled backend fixture. Do not add host snapshots of internal router
   operations or instances.
6. Update [README.md](README.md) when observable ports, timing, buffering,
   arbitration, packet retention, or hierarchy contracts change.

## Focused validation

Run the complete host-side NoC suite from the repository root:

```sh
make noc-test
```

The backend protocol group owns route-computer, router, assembled-network,
wormhole, escape-router, and router-family CIRCT/Verilator fixtures:

```sh
bash tests/backend/run-circt.sh --group protocols
```

That group also includes CHI and device fixtures and is broader than this
directory. Use the backend test
[`DEVELOPING.md`](../../tests/backend/DEVELOPING.md) to select verification,
simulation, or golden-reference modes. Repository test wrappers provide a
fresh `PLTCOMPILEDROOTS`; direct Racket or Rhombus runs must do the same.

For family-slot binding changes, select `noc-router-family` for generic routing
and backpressure behavior, plus `chi-family-noc` and `chi-router-composition`
for protocol integration. The composition fixture is emission/verification-only;
the other two have behavioral benches.
