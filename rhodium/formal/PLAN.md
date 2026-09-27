<!-- Tracks the remaining semantic milestones for Rhodium's optional Rosette formal engine. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Formal-engine next steps

The optional Rosette consumer already supports the documented combinational
equivalence, reachability, and universal output-property queries, including
explicit packed assumptions and the exactly-one proof obligation for
`rtl.onehot_mux`. The [README](README.md) owns the implemented API, semantic
limits, and result statuses; the [development guide](DEVELOPING.md) owns the
solver boundary and validation workflow. This plan covers work beyond the
implemented combinational slice.

## Standing boundary

Formal queries consume verified public Rhodium IR through an immutable
snapshot. They do not participate in elaboration, reinterpret generator
behavior, mutate IR, or make Rosette objects available to ordinary Rhodium
programs. Unsupported reachable semantics fail closed before any proof claim.
Keep the Rosette dependency optional and one-way: neither core, frontend,
standard libraries, nor the CIRCT backend imports the formal engine.

Counterexamples and witnesses must map to stable Rhodium ports and replay
concretely. Unspecified or nondeterministic behavior requires an explicit
quantifier and choice-sharing contract; it cannot be filled with a convenient
solver value.

## 1. Additional partial-operation obligations

For each newly supported partial operation, state its validity precondition
and quantifier order independently of CIRCT lowering. Prove the precondition
under the query's explicit assumptions before interpreting the operation.
Keep unsupported or unproved cases distinct from a counterexample or a
vacuous query. Add valid, invalid, unsupported, and differential fixtures for
that operation; do not generalize `rtl.onehot_mux` by assuming all partial
operations share its exactly-one contract.

## 2. Single-clock bounded checking

Define a transition system with current state, next state, and top inputs.
Require the caller to supply an explicit clock domain and bound. Treat
resetless initial register state as symbolic unless an initial predicate is
provided, and require an explicit reset-prefix policy rather than silently
assuming reset. Interpret existing `verif.assert` guard and reset suppression
at each active edge; return cycle-indexed, replayable counterexample traces.

Exit criterion: bounded results agree with focused Verilator traces for a
counter, shift register, and small flow-control component. Multi-clock
"cycle" semantics are not inferred from one arbitrary schedule.

## 3. Memories and nondeterministic operations

Specify array state, read/write timing, initial contents, disabled reads,
out-of-range addresses, masks, and collision behavior for each memory
primitive before implementation. For synthesis freedom, define a sound
relational equivalence or refinement contract with explicit choice-sharing
and quantifier order. Add adversarial tests that distinguish a real mismatch
from permitted implementation choice, including shared-hole and
independent-hole traps.

Exit criterion: each supported primitive has an independent semantic contract
and differential coverage; all other memory or nondeterministic cases remain
explicitly unsupported.

## 4. Sketches and synthesis

Introduce a separate, explicit sketch representation with bounded holes.
Keep holes out of ordinary frontend values and public core IR. Require a
specification, finite search space, and cost objective. Re-verify and, where
possible, equivalence-check every synthesized result before exposing it as
ordinary Rhodium hardware.

Exit criterion: a small bounded task produces verified Rhodium IR and a
replayable proof query without leaking solver-specific objects into normal
elaboration.

## Stop conditions

Pause and revise the semantic contract if a supported operation lacks a
backend-independent meaning; interpreting IR requires frontend-only metadata
or mutation; unspecified behavior has ambiguous quantifier order; a
multi-clock property lacks an explicit schedule; witnesses cannot be mapped
and replayed; or the Rosette interpreter repeatedly drifts from the core
contract. These are boundary problems, not merely missing solver plumbing.
