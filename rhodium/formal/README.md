<!-- Documents spec authoring, rspecsim concrete execution, and the optional Rosette formal engine. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rhodium formal engine

The formal engine is an optional, solver-backed consumer of verified public
Rhodium IR. It checks deterministic combinational behavior with Rosette
fixed-width bitvectors; it does not participate in elaboration, change Rhodium
semantics, or get imported by `#lang rhodium`.
Author executable specifications in `.rspl` files using `#lang rspecl`.
**rspecsim** is the [specification library](spec.rhm)'s concrete execution mode:
it runs a reference model on known inputs and state, without RTL or a solver.
By contrast, [rsim](../backend/README.md) runs the RTL implementation, and
Rosette can execute the same specification symbolically on unknown values. A
[leaf proof](DEVELOPING.md#shiftqueue-refinement-by-depth) establishes that the
two-bit ShiftQueue (pipe, flow, and flush disabled) matches its FIFO spec on
every edge following sampled reset, separately at depths 1, 2, and 3.
A [transaction-only parent proof](tests/queue-pair-grouped.rkt) checks the
grouped handoff in the actual RTL and uses only the two ShiftQueue transaction
contracts to prove FIFO ordering and bounds of two and four words for pairs
of depth-one and depth-two queues, respectively. A separate
[cycle proof](DEVELOPING.md#two-shiftqueues-in-series) shows a depth-one pair can refuse a second
word while moving the first word to its empty downstream stage. A consumer
of the exported transaction contract sees reset, accepted words, and retired
words. This suffices to prove that the kth retirement matches the kth
acceptance since reset. The contract supplies no cycle pins: it guarantees
neither immediate admission nor eventual delivery. Pass `(grouped-pair-at depth)`
to the [ordering observer](tests/queue-pair-transaction-order.rkt), which checks
event batches with `fifo-allows?`. [QueuePairSeries](tests/queue-pair-series.rkt) connects
`QueuePair(1)` to `QueuePair(2)`. Their transaction contracts suffice to prove
FIFO retirement and at most six pending words. The chain can refuse the next
offer with just one word pending; its six-word bound is not a readiness guarantee.
A separate [arbiter proof](DEVELOPING.md#two-input-fixed-priority-arbiter)
covers a two-input, two-bit Decoupled arbiter: lane 0 wins ties and each
output transfer consumes exactly the winning input. A stalled grant is not
locked. When that arbiter feeds the FIFO, the
[combined proof](DEVELOPING.md#arbiter-feeding-a-shiftqueue) shows that a
dequeue returns the oldest accepted winner since reset at all three tested
depths. An offered word that
was never accepted does not enter the queue. Reset discards pending words;
the proof promises neither eventual delivery nor fairness between lanes.
The [nested example](../../examples/formal/buffered-arbiter-queue.rhdl) adds
a one-entry ShiftQueue after that proved ArbiterQueue. Its [event spec](../../examples/formal/buffered-arbiter-queue.rspl)
observes just the external handshakes. Once the RTL wiring is checked, the
[ordering proof](tests/buffered-arbiter-queue-transactions.rkt) uses only the
two child transaction contracts: every retirement returns the oldest accepted
word, and at most 2, 3, or 4 words are pending for front depths 1, 2, or 3.
It does not need to know which lane wins a tie. Available space alone
does not guarantee admission. [GatedArbiterQueue](../../examples/formal/gated-arbiter-queue.rhdl)
demonstrates the next boundary: the proof executes the wrapper's own admission
register but substitutes the verified queue spec. Reset closes admission;
otherwise the previous edge's enable controls new transfers. Closing the gate
does not block already accepted words from draining. Its [transaction proof](tests/gated-arbiter-queue-transactions.rkt)
uses only the child's FIFO contract: each public acceptance or retirement
matches the child's transfer and payload on that edge. Ordering and capacity
(1, 2, or 3 words) follow without knowing when the child is ready.

[TwoTagROB](../../examples/formal/two-tag-rob.rhdl) uses that same verified
depth-two ArbiterQueue to remember issue order. Each tag can be live at most
once. A wins when both tags are eligible. On a grant, sample `issue_gen`
alongside the winning tag; there is no issue token on a non-grant edge.
Each tag's grants alternate generation 0, 1, 0, … independently. A completion
pulse must echo its issue bit on `complete_gen_a` or `complete_gen_b` and
its tag must already be live **before** the edge. An idle, issue-edge, or
wrong-generation completion has no effect on that allocation.
The first matching pulse captures the tag's two-bit `result_a` or `result_b`;
later pulses for that issue cannot overwrite it. Retirement offers the completed
head's tag on `retire.bits` and its word on `retire_result`. Both share the
same valid/ready handshake and remain stable while stalled (unless reset).
The result has no meaning while invalid.
Completing B while an unfinished A is ahead of it cannot retire either tag.
A retired tag becomes eligible for reissue on the next edge; synchronous reset
discards outstanding work and restarts both generation sequences at zero.

The [transaction-only retirement proof](tests/two-tag-rob-transactions.rkt)
establishes a narrower guarantee using only the child FIFO contract: at most
two granted tags are outstanding. Each retirement is the oldest outstanding
grant, and its `retire_result` equals the payload of **that allocation's first
matching-generation completion** on an earlier edge. Later pulses cannot
replace it, even while retirement is stalled. This proof checks data on
transfer only; grant priority and stability of a stalled offer remain cycle claims.

The [ROB spec](../../examples/formal/two-tag-rob.rspl) describes the implementation
relation alongside the monitor. With the elaborated RTL `top` and the existing
FIFO evidence for its child, use the [shared proof API](../../rspecl/proof.rkt):

```racket
(define view (TwoTagROB top #:transaction_view #t))
(define evidence (prove-transaction top view child-evidence))
(define-values (permitted next) (transaction-step evidence view old-state batch))
```

Only use `next` when `permitted` holds. The public batch is reset, optional
grant (tag and generation), both completion pulses (echo and payload), and
optional retirement (tag and payload). An RTL parent can use `transaction-cut`
to bind the seal to an actual child occurrence without reopening its registers.
A completion applies to an already-live grant, not a
grant on that same edge. No retirement deadline is implied, even for a
completed head: an idle batch may leave the abstract history unchanged.

[LoggedTwoTagROB](../../examples/formal/logged-two-tag-rob.rhdl) forwards
the ROB interface and adds a one-entry record of the last retirement. A reset
edge clears `has_commit`; until the first transfer, `last_tag` and
`last_payload` have no specified initial value. On a non-reset edge with
`retire.valid && retire.ready`, the next record contains the retiring tag
and its first matching-generation completion payload. Otherwise the record
holds, even when the stalled offer differs from the saved record. The
[proof](tests/logged-two-tag-rob.rkt) needs no child offer-stability or timing guarantee.

The [spec](../../examples/formal/two-tag-rob.rspl) guarantees rejection of a
duplicate from the immediately previous use of the same tag **within a reset
epoch**. It does not identify the true age of a matching response. After two
reuses, or after reset, an old token may match again. Systems needing protection
there must drain old responses or prevent token reuse until they can no longer
arrive. No eventual completion is assumed.
These proofs cover the stated specializations. They share an internal run-local
refinement checker; they are not a parameter-universal proof.

This README documents the implemented public contract. The
[formal plan](PLAN.md) sets out Rhodium's direction: executable specifications,
leaf refinement proofs, and parent proofs that use verified child models.
Contributors changing solver semantics, query handling, replay, or coverage
should read [`DEVELOPING.md`](DEVELOPING.md).

## Install and quick start

To run a specification in rspecsim, import `step_spec` directly. For example, on
an edge that consumes 1 and accepts 2, the old head remains visible:

```rhombus
import:
  lib("rhodium/formal/spec.rhm").step_spec
  lib("rhodium/frontend/foundation.rhm").elaborate
  lib("rhodium/frontend/foundation.rhm").Bits
  lib("flow/shift-queue.rhdl") as rtl
  lib("examples/formal/shift-queue.rspl").ShiftQueue

def fifo = ShiftQueue(elaborate(rtl.ShiftQueue(Bits(2), 2)).top)
def edge = step_spec(fifo, {"q": [1]},
                     {"reset": #false, "ingress.valid": #true, "ingress.bits": 2, "egress.ready": #true})
// edge.outputs["egress.bits"] == 1
// edge.events == {"retired": 1, "accepted": 2}
// edge.next_state == {"q": [2]}
```

For **symbolic spec checks or RTL equivalence**, install Rosette into the active Racket installation:

```sh
raco pkg install --auto rosette
```

Run the focused formal suite. It first probes Rosette and its solver under the
persistent isolated build cache, then runs the Rhodium formal tests:

```sh
moon run rhodium:formal-test
```

Rosette is optional: ordinary Rhodium host and backend targets do not require
it. When CIRCT and Verilator are also available, run the independent backend
differential check:

```sh
moon run rhodium:formal-differential-test
```

## Author an executable spec

Start a `.rspl` file with `#lang rspecl`. For an existing circuit, import its
module as `rtl` and write `spec for rtl.ShiftQueue:`. At module scope this
automatically exports `ShiftQueue`; ordinary helpers stay private unless explicitly
exported. A spec inside a function or block stays local. The resulting
`ShiftQueue(top)` takes one completed specialization; constructing it checks
the declaration identity, but does not prove refinement or advance an edge.
This is the only RSPECL declaration form. A clockless circuit uses the same
syntax as a synchronous one. For example, this contract specifies only the
carry output of the [full adder](../../examples/rtl/full-adder.rhdl):

```rhombus
#lang rspecl
import lib("examples/rtl/full-adder.rhdl") as rtl

spec for rtl.FullAdder:
  observe cout = (a && b) || (cin && (a || b))
  ignore sum
```

RTL inputs and spec state introduce lexical bindings. Helpers can live
in the block, outside it, or in an imported library. Their ordinary Rhombus
bodies build expressions once per model construction; the interpreters never
call them on an edge. `def`, `fun`, and `let` retain their Rhombus scope.
Outputs and events do not introduce bindings. An interface input such as
`ingress.valid` has the public key `"ingress.valid"`.
`observe` describes
what is visible; `event` describes what happened. On either, `when` controls
presence, not the payload value. Omit it for an always-present field.

A `monitor:` block inside `spec for` describes which joint event batches are
legal and how each legal batch changes transaction history. For example, this
excerpt from [TwoTagROB](../../examples/formal/two-tag-rob.rspl) requires a
retirement to name the oldest live tag:

```rhombus
    guarantee "oldest-tag" = !retire || (live && same(retire_tag, head))
```

`batch` introduces typed lexical inputs to the monitor, not new RTL pins.
For a sampler-bound monitor, declare where each field comes from:

```rhombus
    batch grant = present(issued)
    batch grant_tag: Bool = payload(issued) when grant
    batch grant_gen: Bool = payload(issued_gen) when grant
```

A present `issued` event whose payload is false means **grant A**, not “no
grant.” Here `when grant` requires the source event to be present *exactly*
when `grant` is true. It is an obligation on the sampler: a grant without
its generation fails even if that generation would have been false. On idle
edges, unused payload fields get deterministic padding (false, zero, or the
empty sequence). Bind every batch field, or leave them all unbound for a
monitor driven directly by the caller. With bound fields, the outputs of
`step_spec(transaction_batch_model(view), {}, pins)` are the monitor's inputs;
`transaction_batch_model` is available from `rspecl/rtl-boundary.rhm`.

An optional sibling `refinement:` block describes a candidate relation to the
implementation. For the supported single-clock, one-FIFO-child cut, `fifo`
reads only the certified child's ordered history and `local` reads only
parent registers. `relate` gives each monitor state field an implementation
expression and optional care condition. For example, `relate saved_a = saved_a
when done_a` does not require the unreset payload bank to be zero. Auxiliary
`invariant` predicates must hold after reset and be preserved, too. Pin- and
batch-dependent `check` obligations and `cover` feasibility queries cannot
restrict the inductive relation. Neither the hints nor a concrete replay
authorize substitution; only a successful proof does.

`state`, `next`, and ordinary Rhombus helpers work as in the cycle body.
Each named `guarantee` reports a Boolean for the **same pre-edge batch and
history**. The reserved output `allowed` is their conjunction. If any guarantee
fails, its diagnostic remains visible and all history holds; no `next`
expression is evaluated. An accepted batch applies all updates simultaneously.

`sample` selects the stateless pin sampler; `publish` lists the sampler
events exposed by the cycle model. In `TwoTagROB(top)`, the sampler reads
the cycle model's outputs and publishes the existing `issued`, `issued_gen`,
`retired`, and `retired_value` events. At a proof boundary the same sampler
reads physical pins; the full batch includes reset and both completion lanes.
The monitor judges that batch without predicting when a grant or retirement occurs.
Use `TwoTagROB(top, ~transaction_view: #true)` for the signature-checked
view. `TwoTagROB(~transaction_view: #true)` returns just the unproved
description, so a verifier can test a candidate implementation against it.
Neither form supplies proof evidence. This attachment is built at declaration
scope and currently supports a fixed transaction schema, not per-specialization
cycle state or parameters. The lower-level `transactions transaction_view(...)`
form can attach an already-built monitor instead.

Use `Bool`, `Bits(width)` or `Seq(element, capacity)` for types. For host
configuration decisions, ordinary `if` runs at construction time. For a
sampled condition, use `select(test, yes, no)`: a model node is a Rhombus
object, not a host Boolean. Helpers defined in `#lang rspecl` get the spec
operators automatically. A helper in an ordinary `.rhm` module can import
them from [spec-dsl.rhm](spec-dsl.rhm) or use typed IR constructors such
as `bool_and` directly; its host Boolean operators use host truthiness.
`SpecExpr` is a type
annotation for their nodes, not an unchecked constructor.

Build typed constants with `const(type, value)` and sequence expressions with
`empty`, `full`, `front`, `tail`, or `append`. To name a typed initial
constant, write e.g. `state q: Seq(Bits(width), depth) initially [] as cleared`;
using `cleared` in a reset update is explicit. All `next` clauses read the
same old state. Omitted updates hold; two updates to one field are an error.

See the [arbiter](../../examples/formal/arbiter.rspl) for combinational
priority and the [FIFO](../../examples/formal/shift-queue.rspl) for guarded head
reads and an explicit simultaneous enqueue/dequeue update.

A combinational or single-clock leaf can declare a named Boolean input assumption. For example,
the [two-lane selector](../../examples/formal/granted-selector.rspl) declares
`assume exclusive = !(enable_a && enable_b)`. Neither lane need be enabled:
idle returns zero. One enabled lane returns its word. Both enabled is outside
the contract; rspecsim rejects that sample. Assumptions may use current scalar
inputs and constants, not state or history. Multiple assumptions are conjoined.

The [parent RTL](../../examples/formal/granted-selector.rhdl) accepts arbitrary
requests, including simultaneous ones. Its verified arbiter drives the selector
with grants. Before using the selector's guarantee, the checker proves its
assumption from those wires. Driving the enables with the raw requests instead
yields a counterexample, not a restricted parent proof.

In the same example, `RoutedSelector` puts a conditional `LaneRouter` between
the arbiter and the selector. The router passes through or swaps the arbiter's
two grants. Once its own input contract is proved, its outputs can establish
the selector's contract. Both requests can still be asserted, and either swap
setting is legal. Connecting one routed grant to both enables fails at the
selector, with a parent input sample that replays in rspecsim. A child may
not use its own or a mutually dependent same-edge guarantee.

`RegisteredRoutedSelector` uses the same arbiter and router to feed a
`RegisteredLaneSelector`. Its leaf contract is
`assume exclusive = reset || !(enable_a && enable_b)`: reset permits either
or both enables and clears the word on the sampled edge. The pre-edge word
is not observed during reset. On other edges the output shows the previously
captured word; one enable captures its lane for the next sample, and neither
enable leaves the word unchanged. The parent has no input restriction.
For a clocked contract, assumptions must admit both a reset and a non-reset
sample; they may not refer to history.

An assumption can also belong to a parent. In the [GatedCapture spec](../../examples/formal/gated-capture.rspl),
`assume capture_exclusive = reset || !capture || !(grant_a && grant_b)` permits
both grants on reset or while capture is disabled. The [wrapper RTL](../../examples/formal/gated-capture.rhdl)
gates the enables, so that contract suffices for its registered selector. A
successful proof certifies the wrapper **with** its assumption. One level up,
`ArbitratedCapture` proves the assumption using arbiter grants and accepts
unrestricted requests: on an enabled tie it stores lane A's word. Wiring raw
requests to the wrapper instead fails when both request on an enabled non-reset edge.

### Specify a parent without copying its implementation

The [ArbiterQueue contract](../../examples/formal/arbiter-queue.rspl) predicts
observable grants, head data, and transfers using one sequence of accepted
external words. The [RHDL circuit](../../examples/formal/arbiter-queue.rhdl) owns its
two Decoupled input lanes (`ingress[0]`, `ingress[1]`) and one Irrevocable
output (`egress`). Bind the model once, before simulating or solving:

```rhombus
#lang rspecl
import:
  lib("rhodium/frontend/foundation.rhm").elaborate
  lib("examples/formal/arbiter-queue.rhdl").ArbiterQueue as RtlArbiterQueue
  lib("examples/formal/arbiter-queue.rspl").ArbiterQueue

def top = elaborate(RtlArbiterQueue(4, ~depth: 2)).top
def model = ArbiterQueue(top)
```

In the spec file, import the circuit module as `rtl` and start the body with:

```rhombus
spec for rtl.ArbiterQueue:
  state pending: Seq(ingress[0].bits.type, depth) initially [] as cleared
```

The declaration defines `ArbiterQueue(top)`. It accepts any completed
specialization of that circuit family, not an unrelated circuit with the
same ports. Directly declared scalar inputs (including grouped declarations)
and the synchronous circuit’s implicit `reset` are lexical bindings. A clockless
`circuit` has no implicit controls. Directly declared interfaces expose sampled
input members (`ingress.bits`, `ingress[0].valid`) and output observation names
(`egress.valid`, `ingress[0].ready`). Directions and sorts come from the
elaborated interface, not from the spelling of a flattened wire. In ArbiterQueue,
`ingress[0].bits.type` comes from the input; `depth` comes from the parent’s resolved
constructor arguments, not a sampled input or a measurement of its child.
Integer and Boolean arguments (including keyword defaults) are available
by their local binding names. Prefix a binding with `_` to keep it private.
Local helpers and imports work as in ordinary Rhombus. Ports generated only
by runtime helpers do not introduce lexical names. Declare an input on the
circuit boundary if the spec needs to reference it by name.

For a synchronous spec, `step_spec` samples one clock edge; clock is not a data
input. For the stateless arbiter it evaluates one input sample. An observation such
as `observe egress.bits = front(pending) when !reset && valid` supplies the expected
value and its care condition, not a port declaration. Every signature output
must have exactly one correctly typed observation or be explicitly excluded
with `ignore`. The FIFO excludes `count` and `mask`: neither is a guarantee
that a parent may use when substituting the FIFO spec. Scalar `Bool` and `Bits(n)`
are supported; `Bits(1)` is not `Bool`. Events and abstract state belong to
the spec, not the physical signature. Once constructed, the model steps
without the RTL module.

On a non-reset edge with room,
lane 0 wins a tie. Retirement removes the oldest word; a full queue may
retire but cannot accept on that same edge. Reset clears the sequence and
hides the grants and head. Run it like any leaf with `step_spec`.

The [RHDL parent](../../examples/formal/arbiter-queue.rhdl) alone defines the wiring.
The fixed proof substitutes the verified Arbiter and ShiftQueue models at
the actual child instances and compares the result with the independent
contract. It checks ordering at the external ports, not by assuming the FIFO
received the winner. Run `moon run rhodium:formal-spec-test` for concrete
replay or `moon run rhodium:formal-test` for induction.

Concrete composition accepts ordinary specs. For formal substitution, each
child must have a successful refinement proof for that exact elaborated definition,
sampled port layout, and contract (including initial state and observation
care). A depth-one FIFO result cannot stand in for a depth-two FIFO, even if
their ready/valid interfaces look alike. Two occurrences of one proved
specialization can reuse its result, with independent state. A refinement result
does not survive re-elaboration. It justifies substituting that child, not
assuming that the parent wired it correctly. A supported parent becomes a
verified child only after its own reset, edge, and observation obligations
pass. The wrong-lane ArbiterQueue has proved children but earns no such result.

## Concrete specification API

`make_spec(inputs, state, initial, outputs, events, updates)` builds an immutable
model. The first two maps declare separate namespaces of string names and
types: `SpecBool()`, `SpecBits(width)`, or `SpecSeq(scalar_type, capacity)`.
Widths and capacities are positive. Values are Booleans, unsigned naturals
that fit their width, and immutable lists of at most the declared capacity.
`model.initial` is a complete concrete state, not an assumption about RTL startup.
The optional `~assumptions` map binds names to Boolean input predicates.

Build expressions using `literal`, `input_ref`, `state_ref`, Boolean `bool_not` /
`bool_and`, `bits_equal` for equal-width packed operands, deterministic `choose_if`,
and `seq_empty`, `seq_full`, `seq_front`,
`seq_tail`, and `seq_append`. Model construction checks the names and types of
references on both sides of conditionals, even in inactive branches. Output
and event maps contain `Observed(value, care)` fields; care defaults to true.

`step_spec(model, state, inputs)` takes exact bindings and returns three maps:
pre-edge `outputs`, the joint `events` on that edge, and `next_state`. False care
omits the key and does not evaluate its value. An observed false or zero remains
present. Every update reads the same old state; unassigned fields hold. Reading
or removing an empty head, or appending to a full sequence, is an error on an
executed path. Reset and which events it suppresses are authored in the model,
not imposed by the interpreter. The optional [symbolic step](DEVELOPING.md#symbolic-specification-step)
has the same care and update semantics; it returns a condition for when partial
operations are defined instead of throwing on a symbolic input. There is no
public RTL refinement query yet.

To check an observed FIFO trace without running Rosette, import
[`fifo-monitor.rhm`](fifo-monitor.rhm) and build `fifo_monitor(capacity, width)`.
Start with an empty list after reset and pass each batch to `step_fifo_monitor`:

```rhombus
def monitor = fifo_monitor(2, 3)
step_fifo_monitor(monitor, [5], {"retired": 5, "accepted": 0}) // [0]
step_fifo_monitor(monitor, [5], {"retired": 0})                // #false
```

A list (including `[]`) is the new oldest-first history; `#false` rejects the
batch. Unknown event names or ill-typed payloads are input errors. An empty
batch holds the history; `{"reset": #true}` clears it and cannot accompany a
transfer. Retirement checks the **old** head, so a full FIFO can replace one
word in a joint batch, but an empty FIFO cannot retire a new arrival. These
are permitted observations, not predictions of ready, valid, or progress.

To obtain those batches from RTL pins, declare a passive view as the body of
`spec for`, as in the [QueuePair and QueuePairSeries specs](tests/queue-pair.rspl):

```rhombus
spec for rtl.QueuePair:
  events ready_valid_events(reset, rv_pins("offer", "ready", "data"), rv_pins("valid", "consume", "head"))
```

Each `rv_pins(valid, ready, payload)` descriptor names three physical scalar
ports. Their directions and types are checked against the elaborated circuit.
Bind the sampler with `QueuePair(top, ~event_view: #true)`. Without that
keyword the request is for a cycle model, which this declaration does not
provide. [`sample_fifo_events(view, pins)`](fifo-view.rhm) takes
one map containing all pre-edge input and output samples except the clock
and returns the batch for `step_fifo_monitor`. An asserted reset suppresses
both transfers; otherwise both can appear in the same batch, even with a
zero payload. The view reads the actual ready and valid pins. It does not
drive them, certify the FIFO, or replace a cycle model.

The [ShiftQueue spec](../../examples/formal/shift-queue.rspl) and the
[ArbiterQueue spec](../../examples/formal/arbiter-queue.rspl) each define
their cycle behavior and transaction events in the same body:

```rhombus
  events ready_valid_events(reset, ingress, egress)
```

`ready_valid_events` is an ordinary function of the lexical reset and endpoints.
`ShiftQueue(top)` runs the cycle model; `ShiftQueue(top, ~event_view: #true)`
returns its passive sampler for pin replay and transaction proofs. It samples
reset and each endpoint's `valid`, `ready`, and `bits`, not `count` or `mask`.
For two-bit ShiftQueues at depths 1–3 with pipe, flow, and flush disabled,
retirements preserve acceptance order since reset and occupancy is bounded
by depth. The transaction result does not promise readiness whenever space remains.

`ArbiterQueue(top, ~event_view: #true)` works the same way with two ingress
lanes. Each lane fires on
its own `valid && ready`. Two requests are fine; **two transfers on a
non-reset edge are not**. Concrete sampling rejects such a sample, and
certification must prove it impossible. The accepted word comes from the
lane that actually fired. For two-bit words at depths 1–3, the cycle proof
implies that retirements preserve acceptance order and pending words never
exceed depth. This weaker view does not promise which lane wins, when either
lane is ready, or whether a waiting lane will ever be served.

## RTL equivalence API

Import `rhodium/formal/main.rhm` explicitly. Each subject must be a verified
`DesignElaboration` with an explicit top or a completed core `Module` owned by
its verified design. A raw `Design` is rejected because it does not identify a
top module.

Use `check_equivalent(left, right, query = FormalQuery())` to compare every
paired top output on every permitted input. It returns a `FormalResult`.

Equivalence requires exactly the same top input and output names and mutually
equal Rhodium types. Paired inputs receive the same symbolic value, and all
outputs are compared simultaneously. Module structure, internal names,
operation order, and printed IR may differ; this is behavioral equivalence,
not a structural diff.

### Assumptions and packed patterns

`FormalQuery` contains a conjunction of explicit assumptions over named top
inputs:

| Form | Meaning |
|---|---|
| `FormalInputAssumption(port, value)` | The complete canonical packed input equals `value` |
| `FormalInputAssumption(port, value, care)` | The cared bits satisfy `input & care == value` |
| `FormalOneHotAssumption(port)` | The complete packed input has exactly one set bit |

Values and masks must fit the named port's canonical packed width, and
`value & care` must equal `value`. Omitting `care` selects the all-ones mask.
Aggregate ports use their canonical Rhodium packed representation.
No symbolic Rosette value or live
Rhodium object crosses the query boundary.

For example, this query fixes `mode`, constrains the high bits of `tag` to
`10`, and requires `grant` to be exactly one-hot:

```rhombus
def query:
  FormalQuery([FormalInputAssumption("mode", 0b0000),
               FormalInputAssumption("tag", 0b1000, 0b1100),
               FormalOneHotAssumption("grant")])
def equivalent = check_equivalent(reference, candidate, query)
```

Every reachable `rtl.onehot_mux`, including one below an instance or driven by
a derived value, creates a separate validity obligation. The engine proves
from the complete query that its selector is exactly one-hot before evaluating
the requested query. A missing or insufficient proof returns `unsupported`
with the operation path. Contradictory assumptions return `vacuous` before any
one-hot validity claim.

Every reachable `rtl.urem` likewise requires a proof that its divisor is nonzero,
including unused operations and operations beneath instances. The engine returns
`unsupported` if the query does not establish that precondition; it does not use
SMT's chosen zero-divisor value as an RTL guarantee.

### Result statuses

An `equivalent` result means no permitted input produces a difference. A
`counterexample` retains the assumptions, complete input assignment and all
differing outputs. Contradictory assumptions yield `vacuous`; an incompatible
interface or unsupported operation yields `unsupported`; solver uncertainty yields
`unknown`. None of the latter three is a proof.

Inputs omitted from a partial solver model are completed with zero. The engine
then interprets the design concretely before returning a counterexample, so
reported packed values are replayable nonnegative integers.
`unsupported` includes a focused diagnostic for the first rejected reachable
operation when applicable; engine bugs are not converted into scope results.
Invalid API arguments, unknown assumption ports, out-of-width packed
patterns, and raw `Design` subjects raise errors instead of returning a formal
status.

## Supported combinational semantics

The engine validates the complete module hierarchy reachable from the selected
top, not only the backward slice of a requested output. Every value must be a
positive-width flat, record, or vector data type with a canonical packed
layout. A record's first declared field occupies the most-significant packed
bits; vector element zero occupies the least-significant element-width bits.
Frontend-defined flat types need no solver special case. Strict interface
checks still use Rhodium type equality rather than packed width alone.

The implemented deterministic operation contract is:

| Category | Supported operations |
| --- | --- |
| Structure | `rtl.input_port`, `rtl.output_port`, `rtl.wire`, `rtl.drive`, `rtl.instance` |
| Constants and bitwise | `rtl.constant`, `rtl.not`, `rtl.and`, `rtl.or`, `rtl.xor`, `rtl.any`, `rtl.all` |
| Modular arithmetic | `rtl.add`, `rtl.mul`, `rtl.sub` |
| Unsigned remainder | `rtl.urem` when query assumptions prove the divisor nonzero |
| Shifts | `rtl.shl`, `rtl.shru`, `rtl.shrs`, including unequal operand widths and overshifts |
| Comparisons | `rtl.eq`, `rtl.ult`, `rtl.slt` |
| Selection | total `rtl.mux_lookup`; non-overlapping `rtl.decode` with every output bit cared in every case and the default; `rtl.onehot_mux` when query assumptions prove its selector exactly one-hot |
| Packing and widths | `rtl.cast`, `rtl.concat`, `rtl.extract`, `rtl.zext`, `rtl.sext`, `rtl.trunc` |
| Aggregates | `rtl.record_create`, `rtl.record_get`, `rtl.vector_create`, `rtl.vector_get` |

Shifts include Rhodium's unequal value/amount-width normalization and defined
overshift behavior. Hierarchy is interpreted compositionally through driven
instance ports. Equal-width casts preserve the packed representation.

## Scope limits and fail-closed behavior

The equivalence query never invents values or assumptions for unspecified behavior.
Structural exclusions are rejected during preflight. When assumptions are
present, their feasibility is checked first; operation validity is then proved
before the requested query.

| Unsupported category | Current stop condition |
|---|---|
| Free or partial values | `rtl.dont_care`; any `rtl.decode` row or default with an uncared output bit |
| Unproved partial selection | Any `rtl.onehot_mux` whose exactly-one selector condition is not implied by the query |
| Unproved remainder domain | Any `rtl.urem` whose nonzero-divisor condition is not implied by the query |
| Sequential behavior | Registers, reset behavior, every memory form, and memory writes |
| Verification effects | Assertions, including `verif.assert` |
| External behavior | DPI calls and DPI result registers |
| Types | `Clock`, `Reset`, nonpackable values, and types without a positive canonical packed width |
| Schema coverage | Any operation not listed in the supported-semantics table |

An internal single-clock stepper also executes register-based hierarchies for
concrete replay and symbolic next-state calculations. It reports clocked RTL
assertions as obligations on the sampled edge, not assumptions. It is not a public
sequential proof or refinement API. See the [transition boundary](DEVELOPING.md#register-transition-foundation).

This is not an unbounded or bounded sequential model checker, a four-state
simulator, a structural equivalence checker, or a synthesis engine. It does
not assign semantics to invalid one-hot selectors, memory collisions,
disabled memory reads, external DPI behavior, or other unspecified hardware
choices.

## Validation targets

| Command | Coverage |
|---|---|
| `moon run rhodium:formal-spec-test` | rspecsim FIFO steps against an independent list oracle; no solver required |
| `moon run rhodium:formal-test` | Fixed-priority arbiter and ShiftQueue refinement, FIFO-pair capacity and ordering, arbiter-to-FIFO winner ordering, symbolic/concrete spec agreement, and RTL equivalence, hierarchy, register stepping, replay, and fail-closed tests after a live Rosette/Z3 probe |
| `moon run rhodium:formal-differential-test` | Independent CIRCT/Verilator replay for structurally different shifts, aggregates, hierarchy, fully cared decode, and assumption-constrained one-hot implementations, plus register traces against native and HDL simulations |

Detailed test ownership and the differential oracle matrix are in
[`DEVELOPING.md`](DEVELOPING.md#validation). Set
`CIRCT_OPT=/path/to/circt-opt` when the pinned tool is not under the current
checkout's `.tools/` directory.

## Compatibility and troubleshooting

| Component | Supported or validated evidence |
|---|---|
| Racket | Rosette declares Racket 8.1 or newer; this repository's formal target is validated with Racket 9.2 |
| Rosette | The focused targets require and are validated with Rosette 4.0 |
| Solver | The focused targets require and are validated with Rosette's pinned Z3 4.8.8 binary |

Other compatible Racket releases have not been exercised by this target.

If `moon run rhodium:formal-test` fails before running tests, first run the install command
again in the same Racket installation. The target deliberately performs a live
solver probe, so a successful package import without a working solver is still
a setup failure. The repository does not vendor Rosette or its transitive
packages.

<details>
<summary>Historical clean-install evidence</summary>

A clean catalog installation from an empty `PLTUSERHOME` was validated with
Rosette checksum `373c8c35e4a7667f38fce10cf0b74ae17de07f1d` and transitive
`rfc6455` checksum `e3a87e914e25841a6e1bb996aa001aeb178284bf`.
An earlier `rfc6455` resolution failure was caused by a restricted test
environment, not an unavailable package source.

</details>
