<!-- Plans static strap specialization for RFPL physical views over existing Rhodium designs. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RFPL next milestone: strap specialization

RFPL's initial physical-annotation slice is implemented. The [README](README.md)
owns its public hard-macro, composite-floorplan, geometry, and annotation
contracts; the [development guide](DEVELOPING.md) owns implementation and
validation. This plan covers the remaining strap-specialization milestone.

## Boundary

A strap pin is a static physical specialization input, not a Rhodium runtime
port or a power-grid strap. It may select physical dimensions and descendant
placements, but cannot alter logical wiring or behavior. A value that changes
logic belongs in a Rhodium generator parameter instead.

RFPL must keep referencing the original verified `DesignElaboration`. It must
not create or mutate Rhodium modules, instances, ports, or operations, and
ordinary CIRCT/Verilog emission of that logical design must remain unchanged.
Any physical-export aliases or clones belong to a later downstream stage.

## Work

1. Define a small closed set of strap value types and canonical equality.
   Reject unsupported values and ambiguous or incomplete bindings rather than
   making host-object identity part of specialization.
2. Add strap declarations to physical-view templates and bind them at placement
   sites. Check binding names and types, including nested composite views.
3. Resolve each view variant from its logical module and canonical bindings.
   Equal bindings must reuse one physical variant; different bindings may
   produce different geometry or descendant placement metadata without
   changing the logical module.
4. Retain provenance for every resolved variant and placement so a consumer can
   explain which binding caused de-uniquification.

The intended specialization key is the logical module identity plus canonical
resolved bindings. Each resolved view still checks the target identity of its
existing logical instance.

## Acceptance

- Structural tests cover equal-binding reuse, different-binding variants,
  nested binding propagation, incomplete or wrong-typed bindings, and stable
  variant/provenance order.
- Existing placement completeness, target identity, geometry, and containment
  checks apply to each resolved variant.
- The retained logical design emits identical CIRCT and Verilog before and
  after annotation, including when a logical module has multiple physical
  variants.

Orientation, overlap, routing, PDN, technology grids, timing, and physical
export remain outside this milestone.
