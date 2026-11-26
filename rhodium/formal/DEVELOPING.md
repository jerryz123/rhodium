<!-- Explains how to extend and validate Rhodium's Rosette-backed formal engine. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the formal engine

Read the formal [README](README.md) for the public queries, assumptions, result
statuses, supported semantics, and fail-closed limits. This guide owns the
implementation and coverage workflow.

## Architecture and ownership

The formal engine is an optional consumer of verified public IR. It validates
the complete hierarchy, snapshots the supported structure, translates packed
values and operations into Rosette bitvectors, solves the requested query, and
replays any model concretely before returning it.

```mermaid
flowchart LR
  Subject["Verified design and explicit top"] --> Snapshot["snapshot.rhm<br/>closed supported hierarchy"]
  Query["FormalQuery input assumptions"] --> Preflight["preflight.rhm<br/>concrete checks"]
  Snapshot --> Preflight
  Preflight --> Engine["engine.rkt<br/>Rosette translation and solving"]
  Engine --> Replay["Concrete replay"]
  Replay --> Result["Equivalence result or counterexample"]
```

[`main.rhm`](main.rhm) owns the public Rhombus API and result construction.
[`snapshot.rhm`](snapshot.rhm) owns backend-independent traversal and immutable
input to the RTL solver boundary. [`preflight.rhm`](preflight.rhm) checks packed
types, operation shapes, decode domains, port compatibility and input constraints
without importing Rosette. Unsupported semantics carry source diagnostics;
malformed snapshots are errors. [`engine.rkt`](engine.rkt) interprets only RTL;
the symbolic spec interpreter consumes a separate format. Core, frontend, and backend packages must not import
the formal engine.

The independent [spec.rhm](spec.rhm) owns typed expression construction,
reference validation and rspecsim, the concrete single-edge interpreter. It does not import
the hardware graph or the solver. Keep new operators as typed data with
defined behavior on partial inputs, not callbacks that a symbolic interpreter
would have to reverse-engineer. The [FIFO example](../../examples/formal/shift-queue.rspl)
is a client, not a privileged opcode.

[The RSPECL reader and language profile](../../rspecl/language.rhm) expose
[spec-dsl.rhm](spec-dsl.rhm) to `.rspl` authors without loading Rosette.
The profile handles `spec for` in two Rhombus contexts: a module declaration
exports the generated constructor, while a local definition only binds it.
Both expand through the same constructor builder; ordinary helpers retain
Rhombus export rules.
For `spec for rtl.Name`, both circuit forms use the declaration's namespace to supply
scalar input, interface, indexed interface, and non-private parameter names at expansion time. RSPECL
introduces lexical bindings in the spec's scope; model construction checks
the actual top's family identity before reading its ports or configuration.
Parameter references read only the retained integer or Boolean values, so
an unused type descriptor or function never enters the model.
[The RTL boundary adapter](../../rspecl/rtl-boundary.rhm) projects each
interface direction record into logical member names. Only input members
can build expressions; output members are observation targets. The leaf proofs
pack and unpack the actual library ports in declared record order. Explicit `ignore` excludes
an output from the leaf guarantee, never from the RTL itself.
The frontend does not import RSPECL, and the spec body is not scanned for
unbound names. The profile neither elaborates a second module nor passes one
into the behavioral model. [port-types.rhm](port-types.rhm) checks observation
names and sorts. All sampled data inputs remain in the schema, even unused ones.
The DSL collects named declarations and rejects duplicate fields before assembling
the maps. Emit each right-hand side at its original lexical site: a later
`let` must not change an earlier output. Rhombus expands expressions,
operators and helper calls, including imported bindings. Only their resulting
typed IR crosses the interpreter boundary. The [surface tests](tests/spec-dsl-test.rspl)
exercise a separately imported helper, lexical shadowing, host short-circuiting
and partial sequence reads; the proof bindings use the same model unchanged.

Cycle and monitor bodies share the lexical `state`/`next` lowering. For a
`monitor:`, bind batch names as spec inputs, emit every guarantee as an
always-present Boolean output, and guard each update with their conjunction.
Keep this in the language profile: both rspecsim and Rosette already implement
guarded evaluation. The sampler, the translation from sparse sampled events to
typed batch fields, and the decision to issue proof evidence remain separate.

### Symbolic specification step

`spec_snapshot(model)` copies the checked input/state schemas, typed expressions,
care predicates, input assumptions and updates. The internal [spec-engine.rkt](spec-engine.rkt)
entry point `symbolic-spec-edge` consumes that snapshot and returns one edge
from fresh pre-edge state and input terms, **not** a run from `model.initial`.
Its `legal` predicate excludes out-of-range sequence counts only. Use
`spec-input-assumptions` separately: the same predicates are premises at a
conditional leaf and proof obligations at its parent. Sequences
use a bitvector count and fixed scalar slots; only the live prefix is meaningful.
The result has separate output/event maps of (care, value) and simultaneous
next-state terms. Compare presence first and payload only when present.

