<!-- Defines the architecture and phased implementation plan for Rhodium clock-domain and reset-domain reasoning. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Clocking, CDC, and RDC plan

## Decision

Rhodium should add backend-independent temporal-domain semantics and expose them
through a selectable clocking frontend layer.

This is not only a frontend convenience. Clock-domain crossing (CDC) and
reset-domain crossing (RDC) checks must apply uniformly to every producer of
public hardware IR. Intrinsic hardware facts and explicit crossing operations
belong in `rhodium/core/`; derived provenance, environment resolution, and reports
belong in `rhodium/analysis/clocking/`; frontend syntax belongs in
`rhodium/frontend/layers/`; reusable crossing circuits belong in `rhodium/std/`; and
implementation attributes and constraints remain backend responsibilities.

CDC enforcement is the first target. Initial RDC support should inventory and
report reset relationships without rejecting every interaction between
differently reset state. Full RDC enforcement depends on explicit asynchronous
reset semantics and protocol-level state-lifetime contracts that Rhodium does not
yet have.

## Goals

- Preserve clock and reset intent in the public, backend-independent IR.
- Determine which temporal sources can influence every sequential sink.
- Reject raw sampling from incompatible or unknown clock domains.
- Require explicit evidence for every accepted asynchronous transition.
- Retain enough crossing lineage to diagnose fanout, independently
  synchronized buses, multi-clock fan-in, and reconvergence.
- Summarize domain behavior through reusable modules without flattening the
  design.
- Give top-level inputs explicit environment-domain contracts.
- Generate backend attributes and timing collateral from the same verified
  crossing descriptions that produced the hardware.
- Keep crossing implementations inspectable as ordinary Rhodium structure where
  practical.
- Preserve the concise single-domain `sync_circuit` authoring model.

## Non-goals

- Clock domains do not become Rhombus static types or parameters of Rhodium
  `DataType`s.
- The initial implementation does not infer the designer's intent from an
  arbitrary synchronizer-shaped register chain.
- The initial implementation does not prove a pulse is semantically a level
  merely because both use a one-bit representation.
- Rhodium CDC verification does not replace static timing analysis, physical
  synchronizer placement, metastability/MTBF analysis, or independent signoff
  tools.
- V1 does not include event transfer, a general handshake bridge, or an
  asynchronous FIFO.
- V1 does not add asynchronous reset, reset polarity, or a blanket rule that
  differently reset registers may not interact.
- Unknown clock relationships do not cause Rhodium to emit false-path or
  asynchronous-clock constraints automatically.

## Current baseline

The current core already exposes the physical facts needed to begin analysis:

- A register has a current `Value`, a next-state `Place`, an explicit `Clock`,
  and an optional active-high synchronous `Reset` with reset value.
- Memory writes, synchronous memories, clocked assertions, and clocked DPI
  effects also carry explicit clocks.
- `Clock` and `Reset` are nominal control `HardwareType`s, but both have a
  one-bit packed representation and can currently be constructed through an
  equal-width cast.
- `sync_circuit` creates one ambient clock/reset pair in frontend support and
  expands it into ordinary core ports, operands, instance inputs, and drives.
- Core verification checks clock and reset types, but it does not relate data
  dependencies to the clock or reset of the state that samples them.
- The combinational-cycle verifier already builds leaf-sensitive module
  summaries through records, vectors, casts, and instances. Domain analysis
  should reuse this style rather than introduce a flattened second IR.
- `DesignElaboration` already pairs a verified design with an explicit top.
  Closed-system domain analysis should use that top rather than infer it from
  module order.

## Semantic model

### Clock and reset domains

`ClockDomain` and `ResetDomain` are separate concepts.

A `ClockDomain` identifies a symbolic sampling event stream within a module.
It refers to the module-local `Clock` value used by sequential operations and
can participate in declared relationships with other clock domains.

A `ResetDomain` identifies a reset and state-lifetime boundary relative to a
clock domain. In the first implementation it describes the existing
active-high synchronous reset behavior. Resetless state has an explicit
no-reset classification rather than silently sharing the ambient reset
domain.

Domains are not hardware data types. `Bits(8)` remains `Bits(8)` regardless of
where a particular value is produced or sampled. A domain declaration is
module-owned symbolic IR information, and a module instance binds the child's
domain symbols to the parent signals and relationships that drive its clock
and reset ports.

The initial clock relationship vocabulary is:

- `identical`: the same sampling event stream after alias and hierarchy
  substitution;
