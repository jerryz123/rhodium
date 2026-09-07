<!-- Defines the dependency and behavioral contract for NoC-specific Rhodium hardware. -->

# NoC RTL

This directory contains the hardware consumers of the pure NoC model. It may
depend on public `#lang rhodium` facilities and reusable Rhodium standard-library
primitives, but the pure `noc/model`, `analysis`, `authoring`, `language`,
`plan`, and `std` directories never depend on this package.

The [pure NoC package](../README.md) owns topology, routing, validation, and
planning. This package starts at `RouterPlan` or `RouterFamilyPlan`; it cannot
construct a proof or turn an unchecked relation into hardware.

Contributors changing the hardware realization should read
[`DEVELOPING.md`](DEVELOPING.md).

## Component map

| File | Responsibility |
| --- | --- |
| `route-computer.rhdl` | Compile validated local or family route rows into typed combinational lookup hardware |
| `route-adapter.rhdl` | Add and remove route metadata at protocol-neutral ready-valid boundaries |
| `allocator.rhdl` | Match input requests to output targets while realizing fallback obligations |
| `router.rhdl` | Buffer and switch complete single-beat packets, including uniform router families |
| `wormhole-router.rhdl` | Reserve a selected target from packet head through tail |
| `main.rhdl` | Export the public NoC RTL surface |

## Route decoding

`route-computer.rhdl` defines `RouteDecoder`, `route_decoder_lookup`,
`compile_route_decoder`, and `RouteComputer`. `compile_route_decoder` accepts
only a `RouterPlan` projected from opaque `ValidatedRouting`; it cannot consume
an unchecked relation or rerun topology, reachability, dependency, or deadlock
analysis. Route keys retain their global stable encoding, while origin keys
and outgoing-VC masks are local to one router. The decode relation uses a typed
`RouteLookupKey` input and a typed `RouteDecision` output with a unified
`target_mask`, its certified `fallback_mask` subset, and a Boolean `valid`
field. Whole-graph-acyclic plans mark every legal target as fallback.
Escape-certified plans mark only escape VCs and exact local ejection targets.
The receiver-explicit `route_decoder_lookup` function accepts an explicitly
declared typed selector wire and preserves those decoder-dependent exact types
at its boundary. Compilation returns an opaque `RouteDecoder` that stores only
the plan, derived decode widths, and compiled `DecodeGen`; route mappings and
rows remain owned by `RouterPlan`. `DecodeGen` alone owns any flattening needed
by CIRCT;
neither the route computer nor router logic concatenates selector fields or
slices a packed decision representation.

### Protocol-neutral route adapters

`route-adapter.rhdl` contains the protocol-neutral ready-valid boundary around
that metadata. `RoutedFlowInjector` adds a compiled route key and permits a
transfer only for a valid route decision; `RoutedFlowEjector` removes the
`RoutedBeat` envelope. Protocol packages remain responsible only for selecting
their destination field and checking endpoint identity.
`bind_routed_flow_ejector` instantiates one `RoutedFlowEjector` between supplied
ready-valid endpoints, preserving combinational payload and backpressure
behavior without adding buffering or protocol identity checks.

### Shared family decoding

For shared physical implementations, `RouterFamilyRouteDecoder` adds a
`site_key` field ahead of the same route and origin lookup. Its rows come only
from `RouterFamilyPlan`; the site key is intended to be tied to a static value
at each occurrence and is not a runtime routing mode. One typed `DecodeGen`
therefore represents all site-specialized tables without reimplementing decode
lowering or graph reasoning in RTL.

## Allocation

`allocator.rhdl` defines the NoC-specific `RouterAllocator` around the generic
standard-library `OutputGreedyRoundRobinMatcher`. Every output rotates its
input priority only after an actual transfer. For escape-certified rows, an
adaptive target is eligible only when its separate `target_available` sideband
says that the VC can be acquired immediately; otherwise the input continuously
requests its fallback targets. The sideband describes resource availability
independently of ready/valid selection. A physical VC link therefore supplies
per-VC readiness from the standard `VcDemux`, not the selected mux ingress's
ready signal. This separation avoids a valid-to-ready arbitration loop.

## Single-beat router

`router.rhdl` defines `RoutedBeat`, opaque `SimpleRouterConfig`,
`compile_simple_router`, and `SimpleRouter`. Compilation checks the supported
proof regime, compiles the route decoder, and freezes the hardware port and
target shape. `compile_simple_router` also accepts a uniform positive
`input_buffer_depth`, defaulting to one. One beat is one complete packet. Each
local origin has one standard `Queue` of that depth; bypass (`flow`) and
same-cycle replacement (`pipe`) remain disabled so the registered timing is
preserved. Route decisions form the NoC-specific request matrix. A parallel
flow handle connects the ingress array through those queues, and the
endpoint-first `grant_crossbar` helper connects their outputs directly to
the nested egress endpoint of each `SimpleRouterTarget`. That interface couples
one ready-valid egress with the downstream-owned availability indication used
by adaptive allocation, instead of exposing unrelated top-level ports. The
allocator remains an explicit sideband controller: it observes requests,
fallback classification, availability, and completed transfers but does not
pretend to consume or forward payload flow.
A NoC-specific ingress monitor checks every externally offered route key
against the physical input's origin key before buffering. Outgoing VCs are the
first targets and every local ejection terminal follows. The router contains
no singular ejection convention, so a linkless plan with several terminals is
an ordinary many-input, many-output crossbar instance.