The separate `defined` predicate covers every executed partial operation,
including reads in care predicates. Inactive operands of a conditional or
short-circuit conjunction incur no obligations. Do not assume `defined`:
ask whether any legal edge violates it before comparing values. For the FIFO,
`tests/spec-symbolic-test.rhm` calls the concrete step for every legal row;
the solver checks coverage, definedness and disagreement over arbitrary padding.

### Register-transition foundation

The solver-free [transition planner](transition-plan.rhm), also exposed by the
engine as `prepare_transition_snapshot(snapshot, clock)`, lowers a
verified single-clock hierarchy once. It expands each register occurrence into
a synthetic current-state input and next-state output of a combinational
snapshot; the existing packed interpreter owns all arithmetic and aggregate
semantics. Resolve instance ports lazily by value, not by eagerly evaluating all
child inputs: feedback through a child register must not become a false
combinational cycle. Retain outputless instances, unused partial operations,
and occurrence/source diagnostics. Unsupported operations fail before lowering.

`transition_registers(plan)` returns deterministic descriptors keyed by
`Top.instance.register`. `symbolic_transition_state(plan)` creates independent,
unconstrained bitvectors for every register, including resettable registers;
`symbolic_transition_inputs(plan)` creates fresh data/reset samples, excluding
the selected clock. These are internal solver objects, not authoring values.

`step_transition(plan, state, inputs)` requires complete, exact-name assignments
with packed naturals or exact-width bitvectors. It returns pre-edge outputs and
a separate simultaneous next-state map, without mutating the plan or prior
state. Reset selects the reset value only on that sampled edge. There is no
implicit initialization, reset prefix, input forcing, or clock waveform: one
call means one rising edge of the explicitly selected top-level Clock. Only
structural clock aliases are admitted.

The step also returns obligations for partial operations and clocked assertions,
including in outputless children. Each assertion must use the selected clock;
it samples `!guard || reset || condition` before register updates, even when
only its successor state would satisfy the condition. The obligation carries
the label and original occurrence diagnostic; a false assertion is returned,
not assumed. `transition_step_validity(step)` conjoins all obligations; it is
**not a proof**. Concrete invalid operation domains are rejected. The public
combinational query API retains its existing restrictions.

### ShiftQueue refinement by depth

The shared [run-local checker](refinement.rkt) takes a completed definition,
its executable spec, the interface naming function, and an independent
observer. It snapshots the definition and spec itself. A clockless leaf is
checked over every input sample allowed by its spec. For a single rising clock and active-high
sampled reset, supply a state relation, not a premise: the checker builds the
reset and preservation domains and requires them to be feasible. Reset starts
with unconstrained RTL state and must establish both the declared spec start
and the relation. Both phases discharge RTL obligations, spec definedness,
cared physical outputs and event agreement. Callers own the interpretation of
care and transfer events and the abstraction relation. Only input assumptions
in the matched leaf contract can restrict external traffic. A counterexample, unknown, or ambient constraint
cannot mint a result.

[The leaf proof](tests/shift-queue-induction.rkt) uses one symbolic input sample
for the RTL and the executable spec, but allocates their old states independently.
For spec length `n <= depth`, RTL slot `i` must be occupied exactly when `i < n`;
only then must its two-bit payload equal spec item `i`. Slot zero is the head.

The [proof inventory](tests/composition-binding.rhm) elaborates depths 1, 2,
and 3 together; each leaf proof starts from the exact FIFO definition found
in its parent's wiring. Each occupancy from empty through full must be feasible.
On the first sampled reset, the RTL state is unrestricted, including holes
where the depth permits them. The solver checks that the successor represents the declared empty
spec start. The same reset proof covers later resets. The non-reset induction
step assumes only a legal related old state; FIFO traffic is unrestricted. Separate queries establish RTL
assertions and operation domains, spec definedness, cycle observations, the
joint event, and the successor relation. Care must match even when the value
is absent, and a valid head is observed even under backpressure. Count and mask
ports are not part of the exported view.

[`check-cycle-monitor!`](refinement.rkt) admits the exact certified cycle
definition and member layout, then steps only its certified cycle snapshot.
Each sampled output gets one shared physical value, arbitrary outside care.
The spec constructor returns the same public view plus a private state-only
IR model when the issuer requests `~cycle_derivation`. Lower `relate` and
`invariant` in the cycle's lexical scope; rebind state references, reject pins
even in care predicates, and match source sorts to the **certified** cycle model.
Never use a freshly constructed cycle model as proof evidence. The FIFO adapter
infers capacity from the projected sequence and checks the endpoint width;
other monitors use the bound dense sampler from their RSPECL declaration.
The four-argument low-level derivation still accepts explicit checked projections.
When a general monitor is derived under input assumptions, retain the exact
named predicate IR from the certified snapshot in the seal. Never evaluate a
conditional guarantee as a standalone event step. At a transaction cut, bind
its premises to the actual occurrence inputs and prove them before using
**that occurrence's** verdict. Each open output is a fresh source net shared
by its sinks. Trace the free nets in each wired predicate to their owners,
recursively admit those owners, then prove the predicate under their public
step verdicts on these same pins. Use arbitrary legal histories, not the parent
relation. Reject a visit back to an occurrence being admitted. Unconditional
stateless cycle evidence can alternatively drive the cut as a scheduled actor.
Clockless monitor sites are open nets, never scheduled cycle actors; issue
their stateless seals by checking every named guarantee on a symbolic sample.
Reject wrong occurrences and combinational cycles. Whole-batch forwarding uses
the identical public monitor and fieldwise symbolic equivalence, so it needs
neither a gate-state projection nor a new monitor induction at the parent.
For a different parent monitor, `prove-transaction` passes the admitted
combinational sites to the same pin check before using the dependent monitor.
The one-bit capture regression relates only the child's public `previous` word
to the parent's zero flag. Raw double grants must fail the named pin check;
an inverted relation on good wiring must fail reset initialization instead.
Keep the two-transaction version independent: its weak producer must pass
admission and fail the parent's readback guarantee, while duplicated grant
wiring fails admission and replays in rspecsim.