- `derived`: an explicitly described, timing-related event stream;
- `asynchronous`: explicitly declared to have no usable phase relationship;
- `exclusive`: clocks whose selection relationship is explicitly described;
- `unknown`: no relationship has been established.

Only `identical` is automatically compatible in the first checker. A
`derived` relationship may become compatible when its exact timing contract is
represented and the backend can preserve it. `asynchronous` and `unknown`
require explicit crossing evidence. `exclusive` requires an explicit transfer
or selection contract and is not treated as automatically safe.

### Temporal provenance

Temporal provenance is an analysis result, not one mutable `domain` field on
every `Value`. A combinational value may depend on no temporal state, on one
domain, or on several unrelated domains. A module input is symbolic until the
module is instantiated, and one module definition may be instantiated under
different parent domains.

Analysis should compute provenance independently for every aggregate leaf. At
minimum it distinguishes:

```text
Static
External(input leaf, declared domain or unknown)
State(clock domain, reset domain or no reset)
Crossed(kind, source domain, destination domain, crossing identity)
```

Constants and synthesis don't-cares are `Static`. A register result and a
synchronous-memory result introduce `State` in the sequential operation's
domain. Ordinary combinational operations union the provenance of the leaves
they depend on. Module output summaries retain symbolic input dependencies and
local state origins for substitution at each instance.

An accepted crossing changes the domain in which its result may be sampled,
but it must retain its source domain, crossing kind, and crossing identity.
This lineage is required to detect independently synchronized bits, duplicated
synchronizers, and reconvergence after apparently successful relabeling.

### Temporal sinks

Compatibility is checked when a value is sampled or has a clocked effect, not
merely when signals from different domains meet in combinational logic.

The initial sink inventory includes:

- register next-state data and synchronous-reset controls;
- asynchronous-memory write address, data, and enable;
- synchronous-memory address, data, mode, mask, and enable inputs;
- clocked DPI inputs and enables;
- clocked assertion conditions and guards;
- inputs to an explicit crossing contract;
- top-level outputs, which are reported for environment review but are not
  rejected solely for being externally observed.

For each sink, every non-static source must be compatible with the sink clock
or arrive through recognized crossing evidence. Several unrelated temporal
sources in one crossing input are a multi-clock-fan-in error.

### Hierarchy and tops

Domain verification has two levels:

1. Module summarization records symbolic dependencies between input leaves,
   output leaves, local state, clocks, resets, and crossings.
2. Closed-design analysis starts from a `DesignElaboration` top and substitutes
   those summaries through each instance path.

This keeps modules reusable and avoids assigning a concrete parent domain to a
child `Value` in the child definition. It also permits one module definition to
be instantiated under several domain bindings without specialization.

Top-level data inputs require an environment contract such as synchronous to a
named clock domain, asynchronous level, or unknown. Distinct top-level clocks
remain `unknown` until a relationship is declared. Unknown is conservative for
verification but is never sufficient authority to emit a timing exception.

## Crossing evidence and implementations

The core must retain first-class crossing evidence, but the physical
implementation should remain inspectable. Do not make an opaque asynchronous
FIFO operation the initial abstraction.

Crossing evidence may be represented by a dedicated operation or a specific
module contract. The representation selected during implementation must:

- survive frontend elaboration in the public IR;
- identify source and destination domain symbols;
- identify correlated leaves as one crossing group;
- retain a stable crossing identity through downstream provenance;
- point to the ordinary registers and logic implementing the crossing;
- carry source location and origin information;
- be independently verifiable by core;
- be consumable without importing a backend.

### `SyncLevel`

The first supported crossing is a persistent one-bit level into a destination
clock domain.

Its initial contract is:

- exactly one input bit and one output bit;
- one source domain or an explicitly asynchronous external input;
- no multi-clock combinational fan-in;
- a stable-level promise at the boundary;
- at least two destination-domain sampling stages;
- one recognizable chain with no functional fanout between its first and last
  stages;
- output provenance in the destination domain with the crossing lineage
  retained;
- backend synchronization attributes derived from the verified chain.

The stable-level promise is a semantic precondition. Rhodium cannot infer that a
`Bool` or `Bits(1)` pulse was intended to persist. A later frontend or protocol
abstraction may distinguish `Level` and `Event`, but V1 must not claim to catch
that misuse from representation alone.

### No generic unsafe escape hatch

Do not add `unsafe_cross_domain` or a generic CDC waiver operation. A reason
string records intent but cannot establish electrical or protocol safety, and
a generic domain relabel would weaken the guarantee that accepted crossings
have inspectable implementations.

