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
  state saved: Bits(2) initially 0 as cleared
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

For ready/valid interfaces, `rv_pins` identifies valid, ready, and payload;
`ready_valid_events` samples transfers. Neither promises FIFO behavior:
apply `fifo_monitor` from `lib("rspecl/fifo.rspl")` when ordering and a bound
are the contract you intend to prove.

## Prove a leaf or a parent

Proof hints say **how to check** the contract; they cannot grant it:

- A leaf's `rtl_refinement:` relates physical registers to cycle state and
  identifies which physical outputs or events to check. Its `refinement:`
  can relate monitor history to cycle state.
- A parent's `refinement:` names direct RTL occurrences with `child`, reads
  their public `history`, and proposes a `relate` expression. Each occurrence
  has independent history even when two instances reuse one proof. The runner
  checks the relation at reset and across steps. `direct_wires` checks a claimed
  connection; `forward` checks that a wrapper preserves a child's event batch.
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
child and no others. There is no fallback to recursive proof. Swapping the
two QueuePair seals fails: their specializations and public views differ.

The [series spec](../examples/formal/queue-pair-series.rspl) still has to check
the handoff and relate the downstream history followed by the upstream history
to its six-word FIFO. A successful proof returns an opaque seal for that
specialization and contract. It does **not** promise that `ready` is high
whenever space remains, or that every word eventually retires.

For the concrete model API, solver setup, supported RTL, and proof limits,
see the [formal engine guide](../rhodium/formal/README.md). Proofs here are
safety claims for the checked finite specializations, not claims over all
parameter values or clock domains.