For an adjacent-sample monitor, check reset from arbitrary legal cycle and
monitor state, then step the certified cycle twice with independent inputs.
An admitted first edge is the induction hypothesis; every second edge must
pass, including reset and the handshake that ends a stall. Keep off-care
output values arbitrary. Allocate independent old states for the required
input monitor and the public receipt, and step both on the same physical
sample. Bind `required` hints to the input monitor's old, successor, or
declared initial state for the corresponding relation check. Reset must be
admitted without assuming the relation, restore the input monitor's own
initial state, and establish the relation. An admitted active edge must
preserve it. Never add required history to the child state exported at a
parent cut. To discharge the requirement, require an unconditional supplier
of the identical monitor, direct clock/reset, and fieldwise equality of the
independently sampled batches *before* using child verdicts. Alternatively,
an exact-receipt forwarding wrapper may retain the requirement: compare its
external input batch to its child's actual input batch on unrestricted pins
before admitting the child. Match the full requirement monitor (including
initial state and updates), verify receipt forwarding separately, and seal the
wrapper's sampler, not the child's. The [stalled-offer regression](tests/stalled-offer.rkt)
checks both cuts and replays stall-only payload faults in RTL and rspecsim.

For conditional forwarding, take the named, input-only predicate snapshot
from the bound parent declaration. Evaluate it on external pins only, check
reset and non-reset feasibility separately, and use that domain for wrapper
definedness, child admission and forwarded batch equality. Seal exactly those
predicates. The capture regression obtains gate evidence this way from the
selector seal, checks concrete active captures on both lanes, and keeps the
whole-gate cycle derivation as an independent cross-check.

For general monitor induction, `#:under` supplies the same input-only snapshot
independently of the selected view. Seed the joint child-step domain with the
allowed parent inputs returned by the pin check; reset, preservation, side checks
and coverage must all use it. Seal the snapshot only after induction succeeds.
The one-bit gate regression rejects a double-capture cover outside this domain;
the outer regression cannot recover an exact nonzero word from the zero bit.

Cycle derivation and hierarchical monitor refinement pass their state projections
to the [same induction](transaction-induction.rkt). Reset establishes the relation without a related-old
premise. Active edges must preserve it; definedness and the sampler domain
are goals, never premises. The [capture regression](tests/selector-refinement.rkt)
rejects a same-edge winner expectation and an invariant that excludes a legal capture.
The [leaf transaction regression](tests/shift-queue-transactions.rkt)
applies the same retirement invariant at each depth. A separate coverage query
asks whether the tracked word can stall behind an older word: UNSAT at depth
one and SAT at depths two and three. Neither answer restricts the theorem.
ShiftQueue derives a generic seal for its specialized RSPECL monitor. The
[observer reader](tests/fifo-observer-input.rkt) checks the exact top, view
and absence of premises, then calls `transaction-step`; it supplies no FIFO
transition of its own. The direct series checker still expects the legacy
evidence type, independently derived on demand from the same cycle proof.

Run `moon run rhodium:formal-test` to reproduce the proof. A failed induction
query alone is not a trace from reset. For the faulty leaf, the test obtains a
SAT payload mismatch at a singleton, reaches that queue with a real enqueue,
and replays the solver's final input through RTL and rspecsim. It checks the
first differing observation and that the same trace passes on the correct leaf.

### Two ShiftQueues in series

[QueuePair](tests/shift-queue-fixture.rhdl) instantiates the same verified leaf
definition twice. The [composition binding](tests/composition-binding.rhm) reads the
parent's ports, drivers, and instance signatures, not its children's bodies.
The leaf proof runs once on the shared definition and releases its exact spec
only after the initialization, preservation, and feasibility checks pass.
Both occurrences admit the same leaf result. There is no persistent certificate.
The [parent proof](tests/shift-queue-pair.rkt)
steps the extracted wiring, including the common clock/reset, and allocates just
`a.q` and `b.q`. Evidence for the wrong depth or a consumer of a hidden
port such as `mask` does not qualify.

