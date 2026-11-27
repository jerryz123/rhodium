<!-- Plans the migration from demonstrated hierarchical refinement to reusable, spec-authored proofs. -->
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

## Active roadmap: reusable refinement

The [three original milestones](#delivery-milestones) are delivered for the
stated FIFO specializations. The next delivery is about **who has to write
proof code**: for a supported block, an author supplies RTL and an RSPECL spec,
including any private refinement hints. A caller selects a specialization and
supplies evidence for its immediate children. It should not need a new Rosette
procedure, evidence type, or knowledge of a grandchild to obtain a proof.

### What belongs where

| Finding in the current implementation | Destination |
|---|---|
| FIFO specs explicitly apply the [RSPECL monitor](../../rspecl/fifo.rspl) to a named event reader. The [passive view](fifo-view.rhm) samples pins for replay and the independent handwritten proofs; it contains no behavior or refinement relation. | Keep protocol semantics in ordinary helpers and relate child histories by name. The language does not infer a FIFO contract from a handshake. |
| [QueuePair](../../examples/formal/queue-pair.rspl) and [QueuePairSeries](../../examples/formal/queue-pair-series.rspl) express ordering and transfer transport in RSPECL. The handwritten oracles call [FIFO-specific issuers](refinement.rkt) directly, outside generic certification. | The parent's private `refinement:` names child occurrences and projects their public histories. Use the actual RTL handoff in induction, not an assumed internal event. Keep exact-driver checks in independent structural tests. |
| [ShiftQueue](../../examples/formal/shift-queue.rspl) owns both its RTL-to-cycle and cycle-to-monitor relations. Its [handwritten oracle](tests/shift-queue-induction.rkt) is independent. | Use this split when porting other leaves: register encodings and physical observation rules belong in checked spec hints, not the runner. Keep coverage, fault injection and replay in tests. |
| [TwoTagROB](../../examples/formal/two-tag-rob.rspl) derives its tag order from typed child history; the [logged parent](../../examples/formal/logged-two-tag-rob.rspl) consumes public retirement history. The [stall spec](../../examples/formal/stalled-offer.rspl) declares its supplier and forwarding occurrence. | Keep implementation invariants and wiring roles in RSPECL. Use proofs on actual pins to discharge requirements; neither declaring a supplier nor matching a port shape is evidence. |

**Trust boundary:** ordinary Rhombus helpers may construct typed spec IR.
A relation, invariant, or pin mapping in `.rspl` is a *candidate to check*,
never evidence. The engine still owns actual RTL pin and occurrence identity,
types, clock/reset, care and definedness, feasible domains, independent child
states, initialization and preservation, and issuance of sealed results.
Keep adjacent-sample induction and exact forwarding as generic rules. Do
not put solver callbacks in specs, infer guarantees from a module name, or
treat a matching batch schema as proof that two monitors agree.

### Migration order and exit tests

1. **FIFO behavior in RSPECL (complete).** The
   [ordinary helper](../../rspecl/fifo.rspl) accepts a capacity, word width
   and event sampler. Its [equivalence proof](tests/fifo-monitor-equivalence.rkt)
   compares arbitrary batches for two-bit words at capacities 2, 4 and 6:
   the verdicts agree, accepted batches produce equal live contents, and
   rejected batches hold each monitor's own history.
2. **Remove FIFO dispatch from generic proof issuance (complete).**
   ShiftQueue's ordering observer reads its generic seal at depths 1–3.
   QueuePair(1) and QueuePair(2) now prove their two- and four-word bounds
   from generic ShiftQueue seals using `child`, `history`, and `relate` on
   the elaborated wiring. QueuePairSeries derives both shared FIFO capacities
   from its depths; the same spec proves (1,1), (1,2), and (2,1). Only its caller names QueuePair.
   [ArbiterQueue](../../examples/formal/prove-arbiter-queue.rhm) now proves
   depths 1–3 from the Arbiter transfer and ShiftQueue FIFO contracts; two-lane
   exclusivity is a checked guarantee, not an environment assumption.
   [BufferedArbiterQueue](../../examples/formal/prove-buffered-arbiter-queue.rhm)
   reuses that seal and a one-word ShiftQueue for the 2-, 3-, and 4-word bounds.
   [GatedArbiterQueue](../../examples/formal/prove-gated-arbiter-queue.rhm)
   forwards the child monitor at depths 1–3 after checking exact transfer
   equality on the wrapper RTL. [TwoTagROB](../../examples/formal/prove-two-tag-rob.rhm)
   proves retirement order and payload from the depth-two ArbiterQueue's public
   word history and its own registers, without a FIFO-specific cut.
   Keep the ordering observer's theorem unchanged, the 2/4/6-word bounds,
   fill/drain replay, and the crossed-ready, wrong-payload, reversed-history
   and ready-when-space negatives. The [generic issuer](../../rspecl/proof.rkt)
   derives leaves and binds child seals without inspecting the protocol.
   An observer may read generic evidence but must not mint it.
3. **Make proof setup declarative.** ShiftQueue and QueuePair now have a
   [standalone proof](../../examples/formal/prove-queue-pair.rhm). The same entry point
   now proves the [combinational Arbiter](../../examples/formal/prove-arbiter.rhm);
   its grant monitor is reusable by ArbitratedCapture. The
   [registered selector leaf](../../examples/formal/prove-registered-lane-selector.rhm)
   retains its off-reset enable-exclusivity premise. The
   [GatedCapture proof](../../examples/formal/prove-gated-capture.rhm) discharges
   that premise on the gated enables and retains the wrapper's own
   `capture_exclusive`. The [ArbitratedCapture proof](../../examples/formal/prove-arbitrated-capture.rhm)
   discharges that premise on the gate's RTL pins with the Arbiter grant contract;
   the outer capture contract has no input premise. The same runner separately
   proves the one-bit zero/nonzero readback monitor from those two child contracts.
   GatedCapture can export just the bit under the same input premise; with that
   child contract, ArbitratedCapture can prove zero/nonzero but not exact readback.
   ArbitratedCapture and QueuePairSeries also run from a supplied map of
   immediate-child seals. The parent checks its cut without invoking any child proof.
   Keep the handwritten harnesses as
   differential oracles. For parents, drive the
   existing generic obligations from the spec's declared child views and
   supplier roles. [LoggedTwoTagROB](../../examples/formal/prove-logged-two-tag-rob.rhm)
   now proves its forwarded batch and recorder state using just the sealed ROB.
   The [wrapped stalled receipt](../../examples/formal/prove-stalled-offer.rhm)
   now discharges its declared requirement with ShiftQueue's stable-offer seal.
   Wrong occurrence, missing supplier, FIFO-only evidence
   for stability, swapped bank and valid-only capture must still fail.
4. **Prove reuse on an independent design.** Use the library
   [OfferRegister](../../flow/offer-register.rhdl), whose occupied word may be
   replaced *during a stall*. Its [two-bit leaf proof](../../examples/formal/prove-offer-register.rhm)
   certifies old-word delivery and new-word retention on a simultaneous update;
   the [regression](tests/offer-register.rkt) rejects the stable-offer claim and
   replays stalled replacement in RTL and rspecsim. The
   [OfferQueue parent](../../examples/formal/prove-offer-queue.rhm) reuses
   that seal and ShiftQueue's FIFO seal to prove order after handoff, without
   reopening either child or changing the engine. Its [regression](tests/offer-queue.rkt)
   distinguishes allowed replacement from a bypass that commits the incoming
   update instead of the old offer.
   The [AtomicFork proof](../../examples/formal/prove-atomic-fork.rhm) checks
   (outputs, word width) = (1,1), (2,2), and (3,8) with the same spec and runner.
   Its [regression](tests/atomic-fork.rkt) distinguishes a one-sided transfer
   from a simultaneous transfer with only the third output's high bit corrupted.
   The [Broadcast proof](../../examples/formal/prove-broadcast.rhm) uses a
   [parameterized monitor](../../rspecl/broadcast.rspl) that tracks one owed copy per
   recipient, checked at (1,1), (2,2), and (3,8). Only the leaf refinement reads the
   pending mask and payload. [QueuedFork](../../examples/formal/prove-queued-fork.rhm)
   proves the same monitor from AtomicFork and one queue contract per recipient. The two
   implementations need not agree on ingress readiness when the last recipient drains.
   [BroadcastReceipt](../../examples/formal/prove-broadcast-receipt.rhm) uses either
   seal at the same occurrence to prove one pulse exactly on the last delivery at all three sizes.
   Its [split-delivery regression](tests/broadcast-receipt.rkt) distinguishes a
   completed old word from its simultaneous replacement and rejects a both-fire-only recorder.
   Its `fanout` binding now names the public broadcast contract, not either
   implementation. Supplied evidence must certify the exact pin sampler and
   monitor on the actual child; a weaker guarantee on identical events fails.
   A missing general capability is a separate proposal with two clients and
   a failing negative test, not an exception for OfferRegister.

Done means one supported proof path for FIFO and non-FIFO monitors, at least
one independent library adoption, and no new required companion proof file
per module. Keep the existing finite, single-clock safety scope. Stronger
monitor implication, persistent proof caching, fairness and additional RTL
semantics are not prerequisites for this migration.

## Delivery milestones

| Delivered milestone | Acceptance evidence |
|---|---|
| Execute a specification | Concrete and symbolic steps agree on the finite FIFO specialization. |
| Prove a leaf | Reset and inductive preservation pass for ShiftQueue at depths 1–3; a faulty payload replays from reset. |
| Prove a parent | Independent child specs give QueuePair ordering and capacity without child RTL registers. The depth-one ready-when-space counterexample remains reachable. |

<details>
<summary>Delivered examples, proof scope, and regression references</summary>

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
The [series parent](../../examples/formal/queue-pair-series.rspl) now composes two such theorems
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
For each child depth (one and two), the [RSPECL proof](../../examples/formal/queue-pair.rspl)
certifies the declared FIFO monitor using two verified ShiftQueue occurrences
and a private downstream-first history relation. Each parent seal feeds
the existing ordering observer; a crossed handoff or reversed relation earns
no evidence. The same occurrence-based transaction induction also proves
[two non-FIFO monitors in parallel](tests/parallel-monitors.rspl); crossing
the inputs breaks the toggle child's history relation.
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
ArbiterQueue and FIFO specs. The same parent now has a
[transaction-only proof](tests/buffered-arbiter-queue-transactions.rkt) at
capacities 2, 3, and 4: the checker composes only the exported child contracts
on the elaborated RTL wiring. A crossed lane payload or miswired handoff ready
violates the parent FIFO contract.
These proofs do not inspect child RTL state and are not
quantified over depth. The [gated wrapper](tests/gated-arbiter-queue-order.rkt)
adds one parent-local admission register while the child remains opaque; this
is a limited RTL fragment, not arbitrary parent logic. Its [transaction proof](tests/gated-arbiter-queue-transactions.rkt)
also exports order and capacity at depths 1–3: the wrapper preserves the
child's accepted and retired batches without using its cycle behavior. The [two-tag reorder
buffer](tests/two-tag-rob-proof.rkt) now proves that completion may be out
of order while retirement must follow issue order and the head's own
completion. It uses the verified depth-two ArbiterQueue and per-tag busy, done, and generation bits plus two unreset result banks.
The [transaction-only ROB proof](tests/two-tag-rob-transactions.rkt) now
establishes oldest-grant retirement, the two-tag bound, and the first
matching-generation completion payload on each retirement, without stepping
the queue's cycle spec. The swapped-bank mutant fails on payload alone.
The [shared checker](../../rspecl/proof.rkt) now issues run-local transaction evidence
using the [monitor and implementation relation in the ROB spec](../../examples/formal/two-tag-rob.rspl);
the issuer and every consumer step that same monitor snapshot.
An independent [commit-log observer](tests/rob-commit-log.rkt) proves the
kth retirement records the kth grant's tag and first matching-generation
payload using only public event batches. This is reusable event evidence,
not a cycle replacement or a guarantee of progress.
The [last-commit RTL wrapper](../../examples/formal/logged-two-tag-rob.rhdl)
now consumes that seal at a physical child occurrence. Its [RSPECL refinement](../../examples/formal/logged-two-tag-rob.rspl)
issues reusable recorder evidence: [readback after capture](tests/logged-refinement.rkt)
uses only that evidence. A parent-owned ordered-retirement contract lets the
[ordinal observer](tests/logged-ordinal.rkt) connect that record to the kth
grant and first matching-generation response using the same parent seal and
one joint public sample, unchanged when the child is renamed. The [independent handwritten proof](tests/logged-two-tag-rob.rkt)
retains the reset-to-stall replay of a valid-only write. This cut covers one
forwarded transaction boundary, not arbitrary adapters or multiple sealed children.

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
The proved outer cycle spec also implies a transaction view: readback on a capture
is the previous winning word. The [derivation](tests/selector-refinement.rkt)
uses the same cycle-to-monitor induction as ShiftQueue without reopening either
implementation. Both declarations now own their private cycle-to-monitor relation;
the issuer needs the certified cycle, top, and spec constructor, not a separate
projection supplied by the proof harness. Expecting the new winner on that edge
fails preservation.

