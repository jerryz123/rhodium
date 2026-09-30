<!-- Tracks remaining named-domain, reset-domain, and semantic-transfer work in Rhodium. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Clocking, CDC, and RDC next steps

Temporal analysis, root-owned environment declarations, and closed-design CDC
diagnostics for the inspectable, resetless `SyncLevel` crossing are implemented.
The clock target returns complete findings; verification callers gate dependent
work on the compilation result's `has_errors`. Current behavior and ownership live in the
[clocking analysis guide](analysis/DEVELOPING.md),
[frontend guide](frontend/DEVELOPING.md), and
[standard-library guide](std/DEVELOPING.md). This plan records work that is
still open; it is not a second contract for the implemented API.

## Invariants for the next stages

- Temporal-domain facts and crossing evidence remain backend-independent.
  Intrinsic facts belong in core IR, derived provenance and reports in
  `analysis/clocking/`, authoring syntax in the frontend clocking layer,
  crossing implementations in `std/`, and attributes/constraints in backends.
- Domains are not hardware data types. Module summaries retain symbolic input
  and state provenance so one child definition can be reused under different
  parent clocks; closed-design resolution starts from an explicit top.
- An incompatible or unknown-domain sample needs a recognized, structurally
  verified crossing contract. A reason string, generic waiver, or domain
  relabel is not evidence. Unknown relationships must not generate timing
  exceptions automatically.
- Crossing lineage must retain source, destination, identity, and correlated
  leaves so independent synchronizers and reconvergence remain diagnosable.
- CDC checks concern sampling and clocked effects, not every combinational
  meeting of signals. RDC must distinguish electrical reset safety from
  logical reset epochs; different reset identities alone are not errors.

## 1. Durable named domains and scopes

The existing frontend environment slice declares top-level input timing and
clock relationships, but does not author durable named domains inside reusable
modules. Add declarations and scopes that bind module-local clock and reset
origins, then resolve them through instance paths. Connect the already
certified single-domain `sync_circuit` invariant to this representation without
changing its concise authoring syntax or moving report policy into core.

Define and test the public syntax for named domains, scopes, and input
contracts. Reject duplicate, contradictory, partial, or foreign-module
declarations. Keep `identical` automatically compatible; specify precise
conditions before treating `derived` or `exclusive` clocks as compatible.
Preserve the existing explicit distinction between `asynchronous` and
`unknown`.

Exit criterion: one reusable multi-domain child resolves correctly under
different parent bindings, while an undeclared or incompatible raw sample
produces a deterministic closed-design finding.

## 2. Reset foundations

Specify synchronous versus asynchronous reset semantics, polarity, assertion,
and release in the public IR. Add a canonical asynchronous-assert,
synchronous-release reset synchronizer with inspectable implementation and
evidence. Track reset origin, resetless state, synchronizer identity, and
independently reset reconvergence separately. Replace generic-cast construction
of computed synchronous resets with an explicit, checked path.

Define which RDC findings are errors, warnings, or protocol obligations only
after those semantics exist. In particular, resetless payload paired with
reset validity must remain representable; reset-epoch policies such as flush,
preserve, reinitialize, or abort belong to the relevant interface or transfer
contract, not a blanket core rejection.

Exit criterion: focused fixtures distinguish unsafe asynchronous release from
valid synchronized release and report reset-epoch interactions without
rejecting every differently reset path.

## 3. Semantic transfers

Specify a narrow delivery contract for an acknowledged event transfer and a
payload-stability, completion, and reset contract for bundled-data handshake
transfer. Implement each as inspectable hardware with independently verified
crossing evidence. Use existing interface/ready-valid descriptions for stream
semantics; core stays protocol-neutral. Add assertions or bounded formal checks
for delivery, stability, and reset behavior.

An asynchronous FIFO comes later, after its dual-clock storage, pointer,
reset, and physical-constraint contracts are explicit. Do not model it as an
opaque operation or assume the current one-clock synchronous-memory primitive
is a dual-clock implementation.

## Validation and decisions to settle

Use focused core, analysis, frontend, standard-library, backend, and external
CIRCT/Verilator checks according to the boundary changed. Run Racket and
Rhombus commands through the repository wrappers and `make check-boundaries`
when modules or dependency direction change. Compare any emitted timing
collateral against verified relationships and crossing evidence.

Before broad authoring work, settle the exact named-domain syntax, the
compatibility rules for derived and exclusive clocks, and compatibility of
new declarations/report fields with existing inspectable IR consumers.