The spec engine resolves pre-edge offers from each child's old state and reset
before binding the handshake inputs. A missing input dependency is an error,
not a wire equality assumed to have a solution. Uncared wires get arbitrary
values. Both child input domains and definedness must hold under the parent
invariant for every external sample; no child assumption may restrict the proof.

The external transaction sequence is the live contents of B followed by A.
Reset empties it; otherwise an external dequeue removes its oldest word and
an enqueue appends the offered word. A middle handshake agrees at both ends
and does not by itself change this sequence. The solver checks length and each
live successor position, including simultaneous external transfers. Reset/fill/drain
traces attain capacities two and four at child depths one and two. Run
`moon run rhodium:formal-test` to reproduce the proofs.

[FIFO transaction certification](refinement.rkt) takes a [bound view](tests/queue-pair.rspl),
not two hand-written endpoint lists. RSPECL checks the declaration's circuit
family; the [view binder](fifo-view.rhm) checks the actual module's port names,
directions, sorts, and clock/reset. It builds a stateless spec-IR sampler over
both inputs and outputs. An events-only body binds the helper to that module
without constructing a cycle model. A body with any cycle clauses still checks
the full model, even when only its event view is requested. The cut must match
that exact module. The caller projects the old and new
cut states into pending words; reset and preservation must prove the same
`fifo-allows?` relation that downstream clients consume. Certification also
discharges child definedness and legality. A successful result retains the
definition, endpoint mapping, width, and capacity bound, not a steppable cut.
It cannot serve as cycle evidence for `prepare-proved-wiring`. The
[FIFO monitor](fifo-monitor.rhm) defines the relation once in typed spec IR.
rspecsim returns the concrete live words on an allowed batch; certification
and consumers interpret the same IR with Rosette. The adapter rejects invalid
mathematical counts before they can wrap, ignores the predecessor count on
reset, and compares only live successor slots.

Keep the [RSPECL QueuePair induction](tests/queue-pair-refinement.rkt) alongside the
[handwritten series proof](tests/queue-pair-grouped.rkt). Both use the checked
RTL handoff and independent child contract edges. The
[transaction induction](transaction-induction.rkt) opens all declared occurrences
on one actual wire graph, interprets only parent-local RTL, and checks the same
reset and preservation obligations for QueuePair and [two parallel monitors](tests/parallel-monitors.rkt).
A protocol adapter supplies the public batch and an independent monitor step
per occurrence. It may reject bad wiring before induction; it must not supply
the relation as a theorem or mint evidence on its own. Only the RSPECL path reads the
declared monitor and evaluates the authored relation on the child histories;
it must establish an empty projected successor on reset and a defined, bounded
projection and legal public transition on every admitted non-reset edge. Reversing
the two histories must fail preservation even when every wire is correct. Run
`moon run rhodium:formal-test` for both paths and the unchanged ordinal observer.

The [event-only consumer](tests/queue-pair-transaction-order.rkt) tracks an
arbitrary acceptance ordinal and its word. Its induction ranges over **all**
joint events allowed by the exported relation, including stutter and simultaneous
accept/retire. It imports neither child model. From reset and one acceptance,
both refusal and admission of a second offer are consistent with the contract;
only the concrete implementation determines which occurs on that edge.

`compose-fifo-series` admits a narrower wiring-only parent using **only** two
transaction results. Rosette checks that both results are sealed; the concrete
[Rhombus wiring check](wiring.rhm) matches the actual child definitions, valid/ready/data
on each external ingress lane, the single handoff and egress, and each clock/reset
driver. The parent can have scalar or grouped ports; the cut allows no local behavior.
For each child occurrence, the checker obtains the certified physical-port
layout only after matching the exact definition. It follows RTL record creation
and field selection in the parent to identify the actual driver of every sink.
The [grouped QueuePair proof](tests/queue-pair-grouped.rkt) uses two independent
transaction histories, even though both sites reuse the same ShiftQueue evidence.
It needs neither child cycle state nor RTL registers. An independent ready
driver, external data substituted at the handoff, or routing the hidden mask
as head data is rejected before sharing any symbolic pins. After that check,
each lane, the handoff, and the egress get independent symbolic triples.
The checker proves the parent sampler is defined wherever both certified
child samplers are defined. For two lanes, the front contract guarantees at
most one transfer outside reset; it does not prohibit simultaneous offers.
It ranges over all successors allowed by both child relations, without using
any cycle timing guarantee. It proves the joint external FIFO rule on the downstream
pending words followed by the upstream words; only the internal transfer is hidden.
The [series regression](tests/queue-pair-series.rkt) composes the two grouped-proof
results again and feeds the resulting six-word contract to the original ordering
observer. Its separate full-RTL replay runs the same declared parent sampler in
rspecsim and sends the resulting
batches to the concrete monitor, including refusal on
the second offer and a six-word fill/drain. Corrupting the first retirement
after the same prefix is rejected. The trace is not a proof premise. A separate
symbolic regression checks both directions against the previous arithmetic
relation at capacities two, four, and six, including arbitrary slot padding.