A crossing is accepted only when its source is compatible with the sink or it
uses a recognized, structurally verified crossing contract. Legitimate cases
that `SyncLevel` cannot express should add a narrowly specified crossing kind,
such as an acknowledged event or bundled-data handshake, with its own
implementation and verification rules. External or black-box implementations
likewise need an explicit contract describing the guarantee they provide;
unsupported crossings remain errors.

### Later crossings

- `SyncEvent` requires a specified delivery contract, such as at-most-once,
  exactly-once with acknowledgement, or loss permitted, plus reset behavior
  for any toggle state.
- A handshake crossing requires payload-stability and abort/reset contracts.
- Stream crossings should use the existing interface and ready-valid protocol
  abstractions; core should remain protocol-neutral.
- An asynchronous FIFO should wait for an appropriate dual-clock memory or
  black-box contract. The current synchronous-memory primitive owns one clock
  and is not the right physical abstraction for a general async FIFO.

## Reset-domain reasoning

RDC work has two distinct subjects and must not conflate them.

### Electrical reset safety

This includes asynchronous assertion/deassertion, reset synchronizer topology,
multiple independent synchronizations, recovery/removal concerns, fanout, and
reconvergence. Rhodium needs explicit asynchronous-reset semantics, polarity, and
assertion/deassertion behavior before it can enforce these rules soundly.

When asynchronous reset is added, a reset synchronizer should be canonical per
raw reset source and destination clock domain unless the design records an
explicit exception. Its output is a distinct destination `ResetDomain`.

### Logical reset epochs

A reset also determines which state remains live across a reset event. Two
registers on the same clock can intentionally have different reset behavior.
Common examples include reset validity state paired with resetless payload
state, and a local queue reset produced by global reset OR protocol flush.

Consequently, V1 should report reset-domain dependencies and reconvergence but
must not reject every differently reset data path. Later protocol adapters can
declare epoch policies such as:

- `flush`: pre-reset transactions become invalid;
- `preserve`: state remains meaningful across the reset;
- `reinitialize`: both sides perform a coordinated restart;
- `abort`: an in-flight operation is explicitly canceled.

These contracts belong with the interface or transfer abstraction whose
meaning they describe. Core should preserve only the protocol-neutral reset
origins, dependency paths, and crossing evidence needed to verify them.

## Clock and reset construction

The current generic equal-width cast can construct `Clock` and `Reset` from an
arbitrary one-bit value. That is insufficient evidence for temporal analysis.

During migration:

- an existing cast to `Clock` or `Reset` produces an unknown domain relation;
- using that control at a sequential sink requires an explicit domain or
  unsafe declaration;
- existing same-domain derived synchronous resets can migrate to an explicit
  synchronous-reset constructor;
- generated clocks should eventually use explicit constructors such as clock
  inversion, division, gating, or selection, each of which declares its
  relationship to its source;
- raw external or black-box clock/reset conversion remains possible only
  through an explicit unsafe or environment boundary.

Do not parameterize the `Clock` or `Reset` hardware type with domain identity.
The signal representation and the design-level temporal relationship are
separate concerns.

## Package ownership and proposed files

```text
rhodium/core/dependencies.rhm

rhodium/analysis/clocking.rhm
rhodium/analysis/clocking/types.rhm
rhodium/analysis/clocking/module.rhm
rhodium/analysis/clocking/environment.rhm

rhodium/frontend/layers/clocking.rhm
rhodium/frontend/support/clocking.rhm

rhodium/std/cdc.rhdl
rhodium/std/cdc/level.rhdl
rhodium/std/cdc/handshake.rhdl       # later
rhodium/std/cdc/event.rhdl           # later
rhodium/std/cdc/async-fifo.rhdl      # later

tests/analysis/clocking-test.rhm
tests/analysis/clocking-provenance-test.rhm
tests/analysis/clocking-environment-test.rhm
tests/frontend/clocking-fixture.rhdl
tests/frontend/clocking-test.rhm
tests/frontend/invalid/*clocking*.rhdl
tests/backend/clocking-test.rhm
```

`rhodium/core/dependencies.rhm` owns the leaf-sensitive dependency semantics
shared by intrinsic combinational-cycle verification and optional temporal
analysis. Core continues to own `Clock` and `Reset` types, explicit operation
operands, and any future crossing operation whose meaning must survive
lowering. It does not export report or environment objects.

