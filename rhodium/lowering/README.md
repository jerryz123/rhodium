<!-- Documents the program boundary and graph materialization contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Program materialization

[`program.rhm`](program.rhm) prepares an isolated, verified copy of an elaborated
RTL graph for backends and analyses. Both source and prepared graphs use core
`DesignElaboration`; direct Builder clients need no frontend or source text.
Contributors should read [DEVELOPING.md](DEVELOPING.md).

```rhombus
import:
  lib("rhodium/core/ir.rhm").DesignElaboration
  lib("rhodium/compile/program.rhm").compile_program
  lib("rhodium/backend/circt-target.rhm").circt_target

// design and top were constructed and finished through the core Builder.
def program = DesignElaboration(design, top)
def result = compile_program(program, circt_target)
```

`DesignElaboration(design, top)` records an explicit, finished top belonging to
the supplied design. Construction checks that boundary but does not certify
the whole design. Whole-design errors, including hierarchy cycles and
unfinished sibling modules, are rejected during compile-target preparation.

Pass this envelope to [`compile_program`](../compile/README.md) with an explicit
target. Concrete RTL targets validate the complete source, then copy only the
selected top's hierarchy into a fresh graph.
Reachable operations and metadata import needed DPI declarations; metadata
references outside the selected module closure fail explicitly. Preparation
leaves the source unchanged, including its sealing state. The materializer is
an internal compiler implementation, not a separate public compilation path.

All ordinary module operations, values, places, memory resources, DPI imports,
locations, and origins are copied into the destination. Metadata participates
through `ModuleMetadataPayload.remap_ir(remap)`: an implementation returns a new
payload and uses the supplied resolver for every IR or mutable nested reference.
Nested extension-owned objects implement the core `IRRemappable` interface.
Nominal, immutable descriptors such as interface types retain their identity.

The resolver covers values, places (including projections), operations, memories,
module boundaries, ports, instance views, and DPI declarations. Lists, maps, and
arrays are recursively remapped. One identity map covers the complete design,
so shared payloads and views stay shared within a result. Arrays and mutable
interface endpoint caches are independent across materializations. Metadata is
resolved after all copied module bodies exist, including later siblings.
Unsupported payloads, unresolved IR references, and cycles
in extension-owned metadata fail explicitly; metadata is never silently dropped.

Extension metadata may implement `MaterializationCheck`, extending
`ModuleMetadataPayload` with `check_materialized(module)`. Checks run after
all modules are finished and concrete core verification has sealed the design,
and are read-only; an exception prevents a materialization result from being returned. Sync circuit
metadata uses this protocol to rerun clock certification after copying.

Sync circuit metadata and ordinary interface/trace metadata support copying,
including grouped interfaces, Flow transforms, queue/storage controls, event
captures, and instance context.


Language users can construct a program with
[`elaborate(...)`](../frontend/README.md#circuits-and-elaboration).
Concrete graph consumers select [`rtl_target`](../compile/README.md#targets-and-compatibility)
through `compile_program`; emission consumers select their emission target on the original program.

Implementation ownership and validation are in [`DEVELOPING.md`](DEVELOPING.md).