The [depth-one replay](tests/queue-pair-trace.rhm) steps rspecsim on the extracted
wiring and sends the same eight input samples and cared pre-edge observations to
the [native RTL driver](tests/queue-pair-replay.cpp). It checks refusal during
an internal handoff, successful retry, and ordered drain. This replay runs in
`moon run rhodium:formal-differential-test`; the parent proof never expands child RTL.

### Two-input fixed-priority arbiter

[The arbiter proof](tests/arbiter-refinement.rkt) compares the clockless Flow circuit
`Arbiter(Bits(2), 2)` with its [stateless spec](../../examples/formal/arbiter.rspl).
`combinational_sample(snapshot, pins)` accepts exact-name, exact-width
bitvectors or fitting naturals and returns outputs and partial-operation
obligations, including those in child instances. Combinational preflight
rejects state. Prove these obligations even on idle samples; the proof never
assumes mutually exclusive requests or that the output is ready.

Both input ready signals and output valid are always observed. Data and chosen
index are observed whenever valid, including under backpressure. Events are
actual handshakes; exactly one input supplies each output transfer, and stalls
consume neither input. The negative fixture swaps the physical priority lanes
and maps grants back to external lane identities. The solver's contested
transfer replays as opposite winners and payloads in concrete RTL and rspecsim.
The proof is part of `moon run rhodium:formal-test`.

### Discharging a conditional child's inputs

The [selector regression](tests/selector-refinement.rkt) proves the leaf on
the nonempty conjunction of its named input predicates. The predicates are part
of the exact spec snapshot matched at reuse. After evidence matching but before
returning an admitted cut, `prepare-proved-wiring` checks each predicate as
a goal over unrestricted external samples and independently legal old states.
The discharge pass follows each predicate's input references through the checked
wiring. Reading an offer first requires its source occurrence's whole contract,
then the inputs used in both its care and value. Cache each physical offer for
the sample, including an arbitrary uncared value shared by all fanout.
The solver must establish upstream definedness and every named predicate
before that occurrence becomes available. Revisiting an in-progress contract
is a same-edge proof cycle, not a counterexample on external inputs. Even two
instances of the same verified leaf must discharge their premises independently.

The routed example discharges the router using verified arbiter grants, then
the selector using the router's outputs. Duplicating a routed grant fails
at `lanes/exclusive`; replay the returned external sample on both wirings.
The complete parent proof still checks all outputs and child transitions, not
just the cones used during discharge.

For a clocked leaf, conjoin the same current-input predicates with both phase
domains. Reset must be feasible and establish the relation without assuming
it; preservation must be feasible from a related state with reset *deasserted*.
Putting reset in the guard is an author decision, not an implicit exemption.
The registered selector regression removes the guard to expose a bad successor
and tries reset-only and non-reset-only guards to check phase non-vacuity.
At the parent cut, the relation mentions the stored word in the child's spec,
never its RTL register. The duplicated-grant witness replays after a sampled
reset, and changing only its reset bit makes the same enables legal.

For a composite block with an input contract, pass the raw `proof_cut` and
`#:child-evidence` to `prove-parent`. After matching signatures and evidence,
the checker evaluates the parent snapshot's named predicates on each query's
external inputs. They are premises for ordered child discharge and for the
parent's reset and preservation obligations. This path returns only refinement
evidence bound to the full parent snapshot, never a separately steppable cut.
`prepare-proved-wiring` still admits children only on unrestricted external
inputs. On reuse, the outer proof must match and discharge the parent's exact
contract at the occurrence before using its outputs.

The GatedCapture regression uses only registered-selector evidence inside the
wrapper, then only arbiter and wrapper evidence outside it. Removing the
wrapper premise fails the inner child check; substituting a premise-free
wrapper spec with the original evidence fails matching. The raw-request
fixture reports `gate/capture_exclusive` on the bad edge and replays the same
two requests with capture disabled and with reset asserted.

### Arbiter feeding a ShiftQueue

The [example RTL](../../examples/formal/arbiter-queue.rhdl) owns the wiring and port
signature. Payload width specializes both leaves. The parent's requested
depth is retained automatically and forwarded to the FIFO. The
spec reads that parent constant at construction, not child metadata or state.
The proof binds the external contract to the same elaborated top used for substitution.
The [bridge](wiring.rhm) reads the parent operations and child signatures,
then substitutes models by exact child definition. The fixture identifies the
arbiter and FIFO sites by their circuit declarations, not generated names. It follows direct scalar
wires and directional interface records, including
indexed lanes. Record creation and selection route individual members. The
concrete wiring bridge stops there; proof cuts also support the local RTL
fragment described below. A child clock must connect directly to
the parent clock. The cut uses the same logical member names as the leaf spec.
Unused unmodeled outputs (count/mask)
are allowed; a parent consuming one is rejected. The bridge constructs a
graph, not a proof certificate. For symbolic substitution, `proof_cut` keeps
the actual occurrence definitions and sampled layouts alongside the graph.
`prepare-proved-wiring` compares them and the complete spec snapshots against
opaque successful refinement results, before any parent solver query. The admitted
plan supplies the models to `proved-edge-at`; no child registers are expanded
and no new models may be passed at step time. This check does not discharge
the parent's own obligations. For reusable evidence, clocked children must
sample the parent clock and reset directly. Both executors settle each output once from
its old-state care/value dependencies; all child updates see the same edge.
An uncared internal pin is false/zero in concrete replay and fresh per
source in Rosette, never a guaranteed payload.

