<!-- Introduces the RSPECL authoring language, concrete execution, and public hierarchical proof driver. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RSPECL: executable hardware contracts

Use `#lang rspecl` in a `.rspl` file to describe what a client may observe
from a Rhodium circuit. A spec takes its ports and parameters from the RTL
declaration but defines its own behavior. **Simulating** it tells you what
the spec says about one input trace. **Proving** it checks the RTL against the
contract and returns evidence another module can use. Only proof needs Rosette.
For contributors, see [Developing the formal engine](../rhodium/formal/DEVELOPING.md).

## Write and run a leaf

This selector remembers a two-bit word. On a capture edge the caller reads
the **previous** word, not the word being captured. Save this as `selector.rspl`
and run `PLTCOLLECTS="$PWD": tools/run-racket.sh selector.rspl` from a
[configured checkout](../README.md#requirements).

```rhombus
#lang rspecl
import:
  lib("examples/formal/granted-selector.rhdl") as rtl
  lib("rhodium/frontend/foundation.rhm").elaborate
  lib("rhodium/formal/spec.rhm").step_spec

spec for rtl.RegisteredLaneSelector:
  state saved: word_a.type initially 0 as cleared
  assume exclusive = reset || !(enable_a && enable_b)
  observe selected = saved when !reset
  event captured = select(enable_a, word_a, word_b) when !reset && (enable_a || enable_b)
  next saved = select(reset, cleared, select(enable_a, word_a, select(enable_b, word_b, saved)))

module main:
  def model = RegisteredLaneSelector(elaborate(rtl.RegisteredLaneSelector()).top)
  def edge = step_spec(model, model.initial,
                       {"reset": #false, "enable_a": #true, "word_a": 2,
                        "enable_b": #false, "word_b": 0})
  println([edge.outputs["selected"], edge.events["captured"], edge.next_state["saved"]])
```

The result is `[0, 2, 2]`: pre-edge readback, capture payload, successor state.
`model.initial` initializes the **spec**; it is not evidence that an RTL
register powers up with that value. This example executes a cycle contract.
The [proof-ready selector](../examples/formal/granted-selector.rspl) also
declares a transaction monitor and a checked relation to the RTL register.
With [Rosette installed](../rhodium/formal/README.md#install-and-quick-start),
run its leaf proof with
`PLTCOLLECTS="$PWD": tools/run-racket.sh examples/formal/prove-registered-lane-selector.rhm`.

## Describe the contract

`spec for rtl.Name:` binds a constructor named `Name`. A top-level declaration
exports it automatically. Helpers and locally declared specs follow ordinary
Rhombus scope; export helpers explicitly to share them. Call `Name(top)` on
the matching elaborated specialization to get its cycle model. Port names,
interface members and supported integer and Boolean parameters are lexical
bindings. Use ordinary Rhombus functions to build expressions; the solver does
not invoke those functions on each hardware edge.

For an interface array, a Rhombus `for (i in 0..n):` can generate
`observe egress[i].bits = ...` in a cycle model and `pin`/`care` declarations
in its RTL refinement. Within a monitor loop, `batch sent(key) = present(name)`
binds the lexical variable `sent` to a batch field named by the string `key`;
`batch word(data_key): T = payload(name) when sent` binds its guarded payload.
`guarantee (label) = ...` names a verdict for that iteration. These expressions
run at specialization time; generated names must be unique within their schema.
A loop can also declare a named cycle or physical transfer with `event (name) = value when guard`.
See the [AtomicFork spec](../examples/formal/atomic-fork.rspl) for a complete example.

A monitor that also needs to run without an RTL instance can write
`specialize w = word_a.type.width ~default: 2`. With an elaborated top,
the RTL expression always sets `w`; the default applies only to a detached
`~transaction_view: #true` request. This describes a monitor, not proof
evidence. A monitor with a bound `events` reader already specializes to
the actual top and needs no default.

`state slots[names]: T initially value` declares one ordinary state field per
name and binds `slots` to their references in list order. Use `next slots[i] = ...`
in a loop; all updates still read the old state. In a cycle or RTL projection,
`relate (name) = ...` names the target field for that iteration. The proof must
still cover each field with the correct type.

| Inside a spec | Meaning |
|---|---|
| `state`, `next` | Declare reference state and its update. All updates read old state and commit together. An unassigned field holds. |
| `observe`, `ignore` | In a cycle spec, account for an output or explicitly leave it unspecified. A false `when` guard means **not observed**, not zero. |
| `event`, `events` | Name a payload and when it occurs, or bind a reusable event sampler. A present false or zero payload is still an event. |
| `assume` | Name a predicate on current input pins. rspecsim rejects a violating sample. A parent must establish the predicate on the child's actual pins before using its guarantee. |
| `monitor:`, `transactions` | Describe legal joint event batches and their history, inline or with a monitor returned by a helper. `batch` binds event presence or payload; a named `guarantee` must be proved, not assumed. |

Values include `Bool`, fixed-width `Bits(w)`, and bounded `Seq(element, capacity)`.
Use `select` on sampled conditions, `bool_equal` on logical predicates (not
`Bits(1)`), `bits_equal` on packed values, and
`front`, `tail`, `append`, or `seq_concat` on sequences. Guard partial
operations such as reading an empty sequence. See the [capture monitor](../examples/formal/capture-history.rspl)
for a complete `sample` / `publish` / `batch` / `guarantee` example, and the
[ordinary FIFO helper](fifo.rspl) for a sequence monitor that different modules reuse.

In a monitor, `sample` selects the pin sampler and `publish` names the events
also exposed by the cycle model. `present(e)` tests for an event;
`payload(e) when active` reads its data when present. If any guarantee
rejects the batch, monitor history holds. For a spec with a transaction
monitor, `Name(top, ~transaction_view: #true)` returns its description,
**not** a proof. A monitor-only spec need not predict cycle outputs.

A guarded guarantee still returns a verdict on **every** sample:
`guarantee "left-payload" = bits_equal(incoming, left_word) when take && send_left`
passes when the guard is false without evaluating the body. It does not assume
that the two events coincide; that needs its own guarantee.

For custom transfers, declare a physical sampler inside `spec for`. The name
after `from` is a local pin reader. It resolves each scalar or interface
member against the elaborated RTL, inferring direction and width:

```rhombus
events samples from pin:
  def active = !pin(reset)
  event installed = pin(update.bits) when active && pin(update.valid)
  event delivered = pin(egress.bits) when active && pin(egress.valid) && pin(egress.ready)
```

Use `sample samples` and `publish [installed, delivered]` in the monitor.
On a physical trace, `pin(egress.bits)` reads the actual output; only cycle
simulation substitutes the spec's predicted output. Both use the same event
definitions. The [OfferRegister spec](../examples/formal/offer-register.rspl)
also samples stalled offers without publishing them as deliveries. This form
requires an elaborated top, including when requesting a transaction view.

Inside a sampler, a loop can bind an indexed event to an actual pin:
`event (grants[i]) = #true when pin(ingress[i].ready)`. The index selects
both the event reference and the physical port at elaboration. Static and
indexed events share one name space; duplicate labels are rejected.
The [Arbiter spec](../examples/formal/arbiter.rspl) samples requests and
grant outputs separately so its priority check can detect a spurious grant.

A reusable monitor can also be constructed before it has an RTL instance.
In its `sample fun (input, output): ...` callback, use
`sample_pin(input, complete_gen_a, Bool)` or `sample_pin(output, retire.bits, Bool)`.
The reader supplies the direction and the last argument supplies the expected
sort; binding the monitor checks both against the RTL. The
[ordered-retirement monitor](../examples/formal/ordered-retirement.rspl) uses
this form so the ROB and its logger share the same completion pin mapping.

For independent ready/valid handshakes, name the endpoints directly:

```rhombus
events transfers = rv_transfers(~accepted: ingress, ~left: egress[0], ~right: egress[1])
```

Use `sample transfers` and `publish [accepted, left, right]` inside the monitor.
The keywords name events; the endpoint expressions use ordinary lexical bindings.
A list of identifiers in `publish` names sampler fields, not local variables.
Unknown or duplicate names are rejected when the monitor binds its sampler.
Helpers can still pass a string-keyed map to `rv_transfers` or a computed list
of strings to `publish`. Both forms produce the same public event names.

For parameterized ports, declare trace labels once with `event_family(names)`
and public state keys with `history_family(names)`. Both return lists in the
supplied order and reject empty or duplicate labels. Use an event reference
as an `rv_transfers` map key, in `present` or `payload`, or after a static
publish list: `publish [reset, accepted] ++ lanes.delivery`. Use a history
family to declare state (`state owed[lanes.owed]: ...`) and to read a sealed
child's public cell (`history pending from fanout[lanes.owed[i]]: ...`).
Event and history references are not interchangeable.
The [broadcast contract](broadcast.rspl) binds both families to the elaborated
egress array; the two-output specialization keeps the trace labels `left` and
`right` without making consumers special-case lane count.

Each event carries its own endpoint's
payload exactly when its valid and ready are both high. Pin direction and payload
type come from the elaborated ports; use `rv_pins("valid", "ready", "data")`
for three scalar pins. There is no implicit reset or choice of winner. Even a
one-sided transfer remains visible. The [atomic fork](../examples/formal/atomic-fork.rspl)
uses [`DeliveryLanes(egress)`](delivery.rspl) to bind these events, then checks
that they occur together and carry the accepted word. This sampler reads no
reset pin and tracks no outstanding copies. For FIFO-style reset, accepted and retired events use
`ready_valid_events`; add [`fifo_monitor`](fifo.rspl) to promise ordering and a bound.
For a buffered broadcast, construct `lanes = BroadcastLanes(egress)` and bind
`events transfers = lanes.transfers(ingress)` to sample the input and every
output. Pass the same lanes and reader to
[`broadcast_monitor(lanes, width, transfers)`](broadcast.rspl). It samples
the `reset` input and tracks which recipients still need the old word.
Outside the two-output specialization, delivery labels are `lane0`, `lane1`, and so on.
The last recipient may take that word on the edge accepting its replacement;
the monitor promises neither when a recipient is ready nor that it ever retires.
Compare [Broadcast](../examples/formal/broadcast.rspl), which can accept on that
edge, with [QueuedFork](../examples/formal/queued-fork.rspl), which waits for
every branch queue to have space. Both use the same monitor.
An observer can reuse `broadcast_events(transfers)` to sample the same
reset-qualified transfers alongside its own pins, as in
[BroadcastReceipt](../examples/formal/broadcast-receipt.rspl).

## Prove a leaf or a parent

Proof hints say **how to check** the contract; they cannot grant it:

- A leaf's `rtl_refinement:` relates physical registers to cycle state and
  identifies which physical outputs or events to check. Its `refinement:`
  can relate monitor history to cycle state.
- A parent's `refinement:` names direct RTL occurrences with `child`, reads
  their public `history`, and proposes a `relate` expression. Each occurrence
  has independent history even when two instances reuse one proof. The runner
  follows the elaborated RTL wiring and checks the relation at reset and across
  steps, as in [QueuePair](../examples/formal/queue-pair.rspl). Use `forward`
  when the obligation is that a wrapper preserves a child's event batch.
  For a repeated child, `children (sites): ChildSpec` declares one checked
  occurrence per name. Inside a refinement loop, `history h from (site, field): T`
  reads that occurrence's public history; `local bit(name): Bool` reads a parent
  register. Neither construct grants access to a child's registers.
- An alternate monitor can use `under` to retain a declaration's input
  **assumptions**, not its guarantees. See [GatedCapture](../examples/formal/gated-capture.rspl).
  For an assumption spanning samples, see [stalled-offer requirements and suppliers](../examples/formal/stalled-offer.rspl).

The public proof driver is `lib("rspecl/runner.rkt")`. Here is a complete
parent driver. Save it as `series.rhm` and run
`PLTCOLLECTS="$PWD": tools/run-racket.sh series.rhm` from the repository root:

```rhombus
#lang rhombus
import:
  lib("rhodium/frontend/foundation.rhm").elaborate
  lib("rspecl/runner.rkt") as proof
  lib("examples/formal/queue-pair-series.rhdl") as rtl
  lib("examples/formal/queue-pair-series.rspl").QueuePairSeries
  lib("examples/formal/queue-pair.rspl").QueuePair

module main:
  def top = elaborate(rtl.QueuePairSeries()).top
  def front = proof.prove_design(proof.child_definition(top, "front"), QueuePair)
  def back = proof.prove_design(proof.child_definition(top, "back"), QueuePair)
  def seal = proof.prove_design(top, QueuePairSeries, ~children: {"front": front, "back": back})
  println("Proved six-word FIFO ordering")
```

`child_definition(top, site)` looks up a **direct occurrence** by its RTL
instance name. `prove_design(top, spec)` takes the **constructor**, not
`spec(top)`, and recursively obtains child evidence. With `~children:`
(`#:children` in Racket's `prove-design`), the map must supply every declared
child and no others. There is no fallback to recursive proof. A shared
contract has no proof recipe, so its seal must be supplied.

A parent can also name an implementation-independent contract, as in
`child fanout: broadcast_contract` in [BroadcastReceipt](../examples/formal/broadcast-receipt.rspl).
The caller supplies the seal for the RTL at `fanout`; the parent does not
import the implementation's spec. [`shared_contract(build)`](broadcast.rspl)
calls `build(groups, sample)` with the occurrence's ready/valid groups
(including indexed arrays) and a function that binds an event source to its
directional pins. The builder returns a public monitor, not a proof recipe.
The default is a clocked boundary; use `~synchronous: #false` for a clockless one.
This cut requires unconditional evidence, the actual child definition, and
identical executable public IR: published events, pin directions, batch
sampling, initial history, verdicts and updates. Matching event names is not
enough. It does not search for an implementation or prove monitor implication.

The [series spec](../examples/formal/queue-pair-series.rspl) uses
[`fifo_contract(capacity, width, source)`](fifo.rspl) to require capacities
`2 * front_depth` and `2 * back_depth` (two and four words by default). It passes the ready/valid event
*source*, not the parent's sampled events: the same pin names are read separately
on each child. The parent joins the downstream history followed by the upstream
history to describe a FIFO bounded by their sum. The runner checks
this relation on the actual RTL handoff. A successful proof returns an opaque seal for that
specialization and contract. It does **not** promise that `ready` is high
whenever space remains, or that every word eventually retires.

For the concrete model API, solver setup, supported RTL, and proof limits,
see the [formal engine guide](../rhodium/formal/README.md). Proofs here are
safety claims for the checked finite specializations, not claims over all
parameter values or clock domains.