An independent route now proves the same parent guarantee from the gate's
conditional transaction contract. The verified combinational arbiter establishes
`gate/capture_exclusive` on the wired pins before the parent uses the gate's
identical event monitor. Raw requests fail at premise discharge; a crossed
payload fails batch comparison even when exclusivity holds. The exported parent
has no input premise. The general transaction proof also uses that checked
boundary to prove a different parent monitor: one bit tracks whether the
gate's previous winning word was zero. The reset and step relation uses only
the gate's public history. The same one-bit observer also passes using only the
two child transaction contracts. The arbiter's public verdict establishes the
gate's pin premise; exclusivity alone admits the gate but cannot prove winner
readback. Duplicating a grant fails on the actual pins, and mutually dependent
same-edge premises cannot authorize one another. This covers acyclic discharge
of current-input predicates, not history-dependent environmental assumptions.
The gate contract can itself be proved by forwarding the registered selector's
transaction contract: the gate retains its declared pin assumption, and only
the outer arbiter discharges it. Neither composition needs a whole-gate cycle
proof. Dropping the gate premise fails at the inner selector; duplicating the
arbiter grant still fails at the outer gate.
The general monitor proof can also project the selector's full word to one bit
at the gate while retaining that same assumption. The outer proof discharges
the assumption and uses only the zero bit; it cannot certify exact nonzero
readback. Both routes use the same RTL and input contract.

The first history-dependent input requirement is [stalled-offer stability](../../examples/formal/stalled-offer.rspl).
ShiftQueue(1)'s verified cycle behavior establishes a promise across adjacent
samples; a consumer uses it to remember the first stalled word through acceptance.
Only the pending bit is public receipt history; the consumer proves a private
relation between the input monitor's stalled word and its own recorder.
A forwarding wrapper retains the requirement on its own pins. The outer parent
discharges it with the queue's seal, without naming or opening the recorder.
This covers safety after reset on a shared clock, not eventual acceptance or
arbitrary temporal logic.

</details>

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
theorems alone. A one-child wrapper with supported local RTL can also use
the child's FIFO theorem if it proves that every transfer is preserved.
General theorem-only composition remains future work. Parent verification
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