The [ordering proof](tests/arbiter-queue-order.rkt) steps the
[external contract](../../examples/formal/arbiter-queue.rspl) independently of
the composition. At each depth it substitutes the FIFO proved in that run and
the shared stateless arbiter. The history and abstract FIFO must fit the
declared capacity and agree on length and live words, not padding. A one-slot
FIFO cannot admit while retiring; the larger specializations must exhibit
such an edge. Prove reset from unrelated legal states, then check cared
output pins, externally decoded transfer events, and successor agreement.

The [two-lane transaction proof](tests/arbiter-queue-transactions.rkt) takes
only that parent certificate, its bound view, and the sequence field `pending`.
The `events` clause captures a helper expression at its lexical site. Its
stateless sampler is bound once per construction: cycle execution substitutes
the model's observed outputs into the event expressions, while the passive
view still reads external pins. A demanded but uncared output is undefined,
and a sampler domain remains a proof obligation in both interpretations.
Neither the physical view nor the helper callback enters the cycle snapshot.
Its exclusivity predicate is a domain check in rspecsim and a **conclusion**
of the reset/step solver queries, never a premise. Crossed payload bindings
fail the FIFO relation; crossing an ingress ready with egress valid permits
a double transfer and fails the sampler-domain obligation. A series parent
can reuse this proved exclusivity only after checking each lane's wiring.

Both [faulty parents](tests/arbiter-queue-fixture.rhdl) have the same interface as
the correct parent but are separate circuit declarations. Always build the reference
contract from the correct parent at the requested depth. At each depth the wrong-lane
fixture acknowledges the winning lane but routes lane one's payload to the FIFO;
reset, contest, and drain expose the error. The undersized fixture instantiates
a one-entry FIFO. After reset and two offers without a dequeue, its real RTL
rejects the second word, while the depth-two contract and correct RTL accept. This replay does not substitute
the depth-two certificate for the smaller child.

`prove-parent` uses the admitted cut's actual parent identity and sampled
layout. It quantifies over independent child spec states and external inputs;
from reset it establishes the reference start and relation, then checks that
every related edge agrees on cared pins and transfers and preserves the
relation. A failed or unknown obligation returns no evidence. The wrong-lane
fixture fails preservation even though both of its child results are valid.

The [nested ordering proof](tests/buffered-arbiter-queue-order.rkt) admits
only that ArbiterQueue result and a one-slot ShiftQueue result. Its ordered
backlog is the tail's live words followed by the front's pending winners.
It proves head retirement, suffix retention, winner append, and occupancy
conservation, and exercises a reachable fill/stall/drain trace for each front
depth.

The [transaction-only proof](tests/buffered-arbiter-queue-transactions.rkt)
establishes the same order and occupancy bounds (2, 3, 4) from the two
child transaction results alone. The parent declares only its external
events. Crossed lane data and handoff ready driven by the outer consumer
are rejected by the structural check, before any FIFO reasoning.

### Parent-local RTL with an opaque child

The [local lowering](parent-local.rhm) reads operations from the completed
parent, not a second model supplied by the proof author. It adds one reserved
`@local` graph participant for packed constants and comparisons, casts
between Bool and Bits(1), Boolean NOT/AND/OR, binary muxes on Boolean or packed data, synchronously reset Boolean registers,
and directly clocked packed registers without reset. The current register is an old-state output; its actual driver
and reset value determine the successor. Local taps and child outputs use
the same per-output dependency schedule, including fresh values on uncared
pins. Each real instance still requires matching refinement evidence.
Unsupported operations or non-direct clock/reset domains are rejected.

The [gated proof](tests/gated-arbiter-queue-order.rkt) relates exactly one
parent register and the verified queue's pending sequence to the wrapper's
independent admission and history spec. Check reset and preservation at depths
1–3. The leaky fixture fails preservation; a separate full-RTL replay shows
an unacknowledged word retiring. That replay is not an input to certification.

The independent [gated transaction proof](tests/gated-arbiter-queue-transactions.rkt)
uses `prove-fifo-wrapper` instead of a cycle cut. It matches one certified child,
checks its direct clock/reset, and turns its visible outputs into unconstrained
graph inputs and its driven inputs into observed sinks. Only the wrapper's local
RTL executes. For arbitrary register state and enable, prove the public sampler
domain and equality of reset, transfer presence, and active payloads against the
child sampler. The same child history then satisfies the public monitor.
The leaky gate fails **accepted** on a non-reset edge, before evidence is issued.
This is a safety rule for a transfer-preserving wrapper, not a cycle or progress guarantee.

