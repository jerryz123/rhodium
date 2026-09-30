<!-- Documents explicit target compilation, immutable artifacts, and occurrence-level lowering reports. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Compile a program

This package compiles an elaborated program through an explicitly supplied
target. CIRCT and clock analysis supply targets. Compilation returns in-memory
artifacts, a manifest, and optional structured findings; it does not write files or run external tools. Contributors should
read [DEVELOPING.md](DEVELOPING.md).

```rhombus
#lang rhombus
import:
  lib("rhodium/core/main.rhm") open
  lib("rhodium/lowering/program.rhm").ElaboratedProgram
  lib("rhodium/compile/program.rhm").compile_program
  lib("rhodium/backend/circt-target.rhm").circt_target

def design = Design()
def builder = Builder(design)
def top = builder.module("Identity")
def source = builder.input(top, "source", Bits(8))
builder.drive(builder.output(top, "result", Bits(8)), source)
builder.finish(top)
def compiled = compile_program(ElaboratedProgram(design, top), circt_target)
def mlir = compiled.artifacts[0].content
```

Direct Builder clients pass an
[`ElaboratedProgram`](../lowering/README.md) without using the frontend.
Targets are explicit objects, not names resolved through a global registry.
Importing the compiler does not load any backend.

The public compilation contract is:

```mermaid
flowchart LR
    Program["ElaboratedProgram<br/>source inventory and explicit top"] --> Compile["compile_program"]
    Target["Explicit CompilationTarget"] --> Compile
    Compile --> Prepare["Target preparation"]
    Prepare --> Plan["TargetPlan<br/>target-owned representation"]
    Plan --> Result["Validated CompilationResult<br/>manifest, artifacts, optional report"]
```

Preparation owns any expansion or lowering. The compiler collects the plan's
projections and returns them together only after artifact validation succeeds.

## Results and source lifetime

`compile_program(program, target, ~options: CompileOptions())` returns a
`CompilationResult` only after preparation and complete artifact emission
succeed. The default expansion limit is 256; `CompileOptions(limit)` selects
another positive limit. Provider, verification, and emitter exceptions propagate
without a silent fallback or a partial result. Artifact names must be unique
within the nonempty returned set.

The result contains:

- `target`: the selected target's name.
- `artifacts`: immutable `Artifact(name, media_type, content)` values. CIRCT
  returns one `<top>.mlir` artifact with media type `text/x-mlir`.
- `report`: an optional target-owned `CompilationReport`. Clock analysis returns
  a `ClockingReport`; CIRCT returns `#false`. Findings reference the target
  prepared graph, never source IR.
- `manifest.top`, `.modules`, and `.signature`: the emitted top name, ordered
  module names, and physical top ports. CIRCT preserves port names, directions,
  types, and order, so this signature is its public port map.
- `manifest.lowerings`: ordered `LoweringDecision` values containing source
  instance path, detached construct definition (identity, revision, parameters,
  and contract), action, reason, emitted implementation name, location, and
  origin. Paths are relative to the selected top. A retained top has the empty
  path and no instance location. Shared expansions still have separate entries
  for each occurrence.

The CIRCT target creates a fresh, verified concrete graph, including for an
already concrete program. It does not seal or modify the input design. Its
module inventory is the selected top's reachable hierarchy. Unused providers
are not called, and unused modules and DPI declarations are not emitted.
The entire source design still receives structural verification, so malformed
unused modules remain errors. Retained provider bodies receive the existing
signature, dependency, state, and metadata checks.

Metadata must be remappable within the selected closure. A live metadata
reference outside that closure is diagnosed rather than silently discarded.
Target preparation and expansion caches are local to each invocation.

## Targets and compatibility

[`contracts.rhm`](contracts.rhm) defines `CompilationTarget(name, prepare)` and
`TargetPlan`. A target's preparation function receives `(program, options)` and
returns a plan implementing `manifest()` and `emit()`. Its optional `report()`
method returns structured findings and defaults to `#false`. The target owns its
prepared representation and must validate it before returning artifacts.
These are trusted extension interfaces: target implementations must preserve
the source and keep emission free of publication side effects.

[`rtl.rhm`](rtl.rhm) supplies `prepare_rtl(program, options, reason)` for targets
requiring concrete RTL and re-exports `ElaboratedProgram` for target input
annotations. It returns a `PreparedRTL` containing `.rtl` and
`.manifest`. CIRCT uses this helper and expands every reachable retained
construct through its portable implementation. A future target can supply
its own plan without first erasing retained constructs.

Use the [clock-analysis target](../analysis/README.md) for temporal reports or
strict CDC checking; it expands retained constructs for concrete provenance.

All public program compilation goes through `compile_program` with an explicit
target. Materialization and textual emission are implementation steps owned by
the target. Direct construct adapters, capability negotiation, force-expansion
selection, and other targets are subsequent work.
