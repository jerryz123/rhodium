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
[leaf proof](DEVELOPING.md#fixed-shiftqueue-refinement) establishes that the
two-bit, depth-two ShiftQueue (pipe, flow, and flush disabled) matches its FIFO
spec on every edge following sampled reset. A [parent proof](DEVELOPING.md#two-shiftqueues-in-series)
uses two copies of that spec to prove four-word capacity and FIFO ordering for
the series connection. It does not promise admission whenever there is space
or eventual delivery. A separate [arbiter proof](DEVELOPING.md#two-input-fixed-priority-arbiter)
covers a two-input, two-bit Decoupled arbiter: lane 0 wins ties and each
output transfer consumes exactly the winning input. A stalled grant is not
locked. When that arbiter feeds the FIFO, the
[combined proof](DEVELOPING.md#arbiter-feeding-a-shiftqueue) shows that each
dequeue returns the oldest accepted winner since reset. An offered word that
was never accepted does not enter the queue. Reset discards pending words;
the proof promises neither eventual delivery nor fairness between lanes.
These are fixed proof fixtures, not a public refinement checker.

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
  lib("examples/formal/shift-queue.rspl").ShiftQueue

def fifo = ShiftQueue(2, 2)
def edge = step_spec(fifo, {"q": [1]},
                     {"reset": #false, "in.valid": #true, "in.data": 2, "out.ready": #true})
// edge.outputs["out.data"] == 1
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

Start a `.rspl` file with `#lang rspecl`. Name a spec for a particular RTL
circuit after that circuit: `spec ShiftQueue(width :: PosInt, depth :: PosInt):`
in `shift-queue.rspl` corresponds to `ShiftQueue` in `shift-queue.rhdl`.
`ShiftQueue(2, 2)` constructs a checked model; it does not advance an edge.
The name is a convention, not a binding or a proof: the refinement fixture
still selects the RTL configuration and spec explicitly. Use qualified imports
if one module needs both. An independent or shared contract can use its own name.

For a fixed model, omit the parameter list. For example, this pending flag reports its
**old** value even on the edge that clears it:

```rhombus
#lang rspecl

fun both(a :: SpecExpr, b :: SpecExpr): a && b

spec PendingFlag:
  input clear: Bool
  state pending: Bool initially #true
  output busy = pending
  event cleared = pending when both(clear, pending)
  next pending = pending && !clear
```

`PendingFlag` is the model itself. Anonymous `spec:` remains available when
a model is needed as an expression. All three forms use the same checked
spec IR without building RTL or running a solver.

Inputs and state introduce lexical bindings. Helpers such as `both` can live
in the block, outside it, or in an imported library. Their ordinary Rhombus
bodies build expressions once per model construction; the interpreters never
call them on an edge. `def`, `fun`, and `let` retain their Rhombus scope.
Outputs and events do not introduce bindings. A dotted input such as
`in.valid` is a lexical namespace member with the public key `"in.valid"`.
`output` describes
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

## Concrete specification API

`make_spec(inputs, state, initial, outputs, events, updates)` builds an immutable
model. The first two maps declare separate namespaces of string names and
types: `SpecBool()`, `SpecBits(width)`, or `SpecSeq(scalar_type, capacity)`.
Widths and capacities are positive. Values are Booleans, unsigned naturals
that fit their width, and immutable lists of at most the declared capacity.
`model.initial` is a complete concrete state, not an assumption about RTL startup.

Build expressions using `literal`, `input_ref`, `state_ref`, Boolean `bool_not` /
`bool_and`, deterministic `choose_if`, and `seq_empty`, `seq_full`, `seq_front`,
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
