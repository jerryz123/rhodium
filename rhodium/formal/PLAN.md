<!-- Defines Rhodium's formal roadmap, refinement contracts, and acceptance criteria for compositional proofs. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rhodium formal plan: executable specifications and hierarchical refinement

Rhodium's formal direction is to describe a block's permitted behavior in an
independent executable model, prove that its RTL refines the model, and let a
parent use the verified child model in place of its RTL. The first demonstration
is a pair of FIFOs: prove the leaf once, then prove ordering in the pair without
reading either child's RTL registers.

**Implemented today:** [rspecsim](README.md#concrete-specification-api)
executes a typed reference model and the FIFO example without Rosette. The
[symbolic spec step](DEVELOPING.md#symbolic-specification-step) interprets the same expressions
and agrees with the concrete step for the depth-two, two-bit FIFO. The
[RTL equivalence API](README.md#rtl-equivalence-api) checks combinational
equivalence. The [internal RTL stepper](DEVELOPING.md#register-transition-foundation)
provides pre-edge outputs, simultaneous register updates, and partial-operation
obligations for a supported single-clock hierarchy. It expands child state;
it does not prove refinement. Rosette stays optional and outside elaboration.

## Delivery milestones

All three FIFO milestones are complete for two-bit queues at depths one and
two. The [leaf proof](DEVELOPING.md#shiftqueue-refinement-by-depth)
now checks reset and preservation separately for depths 1, 2, and 3;
the faulty-payload replay remains at depth 2. The
[series proof](DEVELOPING.md#two-shiftqueues-in-series) discharges the boundary
and proves ordering and capacities two and four using two independent spec states.
It now exports a run-local FIFO transaction contract. An
[event-only consumer](tests/queue-pair-transaction-order.rkt) proves retirement
order from that contract without stepping the children. This result cannot
drive cycle pins or require admission whenever space remains.
The [series parent](tests/queue-pair-series.rkt) now composes two such theorems
across a checked RTL handoff and exports a six-word FIFO bound and ordering.
The [executable event monitor](fifo-monitor.rhm) now gives rspecsim and Rosette
one transaction model: equivalence to the original relation is checked at
capacities two, four, and six, and rspecsim replays the six-word parent trace.
The [bound FIFO views](tests/queue-pair.rspl) now declare which physical
handshakes form those batches. Certification and replay execute the same
passive sampler; the series proof still checks the physical handoff independently.
The [ShiftQueue transaction proof](tests/shift-queue-transactions.rkt) now
derives this weaker contract directly from verified cycle evidence at depths
1–3 using grouped handshakes, without rechecking RTL or exposing diagnostics.
The [transaction-only QueuePair proof](tests/queue-pair-grouped.rkt) now consumes
two such leaf results directly. The checked grouped handoff yields bounds two
and four and ordered retirement without stepping a child cycle model. The
six-word series reuses those results; the separate cycle proof still owns
the reachable readiness counterexample.
The parent fixtures share an admission check: each
occurrence needs run-local evidence for its actual implementation and full
contract. No persistent evidence cache or general parent-logic checker is
implemented. The depth-one pair's readiness counterexample is replayed in
rspecsim and compiled RTL; the ArbiterQueue capacity mismatch is also replayed against RTL.

The [two-input fixed-priority arbiter](DEVELOPING.md#two-input-fixed-priority-arbiter)
is a second verified leaf. Unlike the FIFO, its cycle outputs depend on
current inputs. The [arbiter-to-FIFO proof](DEVELOPING.md#arbiter-feeding-a-shiftqueue)
derives the parent wiring from elaborated RHDL and substitutes the verified
children there. An [independent parent contract](../../examples/formal/arbiter-queue.rspl)
records external winners, not child events or instance connections. The proof
establishes separately at depths 1, 2, and 3 that each dequeue returns the
oldest accepted winner since reset. Its successful refinement result can
itself authorize substitution. A [two-lane transaction proof](tests/arbiter-queue-transactions.rkt)
now uses the ready/valid events declared in the cycle spec and projects its
`pending` sequence without reopening either child. It proves exclusivity of
actual ingress transfers and exports order and capacity, not arbitration policy.
The [next level](tests/buffered-arbiter-queue-order.rkt)
proves ordered retirement with an additional one-slot FIFO using only the
ArbiterQueue and FIFO specs. Wrong-lane wiring fails induction and cannot
supply that result. These proofs do not inspect child RTL state and are not
quantified over depth. The [gated wrapper](tests/gated-arbiter-queue-order.rkt)
adds one parent-local admission register while the child remains opaque; this
is a limited RTL fragment, not arbitrary parent logic. The [two-tag reorder
buffer](tests/two-tag-rob-proof.rkt) now proves that completion may be out
of order while retirement must follow issue order and the head's own
completion. It uses the verified depth-two ArbiterQueue and per-tag busy, done, and generation bits plus two unreset result banks.

Milestone 3 also has a [nontrivial input-contract check](DEVELOPING.md#discharging-a-conditional-childs-inputs):
an unconditional arbiter establishes a selector's at-most-one enable requirement,
either directly or through a conditional lane router. The latter discharges
two contracts in dependency order. Raw requests and duplicated routed grants
fail before substitution; mutually dependent contracts cannot justify themselves.
The registered selector extends this to a clocked leaf: reset clears the
word with any enables, and non-reset edges capture or hold it under the
discharged grant contract. This covers stateful guarantees under scalar
current-input contracts, not history-dependent protocol assumptions.
A [conditional parent](../../examples/formal/gated-capture.rspl) now discharges
the selector's premise from its own contract and wiring. The next level
reuses the wrapper as an opaque child and discharges that contract anew;
the outer arbiter needs no restriction on simultaneous requests.

| Milestone | What to build | How to tell it works |
|---|---|---|
| 1. Execute a specification | Define the cycle and transaction views, a finite deterministic spec IR, and a concrete interpreter. Use a bounded sequence as the FIFO model; choose surface syntax after the step semantics work. | An independent oracle checks idle, enqueue, dequeue, simultaneous transfer, reset from nonempty, and ignored payload when invalid. Concrete and symbolic steps agree for a small finite specialization. |
| 2. Prove a leaf | Bind one register-backed FIFO configuration to the model with a state relation. Use bounded replay to debug it, then check initialization, active RTL assertions, operation domains, observation agreement, and inductive preservation. | All obligations pass for the stated configuration. A defect in the returned payload produces a reachable mismatch that replays from reset. A bounded pass alone cannot authorize substitution. |
| 3. Prove a parent | Connect two verified FIFO cycle models, discharge their input assumptions, and prove the parent's transaction-order and capacity contract. | The proof inventory contains two independent child spec states and no child RTL registers. Ordering passes; the stronger exact-readiness claim fails on the depth-one example below. An undischarged child assumption prevents reuse. |

## What a module promises

A module can have three independent artifacts: its implementation, an executable
specification of permitted behavior, and evidence that the former refines the
latter. The specification need not use hardware registers or describe the
microarchitecture. A parent may depend on the spec only through an explicit
boundary: **which inputs the environment may supply, what the parent may
observe, and when it may observe it**.

Write `I ⊑ S` when, for every environment allowed by that boundary, every
observable finite execution of implementation `I` is permitted by spec `S`.
This is one-way inclusion, not equality. A fixed-priority arbiter could implement
“grant any requester”; the converse is false. Finite-trace inclusion alone
does not require an implementation ever to grant a request.

## Executable model

For the first checker, specialize widths and capacities at elaboration time.
Use finite two-state values, records, products, and bounded sequences. Model
one explicit rising-edge clock. A spec declares state, legal initialization,
typed inputs, and an atomic step. Its concrete and symbolic interpreters must
evaluate the same typed operations:

```text
step(old_state, sampled_inputs) -> (pre_edge_outputs, event, next_state)
```

Read all guards and right-hand sides from the old state and current inputs;
commit the next state simultaneously. Unassigned state holds. Simultaneous
actions must have a defined joint transition: compose disjoint updates, or
write an explicit combined rule. Reject conflicting writes without a rule;
source order is not a scheduler. For a deterministic cycle model there must be
exactly one defined step for every legal state and allowed input, including
idle cycles. Preconditions on an accepted action are not assumptions that
prevent an environment from *offering* that action.

Separate the pure spec IR and concrete interpreter from the solver adapter.
Keep the existing [RTL stepper](DEVELOPING.md#register-transition-foundation)
as the implementation semantics. Neither spec values nor Rosette terms become
RTL values, and ordinary elaboration does not run proofs. The implemented
[spec block](README.md#author-an-executable-spec) binds to a circuit’s inputs and declares state,
cycle observations, guarded events, and simultaneous updates. Refinement bindings
and theorem obligations remain separate proof code; a theorem is not a step.

## Observation, time, and reset

| View | What is observed | What substitution permits |
|---|---|---|
| Cycle | A sample on every edge: named control signals and declared data fields with explicit care conditions. | A cycle-sensitive parent may use the specified signals when cared. An idle edge still counts. |
| Transaction | An event records completed transfers and their payloads. An edge with no visible event may be erased. Simultaneous visible transfers are one event, not two independently reorderable actions. | A consumer restricted to this event stream may use order and values, not exact readiness, latency, or throughput. |

The view names every relevant port and direction, the sampling edge, and any
hidden or conditionally cared values. For example, comparing output payload
whenever `valid` is true is stronger than comparing it only at handshake. A
parent that branches on invalid payload, a hidden sideband, or a count of idle
edges cannot use a view that hides that information. A physical RTL parent is
not transaction-only merely because its ports are called ready/valid.

Initialization is part of the claim. The first FIFO proof starts from arbitrary
RTL bits, applies one explicit sampled reset, and proves that the resulting
state represents the empty sequence. Claims begin after that edge. Later reset
edges emit a public reset event, cancel transfers for this view, and start a
new empty epoch. Reset does **not** imply that physical pre-edge offers are
zero or that unused payload registers are cleared. A parent that observes those
offers during reset needs a stronger contract.

## What the checker must prove

Let `R(r,s)` relate RTL state to spec state; a mapping `s = α(r)` is the common
special case. Use an auxiliary invariant `Inv` if not every bit pattern is
reachable. For a fixed specialization the checker must discharge:

1. **Initialization.** Every permitted RTL start (or every state after the
   specified reset prefix) has a legal related spec start satisfying `Inv`.
   Show that the starting contract is feasible.
2. **Definedness.** Under the established invariant and environmental contract,
   every admitted input has a legal implementation step. Prove partial-operation
   domains and all active ordinary [RTL assertions](../core/README.md#clocked-assertions)
   as obligations. An assertion cannot constrain the search for its own failure.
   The fixed FIFO proof discharges the extracted step obligations at reset
   and on every related edge.
3. **Observation and preservation.** For every related invariant state, allowed
   input, and possible implementation step, find a legal spec step with the
   same observation and a related successor satisfying `Inv`. For an invisible
   transaction step the spec may stay put, but the relation must still hold.
   A visible transfer cannot be matched by stuttering. For a cycle contract the
   spec must account for exactly one cycle.

Bounded exploration is useful for debugging these obligations but cannot
produce a reusable induction result. A failed induction step may start in an
unreachable state: report that state and the failed obligation, not a claimed
RTL execution from reset. A reachable counterexample must replay from the
declared start with the complete inputs, observations, and first difference.
Report infeasible assumptions, unsupported semantics, solver uncertainty, and
replay failure separately; none authorizes substitution.

## The composition rule

For parent wiring and local logic `W`, the intended rule is:

```text
A ⊑ SA       B ⊑ SB       W[SA, SB] ⊑ SP       boundary checks discharged
-----------------------------------------------------------------------
                         W[A, B] ⊑ SP
```

The parent proof state is its local state plus one independent abstract state
per child occurrence. Instantiate a child's spec at the cut **before** lowering
the remaining RTL; flattening all registers and hiding their names afterward
does not qualify. The boundary checks are part of the rule:

- Match specialized interfaces, observation views, clock/reset epochs, and
  legal wiring. The parent may project to a weaker view for its own export;
  it cannot use that projection to erase signals its internal logic consumes.
- Prove each child's environmental assumptions from the parent contract,
  wiring, and already established facts. Check that the parent contract admits
  executions. Checking assumptions for satisfiability is not proof that the
  wiring satisfies them. Reject circular same-edge dependencies between
  assumptions and guarantees in the first checker.
- Bind leaf evidence to the implementation, spec, parameters, view, assumptions,
  and semantics that were checked. The parent proof depends on the child
  **contract**, not the child's implementation fingerprint. An implementation-only
  change requires new leaf evidence for that same contract; changing the view
  or a relied-on assumption invalidates the dependent parent claim.

Cycle-sensitive RTL cuts use the **operational** child spec. Event-only observers
and direct series wiring between two certified FIFO endpoints can use exported
theorems alone; general theorem-only composition remains future work. Parent verification
must consider all behavior those theorems permit. A theorem-only contract is
not automatically an executable replacement for simulation.

## Worked example: two queues, two different claims

Use [ShiftQueue](../../flow/shift-queue.rhdl) with fixed depth `N > 0`,
`pipe = false`, `flow = false`, and no flush. The independent model stores
only a sequence `q`, oldest first. In a non-reset cycle:

```text
in.ready  = length(q) < N
out.valid = length(q) > 0
out.data  = head(q) when out.valid             # otherwise not observed
take_in   = in.valid and in.ready
take_out  = out.valid and out.ready
next_q    = (tail(q) if take_out else q) ++ ([in.data] if take_in else [])
```

This defines idle, enqueue, dequeue, and simultaneous transfers without
evaluating `head` or `tail` on an empty sequence. A full queue cannot replace
its head on the same edge in this configuration. Map the compact occupied
prefix of RTL payload slots to `q`; prove compactness rather than assuming it.
If exported in the cycle view, `count = length(q)` and `mask` has exactly the
lowest `length(q)` bits set. Otherwise explicitly exclude those ports and
verify that the parent does not use them.

Connect A's output to B's input. The parent uses the children's **cycle**
contracts to determine all three handshakes; it exports a **transaction**
contract at the two external ports. Its abstract contents are `B.q ++ A.q`.
External dequeue removes the oldest element; external enqueue appends the new
element. Moving A's head to B's tail preserves that concatenation. If a transfer
coincides with either or both external handshakes, it is the *internal part*
that disappears, not the entire edge. Prove ordered payloads and capacity
`2*N` within each reset epoch. This is not a proof of eventual delivery.

**Required negative example:** use two depth-one queues. From empty, accept
`x` into A. Before the next edge A is full and B is empty. The chain reports
`in.ready = 0` even though occupancy is one and an ideal depth-two queue
reports `in.ready = 1`. On that edge A can move `x` to B, but cannot accept
`y`. This trace must refute the stronger readiness claim while leaving
transaction-order refinement intact. The transaction spec accepts idle steps;
it does not promise admission whenever space exists.

## Later extensions

Concrete simulation executes the spec directly. For future nondeterministic
`choose`, a simulation policy selects a legal outcome and records the choice
for replay. For example, “grant any requester” permits a lowest-index policy;
that one simulation does not establish anything about all arbiters. A theorem
about the spec must hold for **all** legal choices. Refinement instead requires
**each** implementation behavior to have **some** matching legal spec behavior;
the matching prefix must remain consistent. Reject nondeterminism in the first
checker rather than accidentally equating these quantifiers. Mixed RTL/spec
simulation must respect the same observation boundary as formal substitution;
a functional FIFO model is not a performance model without a timing contract.

The implemented reorder-buffer demonstration covers fixed tags A and B and
their first two-bit completion results, including stability under backpressure.
One-bit issue tokens distinguish adjacent generations within a reset epoch.
Rejection of old responses across token wrap or reset, and architectural squash,
remain beyond that proof. Unbounded
liveness, fairness, memory primitives, multiple clocks, parameter-universal
proofs, and RTL generation from specifications are outside the first delivery.