`rhodium/analysis/clocking/types.rhm` owns clock-use summaries, provenance,
relationships, environments, and report objects.
`rhodium/analysis/clocking/module.rhm` owns clock-use certification, provenance
propagation, reusable module summaries, instance substitution, and diagnostics.
`rhodium/analysis/clocking/environment.rhm` owns closed-design resolution from an
explicit top. `rhodium/analysis/clocking.rhm` is their public aggregate. Analysis
depends on core; core never depends on analysis.

`rhodium/frontend/layers/clocking.rhm` owns selectable public syntax for named
domains, relationship declarations, top-level environment contracts, domain
scopes, and crossing evidence. It must not import sibling layers.

The existing `rhodium/frontend/support/clocking.rhm` continues to own shared
ambient context and instance propagation. It should be generalized so the
current `sync`, `sequential`, `memory`, `sync-memory`, `assertion`, `dpi`, and
`hierarchy` layers can consume domain handles without importing the new public
layer. `sync_circuit` remains single-domain sugar and should elaborate to the
same durable domain model as explicit clocking syntax.

`rhodium/std/cdc/` contains ordinary Rhodium circuit implementations. The public
facade is `rhodium/std/cdc.rhdl`. Higher-level stream implementations may import
the existing public interface and ready-valid libraries, but frontend
implementation packages must not import `std/`.

The CIRCT backend consumes verified domain facts and emits only backend-owned
representation, attributes, and collateral. Vendor-specific constraint
generation should remain separate from core verification and should never be
required to understand whether an Rhodium crossing is legal.

## Phased implementation

### First slice: certify the existing `sync_circuit` invariant

Before introducing named domains or temporal provenance, make the current
single-domain abstraction mechanically true:

1. Classify every core sequential and verification opcode as clocked, with
   exact clock/reset operand positions, or explicitly unclocked state.
2. Summarize each completed module as `Combinational`, `SingleClock`, or
   `MultiClock`, with reset use separately inventoried as ambient, local, or
   absent.
3. Treat only transparent `rtl.wire` paths as clock aliases; do not treat a
   packed cast to `Clock` as identity evidence.
4. After `sync_circuit` finalization, require every locally owned clocked
   effect to use the generated ambient clock.
5. Require a sync child instantiated inside a sync parent to inherit that
   parent's clock. Keep explicit sync-child binding legal from an ordinary
   circuit, which is the intentional multi-clock composition boundary.
6. Preserve resetless state and explicit local synchronous resets. Reset
   identity is reportable in this slice but does not decide clock-domain
   compatibility.

This slice adds no IR fields, named domains, CDC provenance, crossing
primitives, backend attributes, or CIRCT changes. Its result is a dependable
single-clock island boundary that later CDC analysis can summarize as one
domain instead of rediscovering clocks operation by operation.

### Second slice: report-only temporal provenance

Build the first Phase 1 analysis without changing what designs are accepted:

1. Extract the existing leaf-sensitive combinational dependency rules into
   one core module shared by cycle verification and temporal analysis.
2. Classify origins as static, symbolic module-input leaves, or state tied to
   its clock/reset controls and concrete instance path.
3. Substitute symbolic input and state origins through reused module
   definitions without flattening hierarchy.
4. Inventory every existing clocked sink and classify each sampled leaf as
   static, same-clock, foreign-clock, unknown-input, unknown-clock, or
   multi-clock fan-in.
5. Expose inspectable summary objects and deterministic text reports.

This slice is implemented in `rhodium/analysis/clocking/`. It deliberately adds
no domain declarations, clock-relationship policy, crossing evidence,
frontend syntax, automatic verification call, rejection, IR mutation, or
backend lowering. Existing
`sync_circuit` certification remains the enforcement boundary; because those
circuits already prove one ambient clock locally and propagate it to sync
children, later closed-design analysis can treat each certified subtree as a
known single-clock island rather than infer that invariant from dataflow.

### Third slice: closed-design environment resolution

Resolve the symbolic module report at one explicit system boundary without
changing hardware or acceptance:

1. Require `DesignElaboration`, making its completed top module authoritative.
2. Give each top data-input aggregate subtree an optional unknown,
   synchronous-to-clock, or asynchronous timing contract; uncovered leaves
   remain unknown.
3. Describe top-clock pairs as identical, derived, asynchronous, or exclusive;
   an undeclared non-identical pair has an unknown relationship.
4. Treat declared identical clocks as an equivalence class, but keep exact
   identity separately visible in reports.
