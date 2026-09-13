<!-- Defines RFPL as physical annotation over an existing Rhodium circuit hierarchy. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RFPL physical-annotation plan

## Status

RFPL's initial annotation vertical slice is implemented. `#lang rfpl` consumes
an already elaborated Rhodium design, classifies selected circuits as hard macros
or composite floorplans, assigns exact rectangular dimensions, and attaches a
coordinate to every direct instance in a composite floorplan.

RFPL no longer authors ports, instances, wiring, or modules. Those are written
once in Rhodium. RFPL verifies the physical interpretation of that existing
logical hierarchy and retains it as backend-independent metadata.

Strap-pin physical specialization remains the next milestone.

## Decision

RFPL is a physical-view language over Rhodium, not a second hardware-description
language. Its input is an explicit Rhodium `DesignElaboration` containing a
verified `Design` and stable top `Module`. Its output is a `FloorplanDesign`
that references the same logical modules and instance operations.

RFPL never creates an Rhodium `Module` or `rtl.instance` operation. Consequently,
annotating a design does not change its Rhodium IR, CIRCT MLIR, or generated
Verilog.

## Physical roles

Every circuit reachable in the physical hierarchy has exactly one physical
role in the initial cut.

### Hard macro

A `HardMacro` is a physical leaf:

- It references one finished Rhodium `Module`.
- It has one exact rectangular outline.
- Its logical body may contain any operation legal in Rhodium.
- RFPL does not traverse or place its logical descendants.

Hard-macro status means physical opacity, not absence of logic. A synthesized
RTL block, an implementation hierarchy, and a future imported black box can
all be treated as one physical leaf.

### Composite floorplan

A `CompositeFloorplan` is a transparent physical assembly:

- It references one finished Rhodium `Module`.
- It has one exact rectangular outline.
- Its module body contains only ports, instances, and wiring.
- Every direct logical instance has exactly one physical placement.
- Every placed child has a hard-macro or composite-floorplan view.

A composite floorplan cannot contain constants, operators, registers,
memories, assertions, simulation effects, or other logic. Logic belongs in an
Rhodium circuit annotated as a hard macro.

### Ordinary circuit

An ordinary Rhodium circuit has no RFPL view. It may exist outside the selected
physical tree or below a physically opaque hard macro. It cannot be a direct
child of a composite floorplan, because every direct child of a composite must
have a placement and physical view.

This is the physical meaning of “floorplans can only be inside floorplans.”
The selected physical top is the sole root exception; every other physical
view is reached through a placement owned by a composite floorplan.

## Data model

The initial metadata objects are:

- `Length(picometers)` — exact, nonnegative physical distance.
- `RectOutline(width, height)` — positive rectangular dimensions.
- `Coordinate(x, y)` — nonnegative lower-left placement coordinate.
- `HardMacro(module_def, outline)` — opaque physical view of one module.
- `CompositeFloorplan(module_def, outline, placements)` — transparent physical
  view of one structural module.
- `FloorplanPlacement(parent_module, child, instance, coordinate)` — physical
  interpretation of one existing direct `rtl.instance` operation.
- `FloorplanDesign(logical, top, views, placements)` — verified physical tree
  over one `DesignElaboration`.

Dimensions belong to a physical view of a module. Coordinates belong to an
instance operation in its parent module. Reusing one composite module stamps
the same internal arrangement; each occurrence of that module has its own
coordinate in its parent.

The initial cut permits only one physical-view object for a given logical
module in one `FloorplanDesign`. Explicit variants arrive with strap-pin
specialization.

## Rhodium elaboration and inspection

Rhodium provides two elaboration forms:

- `elaborate(top)` retains the established result of a bare `Design`.
- `elaborate_with_top(top)` returns `DesignElaboration(design, top)`.

RFPL requires the latter. It never guesses the top from module ordering.
`Module.find_instance(name)` and RFPL's checked `child_instance(module, name)`
return the stable `rtl.instance` operation used as a placement identity.

The Rhodium core, frontend, and backend do not import RFPL. RFPL depends only on
the backend-independent public Rhodium IR.

## Initial authoring surface

`#lang rfpl` exports ordinary Rhombus plus:

- `hard_macro(module, width: ..., height: ...)`
- `floorplan(module, width: ..., height: ..., placements: [...])`
- `child_instance(module, name)`
- `instance_target(instance)`
- `place(instance, child_view, at: (x, y))`
- `annotate(logical_elaboration, top_view)`
- The physical metadata classes and `nm`/`um` constructors

It does not export Rhodium circuit, port, instance-construction, connection, or
logic forms.

The intended split is:

```rhombus
// blocks.rhdl
#lang rhodium

circuit Adder(width):
  input(a, b): Bits(width)
  output sum: Bits(width)
  sum <== a + b

circuit Pair(width):
  input(a, b): Bits(width)
  output(sum0, sum1): Bits(width)
  def adder = Adder(width)
  inst left(adder)
  inst right(adder)
  left.a <== a
  left.b <== b
  right.a <== b
  right.b <== a
  sum0 <== left.sum
  sum1 <== right.sum

def logical = elaborate_with_top(Pair(8))
export logical
```

