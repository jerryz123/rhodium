<!-- Documents the program boundary and concrete RTL materialization API. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Program materialization

[`program.rhm`](program.rhm) separates a completed hardware program from the
verified concrete RTL consumed by backends and analyses. It depends only on
the public core; direct Builder clients need no frontend or source text.

`ElaboratedProgram(design, top)` records an explicit, finished top belonging to
the supplied design. Construction checks that boundary but does not certify
the whole design. Whole-design errors, including hierarchy cycles and
unfinished sibling modules, are rejected by `materialize_rtl(program)`.

`materialize_rtl(program)` returns the existing core `DesignElaboration`,
containing `.design` and `.top`. Concrete-only programs retain the identity
path: verification seals the same design. A mixed program instead expands into
a fresh design on each call. The source program remains unsealed and unchanged;
instances never share state across independent materializations.

`ElaboratedProgram(design, top, providers)` accepts an immutable map from
`ConstructDefinition` objects to `ExpansionProvider` objects. A provider wraps
a function taking `(builder, definition)` and returning
`ExpansionResult(module, providers)`. The supplied Builder owns a fresh scratch
design. The result must belong to that design and match the declared signature.
Its optional provider map supplies nested constructs. Providers must capture
only immutable configuration, not live source IR or an old frontend context.

Expansion is cached per definition object within one materialization. There is
no global registry or cache. Conflicting providers and recursive expansion are
errors. `~expansion_limit` bounds the total number of distinct expansions
(default 256), including recursion that keeps inventing new definitions.
Source module names, including the selected top, are reserved. Expansion
modules receive deterministic suffixes when their names collide.

The first retained contract is pure combinational hardware with data ports and
explicit output-leaf dependencies. Expansion rejects hidden state or effects,
and checks that actual dependencies are a subset of the declared edges before
and after nested expansion. Missing edges are errors; conservative extra edges
can reject otherwise legal feedback at mixed-program verification.

All ordinary module operations, values, places, memory resources, DPI imports,
locations, and origins are copied into the destination. Metadata participates
through `ModuleMetadataPayload.remap_ir(remap)`: an implementation must return a
new payload using the supplied object resolver for IR references. Local values,
places, operations, memories, module boundaries, and imported DPI declarations
can be remapped. Unsupported payloads and unresolved cross-module references
fail explicitly; metadata is never shallow-copied or silently dropped. Sync
circuit port-name metadata supports copying. Existing interface/trace metadata
has not yet adopted this protocol.

```rhombus
import:
  lib("rhodium/lowering/program.rhm") open

// design and top were constructed and finished through the core Builder.
def program = ElaboratedProgram(design, top)
def rtl = materialize_rtl(program)
```

Language users can construct a program with
[`elaborate_program(...)`](../frontend/README.md#circuits-and-elaboration).
Existing `elaborate(...)` and `elaborate_with_top(...)` include materialization
and continue to return their original concrete result types.

Pass `rtl.design` or `rtl.top` to existing consumers as required by their APIs.
CIRCT emission and analyses do not accept a program envelope or execute
implementation providers. Retained children are
opt-in through the frontend `retained_circuit` API. Ordinary `CircuitReference`
bodies retain their eager instantiation behavior. Stateful retained constructs,
nominal grouped interfaces, and backend selection remain separate work.

Implementation ownership and validation are in [`DEVELOPING.md`](DEVELOPING.md).