### Uniform router families

`compile_simple_router_family` and `SimpleRouterFamily` provide the uniform
counterpart. It accepts the same uniform `input_buffer_depth`; every occurrence
in one family necessarily shares that physical queue implementation. All
occurrences have the family maxima for their ingress and target arrays and
differ only in the constant `site_key` connection. Defining the generated
module once and passing that module value to several `inst` forms produces one
shared module definition with multiple occurrences. The pure family plan
supplies the remapped connection indices; this RTL package still does not
instantiate the network or assume that its routers share a parent module.
`bind_simple_router_family_site` drives one occurrence's constant site key and
closes only its family-padding inputs and targets. Local endpoint attachment and
physical-link wiring remain explicit responsibilities of the owning subsystem.
`bind_router_plane` wires supplied direct ready-valid physical-link arrays into
the family's physical slots and drives the supplied static site key. It ties
each physical target's availability to its egress readiness; VC links with
independent availability need their own wiring. `close_router_local_ports`
disables unattached local injection and ejection slots, separately from family
padding. Its connected-input indices refer to the local injection prefix;
connected-target indices are absolute target-array indices, with local
ejections starting at `family.egress_count`. Neither helper instantiates
routers or chooses topology or hierarchy.

`RouterFamilyPhysicalPlan`, in the pure planning package, projects the one
canonical ordered physical-link shape shared by several independently compiled
route families. It rejects a channel family whose per-site physical ingress,
egress, or link ordering differs, while leaving each channel's routes, route
keys, and local terminal slots independent.

## Runtime representation

Router runtime collections are Rhodium `Vec` values, not host lists of hardware
objects. Route-decision fields and request/grant bits therefore compose through
ordinary field projection and indexing instead of explicit packed extraction.

## Wormhole router

`wormhole-router.rhdl` defines the sibling `WormholeRouterConfig`,
`compile_wormhole_router`, and `WormholeRouter` transport over the standard
`VariableFlit(RoutedBeat(T, route_width))` composition. `VariableFlit` owns
packet boundaries while `RoutedBeat` owns the NoC route key and opaque
application payload. A head fragment is the only fragment whose route key is decoded.
After the head transfers, the selected output VC or local ejection target is
owned by that input until its tail transfers. Body and tail fragments therefore
cannot be rerouted or interleaved with a different packet on the reserved VC.
`compile_wormhole_router` accepts the same uniform positive
`input_buffer_depth`, also defaulting to one.

## Physical links and virtual channels

The standard `VcMux` and `VcDemux` keep logical VC routing distinct from
physical-link sharing. `VcMux` fairly selects at most one logical VC payload
per cycle and adds its link-local VC index; `VcDemux` validates that index and
delivers the payload to the corresponding downstream VC. The demultiplexer also
returns a readiness bit for every VC, allowing the multiplexer to select a
ready VC instead of letting one blocked VC stall the entire link. Link
arbitration is per beat and retains no physical-link resource across cycles, so
the only retained network resources remain the VCs represented in the
validated dependency graph. The ready-valid physical link plus per-VC reverse
readiness is the initial same-clock realization; an eventual credited or
pipelined link adapter must preserve the same logical VC acceptance contract.

Protocol packetization remains outside `noc/rtl`. A protocol adapter may map a
wide object such as a CHI flit into any finite head/body/tail sequence and
reassemble it after ejection without changing topology, routing, or VC
analysis. CHI link credits and internal physical-link credits remain separate
protocol layers.

## Proof obligations realized in hardware

Both router implementations accept whole-graph-acyclic and escape-certified
plans. The escape contract is realized by persistent fallback requests and
transfer-based round-robin rotation. Adaptive outputs are considered only
when their independent availability sideband permits an immediate acquisition;
ejection is always a fallback because it releases the held network resource.
This implements the certificate's allocator obligation, not a broader claim
of protocol progress, livelock freedom, or fairness through unmodeled shared
resources.

## Hierarchy and integration ownership

There is intentionally no whole-network circuit or router-instantiating
network helper in this package. Real routers live in independently owned tile,
switch, or subsystem modules and may be separated by arbitrary hierarchy.
Integration code obtains each node's `RouterPlan` from the pure `NetworkPlan`,
compiles only its selected local router configuration, and exposes the VC or
physical-link ports appropriate to its own boundary. A user-owned parent
connects those boundaries from the corresponding `NetworkPlan` assignments;
`noc/rtl` neither owns the system hierarchy nor inserts a wrapper around the
complete transport. The hierarchical wormhole fixture follows this pattern:
source, transit, and destination subsystems own their routers and physical-link
mux/demux logic, while their parent connects link payloads and reverse per-VC
availability.

## Executable examples

The focused executable hardware examples live under `examples/noc/`. They
import this domain package directly; only their reusable matching and crossbar
mechanisms come from `rhodium/std`.

[`examples/noc/wormhole-router-diagram.rhdl`](../../examples/noc/wormhole-router-diagram.rhdl)
projects the checked-in phased-XY routing relation onto a corner router and
extracts a logical flow diagram. Generate its JSON and Graphviz DOT files with:

```sh
mkdir -p /tmp/noc-router-diagram
env PLTCOMPILEDROOTS="$(mktemp -d)" \
  racket -y -S "$PWD" tools/write-noc-router-diagram.rhm \
  /tmp/noc-router-diagram
```

Contributor test selection and backend fixture ownership are documented in
[`DEVELOPING.md`](DEVELOPING.md#focused-validation).
