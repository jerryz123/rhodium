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
  Query["FormalQuery input assumptions"] --> Engine["engine.rkt<br/>Rosette translation and solving"]
  Snapshot --> Engine
  Engine --> Replay["Concrete replay"]
  Replay --> Result["Equivalence result or counterexample"]
```

[`main.rhm`](main.rhm) owns the public Rhombus API and result construction.
[`snapshot.rhm`](snapshot.rhm) owns backend-independent traversal and immutable
input to the RTL solver boundary. [`engine.rkt`](engine.rkt) interprets only RTL;
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

### Symbolic specification step

`spec_snapshot(model)` copies the checked input/state schemas, typed expressions,
care predicates and updates. The internal [spec-engine.rkt](spec-engine.rkt)
entry point `symbolic-spec-edge` consumes that snapshot and returns one edge
from fresh pre-edge state and input terms, **not** a run from `model.initial`.
Its `legal` predicate excludes out-of-range sequence counts only. Sequences
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

The internal engine API `prepare_transition_snapshot(snapshot, clock)` lowers a
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
checked over every input sample. For a single rising clock and active-high
sampled reset, supply a state relation, not a premise: the checker builds the
reset and preservation domains and requires them to be feasible. Reset starts
with unconstrained RTL state and must establish both the declared spec start
and the relation. Both phases discharge RTL obligations, spec definedness,
cared physical outputs and event agreement. Callers own the interpretation of
care and transfer events and the abstraction relation. There is no option to
restrict external traffic. A counterexample, unknown, or ambient constraint
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
spec start. The induction step assumes only a legal related old state; inputs,
including subsequent resets, are unrestricted. Separate queries establish RTL
assertions and operation domains, spec definedness, cycle observations, the
joint event, and the successor relation. Care must match even when the value
is absent, and a valid head is observed even under backpressure. Count and mask
ports are not part of the exported view.

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
checks the direct netlist, including the common clock/reset, and allocates just
`a.q` and `b.q`. A changed topology or a consumer of a hidden port such as
`mask` does not qualify.

The spec engine resolves pre-edge offers from each child's old state and reset
before binding the handshake inputs. A missing input dependency is an error,
not a wire equality assumed to have a solution. Uncared wires get arbitrary
values. Both child input domains and definedness must hold under the parent
invariant for every external sample; no child assumption may restrict the proof.

The external transaction sequence is the live contents of B followed by A.
Reset empties it; otherwise an external dequeue removes its oldest word and
an enqueue appends the offered word. A middle handshake agrees at both ends
and does not by itself change this sequence. The solver checks length and each
live successor position, including simultaneous transfers. A reset/fill/drain
trace attains capacity four. Another edge has one free slot but no input
admission. The depth-one readiness example uses specs only, not depth-two RTL
evidence. Run `moon run rhodium:formal-test` to reproduce these checks.

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
indexed lanes. Record creation and selection route individual members; parent
computation and state are rejected. A child clock must connect directly to
the parent clock. The cut uses the same logical member names as the leaf spec.
Unused unmodeled outputs (count/mask)
are allowed; a parent consuming one is rejected. The bridge constructs a
graph, not a proof certificate. For symbolic substitution, `proof_cut` keeps
the actual occurrence definitions and sampled layouts alongside the graph.
`prepare-proved-wiring` compares them and the complete spec snapshots against
opaque successful leaf results, before any parent solver query. The admitted
plan supplies the models to `proved-edge-at`; no child registers are expanded
and no new models may be passed at step time. This check does not discharge
the parent's own obligations. Both executors settle each output once from
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
Both [faulty parents](tests/arbiter-queue-fixture.rhdl) have the same interface as
the correct parent but are separate circuit declarations. Always build the reference
contract from the correct parent at the requested depth. At each depth the wrong-lane
fixture acknowledges the winning lane but routes lane one's payload to the FIFO;
reset, contest, and drain expose the error. The undersized fixture instantiates
a one-entry FIFO. After reset and two offers without a dequeue, its real RTL
rejects the second word, while the depth-two contract and correct RTL accept. This replay does not substitute
the depth-two certificate for the smaller child. Run
`moon run rhodium:formal-spec-test rhodium:formal-test`.

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