```rhombus
// pair.rfpl
#lang rfpl

import:
  "blocks.rhdl" open

def left = child_instance(logical.top, "left")
def right = child_instance(logical.top, "right")
def adder_macro = hard_macro(
  instance_target(left),
  width: um(40),
  height: um(20)
)
def pair_floorplan = floorplan(
  logical.top,
  width: um(100),
  height: um(50),
  placements: [
    place(left, adder_macro, at: (um(0), um(0))),
    place(right, adder_macro, at: (um(50), um(0)))
  ]
)
def physical = annotate(logical, pair_floorplan)
```

## Verification

RFPL validates the following invariants:

- The logical input is a verified `DesignElaboration`.
- The physical top references the explicit logical top.
- Every physical view references a finished module in that logical design.
- One logical module has at most one physical view in the initial cut.
- A hard macro has a positive outline and otherwise imposes no RTL-body
  restriction.
- A composite module contains only `rtl.input_port`, `rtl.output_port`,
  `rtl.instance`, `rtl.drive`, and optional `rtl.wire` operations.
- Every direct instance of a composite appears in exactly one placement.
- A placement's child view references the instance's actual target module.
- Every coordinate is exact, unit-bearing, and nonnegative.
- Every child rectangle fits completely inside its parent rectangle.

Rhodium verification remains authoritative for types, ownership, complete
driving, hierarchy cycles, combinational cycles, and all logical semantics.
RFPL verifies only the additional physical-view contract.

Sibling overlap, orientation, halos, placement grids, routing, and automatic
placement remain deferred.

## CIRCT and Verilog contract

Both hard macros and composite floorplans already have ordinary Rhodium modules.
The existing CIRCT backend therefore emits their existing `hw.module` and
`hw.instance` operations without RFPL-specific lowering.

RFPL metadata does not become ports, constants, logic, or arbitrary backend
text. The same logical `Design` emitted before and after annotation must
produce identical CIRCT and Verilog. A future physical backend may project the
metadata into a sidecar, CIRCT attributes, or a physical dialect when a real
consumer establishes that contract.

## Strap pins

A strap pin is a static physical specialization input. It de-uniquifies a
stamped sub-floorplan; it is not an Rhodium runtime port and is unrelated to
power-grid straps.

The annotation model makes the intended specialization key explicit:

```text
(logical module identity, canonical strap bindings) -> physical view variant
```

Equal bindings may reuse one physical variant. Different bindings select
distinct physical variants with their own dimensions or descendant placement
metadata while continuing to reference the same logical module when possible.
If a downstream backend requires distinct module symbols, RFPL may create
aliases or clones during physical export rather than changing Rhodium authorship.

Straps must not alter logical wiring or behavior. A value that changes logic
is an Rhodium generator parameter, not an RFPL strap.

## Repository shape

```text
rfpl/
  PLAN.md
  main.rkt
  language.rhm
  frontend/
    foundation.rhm
    verify.rhm
  tests/
    structural-test.rhm
    run-negative.sh
    run-circt.sh
    invalid/
    support/
examples/
  rfpl/
    circuit-pair.rhdl
    circuit-pair.rfpl
```

RFPL needs no hardware core or CIRCT backend in the initial cut because it
does not own hardware semantics or alter hardware emission.

## Milestones

### Milestone 1: explicit logical top — implemented

- Add `DesignElaboration(design, top)`.
- Add `elaborate_with_top` without changing `elaborate` compatibility.
- Add stable direct-instance lookup.

### Milestone 2: physical roles — implemented

- Annotate arbitrary logic-bearing modules as hard macros.
- Annotate wiring-only modules as composite floorplans.
- Remove RFPL module, port, wiring, and instance construction.

### Milestone 3: geometry and hierarchy — implemented

- Retain exact dimensions on views and coordinates on instance operations.
- Require complete direct-child placement in composites.
- Verify target identity, physical closure, unique views, and containment.

### Milestone 4: backend stability — implemented

- Demonstrate that RFPL adds no modules or instances.
- Preserve the example-owned Verilog golden unchanged.
- Keep all physical metadata out of CIRCT output.

### Milestone 5: strap specialization

- Define closed strap types and canonical equality.
- Bind straps at placement sites.
- Create and reuse physical variants deterministically.
- Retain provenance explaining each de-uniquification.

## Non-goals

The initial cut does not include nonrectangular geometry, orientation,
sibling-overlap checking, automatic placement, technology grids, pin shapes,
regions, blockages, halos, power distribution, DEF/LEF/OpenDB/OpenROAD export,
timing, routing, clock-tree synthesis, or physical module cloning.