5. Reclassify every hierarchical sink against the environment as exact-clock,
   identical, derived, asynchronous, exclusive, unknown-relationship,
   asynchronous-input, unknown-input, unknown-clock, or multi-clock fan-in.
6. Reject malformed, overlapping, duplicate, and contradictory analysis
   declarations while continuing to report all valid designs without CDC
   enforcement.

This slice is implemented as optional analysis over core IR. It adds no named
frontend domains, clocking syntax, crossing evidence, verification hook, IR
mutation, backend attributes, or timing exceptions. A certified `sync_circuit`
subtree needs only its top boundary contract: its existing invariant already
makes all local state and sync children share the ambient clock/reset, so
contracts are not repeated at each child.

### First frontend environment slice

The first bounded part of Phase 2 is implemented in
`rhodium/frontend/layers/clocking.rhm`:

1. `elaborate_with_clocking` retains the ordinary `DesignElaboration`, builds a
   `TemporalEnvironment`, and resolves the existing closed-design summary.
2. Top data inputs can be declared synchronous to a top clock, asynchronous,
   or explicitly unknown, including existing aggregate-subtree paths.
3. Top clock inputs can be declared identical, derived, asynchronous, or
   exclusive through the existing analysis relationship objects.
4. Declarations are collected per elaboration and must be structurally owned
   by the explicit top. Child-owned, out-of-wrapper, driveable-place, malformed,
   duplicate, contradictory, and hardware-conditional uses are rejected.
5. The result is report-only: the hardware IR is unchanged and the layer does
   not yet approve crossings, enforce CDC compatibility, or emit constraints.
6. The layer is independently selectable from `#lang rhodium/base` and can be
   explicitly imported beside `#lang rhodium`; it remains outside the standard
   aggregate until durable domain and crossing semantics exist.

The next frontend work is not more aliases for environment facts. It is to
design durable domain declarations and scopes that preserve module reuse,
then connect `sync_circuit`'s already-certified single clock/reset invariant to
those declarations without moving report policy into core.

### Phase 1: Semantic inventory and analysis

1. Add analysis-owned domain, relationship, provenance-summary, and report
   objects over stable public core identities.
2. Define identical, derived, asynchronous, exclusive, and unknown clock
   relationships without changing hardware lowering.
3. Build leaf-sensitive, hierarchy-aware temporal dependency summaries.
4. Analyze every clocked sink and produce deterministic text and inspectable
   report objects.
5. Require an explicit `DesignElaboration` top for closed-system environment
   analysis while retaining module-local verification.
6. Add analysis tests for constants, aggregates, casts, registers, memories,
   instances, reused module definitions, and multi-clock fan-in.

Phase 1 reports crossings but does not yet approve a generated synchronizer.
It may reject only unambiguous raw incompatible sampling after the diagnostic
and environment-contract behavior is specified.

### Phase 2: Frontend domain authoring

1. Add the selectable `clocking.rhm` frontend layer. The root-owned,
   report-only environment slice is implemented.
2. Generalize the existing ambient `SyncDomain` wrapper to reference durable
   core domain declarations.
3. Keep existing single-domain `sync_circuit` source syntax working.
4. Add explicit named domains and domain scopes for multi-domain circuits.
5. Add top-level input-domain and clock-relationship declarations. The
   analysis-owned timing and relationship vocabulary is now exposed; durable
   named domains remain later work.
6. Add negative fixtures for partial, contradictory, unknown, and cross-module
   domain declarations. Environment validation and root ownership are covered;
   domain-scope cases await durable declarations.
7. Add the layer to the curated standard profile only after its core semantics
   and explicit base-profile composition are tested as equivalent. Implemented
   with durable `cdc.sync_level` evidence and retained base-profile coverage.

### Phase 3: `SyncLevel` and reconvergence

1. Implement the one-bit `SyncLevel` standard-library circuit. Implemented as
   a resetless two-stage `sync_circuit`.
2. Add durable crossing evidence tied to its inspectable register chain.
   Implemented as the zero-result core `cdc.sync_level` operation.
3. Verify stage count, destination clocks, fanout, input provenance, and output
   lineage. Implemented for the first conservative closed-design CDC policy;
   unknown external timing and multi-clock fan-in remain errors.
4. Diagnose independently synchronized leaves that reconverge. Implemented as
   structured, policy-neutral `CrossingReconvergence` findings on module and
   closed-design summaries; one-identity fanout is not reported.
5. Emit CIRCT/SystemVerilog synchronizer attributes from verified evidence.
   Implemented for all verified `SyncLevel` stages.
