<!-- Guides contributors through changing and validating concrete SoC compositions. -->

# Developing SoC compositions

Read the package [README](README.md) for the public system choices, host and
platform boundary, memory hierarchy, topology, parameters, and deliberate
limits. This guide owns implementation placement and contributor validation.

## Architecture and ownership

The SoC layer composes reusable processors, CHI endpoints and routers, Homes,
devices, address maps, PMA, memory boundaries, and common host/UART interfaces.
It owns concrete NodeIDs, address windows, interrupt wiring, clock/tick policy,
and topology. It must not acquire executable drivers, DPI calls, target
loading, or simulator policy; those belong in [`../sims/`](../sims/DEVELOPING.md).

```mermaid
flowchart LR
  Core["cores<br/>processors"] --> SoC["socs<br/>composition policy"]
  CHI["chi<br/>protocol components"] --> SoC
  NoC["noc<br/>validated plans and routers"] --> SoC
  Devices["devices<br/>reusable peripherals"] --> SoC
  SoC --> Sims["sims<br/>executable harnesses"]
  SoC --> VLSI["vlsi<br/>implementation flows"]
```

Keep reusable component internals in their owning packages. A SoC should
configure and connect those public contracts, not fork their behavior.

Import CHI contracts, NoC adapters, selected Home engines, and SRAM only where
the composition uses them; use defining modules instead of the all-CHI facade.
Shared channel interfaces need only their parameter and channel owners.
Keep shared memory configuration separate from the concrete RAM import and
leave DPI memory to the simulator. See the
[CHI import guide](../chi/README.md#package-boundary-and-import).

MiniSoC, SimpleSoC, and TiledSoC are independent top-level compositions.
`single-core-system.rhdl` owns `SingleCoreSystemParams` and the
`populate_single_core` elaboration helper. Parameters derive routing, endpoint
identities, PMA, and descriptions from each caller's memory service and platform
parameters. The helper emits the processor, chosen Home, routers, and platform
devices directly into the caller; it adds no hardware wrapper. MiniSoC owns its
RAM configuration, while SimpleSoC owns an external service and LLC geometry.
The helper groups its existing router endpoints in CHI's `CHINoCPorts` view and
passes that view to the typed RN/HN/SN attachment helpers. CHI owns the shared
injection/ejection queue policy; this view adds no circuit hierarchy.
`endpoint-params.rhdl` supplies host descriptions shared with the tiled compiler;
exact ICN peers are derived through CHI's `node.icn_peer()` method. Home
parameters obtain subordinate endpoints from their services. `make check-boundaries` rejects imports between peer SoCs and
imports of named SoCs from shared components.

## Implementation map

| Concern | Owner |
|---|---|
| Architectural host description and device-tree projection | [`description.rhm`](description.rhm) |
| Concrete processor profiles and physical-address inputs shared with host generators | [`core-profiles.rhm`](core-profiles.rhm) |
| Core-neutral catalog of concrete RISC-V UDB configurations | [`udb.rhm`](udb.rhm) |
| Common RAM/MMIO host boundary | [`host-interface.rhdl`](host-interface.rhdl) |
| Shared single-core parameter derivation and direct composition | [`single-core-system.rhdl`](single-core-system.rhdl) |
| Shared host endpoint descriptions | [`endpoint-params.rhdl`](endpoint-params.rhdl) |
| Shared boot-address register, BootROM, ACLINT, PLIC, and UART windows, PMA, Home map, and UART boundary | [`peripherals.rhdl`](peripherals.rhdl) |
| Primary external-memory composition | [`simple-soc.rhdl`](simple-soc.rhdl) |
| Compact internal-memory composition | [`mini-soc.rhdl`](mini-soc.rhdl) |
| Tiled public entrypoint | [`tiled-soc/main.rhdl`](tiled-soc/main.rhdl) |
| Tiled layout and authoring form | [`tiled-soc/layout.rhm`](tiled-soc/layout.rhm) |
| Private tiled configuration compiler | [`tiled-soc/compile.rhdl`](tiled-soc/compile.rhdl) |
| Tiled time, ACLINT, and PLIC interrupt distribution overlay | [`tiled-soc/distribution.rhdl`](tiled-soc/distribution.rhdl) |
| Concrete tile implementations | [`tiled-soc/tiles/`](tiled-soc/tiles/) |
| Focused tests | [`tests/`](tests/) and [`Makefile`](Makefile) |

## Change a composition

1. Identify whether the change is reusable component behavior or concrete
   system policy. Move reusable behavior down to its owning core, protocol,
   NoC, or device package before composing it here.
2. Derive PMA and CHI Home routing from one physical-region description so an
   address cannot enter the fabric with contradictory policy.
3. Keep host RAM access coherent and route device access through the device
   Home using the shared `SoCHostInterface`. Derive host access services from
   the same memory and subordinate descriptions, with per-Home capability
   projections for the shared RN-F. Do not add a simulator mailbox or binary
   loader to synthesizable hardware.
4. For tiled changes, extend the author configuration and private compiler,
   then derive occurrence IDs, routes, family plans, and link assignments once.
   Do not expose a second author-managed compiled plan.
5. Keep routers owned by tiles or subsystems and physical-link connections
   owned by their parent. The generic NoC package does not instantiate a whole
   system wrapper.
6. Test pure configuration and topology compilation at the host level. Test
   connected hierarchy and device/core behavior through the executable
   simulator; do not duplicate it with exact module-name or instance-count
   assertions. Update [README.md](README.md) when observable ports, defaults,
   maps, topology, or supported systems change.

## Focused validation

Run SoC configuration and topology-compilation tests from the repository root:

```sh
make soc-test
```

This target also generates native DTBs for SimpleSoC, MiniSoC, and TiledSoC,
round-trips their inspection DTS through `dtc`, and checks architectural
properties with `fdtdump` and `fdtget`. It also checks that the same native DTB
bytes are finalized into each SoC's BootROM image. Generated artifacts remain
temporary.

The device-tree checks resolve UART interrupt-parent phandles and verify every
PLIC context against its hart's interrupt controller. Preserve context list
order: it identifies the PLIC's MMIO context indices, not a sortable CPU list.
The executable simulator smoke exercises UART TX-empty assertion, both hart-zero
external interrupt inputs, M/S claim arbitration, completion, and rearming.

The DTB subprocess uses `tools/run-racket.sh`: local runs rebuild changed
dependencies, while CI reuses its verified exact-commit bytecode artifact.
Keep this entrypoint in the root Makefile's compilation manifest.
`bash socs/tests/check-boundaries.sh` exercises import rejection and ensures
enumeration/search errors cannot silently pass the boundary audit.

`tests/main-memory-test.rhm` checks Ziccif/Ziccamoa memory-map preconditions in
MiniSoC, SimpleSoC, and TiledSoC. Cacheable regions must be coherent HN-F RAM
with execute, read, write, and atomic permissions and idempotent reads. The
disjoint sparse bank sets must cover all described RAM without gaps; do not
infer coverage from only the first and last address. BootROM and device HN-I
regions are outside these cacheable/coherent main-memory requirements.
The [RV5Stage validation guide](../cores/rv5stage/DEVELOPING.md#focused-validation)
owns the fetch, AMO, and coherence datapath checks and their coverage limits.

Use the package-local target while iterating:

```sh
make -C socs config-test
```

For tiled streaming adapters, run the focused behavioral fixtures:

```sh
FIXTURES='tiled-time tiled-distribution' bash tests/backend/run-circt.sh --simulate-only
```

These cover stalled time packets, latest-snapshot coalescing, complete timestamp
reconstruction, and independently backpressured time/interrupt routes. Preserve
the one-VC router's explicit availability signals when changing payload adapters.

The host target covers address maps, node policy, layouts, and compiled routing
plans. Connected hierarchy, time distribution, devices, and processor behavior
belong to the executable smoke tests in
[`../sims/DEVELOPING.md`](../sims/DEVELOPING.md).

For execution of the generated polling ROM, run:

```sh
FIXTURE=rv5stage-io-boot bash tests/backend/run-circt.sh --simulate-only
```

This cores-group CI fixture checks delayed entry publication, uncached fetch
and data contention, secondary-hart parking, and reset using the generated ROM.
