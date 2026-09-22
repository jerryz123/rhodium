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

The evidence sections below are chronological; their pending-work statements
record the scope at that point. This list is the current remaining scope:

- Complete effect/error coverage, preserving assertions and exactly-once effects
  across direct selection and portable expansion.
- Migrate the previous simulator regression suite, including core-only inputs.
- Measure elaboration and compilation time, emitted size, peak memory, and
  throughput for identical direct and expanded workloads after correctness.

Typed payload regions, frontend capture extraction, retained maps, nested native
composition, and initial captured-map execution are implemented; see the later
evidence sections. Completion requires the remaining gates above, not just the
initial mixed Queue milestone.

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

## Captured record feedback follow-up

The [dependent record-map validation](https://github.com/tianrui-wei/rhodium/commit/27ff886)
adds a retained record-producing map with live cross-field Queue feedback.
Sixteen configurations pass 64 positive host checks; the final 65-check record
batch also rejects same-field feedback when empty bypass creates a combinational
cycle. A combined scalar/record batch passes 192 checks. Thirty-two materialized
CIRCT/Verilator models match interpreter and generated-C execution against the
independent oracles, including default optimization: 256 cycles for record
feedback and 512 for scalar captures. The native runner now includes mapped
record models. These focused results do not claim a fresh full-suite run.

Vector/multiword captures, effects, pipes, performance, and previous simulator
regression migration remain open. Native source remains on the dependent branch.

## Captured vector feedback follow-up

The [dependent vector-map validation](https://github.com/tianrui-wei/rhodium/commit/ea08e83)
passes 130 shared record/vector host checks, including genuine bypass-cycle
rejection. Sixteen two-element vector Queue configurations pass 256-cycle
pre/post-edge replay against an independent oracle in interpreter, generated C,
and materialized CIRCT/Verilator, including default native optimization. The
existing scalar and record native replays also pass after the runner change.
Multiword and nested aggregate captures, effects, retained pipes, performance,
and previous simulator-suite migration remain open.

## Captured multiword feedback follow-up

The [dependent multiword validation](https://github.com/tianrui-wei/rhodium/commit/6134de7)
passes 195 record/vector/multiword host checks, including genuine cycle rejection.
Two 65-bit vector elements span three runtime words, with an unaligned second
element. All 16 Queue configurations pass 256-cycle pre/post-edge oracle replay
in interpreter, generated C, and materialized CIRCT/Verilator, including default
optimization. Every payload bit is observed; directed values exercise 64-bit
carry and 65-bit wraparound alongside random upper bits. Narrow-vector replay
also passes with the generalized oracle. Nested aggregates, effects, pipes,
performance, and previous simulator-suite migration remain open.

## Captured nested aggregate and complete-suite follow-up

The [dependent nested aggregate validation](https://github.com/tianrui-wei/rhodium/commit/12786b0)
retains a record containing a vector of 65-bit records. All three aggregate
boundaries preserve live capture dependencies. The four-shape matrix passes 260
host checks, including genuine-cycle rejection; sixteen nested configurations
pass 256-cycle pre/post-edge replay in interpreter, generated C, and materialized
CIRCT/Verilator, including optimization and observation of every payload bit.

The complete updated `make sim-selective-test` passes 777 host checks and all
base, nested-composition, repeated-instance, captured scalar/record/vector,
multiword, and nested-aggregate interpreter/generated-C replays. Verilator is
validated separately; it is not part of that host entry point. Remaining work is
effect/error preservation, retained pipes, previous simulator regression migration,
and performance measurements. Simulator implementation remains on its dependent
branch.

## Retained pipe declarations and portable expansion

Flow now exports nominal identities and deferred providers for `Pipe`,
`ValidPipe`, `ValidPipeAlwaysCapture`, and `CtrlPipe`. Their signatures retain
stage count, payload shape, optional flush ports, backward-ready dependencies
where applicable, synchronous reset, and state effects. Implementation bodies
continue to own register construction and trace controls. `PipeExpansions`
collects all four portable providers without a simulator dependency.

A 288-check focused batch validates skipped bodies, declared versus actual
leaf dependencies, portable materialization, authored hierarchy/names, and
normalized per-module CIRCT equality across stage counts, scalar/wide-vector
payloads, and flush options. The existing Flow chain/static tests pass 78 checks.
The `pipe`, `ctrl-pipe`, `valid-pipe`, and `valid-pipe-capture-always` CIRCT/Verilator
fixtures pass with `--full`, including available exact SystemVerilog references.
Boundary, license, CI-routing, and whitespace checks pass.

Native retained-pipe execution, retained trace equivalence, effect/error coverage,
previous simulator-suite migration, and performance measurements remain open.

## Retained pipe trace preservation

Fixed-latency, flushable, and repeated elastic event fixtures now materialize
retained pipe/map declarations before instrumentation. Their existing public-
transfer scoreboards pass CIRCT/Verilator, including independent reference lanes,
stalls, bubbles, reset, and flush cancellation.

This exposed duplicate module definitions for repeated resolved compositions.
Materialization now reuses definitions keyed by the composition and selected
child implementations, preserving authored names without merging occurrence
state or distinct lowering choices. Complete event manifests match ordinary
elaboration after this fix. The existing core materialization and pipe CIRCT
comparison regressions pass (96 checks). The final trace batch passes all nine
checks, including exact manifests for fixed, flushable, and elastic pipelines.
Boundary, license-header, CI-routing, and whitespace checks pass. Native pipe
execution, broader effect/error coverage, simulator regression migration, and
performance measurements remain open.

## Mixed native pipe execution follow-up

The [dependent simulator validation](https://github.com/tianrui-wei/rhodium/commit/cc4068a)
merges public pipe and trace support. Eighteen mixed Queue/pipe configurations
pass 54 host checks and 512-cycle pre/post-edge oracle replay in interpreter,
generated C, and materialized CIRCT/Verilator, including default optimization.
Elastic, control-only, valid-only, and always-capture families cover one, two,
and four stages and flush options. Direct Queue selection has no Queue expansion
provider; pipe bodies use portable expansion and generic native execution under
the same schedule. Tests cover stalls, invalid payloads, pending reset,
consecutive flushes, and repeated evaluation without clock advancement.

The shared runtime ABI helper passes existing nested multiword replay across
16 configurations. This is focused evidence after the 777-check full baseline.
Broader effects/errors, previous simulator regression migration, and performance
measurements remain open.

## Retained assertion failure/retry follow-up

The [dependent assertion validation](https://github.com/tianrui-wei/rhodium/commit/3ff43b4)
adds a retained guarded assertion/state construct beside Queue. Forty-eight host
checks verify exactly one named assertion after direct/expanded extraction and
optimization. Forty-eight interpreter/generated-C traces each pass 256 accepted
edges, including repeated rejected attempts with different proposed inputs.
Register/Queue state and diagnostic cycle numbers remain unchanged on failure;
retry agrees with clean execution, including later drain. Reset and disabled
guards suppress checks. This is focused evidence; external callback effects,
previous simulator regression migration, and performance remain open.

## Retained external callback follow-up

The [dependent host-effect validation](https://github.com/tianrui-wei/rhodium/commit/4773366)
passes 30 host/native/Queue contract checks. Malformed callback inputs now fail
during lowering. Four 256-edge traces exercise repeated retained host occurrences
in interpreter and generated C, with and without optimization. Accepted edges
invoke each occurrence once; evaluation, failed assertions, and missing sibling
bindings invoke none. Registered 64-bit results, reset handling, and suppression
of tentative outputs after callback failure pass. Native selection never executes
the portable sentinel.

The runtime does not roll back external side effects a callback already performed
before failing. This limit is documented alongside the tested hardware-state
and callback-invocation guarantees. Remaining work includes broader runtime/error
regression migration and performance measurements; this is focused validation,
not a new full-suite baseline.

## Runtime publication/scheduling migration follow-up

The [dependent runtime migration](https://github.com/tianrui-wei/rhodium/commit/f79b7eb)
restores three previous independent C++ regressions under package ownership.
Fifty batched-cycle configurations pass publication/failure, parity, reset,
callback, and reattachment checks. Six reference/parallel scheduling modes pass
512 cycles, including snapshot-prefix splitting and eight-worker execution.
Seventy-five static-demand modes pass 1,000 cycles across shared guards, wide
state, scratch reuse, generated layouts, and strict invalid-selector checks.

A standalone `make sim-runtime-regression-test` entry point and integration in
the selective host runner reuse compiler helpers and runtime builds. CI installs
Clang explicitly. The demand matrix gets ten minutes to compile its 74 generated
libraries; an initial two-minute limit expired before the successful rerun.
Compiler optimization, object-family, arithmetic, and frontend-fixture migration,
plus performance measurements, remain open. These results do not claim a fresh
full selective-suite run.

## FIFO optimization migration follow-up

The [dependent FIFO regression migration](https://github.com/tianrui-wei/rhodium/commit/f24adc7)
restores independent width and derived-field oracles. Two width-growth cases pass
4,000 stimulus iterations each, covering safe 240-to-19-bit narrowing, retained
240-bit cyclic growth, signed/unsigned views, strict selector failures, and invalid
serialized opcode rejection. Derived-field caching passes 6,000 cycles at depths
1, 2, and 3; incompatible pipeline storage stays untransformed. The shared runner
now accepts focused test names after the build directory. Original test semantics
are preserved; source formatting is adjusted for current compiler warnings.
Remaining compiler/object/arithmetic/frontend migration and performance gates stay
open. These focused results do not replace the earlier full-suite baseline.

## Boolean and selector migration follow-up

The [dependent selector regression migration](https://github.com/tianrui-wei/rhodium/commit/c7a592e)
restores three package-owned regressions. Eight Boolean cases exhaust 512 input
combinations each across six-bit and 68-bit fields, including overlapping decoder
rows and first-match/default semantics. Six sparse selector matrices pass 4,000
cycles each against independent software, including disabled rows and serialized
model round trips. Eight ring-selection configurations pass 2,400 cycles each
with 150-bit FIFO payloads and 70-bit aligned views, reference versus generated C,
one/four workers, debug/release builds, strict invalid-selector retry, reset,
wraparound, and compiled-library reattachment. A Boost big-integer oracle
expression now uses explicit bit setting to compile cleanly with GCC 16.

Boundary, license-header, CI-routing, shell syntax, and whitespace checks pass.
These are focused results; remaining compiler/object/arithmetic/frontend
regression migration, integrated validation, and performance measurements remain
required. Simulator sources stay on the dependent branch.

## Matcher and FIFO execution migration follow-up

The [dependent matcher/FIFO migration](https://github.com/tianrui-wei/rhodium/commit/6ac000f)
restores five regression groups without changing their original oracle logic.
Two request-prefix cases pass 4,000 cycles each and reject reuse with mismatched
update operands. Seven packed-matcher configurations pass 700 cycles across
sixteen modes against independent arbitration, including prefix feedback,
priority ownership, grant reuse, reset, and compiled-library swaps. Forty-eight
empty-FIFO configurations pass 500 cycles in five modes across widths 1, 75, and
257, depths 1 and 3, and eight protocol options. Ten stationary-matcher cases
pass 300 cycles in five modes, preserving independent query operands and strict
invalid-grant behavior. Ten FIFO-batch cases pass 400 cycles in five modes,
covering up to sixteen queues, wide payloads, late dependencies, repeated
evaluation, failed host callbacks, retry, and reattachment.

The larger generated-library matrices receive a ten-minute execution limit.
Boundary, license-header, CI-routing, shell syntax, and whitespace checks pass.
Remaining compiler/object/arithmetic/frontend migration, integrated validation,
and performance measurements stay open. These focused results do not replace
the earlier complete selective-suite baseline.

## Contract and payload optimization migration follow-up

The [dependent contract/payload migration](https://github.com/tianrui-wei/rhodium/commit/c6513bd)
restores seven regression groups. Contract kernels pass 1,000 cycles across ten
modes, including captured computations, branches, feedback, snapshot updates,
and malformed-program rejection. Decoder specialization passes 66 shapes over
8,192 inputs in eight modes against independent first-match/default decoding.
Concurrent payload pooling passes 5,000 cycles in five modes. Lifetime sharing
passes 88 configurations at 2,000 cycles each across five modes, including
intermediate readers, exact capacity, and payload-independent control. Routed
exchange passes all 468 width/variant/layout combinations at 4,000 cycles each
across five modes, including 1,024-bit payloads, ownership rejection, failed-edge
retry, and reattachment. Both cross-object cache cases pass 512 cycles in ten
modes; semantic provenance passes demanded-field, feedback, wide-key, pruning,
and malformed-mapping checks.

The original oracle logic remains intact; the decoder cleanup loop is reformatted
for GCC 16. Boundary, license-header, CI-routing, shell syntax, and whitespace
checks pass. Validation artifacts moved to the home-filesystem cache after the
user quota on `/tmp` prevented compilation and diagnostics; prior regression
artifacts were preserved with their old path linked to the new location. These
focused results leave frontend/core-only fixture migration, integrated validation,
and performance measurements open.

## Functional vector-update retention

Restoring the ordinary-core simulator regressions exposed an omitted IR
improvement: `vector_updated` expanded into per-element muxes. The frontend
now preserves one guarded `rtl.vector_write_set`. It compares the full selector
before truncating the write index, so out-of-range updates preserve the source.
Forty-two focused frontend checks pass. Both `vector-update` and
`vector-register-update` CIRCT/Verilator fixtures pass with `--full`; the reviewed
functional-update reference now includes an explicit shared range guard.
Boundary, license-header, CI-routing, example-reference, and whitespace checks
pass. The dependent simulator's functional-update regression retains the compact
operation for single-element, power-of-two, wide-selector, and multiword cases.

## Ordinary core compiler/runtime migration

The [dependent core regression migration](https://github.com/tianrui-wei/rhodium/commit/fcf4141)
adds `make sim-core-regression-test` and integrates it into the selective host
runner. Fresh-root core fixture elaboration passes. Compiler interchange,
deterministic output, malformed inputs, 800 wide arithmetic evaluations, and
1,440 regrouped mask/state evaluations pass. Typed semantic exchange passes its
compiled one/four-worker region and decode checks. Eight ordinary runtime modes
and the x86 assembly mode pass independent arithmetic, hierarchy, memory,
aggregate, assertion, and pipeline oracles. All fifteen generated-C scheduling
modes pass, followed by functional updates, one-hot selections, demand-gated
cones, native-object attachment/tracing, callback failure recovery, and executable
identity checks. Each arithmetic replay checks 59,400 independent observations.

The attachment fixtures now construct FIFO/broadcast object ABI models directly,
without restoring post-expansion library recognition. The migrated regression
exposed and verified the vector-update retention fix above. Boundary, license,
CI-routing, shell, and whitespace checks pass. Remaining library/native-object,
inspection, and harness fixture migration, integrated validation, and performance
measurements stay open; this is focused evidence, not a complete suite baseline.

## Region, primitive, and inspection migration

The [dependent inspection follow-up](https://github.com/tianrui-wei/rhodium/commit/c21b92a)
restores contract-guided grouping and lifted-primitive regressions. Grouping
passes 10,000 independent arithmetic/shared-observer comparisons, analysis-only
ordering checks, and malformed-contract rejection. Five primitive execution
modes pass 1,000 cycles across control-mask boundaries 1/3/64/65, packet ownership,
scoreboard old-state semantics, debug/release generation, parallel schedules,
failed-host retry, and reattachment.

The package-owned inspection CLI and its original lookup regression also pass,
covering sparse/default selection, all result slices, source-mapped plans, and
real x86 compiler-output disassembly. The tool reports static instruction and
register observations rather than dynamic cost. Both runners include the restored
groups. Boundary, license, CI-routing, shell, CLI-help, and whitespace checks pass.
Library/native-object and harness migration, integrated validation, and
performance measurements remain open.

## Native object, library, and replication migration

The [dependent object/library migration](https://github.com/tianrui-wei/rhodium/commit/aedc795)
restores the original protocol oracle files without changing their logic. Explicit
object ABI fixtures replace the removed post-expansion registry. Interpreter and
generated-C replay pass all 36 FIFO width/depth/pipe/flow configurations with
functional and payload-free control models, offer replacement, round-robin
rotation, valid-only pipes, broadcasts, packet arbitration, malformed descriptors,
and bounded snapshot replication. Actual Flow RTL remains the independent
reference for pipe/broadcast/packet behavior. Payload-free test fixtures do not
authorize changing observable public construct payloads.

Shared-Queue replication now selects the retained Queue through `QueueNative`.
Eleven host checks verify byte/work budgets and one state owner; 800-cycle
replay checks ordinary and native shared cones. The canonical standard-library
examples additionally pass six generic execution modes, including x86 assembly
and generated C, against independent scoreboard, queue, pipe, arbiter, and
credited-transport oracles. This validates generic fallback without claiming
direct target registrations for every library construct.

`make sim-object-regression-test` and the selective host runner own this coverage.
Simulator artifact roots now honor `TMPDIR` to avoid the observed temporary-
filesystem quota failures. Boundary, license-header, CI-routing, shell syntax,
and whitespace checks pass. Remaining matcher/ALU/TLB and harness migration,
integrated validation, and performance measurements remain open.

## Matcher and ALU differential migration

The [dependent matcher/ALU migration](https://github.com/tianrui-wei/rhodium/commit/b375a6a)
restores the original independent oracles through explicit native ABI fixtures
and actual library RTL references. Six rotating-matcher shapes pass 700 cycles
each in raw, native, generated-C, and parallel execution. A 250-cycle feedback
fixture checks that later-column requests can depend on earlier grants without
introducing a false cycle. Three retained-construct checks validate shared query
slices and reject invalid ranges or inconsistent dependencies. RV32/RV64 ALUs
pass 24,000 physical-control cases in raw and native execution, including
generated C and stateless query ownership.

The object regression runner includes both groups. Boundary, license-header,
CI-routing, shell syntax, and whitespace checks pass. These are focused results;
TLB and harness migration, fresh integrated validation, and performance
measurements remain open. Simulator sources remain on the dependent branch.

## Sv39 TLB differential migration

The [dependent TLB migration](https://github.com/tianrui-wei/rhodium/commit/8f94cdf)
restores the independent Sv39 oracle at depths 2/4/8, each for 6,000 cycles
across six raw/native/generated-C/parallel modes. Actual processor RTL remains
the reference; explicit ABI fixtures provide optimized and unoptimized native
models without post-expansion recognition. Demand and probe permissions,
noncanonical addresses, overlapping entries, reset/fill/invalidate priority,
replacement, and compiled-library reattachment pass. Three additional execution
modes preserve TLB contents and replacement priority after a failed host effect
and accept a clean retry.

The object runner includes this group. Fresh-root elaboration, native fixture
compilation, the complete TLB replay, boundary/license/CI-routing checks, shell
syntax, and whitespace validation pass. Remaining work includes harness and
legacy coverage auditing, fresh integrated validation, and performance
measurements. These focused results do not replace a complete-suite baseline.

## Native harness regression migration

The [dependent harness migration](https://github.com/tianrui-wei/rhodium/commit/73b741b)
restores benchmark audit and host-loader utilities under `sims/native`, with
package-local tests. Timing-gate rejection, subprocess environment isolation,
shared-inode staging, and immutable artifact audits pass. Loader trace hashing
passes 250,000 independent reset/handshake/wide-input comparisons; sized
transactions, acknowledged boot, polling, reset, and response errors pass.
The original runtime smoke driver also compiles against the current runtime.
These helper tests do not establish an end-to-end SoC boot or measured
profile-guided speedup.

The selective runner includes the harness gate. Boundary, license-header,
CI-routing, shell syntax, and whitespace checks pass. A fresh integrated run
has been started; its result is not yet evidence of completion. Optional legacy
profiling/specialization coverage auditing and performance measurements remain
open alongside integrated validation.

## Profiling helper regression migration

The [dependent profiling migration](https://github.com/tianrui-wei/rhodium/commit/da00b8d)
restores layout sample decoding and scratch inspection under `sims/native`.
Three unit tests validate precise-load/lost-record decoding, allocation-range
precedence, and malformed records. The independent assembly/ELF fixture checks
spill eligibility, overlapping-slot rejection, SIMD aliases, and virtual-address
sample attribution. Both groups pass and join the harness runner. Boundary,
license-header, CI-routing, shell syntax, and whitespace checks pass.

This is parser/analysis validation without PMU permissions, not a live profiling
or performance claim. The integrated simulator run remains pending; external
specialization and ordinary-core Verilator coverage still need auditing, along
with the direct-versus-expanded performance gate.

## External specialization and ordinary-core Verilator migration

The [dependent legacy comparison migration](https://github.com/tianrui-wei/rhodium/commit/875305c)
restores the optional external generated-C specializer and ordinary-core
Verilator comparison. Eight specialization modes pass their original four-engine
512-cycle oracle, including invalid previews, state banks, failed publication,
retry/reattachment, strict diagnostics, and malformed artifact rejection.
The runner explicitly builds the compiler dependencies required by specialization.

Hierarchy and pipeline fixtures each pass 10,000-cycle full-trace comparisons
across eight native/reference/generated-C/parallel/Verilator modes, followed by
five matching-digest repetitions at 10,000 cycles. These comparisons reuse the
previously validated ordinary-core fixture batch. Their timing reports were
produced under concurrent validation load and are not performance evidence.
Boundary, license-header, CI-routing, shell syntax, and whitespace checks pass.
Fresh integrated and retained-construct Verilator runs remain active; the
retained direct-versus-expanded performance gate remains open.

## Fresh retained Verilator and Flow contract export validation

The complete retained-construct CIRCT/Verilator runner passes again using the
fresh integrated host batch's models. Scalar and nested compositions, repeated
occurrence state, captured scalar/record/vector/multiword/nested payloads, and
18 mixed Queue/pipe configurations match independent pre/post-edge oracles in
native interpreter, generated C, and Verilator execution. This closes the fresh
retained Verilator gate; the broader integrated host run is still active.

The [dependent contract-export migration](https://github.com/tianrui-wei/rhodium/commit/11296173)
restores the optional interface-transform metadata adapter without introducing
post-expansion implementation recognition. All 23 original checks pass with a
fresh compiled root: packed bindings, stable/ambient protocol ancestry, routes,
graph immutability, report round trips, compiler remapping, and invalid bindings.
The core regression runner includes this gate. Boundary, license-header,
CI-routing, shell syntax, and whitespace checks pass. Direct-versus-expanded
benchmark infrastructure is under validation; no timing result is claimed yet.
