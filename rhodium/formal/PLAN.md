<!-- Outlines the first compositional refinement experiment and its proof boundaries. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Executable specification and refinement

The [public API](README.md) checks combinational equivalence. The
[internal single-clock stepper](DEVELOPING.md#register-transition-foundation)
supplies RTL states, inputs, outputs, simultaneous updates, and partial-operation
obligations. It expands child state; it is not a compositional verifier. Keep
Rosette optional and outside elaboration.

1. **Define observations before syntax.** Start with a deterministic finite FIFO
   specification and a concrete interpreter. Specify initialization, sampled
   reset, simultaneous transfers, and which outputs matter on each edge.
   Distinguish cycle-level ready/valid observations from completed transactions.
2. **Prove a leaf.** Start with a shallow register-backed FIFO. Compare bounded
   traces to find and replay mistakes, then separately discharge initialization,
   observation agreement, and one-step preservation of a user-supplied state
   relation and inductive invariant. Prove partial-operation domains. Specify
   how existing RTL assertion effects are handled before admitting designs
   containing them; never assume the assertion being checked.
3. **Prove a parent without expanding its children.** Compose two verified FIFO
   specifications, discharge their environmental assumptions at the boundary,
   and prove that internal transfers preserve the abstract sequence. Exhibit a
   counterexample to exact readiness when the upstream FIFO is full but the
   downstream FIFO has space. A transaction-order proof does not imply cycle
   equivalence, fairness, or progress.

Defer nondeterministic choices, mixed-abstraction simulation, and exported
lemmas until the deterministic substitution rule has been exercised. None of
these milestones is delivered by an RTL trace replay or a bounded pass.