The [two-tag ROB proof](tests/two-tag-rob-proof.rkt) relates four parent
busy/done bits, two per-tag generation bits, two unreset payload banks, and the queue's live tag words to
an issue history and the first result of each completed tag. The proof starts
the banks symbolically and relates their contents only while their tags are
complete; the local model's concrete simulation seed is not a reset assumption.
For each tag the RTL advances its bit at retirement, the reference at issue:
their bits are opposite while live and equal while idle. Require canonical tag encoding, uniqueness in the live
prefix, exact busy membership, and no completed idle tag. The reset case
starts from unrelated states; the preservation case assumes no traffic
restrictions. The “either done” mutant must fail induction and retire A early
on an actual reset/issue A/issue B/complete B/drain trace. The swapped-bank
mutant passes the tag-only projection but fails on cared outputs. A separate
stall lemma checks adjacent samples with unrestricted next inputs; full RTL
replays exercise duplicate completions while A is stalled. For each lane, check
that only its grant advances its next token and that a wrong-generation pulse
has the same effect as no pulse, leaving the other lane's traffic unrestricted.
The ignore-generation mutant must fail preservation and retire stale A0 data
during A1. A second reuse shows why the alternating bit cannot protect A2 from A0.

The [ROB transaction regression](tests/two-tag-rob-transactions.rkt) shares
`symbolic-fifo-cut` with the gated proof. Only the parent RTL has an executable
edge; the child supplies a sampler domain and FIFO relation on arbitrary
history successors. The [issuer](../../rspecl/proof.rkt) evaluates the two
hint models generated from `refinement:`: a state-only projection and a
separate model for pin checks and coverage. Start with arbitrary physical state;
reset must relate the physical successor both to the monitor's declared initial
state and to its reset successor. On non-reset edges quantify over all legal
related monitor states, including unconstrained inactive banks. Check local busy
membership, tag uniqueness and canonical encoding inductively. Compare the
monitor successor against the projected local generation/done bits and (only
for completed allocations) saved data. Use the same transition for certification
and consumption; no reset value for either bank is assumed.
The seal retains the exact sampler and monitor snapshots from the
[RSPECL declaration](../../examples/formal/two-tag-rob.rspl). The pin sampler
has no state: cycle execution substitutes cared output expressions into the
four published fields, and the proof samples the full batch from directional
physical nets. RSPECL lowers each bound payload to a guarded spec expression:
the payload event's care must equal its declared presence field in both directions.
The seal retains this projected sampler; the parent reads its dense output record
directly. The independent test oracles retain their historical record adapters.
Before sealing, `check-batch-total!` must prove every
output present and the whole step defined on unconstrained pins. The
[adapter oracle](tests/two-tag-rob-oracle.rkt) compares presence and cared
payloads with the former Racket mapping; the dropped-generation fixture fails
the same pre-seal check and replays as an undefined grant in rspecsim.
A detached view is a candidate
specification, never evidence; each mutant must still fail its own semantic
proof against the same declaration. The monitor's named verdicts diagnose failures before the
successor check; they are not assumptions. The [representation test](tests/two-tag-rob-monitor.rkt)
checks the ordered slots against independent per-tag banks at all three occupancies.
The former Racket transition lives only in the [differential oracle](tests/two-tag-rob-oracle.rkt):
compare the public verdict on arbitrary batches and the ordered live history on
accepted ones, including payloads only for completed allocations.
Check FIFO order separately from completion safety: the any-done mutant
preserves order but can retire A after only B completes. Chain the five
contract edges from reset to check that distinction; the full-RTL replay
above is an independent check.
The swapped-bank mutant must pass tag safety and bank preservation but fail
the retired-payload check. Exercise both tops from reset: grant A then B,
complete B=3 then A=1, and send matching duplicates A=2 and B=0 on a
stalled offer. Check feasibility of both drain handshakes before checking
their tags and data; the trace is not a latency requirement.

Only a successful reset and preservation proof issues a seal for the sampled
definition and layout. The [commit-log observer](tests/rob-commit-log.rkt)
receives that seal, not a cut or child theorem. It counts public grants and
retirements since reset and remembers the first matching response for an
arbitrary grant ordinal. Abstract slots appear in its inductive relation,
never as inputs to the observer's updates. For timing, reach a completed
head from reset and check both a repeatable idle self-loop and a retirement
from the same state.

For the [hardware recorder proof](tests/logged-two-tag-rob.rkt), use
`transaction-cut` to match the seal to the actual occurrence and apply the
issuer's pin decoder to its driven inputs and unconstrained outputs. Check
the wrapper's public batch and record readback before using `transaction-step`.
Prove reset, transfer, and hold from arbitrary local register contents; do
not seed the two data registers with zero. The cut interprets the RTL cast
from sampled Reset to Bits(1) used by `reset_active()`. Keep the full-RTL
replay separate: stall the first completed B=3, then raise ready. A valid-only
recorder exposes B's commit one edge early.

