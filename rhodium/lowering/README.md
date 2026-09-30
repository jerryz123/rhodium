<!-- Documents the program boundary and portable expansion contracts. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Program materialization

[`program.rhm`](program.rhm) separates a completed hardware program from the
verified concrete RTL consumed by backends and analyses. It depends only on
the public core; direct Builder clients need no frontend or source text.

`ElaboratedProgram(design, top)` records an explicit, finished top belonging to
the supplied design, or a detached `ConstructDefinition` top with an entry in
`providers`. A retained top expands directly to the selected concrete module
without an extra wrapper. Construction checks that boundary but does not certify
the whole design. Whole-design errors, including hierarchy cycles and
unfinished sibling modules, are rejected during compile-target preparation.

Pass this envelope to [`compile_program`](../compile/README.md) with an explicit
target. Concrete RTL targets validate the complete source, then copy only the
selected top's hierarchy into a fresh graph. Unused providers are not invoked.
Reachable operations and metadata import needed DPI declarations; metadata
references outside the selected module closure fail explicitly. Preparation
leaves the source unchanged, including its sealing state. The materializer is
an internal compiler implementation, not a separate public compilation path.

`ElaboratedProgram(design, top, providers)` accepts an immutable map from
`ConstructDefinition` objects to `ExpansionProvider` objects. A provider wraps
a function taking `(builder, definition)` and returning
`ExpansionResult(module, providers)`. The supplied Builder owns a fresh scratch
design. The result must belong to that design and match the declared signature.
Its optional provider map supplies nested constructs. Providers must capture
only immutable configuration, not live source IR or an old frontend context.

Expansion is cached per definition object within one materialization. There is
no global registry or cache. `ExpansionProvider(expand, checks)` optionally carries
read-only checks of the finished concrete module. Registrations with the same
expansion callback merge their checks; distinct callbacks for one definition
conflict. All merged checks run before returning, even when registered after
the shared body was expanded. Conflicting providers and recursive expansion are
errors. The compiler's `CompileOptions(expansion_limit)` bounds the total number of distinct expansions
(default 256), including recursion that keeps inventing new definitions.
Reachable source module names, including the selected top, are reserved. Expansion
modules receive deterministic suffixes when their names collide.

Retained definitions declare either pure combinational hardware or
[`SingleClockState`](../core/README.md#retained-constructs), which permits
registers under explicit clock/reset inputs. Expansion validates the state
permission and controls transitively through ordinary and retained children.
It accepts resetless registers, structural clock aliases, and synchronous scoped
resets that OR additional conditions with the declared reset. The independent
`~async_read_memory` and `~clocked_assertions` permissions admit existing async-read
memory with clocked writes and reset-suppressed clocked assertions respectively.
Memory writes must use the declared clock; assertions must use both the declared
clock and a reset preserving the declared reset. Child permissions cannot exceed
the parent's. Undeclared effects are rejected before a nested provider runs.
Memory has no implicit reset behavior: contents persist and writes follow their
explicit enable even during reset. Assertion labels and source attribution are
preserved through copying; no checks are removed by an effect permission.

Actual combinational output-leaf dependencies must be a subset of the declared
edges before and after nested expansion, including for stateful definitions.
Missing edges are errors; conservative extra edges can reject otherwise legal
feedback at mixed-program verification.

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
resolved after all copied module bodies exist, including later siblings and
portable expansions. Unsupported payloads, unresolved IR references, and cycles
in extension-owned metadata fail explicitly; metadata is never silently dropped.

Extension metadata may implement `MaterializationCheck`, extending
`ModuleMetadataPayload` with `check_materialized(module)`. Checks run after
all modules are finished and concrete core verification has sealed the design,
for both concrete and retained program inputs. They are read-only;
an exception prevents a materialization result from being returned. Sync circuit
metadata uses this protocol to rerun clock certification after expansion.

Sync circuit metadata and ordinary interface/trace metadata support copying,
including grouped interfaces, Flow transforms, queue/storage controls, event
captures, and instance context. This preserves ordinary and expanded Flow
circuits in a mixed program. Retained construct semantics come from explicit
definitions rather than being inferred from metadata.

```rhombus
import:
  lib("rhodium/lowering/program.rhm") open
  lib("rhodium/compile/program.rhm").compile_program
  lib("rhodium/backend/circt-target.rhm").circt_target

// design and top were constructed and finished through the core Builder.
def program = ElaboratedProgram(design, top)
def result = compile_program(program, circt_target)
```

Language users can construct a program with
[`elaborate_program(...)`](../frontend/README.md#circuits-and-elaboration).
The eager `elaborate(...)` and `elaborate_with_top(...)` construction helpers
return verified concrete IR. Use the program API to select a compilation target.
Compile targets own provider execution and consumer preparation. Retained children are
opt-in through the frontend `retained_circuit` API. Ordinary `CircuitReference`
bodies retain their eager instantiation behavior. Frontend [detached interface declarations](../frontend/layers/README.md#detached-interface-declarations)
provide grouped retained members without changing core physical signatures.
Additional effect contracts remain separate work.

Implementation ownership and validation are in [`DEVELOPING.md`](DEVELOPING.md).