6. Add backend tests and at least one external CIRCT/Verilator integration
   fixture. Implemented with CIRCT verification, an example-owned Verilog
   golden, and a cycle-level Verilator test for two-edge latency, stable-level
   propagation, and resetless-stage behavior.
7. Require each later crossing kind to define a narrow semantic contract and
   independently verifiable implementation; do not add a generic waiver path.
8. Expose every missing crossing as a structured closed-design finding and
   aggregate strict failures after complete hierarchy analysis. Implemented as
   `CdcViolation`, with a broken/corrected multi-clock hierarchy example.
9. Keep provenance analysis scalable in large flat modules. Implemented with
   one operation and synchronizer-role index per reusable module plus keyed
   value/path memoization, avoiding per-register operation scans and linear
   cache searches.

### Phase 4: Reset foundations

1. Specify distinct synchronous and asynchronous reset behavior, polarity,
   and construction.
2. Add canonical asynchronous-assert/synchronous-release reset synchronization.
3. Track reset synchronizer identities and reset-domain reconvergence.
4. Report resetless, common-reset, and independently reset state separately.
5. Define which RDC findings are errors, warnings, or protocol obligations.
6. Migrate computed synchronous resets away from generic representation casts.

### Phase 5: Semantic transfers

1. Specify and implement acknowledged event transfer.
2. Specify and implement bundled-data handshake transfer.
3. Integrate transfer adapters with interface and ready-valid descriptions.
4. Add formal or assertion-based contracts for delivery, stability, and reset
   behavior.
5. Add asynchronous FIFO support only after its memory, pointer, reset, and
   physical-constraint contracts are explicit.

## V1 acceptance criteria

The first enforceable release is complete when all of the following hold:

- A raw register-to-register transfer between unrelated clocks is rejected.
- A same-clock dependency remains legal through multiple hierarchy levels.
- A reused child module is analyzed correctly under different parent clocks.
- Distinct top clocks without a relationship are reported as unknown.
- An explicitly asynchronous one-bit stable level transferred through
  `SyncLevel` is accepted.
- A multi-bit bus cannot be passed through `SyncLevel` as one value.
- Independent per-bit synchronizers that reconverge are diagnosed.
- Every missing crossing in a closed hierarchy is returned in one structured,
  deterministic list before strict verification fails.
- Multi-clock fan-in before a crossing is rejected.
- A raw cast-produced clock or reset does not silently establish a safe
  relationship.
- A reason string or domain relabel cannot authorize a crossing; transfers
  without a compatible relationship or recognized verified contract are
  rejected.
- Reset-domain differences are inventoried without rejecting valid/resetless
  payload structures solely because their reset identities differ.
- Timing exceptions are emitted only for explicitly declared relationships
  and verified crossings.
- Existing single-domain `sync_circuit` designs retain their hardware behavior
  and concise authoring surface.

## Verification workflow

- Add focused analysis tests before frontend syntax or backend lowering.
- Mirror intrinsic IR checks under `tests/core/`, optional analysis under
  `tests/analysis/`, and supported authoring and lowering under
  `tests/frontend/` and `tests/backend/`.
- Test aggregate leaves and hierarchy explicitly; do not rely only on scalar,
  flat examples.
- Test standard-library crossing circuits semantically and structurally.
- Run `make check-boundaries` after adding modules or changing imports.
- Run each Racket or Rhombus command with one fresh `PLTCOMPILEDROOTS` directory
  per focused validation batch and use `racket -y` for direct Racket commands.
- Run external CIRCT and Verilator checks when lowering, attributes, or
  generated crossing hardware changes.
- Compare emitted constraints against the verified report so stale naming or
  unverified exceptions cannot pass silently.

## Resolved decisions and remaining questions

- Crossing evidence is a dedicated zero-result core operation referencing the
  ordinary register chain; reusable implementation remains a standard-library
  circuit.
- There is no generic unsafe CDC waiver or domain-relabel operation; every
  accepted incompatible-domain transfer requires a recognized verified
  crossing contract.
- Instantiating `SyncLevel` is the explicit stable-level promise.
- `SyncLevel` stages are resetless until asynchronous reset behavior exists.

Remaining questions are:

- The exact public syntax for named domains, domain scopes, and environment
  input contracts.
- The precise compatibility rules for derived and exclusive clocks.
- The public compatibility policy for adding domain declarations and report
  objects to the inspectable IR.

These questions must be settled through small core examples before introducing
generic annotation or rewrite infrastructure.