Keep that handwritten proof independent of the [declarative recorder proof](tests/logged-refinement.rkt).
For a `child` cut, match both the expected view and the actual RTL definition
at the named site, then prove equality of the entire forwarded batch on arbitrary
pins **before** applying child legality. Step only the sealed child's monitor.
Bind its abstract pre/post histories separately from parent registers and
check the parent relation at reset and at each admitted successor. The downstream
readback test must use only the newly issued parent seal. A dropped grant
generation must fail forwarding, whereas valid-only capture must fail preservation.
Keep the public ordered-retirement monitor separate from the refinement binding.
The ordinal observer uses only the parent seal, batch and abstract history. Run
it unchanged on the renamed-child fixture with only the private proof-site
binding changed, and check both back-to-back retirement and an idle self-loop.

The independent [rspecsim oracle](tests/two-tag-rob-test.rhm) records integer
allocation serials on entries and compares their low bits only at the interface.
It exercises all 61 tag/data states against 64 control samples with representative
payloads and generations, plus an independent generation/echo matrix with both
tags pending (4,928 concrete cases total). Induction uses unrestricted inputs.
Run `moon run rhodium:formal-spec-test rhodium:formal-test`.

## Extend supported semantics

1. Define the public meaning and failure boundary in [README.md](README.md).
2. Extend preflight and snapshot coverage so unsupported reachable structure
   fails before a success claim.
3. Add the bitvector interpretation in the engine. Preserve canonical record
   and vector packing, unequal shift-width behavior, hierarchy, and strict type
   equality.
4. If the operation has a partial-domain contract, add an explicit proof
   obligation like the existing exactly-one requirement for `rtl.onehot_mux`.
   Never choose a value for unspecified behavior.
5. Replay witnesses and counterexamples through the concrete interpreter before
   returning them.
6. Add focused valid, counterexample, unsupported, invalid-argument, and vacuous
   coverage as applicable.

Outside the internal register-transition path, sequential state remains unsupported.
Only ordinary clocked assertions have transition semantics among verification effects.
Memories, DPI, partial values, and new operations
remain unsupported until each receives an explicit semantic model. Do not
silently ignore a reachable operation or restrict the query to one convenient
backward slice.

Unsigned `rtl.urem` uses `bvurem` only after proving its divisor nonzero under
the complete query. Collect this obligation even for an unused result or a child
instance, as for one-hot selection; SMT's total zero-divisor interpretation is
not a definition of unspecified RTL behavior. Concrete replay rejects zero.
`tests/remainder-test.rhm` covers equivalence against a lookup oracle,
counterexample replay, unproved and unused-operation domains, and vacuity.

## Change queries or results

Keep assumptions as explicit packed constraints over named top inputs. Validate
ports, types, widths, masks, and satisfiability before evaluating a query.
Result statuses must distinguish proved facts, counterexamples, vacuity,
unsupported semantics, and solver uncertainty. Invalid API arguments remain
errors rather than formal statuses.

Every counterexample must contain complete, nonnegative input assignments and
concrete differences at the paired outputs.

## Validation

For changes to rspecsim, run `moon run rhodium:formal-spec-test`.
This host-only target enumerates all 21 legal contents and all 32 input samples
of a depth-two, two-bit FIFO, checks observations and state against a list
oracle, and exercises invalid models and simultaneous state updates.

The solver-backed suite includes [the leaf replay](tests/shift-queue-replay.rkt).
The binding instantiates a real two-bit, depth-two ShiftQueue, with pipe, flow
and flush disabled. It samples the handshakes at the ports and independently
relates the compact occupied prefix of payload registers to the spec list.
Unused slots are ignored. Count and mask are additional replay diagnostics,
not part of the exported observation view. Each tested trace begins at an
explicit reset. A failing trace retains startup bits, all input samples through
the first mismatch, and expected and actual values. This is not an induction result.

Run the solver-backed API and semantic suite:

```sh
moon run rhodium:formal-test
```

When operation interpretation, packing, replay, or assumptions change, also run
the independent CIRCT/Verilator differential check:

```sh
moon run rhodium:formal-differential-test
```

The differential test exercises all 128 reduced-width unequal-shift inputs, 80
aggregate layouts and projections, 256 hierarchical arithmetic inputs, eight
decode selectors, and 12,288 valid one-hot selections against independent
SystemVerilog oracles.

The formal target performs a live Rosette/Z3 probe under an isolated compiled
root. State the actual solver and external-tool coverage run; an import-only
probe is not equivalent to solving or differential replay.

`tests/transition-test.rhm` applies concrete recurrence oracles and solver checks
to snapshots built with the public Builder: counter wrap/reset, pre-update
sampling, resetless pipelines, packed vector state, repeated child registers,
hierarchical feedback, outputless state, explicit initial-state validation, and
partial-operation obligations. It is included in `moon run rhodium:formal-test`.
`tests/run-transition.py --circt` compares a defined pre-edge trace with an
independent Python recurrence, native rsim under sanitizers, direct SV, and
CIRCT SV. It runs in `moon run rhodium:formal-differential-test`; undefined startup values
are not compared, and uninitialized unrelated feedback state is not treated as
portable zero. All generated artifacts are temporary.
