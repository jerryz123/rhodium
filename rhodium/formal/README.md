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
A [parent proof](DEVELOPING.md#two-shiftqueues-in-series)
proves FIFO ordering and capacities of two and four words for pairs of depth-one
and depth-two queues, respectively. A depth-one pair can refuse a second
word while moving the first word to its empty downstream stage. A consumer
of the exported transaction contract sees reset, accepted words, and retired
words. This suffices to prove that the kth retirement matches the kth
acceptance since reset. The contract supplies no cycle pins: it guarantees
neither immediate admission nor eventual delivery. The [proof example](tests/queue-pair-transaction-order.rkt)
obtains it with `(verified-queue-pair-transactions-at depth)` and checks event
batches with `fifo-allows?`. [QueuePairSeries](tests/queue-pair-series.rkt) connects
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
a one-entry ShiftQueue after that proved ArbiterQueue. Using only those two
specs, the outer proof shows that every retirement returns the oldest accepted
winner and that at most `depth + 1` words are pending. Available space alone
does not guarantee admission. [GatedArbiterQueue](../../examples/formal/gated-arbiter-queue.rhdl)
demonstrates the next boundary: the proof executes the wrapper's own admission
register but substitutes the verified queue spec. Reset closes admission;
otherwise the previous edge's enable controls new transfers. Closing the gate
does not block already accepted words from draining.

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
// edge.events == {"dequeue": 1, "enqueue": 2}
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
  fifo_transactions(reset, ingress(offer, ready, data), egress(valid, consume, head))
```

Each triple is **(valid, ready, payload)**, with directions checked against
the elaborated circuit. This first profile covers one scalar ingress, one
scalar egress, and the standard sampled clock/reset. Bind it with
`QueuePair(top)`. Then [`sample_fifo_events(view, pins)`](fifo-view.rhm) takes
one map containing all pre-edge input and output samples except the clock
and returns the batch for `step_fifo_monitor`. An asserted reset suppresses
both transfers; otherwise both can appear in the same batch, even with a
zero payload. The view reads the actual ready and valid pins. It does not
drive them, certify the FIFO, or replace a cycle model.

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
