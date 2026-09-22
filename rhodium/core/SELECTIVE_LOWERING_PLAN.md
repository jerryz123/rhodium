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
Native integration, three-way differential simulation, and typed payload regions
remain pending. Core control contracts accept direct ports, transparent wires,
and reset casts; a data-to-clock cast introduces a distinct domain and cannot
stand in for a declared boundary clock. Derived controls need explicit support.
The optional `SemanticNode` mechanism remains descriptive and is not the
executable construct protocol. No end-to-end native milestone is claimed.

## Next execution gates

- Connect native Queue and generic RTL to the shared simulation schedule
  and run the three-way differential gate. The hardware-only fixture is not
  evidence for the native implementation.
