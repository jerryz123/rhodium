<!-- Tracks implementation gates for retained hardware constructs and selective lowering. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Selective lowering execution plan

Implement the architecture described in the
[selective Flow lowering proposal](https://gist.github.com/jerryz123/6b898a64e7635156028dc522469dd35c).
The decisive end-to-end gate is a cycle-exact mixed native/RTL design whose
selected queue's RTL implementation is never elaborated. Semantic metadata
alone does not satisfy that gate.

## Delivery and ownership

Continue in the isolated `ir-only` worktree. Keep generic public IR, frontend
staging, and portable Flow implementations in the IR PR; keep simulator
integration separable. Update the PR description to reflect tested behavior
and outstanding gates, not planned capabilities.

Core owns immutable construct/composition records, validation, and generic
selection. Frontend owns authoring and deferred implementation construction.
Flow owns queue semantics and its portable RTL body. Consumer-owned adapters
provide target lowerings. Core and frontend never import Flow or simulation.

## Execution gates

1. **Public construct and mixed composition contract.** Declare nominal
   versioned identities, immutable specialization parameters, typed ports,
   complete leaf-sensitive dependency summaries, clocks, reset and effects.
   Validate scoped wiring, drivers, cycles, core module dependencies, timing,
   and effect preservation. Test direct construction without frontend imports.
2. **Deferred elaboration.** Declare interfaces before implementation bodies
   execute. Select per occurrence; recursively expand unsupported constructs;
   diagnose ambiguity, non-progress, and unsupported terminals. Preserve
   ordinary authoring APIs and specialization isolation. Prove skipped bodies
   with expansion counters and failing sentinel providers.
3. **Portable queue.** Convert explicit `Queue` and configured `queue` to one
   semantic declaration. Preserve existing RTL, count, protocols, assertions,
   trace behavior, and SystemVerilog hierarchy/names. Test depths 1, 2, 3 and
   a larger depth, all pipe/flow options, scalar and aggregate payloads, with
   existing queue-options CIRCT/Verilator coverage. This is the IR PR gate.
4. **Shared simulation execution.** Native queue and generic RTL lower into
   one structured execution model with simultaneous state commit. Start with
   one clock domain and explicit unsupported-contract diagnostics. Mix native
   and expanded queues with ordinary control/arithmetic/registers; test state
   independence, traversal order, and cross-boundary loops.
5. **Differential correctness.** Compare direct native, fully expanded native,
   and CIRCT/Verilator paths before/after clock edges. Cover reset, pending
   reset, stalls, full/empty, bypass, pointer wrap, simultaneous transfers,
   seeded randomized traces, independent transaction scoreboards, and
   first-divergence reports. Define invalid/uninitialized payload observations
   explicitly. Never mask defined behavior to make comparisons pass.
6. **Nested and higher-order constructs.** Expand a parent composition while
   retaining supported children. Retain typed map-flow payload regions with
   explicit hardware captures and lower their computation generically. Test
   live captures, aggregate payloads, stalls, stability promises, and exactly
   once effects. Measure elaboration time, emitted size, and throughput only
   after correctness passes.

## Validation discipline

Use the smallest package-owned tests first. Each Racket/Rhombus validation
batch uses a fresh `PLTCOMPILEDROOTS`, without a trailing path separator, and
reuses that root within the batch. Direct Racket commands use `-y`.
Run frontend/profile, Flow, backend, and external queue fixture coverage when
the corresponding gates change those boundaries. New module/import changes
require dependency inventory, CI routing, boundary, and license-header checks.
Generated products stay untracked. Public contracts belong in owning READMEs;
implementation routing belongs in DEVELOPING guides.

## Current implementation evidence

Core provides construct/composition verification, recursive selection, and
occurrence-local resolution of `construct.apply` operations embedded in ordinary
module DFGs. Frontend signature declarations now precede deferred implementation
bodies. Flow Queue declares its signature independently of its existing portable
RTL body; explicit and configured APIs use the same construct identity.
Expansion bodies are cached by specialization identity within one resolution,
while target selection and lowering remain occurrence-local.

The frontend/core/analysis regression batch passed 2,318 checks, profile
coverage passed 30, Flow passed 591, and backend coverage passed 281. The
`queue-options` and `event-queue` CIRCT/Verilator simulations passed. These
results establish portable RTL and trace preservation, not native simulation
correctness. A fresh focused batch passed 280 checks and exercises skipped
bodies, per-occurrence selection, aggregate dependencies, and effect mappings.

Whole-design materialization passed 142 focused checks across recursive/core
construction and 32 Queue configurations. The backend regression suite passed
409 checks. The `materialized-queue` CIRCT/Verilator
fixture passed a 256-cycle pre/post-edge transaction scoreboard, including
invalid-cycle bypass payloads and pending reset. Materialization builds new core
modules with source-object maps. Signature-only implementation wrappers now
collapse under authored module names. A 272-check batch verifies collapse,
non-identity wiring, per-occurrence choices, source mappings, and per-module
CIRCT equivalence across 32 Queue configurations after alpha-renaming only
backend-generated SSA identifiers. The 256-cycle materialized Queue fixture
passed again; emitted SystemVerilog preserves the Queue/Counter hierarchy,
authored state/instance names, and counter assertion. Extension metadata reconstruction passes 293 focused
checks, including exact ordinary/materialized Queue manifests. The retained
event Queue CIRCT/Verilator fixture passes its functional and ancestry scoreboard.
The frontend/core/analysis regression passes 2,354 checks and frontend negative
cases. A separate 30-check batch passes exact manifest comparisons for Queue
and 13 transport examples, covering pipelines, arbitration, routing, forks,
joins, stalls, retained storage, and windows. After fixing array endpoint
reconstruction, the complete event regression passes 503 checks. The backend
continues to reject inputs not yet materialized.
Native runtime sources remain outside this IR-only branch; dependent execution
evidence is recorded below. Typed payload regions remain pending. Core control contracts accept direct ports, transparent wires,
and reset casts; a data-to-clock cast introduces a distinct domain and cannot
stand in for a declared boundary clock. Derived controls need explicit support.
The optional `SemanticNode` mechanism remains descriptive and is not the
executable construct protocol. The dependent branch establishes the initial mixed-design execution milestone.

## Next execution gates

- Implement typed payload regions with explicit captures.
- Extend effect/error and wide/vector coverage, and migrate the previous
  simulator regression suite.
- Measure elaboration, emitted size, memory, and throughput after correctness
  coverage for the higher-order path passes.

## Dependent native progress

The [separate native implementation](https://github.com/tianrui-wei/rhodium/commit/0ac2a77ff2ac6b8fbf38292c64315061102f6611)
selects Queue implementations by public construct identity before portable
expansion. Its `make sim-selective-test` entry point passes 158 host checks,
512-cycle pre/post-edge oracle replay across 16 scalar depth/pipe/flow
configurations, and 256-cycle independent-state replay for repeated instances
with direct, mixed, and expanded choices. Both interpreter and generated-C
execution pass. CIRCT/Verilator matches the same scalar vectors. The selected
portable Queue body executes zero times.

Native boundary verification now checks packed ranges, complete disjoint output
coverage, leaf-sensitive dependency containment, exact effect names, and Queue
clock/reset/query ABI requirements. Sixteen record-payload configurations with
legal cross-field feedback pass 256-cycle replay against an independent oracle,
including interpreter, generated C, and CIRCT/Verilator. Default-optimized native
models pass as well. Directed cases include reset-time payload writes, full
replacement, pending reset, and empty bypass. Payload queries preserve field
input dependencies while state capture continues to read the full payload.

Remaining gates include nested composition extraction, typed payload regions,
additional effect/error and wide/vector coverage, performance measurements, and
migration of the previous simulator suite. Boundary verification is not proof
of an arbitrary target implementation's semantics; differential evidence remains
required. No simulator implementation is added to this IR PR.

## Nested native execution follow-up

The [dependent nested extractor](https://github.com/tianrui-wei/rhodium/commit/fb97afb)
passes 112 checks across two composition levels and 16 Queue configurations.
Connections use individual leaves in reverse declaration order. Direct selection
skips the Queue body; portable fallback expands it once. Both paths pass
512-cycle pre/post-edge replay in the interpreter and generated C against the
independent mixed Queue/arithmetic/register oracle. A focused existing Queue,
aggregate, and repeated-occurrence regression passes 119 checks.

Remaining nested coverage includes differently selected siblings, aggregate
feedback across nested boundaries, and CIRCT/Verilator comparison of these new
fixtures. Typed payload regions and the other outstanding gates remain active.
The implementation stays on the dependent branch; this PR contains no simulator
runtime or compiler code.

## Nested differential follow-up

The [dependent validation follow-up](https://github.com/tianrui-wei/rhodium/commit/fa25a43)
passes 224 scalar/aggregate nested checks plus seven repeated-instance selection
checks. Two enclosing composition levels preserve legal record-field feedback.
Thirty-two materialized CIRCT/Verilator models match the same oracle as native
interpreter and generated-C execution: 512 cycles for scalar configurations and
256 for aggregate configurations, including default native optimization.
Repeated nested occurrences also pass 256-cycle independent-state replay for
direct, mixed, and expanded choices. These results close the nested validation
items listed above. Typed payload regions, broader effect/error and wide/vector
coverage, performance measurements, and simulator-suite migration remain open.

## Payload region foundation

Core now exposes `PayloadRegion`, `payload_region`, and
`verify_payload_region`. A region partitions every input of a pure core module
into typed arguments and explicit live capture ports. Its `CoreImplementation`
contract derives leaf dependencies from the body. Verification seals the design,
checks the complete input partition, rejects state/effects/control ports through
hierarchy, and checks explicitly supplied dependency contracts.

This is a computation representation, not completed higher-order retention.
Connecting regions to retained construct declarations, extracting frontend
captures, retaining `map_flow`, and testing changing captures under stalls
remain required before gate 6 is complete.

The focused payload/composition batch passes 37 checks, including explicit
capture coverage, pure hierarchical dependencies, state rejection, sealing, and
understated dependency diagnostics. Boundary, license, and CI-routing checks pass.
This evidence does not yet cover captured computation during native execution.

## Retained payload parameters

Verified payload regions can now appear in immutable construct parameters.
A dependency-neutral record and weak identity certificate registry avoid an
import cycle between declarations and full verification. Only successfully
verified regions are accepted; recursive expansion compares regions by identity.
Direct lowerings receive the region without running portable expansion. A
portable provider can return its existing core implementation, and materialization
preserves capture ports as live inputs. IR text exposes the module name and
argument/capture partition.

The focused payload/construct/composition batch passes 68 checks, including
uncertified-region rejection, direct selection without expansion, portable
selection, materialization, and readable IR. Boundary, license, and CI-routing
checks pass. Frontend capture extraction, retained `map_flow`, and changing-capture
execution under stalls remain required; this is still partial gate-6 progress.

## Scoped capture extraction

`capture_payload` copies a scoped pure computation into an independent verified
design and returns original argument/capture values for live binding. It
coalesces repeated captures, includes external results, copies pure child
modules, preserves aggregate dependencies and readable names, and rejects state,
resources, and writes escaping the selected scope. The source design remains
mutable. Aggregate place connections must be complete before extraction.

The frontend `payload_expansion` hook invokes its callback once. Ordinary mode
returns the original hardware result; retained mode also returns the extracted
region and live bindings. The focused core/frontend batch passes 48 checks,
including copied hierarchy, record-field dependencies, retained parameters, and
identical ordinary/retained hardware. Boundary, license, and CI-routing checks
pass. Retained `map_flow`, native execution with changing captures under stalls,
and the other gate-6 work remain unfinished.

## Retained Flow map

`map_flow` now emits `MapConstruct` in retained elaboration. Its parameters carry
the verified payload region and stable-mapping promise; operands carry payload,
live captures, valid, and ready. `MapExpansion` returns a scoped composition
with a generic computation child and direct handshake connections. Default
elaboration retains direct assignments and existing invalid-use diagnostics.
The frontend exposes `apply_construct` and composition records for inline
library providers; chained maps use distinct occurrence names.

The retained-map and existing flow-chain/static/frontend regressions pass 109
checks. The `retained-flow-map` CIRCT/Verilator fixture materializes the canonical
record mapper and passes 512 stimulus steps with a second capture-only change
at every step, including stalls and invalid input. This verifies live captures
on the portable SystemVerilog path. Native captured-map execution, mixed map/Queue
stateful replay, broader capture/effect/vector cases, and performance evidence
remain required. The simulator sources stay on the dependent branch.

The ordinary `flow-map --full` fixture also passes its exact SystemVerilog
reference comparison and existing simulation after the shared macro change.

## Dependent native captured-map evidence

The [native captured-map follow-up](https://github.com/tianrui-wei/rhodium/commit/ee3d88d)
merges the public region/capture/map implementation into the dependent simulator
branch. Maps before and after Queue capture the same independently advancing
register. Across 16 configurations, 128 new host checks establish retained
computations and skipped Queue expansion. Direct, expanded, and default-optimized
native models match a 512-cycle pre/post-edge oracle in interpreter and generated
C, and materialized CIRCT/Verilator matches the same vectors. Captures change on
every edge, including during stalls and pending reset.

The full native entry point passes 517 host checks and all scalar, aggregate,
nested, repeated-instance, and mapped runtime replays. This closes the initial
native captured-map milestone. Broader aggregate/vector captured mapping,
effects, additional higher-order constructs, performance measurements, and
previous simulator-suite migration remain open. Simulator sources remain outside
this IR-only PR.
