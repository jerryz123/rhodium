<!-- Documents the public streaming components and composition contracts of the flow library. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Flow library

`flow/` provides reusable buffering, arbitration, routing, packet adapters,
and typed streaming composition in ordinary `#lang rhodium`. It adds no
compiler or core IR semantics.

Import the facade when composing several facilities, or a focused module
such as `lib("flow/queue.rhdl")` for one family:

```rhombus
import lib("flow/main.rhdl") open

ingress |> queue(4, ~pipe: #true) |> pipe(2) |> egress
```

The chain belongs inside a `sync_circuit` with compatible ingress and egress
endpoints. The facade re-exports the existing `Pulse`, `Valid`, `Decoupled`,
`Irrevocable`, control-only, credited, and flit types from
[`rhodium/std`](../rhodium/std/README.md#protocols-and-transport). These are the
same nominal definitions, not replacement interfaces.

For protocol declarations and `.fire()`, see the
[standard protocol contracts](../rhodium/std/README.md#ready-valid-protocols).
For maintenance and validation, see [`DEVELOPING.md`](DEVELOPING.md).

The flow library has two authoring levels. Instantiate a named generator when
you need its additional ports or instance identity. Use a lowercase configured
stage when the component should participate in a `|>` topology and its payload
or protocol can be inferred.

## Component catalog

[`main.rhdl`](main.rhdl) re-exports the independently importable generators
in this library:

| Valid-only generator | Behavior |
|---|---|
| `ValidPipe(T, stages)` | Fixed-latency registered valid/payload pipeline with no backpressure |
| `ValidPipeAlwaysCapture(T, stages)` | Fixed-latency Valid pipeline that captures payload on invalid cycles too |
| `ValidArbiter(T, n)` | Fixed-priority selection that drops simultaneous lower-priority events |
| `OfferRegister(T)` | Rewritable one-slot state whose accepted Decoupled offer clears when no replacement arrives |

| Transaction generator | Behavior |
|---|---|
| `CompletionQueue(Request, Response, depth)` | Reserves response capacity at a ready-valid request handshake, emits a nonstallable issue, and buffers the matching nonstallable completion |

| Credited transport generator | Behavior |
|---|---|
| `CreditSender(T, credit_limit)` | Converts ready-valid ingress to credited transport using only previously registered credits |
| `CreditBuffer(T, depth)` | Owns receiver capacity, returns credits, and converts credited transport to an irrevocable egress |
| `CreditCounter(limit)` | Tracks a bounded resource balance with simultaneous increment and decrement |

| Allocation generator | Behavior |
|---|---|
| `GreedyMatcher(inputs, outputs)` | Fixed-priority maximal matching over an input-major request matrix |
| `OutputGreedyRoundRobinMatcher(inputs, outputs)` | Fixed-output-order maximal matching with per-output rotating input priority |

| Payload generator | Control-only generator | Behavior |
|---|---|---|
| `Pipe(T, stages)` | `CtrlPipe(stages)` | Registered elastic pipeline with stable output under backpressure |
| `Queue(T, depth)` | `CtrlQueue(depth)` | Configurable FIFO with occupancy count |
| `ShiftQueue(T, depth)` | -- | Shallow fixed-head FIFO with occupancy count and valid mask |
| `Arbiter(T, n)` | `CtrlArbiter(n)` | Fixed-priority, index-zero-first arbitration |
| `RRArbiter(T, n)` | `CtrlRRArbiter(n)` | Fair round-robin arbitration |
| `PacketRRArbiter(T, n)` | -- | Round-robin arbitration that retains an input through its final transferred beat |
| `VcMux(T, n)` | -- | Fairly tags and multiplexes `n` independently backpressured virtual channels |
| `VcDemux(T, n)` | -- | Validates and distributes tagged traffic while exposing per-channel readiness |
| `StateChangeSource(T, n, initial)` | -- | Fair irrevocable emission of indexed values that differ from their last transferred snapshots |
| `StateReplica(T, initial)` | -- | Always-ready application and retention of transferred state updates |
| `Demux(T, n)` | `CtrlDemux(n)` | Selected one-to-many routing with invalid-selector blocking |
| `GrantDemux(T, outputs)` | -- | Optional-one-hot grant routing to ready-valid outputs |
| `GrantMerge(T, inputs)` | -- | Optional-one-hot grant selection from ready-valid inputs |
| `GrantCrossbar(T, inputs, outputs)` | -- | Grant-controlled one-to-one ready-valid payload traversal; configured `grant_crossbar(outputs, ~grants)` stage |
| `Join(T, n)` | `CtrlJoin(n)` | Atomic join that never partially consumes inputs |
| `SelectiveJoin(T, n)` | -- | Selection-token rendezvous that consumes exactly the chosen data flows and reports meaningful lanes |
| `Broadcast(T, n)` | `CtrlBroadcast(n)` | Buffered exactly-once delivery tracked independently per recipient |
| `AtomicFork(T, n)` | `CtrlAtomicFork(n)` | Combinational fanout where every recipient transfers together or none do |
| `SelectiveAtomicFork(T, n)` | -- | Payload-selected combinational fanout where every selected recipient transfers together or none do |

Import the aggregate when several components are needed:

```rhombus
import:
  lib("flow/main.rhdl") open

inst buffered(Queue(Bits(8), 4, ~pipe: #true, ~flow: #false))
```

## Building pipelines

`ValidPipe(T, stages, ~flushable: #true)` and
`ValidPipeAlwaysCapture(T, stages, ~flushable: #true)` expose a synchronous
`flush: Bool` input. The default is false and preserves the existing port set.
Flush clears all stage valid bits at the next clock edge, taking priority over
new input validity. Payload capture behavior is unchanged: `ValidPipe` captures
only on the stage's input valid, while `ValidPipeAlwaysCapture` captures every
cycle. Output validity is not gated combinationally by flush; callers that
cancel a transfer on the flush cycle must filter that output explicitly.
Global reset also clears validity. Surviving transfers retain the declared
fixed latency, and tracing flushes the corresponding lineage without resetting
event history or sequence counters.

Configured stages accept an optional hardware signal and connect the port:

```rhombus
source |> valid_pipe(2, ~flush: recovery) |> sink
```

`valid_pipe_always_capture` accepts the same `~flush` option. Omitting the
option elaborates the ordinary pipeline without a flush input.

Every lowercase flow-stage helper is configured first and receives its input only
through Rhombus `|>`. This makes every stage an ordinary unary host function:

```rhombus
ingress |> queue(4, ~pipe: #true) |> pipe(2) |> egress
```

`trace_event(label)` inserts a transparent compiler-visible checkpoint on a
`Decoupled` or `Irrevocable` payload flow. `trace_valid_event(label)` provides
the same annotation for `Valid`. These helpers do not add state or runtime
effects; `rhodium/event` consumes their metadata to infer possible nearest
dependencies. The [annotation contract](../rhodium/event/README.md#annotate-events)
owns label rules, partial ancestry, and supported tracing behavior.

`trace_event(label, ~stalls: #true)` also requests a `<label>.stall` observation
on each `valid & !ready` cycle. The default is false; `trace_valid_event` does
not accept this option because `Valid` has no readiness signal. See
[stall observations](../rhodium/event/README.md#stall-observations) for ancestry
and capture semantics.

An opaque state machine can connect two annotated routes with
`trace_edge(parent, child, ~scope: "request")`. This records a causal edge for
the event compiler without adding a functional wire or changing the Flow
datapath. The scope names a module-local
`describe_interface_retained_storage(capture, release, active, ~name: ...)`
declaration; one retained owner may feed multiple child routes, while each child
has one declared parent. Use this only when the state machine retains the
transaction across cycles. Ordinary Flow still derives its relationships from
the actual typed topology. The lower-level declaration is documented in the
[interface contract API](../rhodium/frontend/layers/README.md#interfaces-and-topology).

Bare checkpoints capture identity and timing only. Select named scalar observations
without changing the forwarded payload:

```rhombus
def observed = ingress |> trace_event("fetch", ~fields: payload):
  pc: payload.pc
  instruction: payload.instruction
```

The [capture contract](../rhodium/event/README.md#capture-fields) describes the
typed binder, scalar formats, instruction disassembly, and explicit raw dumps.
The event compiler owns the [supported-transform table](../rhodium/event/README.md#traceable-transforms)
and [instrumentation limits](../rhodium/event/README.md#deliberate-limits).
Flow components publish typed contracts using their actual functional controls;
they do not implement compiler lineage propagation or depend on the compiler.
`Pipe`, `ValidPipe`, `ValidPipeAlwaysCapture`, `Queue`, `Arbiter`, `RRArbiter`,
and `Broadcast` own their boundary contracts. Both explicit instances and their
configured adapters are traceable without caller-side redeclarations. Inline
adapters own their contracts where they implement the transformation; see the
[interface contract API](../rhodium/frontend/layers/README.md).

Packet arbitration takes an inline predicate that identifies the final beat.
The selected input remains the sole owner across stalls and bubbles until that
beat transfers, and priority advances once per complete packet:

```rhombus
inputs |> packet_rr_arbiter(flit => flit.tail) |> pipe(1) |> egress
```

Selective atomic fanout similarly derives a nominal `Mask(n)` from the current
payload. Only selected outputs participate in readiness, and all selected
outputs transfer together:

```rhombus
requests |> selective_atomic_fork(4, request => request.destinations)
```

With a concrete endpoint source, each operation connects immediately and
returns its far endpoint shape; a final endpoint terminates the complete
pipeline. A disconnected topology begins with its payload or protocol type
exactly once and returns an ordinary `InterfaceHandle`:

```rhombus
def path = Request()
           |> pipe(1)
           |> queue(4)
def buffered = ingress |> path
```

Use an explicit `Decoupled(T)` or `Irrevocable(T)` seed when the disconnected
path must retain that exact contract. Later stages infer the protocol and
payload from the preceding endpoint or handle; they never repeat it.

## Static typing and reusable topologies

Flow stages preserve enough static information for a statically known endpoint
to produce its connected shape, for an endpoint array to produce the resulting
cardinality, and for a type seed or existing handle to produce a handle.
Consequently `.bits`, `[0].bits`, array destructuring, and handle `.right`
remain available under `use_static` without corrective `:: Endpoint`
annotations. The contributor guide explains the
[shared implementation of that propagation](DEVELOPING.md#static-information-and-topology-results).

Fan-in helpers take an ordinary host `Array`. `arbiter()`, `rr_arbiter()`, and
`packet_rr_arbiter(...)` infer the input count from a connected array. A
disconnected topology states its protocol once and its cardinality in the
configured arbiter:

```rhombus
def selected = Array(first_request, second_request)
               |> arbiter()
               |> pipe(1)

def selector = Request()
               |> rr_arbiter(2)
               |> pipe(1)

def packet_selector = VariableFlit(Request())
                      |> packet_rr_arbiter(2, flit => flit.tail)
                      |> pipe(1)
```

Use an explicit `Arbiter`, `RRArbiter`, or `PacketRRArbiter` instance when its
`chosen` output is needed. The packet primitive takes a `Vec(n, Bool)`
`ends_packet` sideband; the configured helper derives it from the inline
predicate. Inputs must be a nonempty array of mutually compatible `Decoupled`
or `Irrevocable` endpoints.

## Protocol normalization

Ready-valid flow helpers classify protocols through the same nominal
refinement and `supports` relation used by interface connections. A differently
named refinement is accepted and normalized to its canonical `Decoupled(T)`,
`Irrevocable(T)`, or `Valid(T)` contract; an unrelated declaration is rejected
even if it reuses one of those display names. Control-only helpers additionally
require the exact payloadless control-plane member shape, so a payload-bearing
protocol is not silently treated as a `Ctrl` flow.

## Credited transport adapters

[`credit.rhdl`](credit.rhdl) provides the transport adapters.
`CreditSender(T, credit_limit)` accepts `Decoupled(T)`, tracks returned
credits, and emits credited traffic only while its registered balance is
nonzero. `CreditBuffer(T, depth)` owns the receiver capacity, accepts every
legal credited transfer into a pipelined queue, and emits `Irrevocable(T)`.
Its `grant_enable` input controls new grants without revoking credits already
held remotely. The buffer initially grants empty capacity one credit per cycle
and recycles a credit when an item leaves its egress.

`credit_sender(credit_limit)` and `credit_buffer(depth)` are configured unary
flow stages. They make a credited hop compose as `Decoupled -> CreditSender ->
Credited -> CreditBuffer -> Irrevocable`; a source supplies the payload and
protocol through `|>`. Mapping, arbitration, and routing stay in the
ready-valid domain between hop boundaries rather than acquiring duplicate
credited variants. `CreditCounter` is the shared bounded accounting circuit
used by the adapters.

## Packet and framing adapters

[`flit.rhdl`](flit.rhdl) supplies the safe ready-valid conversion
graph:

- `Packetizer(T, chunk_width)` serializes any packed `T` into
  least-significant-chunk-first framed flits, zero-padding the unused high bits
  of the final flit.
- `Reassembler(T, chunk_width)` checks those explicit boundaries and holds the
  reconstructed `T` until it is accepted.
- `frame_fixed_flits(n)` adds canonical markers to `FixedFlit` traffic.
- `strip_fixed_framing(n)` checks canonical markers before removing them.
- `require_fixed_framing(n)` checks variable traffic before strengthening its
  fixed-length contract.
- `forget_fixed_framing()` weakens framed-fixed traffic to variable traffic
  without state or buffering.

Every stateful conversion advances phase only when both `valid` and `ready`
are asserted. Stalls therefore preserve phase and framing. The conversions
preserve `Decoupled` versus `Irrevocable` protocol strength and neither add a
queue nor alter transfer count. `Packetizer` and `Reassembler` are the
width-changing stateful components; the framing conversions remain
representation-only. Variable-to-fixed conversion is deliberately not an
unchecked cast because arbitrary packet lengths still require an explicit
length contract.

## Virtual channels and fanout

`VcMux(T, n)` and `VcDemux(T, n)` let `n` independently backpressured logical
flows share one physical ready-valid flow. The mux fairly selects only lanes
whose corresponding `vc_ready` bit is asserted and emits `VcBeat(T, n)` with
the selected lane index. The demux validates that index, delivers the payload
to exactly one output, and exposes every output's readiness for the upstream
mux. `VcLink(T, n)` groups that multiplexed beat flow with the reverse
per-channel readiness vector at a typed physical boundary. Neither component
allocates per-VC buffering or gives the lanes routing, reservation, credit, or
deadlock semantics; domain libraries and callers own those policies.

## Replicated state

`StateChangeSource(T, n, initial)` converts a live `Vec(n, T)` into an
`Irrevocable(IndexedState(T, n))` stream. It fairly selects vector elements
whose current value differs from the last successfully transferred value for
that index. A selected update remains unchanged while stalled. A newer value
that arrives during that stall remains dirty after the held value transfers,
and the source can capture the next update in the same cycle as a transfer.

Changes that return to the published value before being selected are
intentionally coalesced. This is latest-state convergence, not event delivery;
use a queue when every occurrence must be retained. `T` may be any packable
`DataType`, and `initial` must be an exact `HardwareLiteral` of `T`. Source and
receiver must use the same initial value when they represent replicas of one
state.

`StateReplica(T, initial)` is the corresponding always-ready sink. It applies
each transferred `Decoupled(T)` value to a local register and exposes the held
value through `current`, independently of later flow activity. Neither
component assigns a network destination or transport route.

`atomic_fork` returns an indexable array of `Decoupled` endpoints and permits a
transfer only when every output can accept it. This is useful when one logical
transaction must atomically update multiple downstream flows. In contrast,
`Broadcast` stores per-recipient delivery state so recipients may accept the
same item in different cycles.

`selective_atomic_fork(n, payload => mask)` applies the same all-or-none rule
only to the outputs selected by the payload-derived `Mask(n)`. Its input may be
`Decoupled`: the payload and selection may change together while stalled because
no selected output has transferred. Its outputs are always `Decoupled` because
each output's `valid` depends on the readiness of its selected peers. An empty
mask consumes the input without producing any output transfer.

## Mapping and protocol conversion

`map_flow(payload => expression)` configures an inline payload mapping while
forwarding valid and ready. The binder retains precise bundle field
information, and the result type is inferred from the body. The colon form
supports multiline mappings. The ordinary form returns `Decoupled`, even for
an `Irrevocable` input, because the body may observe changing ambient hardware.
Use `map_flow(~stable: #true, ...)` only when the expression is a stable
function of the held payload; that explicit assertion preserves the input's
`Decoupled` or `Irrevocable` protocol strength:

```rhombus
def tagging:
  map_flow(payload => TaggedRequest()):
    request: payload
    processor: Bool(#false)

def tagged = ingress |> tagging
```

`map_valid(payload => expression)` is the corresponding inline payload map for
`Valid` flows. `filter_valid(payload => predicate)` drops asserted events whose
predicate is false, and `fork_valid(n)` copies every retained event to all `n`
outputs in the same cycle. None adds an instance or a readiness path:

```rhombus
def enabled_filter = filter_valid(request => request.enabled)
def request_map = map_valid(request => translate(request))
def mapped = issued |> enabled_filter |> request_map
def copies = mapped |> fork_valid(2)
```

`to_valid()` explicitly converts a `Decoupled(T)` or `Irrevocable(T)` flow into
same-cycle `Valid(T)` events. It permanently asserts input readiness and adds
neither storage nor hierarchy. Use it only where losing downstream
backpressure is intentional:

```rhombus
source |> to_valid()
  |> eject_flow(~valid: sink_valid, ~bits: sink_bits)
```

`to_decoupled()` performs the checked inverse for a nonbackpressured `Valid(T)`
source. It adds a readiness path and asserts that every valid event is accepted
in the same cycle, making the otherwise unsafe boundary explicit:

```rhombus
valid_source |> to_decoupled() |> arbiter_input
```

`offer_decoupled()` instead presents each `Valid(T)` occurrence as a best-effort
`Decoupled(T)` offer. It forwards validity and payload unchanged, with no storage,
latency, or acceptance assertion. If the receiver is not ready that cycle, the
occurrence is lost at this boundary; the caller owns any retry or replay. Neither
payload stability nor persistence while stalled is promised. Use `to_decoupled()`
when every occurrence must be accepted immediately, or explicit storage when
the producer's protocol provides a way to avoid overflow.

```rhombus
valid_attempts |> offer_decoupled() |> request_sink
```

The adapter uses the existing same-cycle combinational event-lineage contract.
Rejected offers retain no pending identity; a replay is a new upstream occurrence.
Admission/fault policy belongs to the consumer, not the adapter. A qualified
[`trace_event`](../rhodium/event/README.md) can observe successful
admission without changing the functional offer.

## Circuit boundaries

The flow facade retains `inject_flow(protocol, ...)` and `eject_flow(...)` as
convenience aliases for the interface layer's generic `inject_interface` and
`eject_interface` boundaries. Each named binding corresponds to a declared
protocol member. A `Decoupled(T)` boundary therefore names `~valid`, `~bits`,
and `~ready`, while a `Valid(T)` boundary names only `~valid` and `~bits`:

```rhombus
inject_flow(
  Decoupled(Request()),
  ~valid: source_valid,
  ~bits: source_bits,
  ~ready: source_ready
)
  |> pipe(1)
  |> map_flow(request => translate(request))
  |> eject_flow(~valid: sink_valid, ~bits: sink_bits, ~ready: sink_ready)
```

On injection, forward arguments are readable hardware values and return-path
arguments are driveable places. Ejection reverses those requirements: forward
arguments are driveable places and return-path arguments are readable values.
Nested interface members use `flow_fields(~field: value, ...)` to group their
named leaves. The helpers work from declared member directions, including
custom protocols and control-only interfaces; they add neither storage nor
hierarchy.
Neither boundary helper changes the protocol; use an explicit transformation
such as `to_valid()` before ejection when the circuit-side contract differs.

## Filtering, gating, and routing

`filter_flow(payload => predicate)` consumes an offered token without producing
an output when the hardware predicate is false. Its input is ready for a
rejected token regardless of downstream backpressure. `gate_flow(enabled)`
instead blocks both input readiness and output validity while
disabled, preserving the token upstream. Because either decision may observe
live sideband state, both helpers conservatively return `Decoupled` even when
their input is `Irrevocable`; a following `pipe` can re-establish stability:

```rhombus
def stable = buffered
             |> filter_flow(payload => !squash)
             |> gate_flow(!hazard)
             |> pipe(1)
```

`atomic_fork(n)` publishes an all-or-none, same-cycle replication contract for
[event tracing](../rhodium/event/README.md#traceable-transforms). Selective,
control-only, and independently accepted replication have different contracts.

`broadcast(n)` configures `Broadcast(T, n)` as an indexable array of
`Irrevocable` endpoints, or a disconnected handle when given a payload/type
seed. Unlike `atomic_fork`, it stores one payload and lets recipients accept
independently, exactly once each. It publishes actual acceptance and pending
controls for tracing. Reset flushes outstanding delivery; simultaneous final
delivery and replacement still deliver the old item to the completing recipient.

`demux_flow` publishes its actual output-selection predicates for event tracing;
direct `Demux` and control-only routing need separate adapters.

`demux_flow(n, payload => selector)` is a one-to-many routing stage whose
selector may observe the offered payload and ambient hardware. The ordinary
form returns an array of `n` `Decoupled` endpoints. Use
`demux_flow(n, ~stable: #true, ...)` only when the selector remains stable for
the entire stalled offer; that explicit assertion preserves an `Irrevocable`
input contract. An out-of-range selector blocks the input. This distinguishes
exclusive routing from `atomic_fork(n)`, which requires every output to accept
the same item.

## Allocation and grant-controlled routing

`GreedyMatcher(inputs, outputs)` maps an input-major Boolean request matrix to
a Boolean one-to-one grant matrix. Lower input indices have priority, and each
input takes its lowest still-unclaimed requested output. The result is maximal
but not fair; readiness and the application-specific meaning of rows and
columns remain outside the matcher.

`OutputGreedyRoundRobinMatcher(inputs, outputs)` exposes the same input-major
request and grant matrices plus one `accepts` bit per output. Outputs are
considered in fixed index order, while each output selects the first input still
unmatched by earlier outputs beginning at its independent rotating priority. A
priority advances only when that output's grant is accepted. The result is
one-to-one and maximal, not maximum-cardinality, and rotating input priority does
not make the fixed inter-output ordering fair.

`circular_priority_onehot(requests, start)` is the stateless selection primitive
under the matcher and round-robin arbiters. It returns one shared `valid`,
native `MaybeOneHot` `grant`, and binary `index` result. It builds one masked
priority selection and one wraparound selection instead of replicating a full
arbiter for every possible start. `RRArbiter` and `CtrlRRArbiter` own their
priority registers directly and advance them only after successful transfers.

`and_exclusion_reduce(values)` uses a shared balanced reduction tree to return
the full conjunction plus each conjunction with one corresponding input
omitted. `Join`, `SelectiveJoin`, `CtrlJoin`, `AtomicFork`,
`SelectiveAtomicFork`, and `CtrlAtomicFork` use it instead of independently
rebuilding full and peer reductions for every lane.

`GrantDemux(T, outputs)` routes one input according to an optional-one-hot grant
row, while `GrantMerge(T, inputs)` selects one input according to an
optional-one-hot grant column. A zero grant blocks the corresponding flow;
both scalar grant ports use `MaybeOneHot`, so ordinary Rhodium construction keeps
the zero-or-one invariant explicit. An external environment can still violate
the physical encoding and must satisfy the corresponding boundary contract.

`GrantCrossbar(T, inputs, outputs)` structurally composes one `GrantDemux` per
input with one `GrantMerge` per output around an externally generated
one-to-one grant matrix. It does not perform routing, arbitration, or
buffering. Because grants may change while an output is stalled, all three
primitives expose `Decoupled` outputs; add a queue or pipe when the consumer
requires an irrevocable pending offer. The configured
`grant_crossbar(output_count, ~grants)` stage infers the payload and input
count from its input array and returns an output endpoint array:

```rhombus
(buffered_inputs
 |> grant_crossbar(output_count, ~grants: allocator.grants)
) <=> egress
```

Both grant primitives declare event trace contracts using their live grant bits.
Direct and configured crossbars inherit these contracts through their internal
demux/merge structure. Each accepted output retains only its selected input's
ancestry, including through upstream queues; zero grants transfer nothing.
Changing grants during a stall changes the offered ancestry, not a remembered
winner. Strict tracing requires complete annotated ancestry when mixing traced
inputs. Partial tracing preserves known parents and marks missing contributors
unknown, including through selection.

## Joining and branching topologies

`zip_flow` publishes an atomic all-input consumption contract for event tracing.
The [event compiler](../rhodium/event/README.md#traceable-transforms) owns parent
combination and reconvergence semantics; selective/control-only joins need
separate dynamic adapters.

`selective_join()` consumes a flat source array containing one selection flow
followed by homogeneous data flows. The selection token carries `Mask(n)`,
where `n` is the number of data flows. The selection token and every selected
data token transfer atomically; unselected data inputs remain untouched. The
result is `SelectedValues(T, n)`, which carries the selection alongside the
fixed `Vec(n, T)` so downstream logic knows which lanes are meaningful:

```rhombus
def selected = Array(selection, first, second, third)
  |> selective_join()
```

All participating inputs and the output are ordinary `Decoupled` flows, so a
stalled selection may change before any transfer commits. An empty selection
consumes only its selection token and produces an explicitly empty result.

`zip_flow(left_payload, right_payload => expression)` atomically consumes a
two-element source array and maps the pair to one inferred result type. Neither
input can transfer by itself:

```rhombus
def response_join = Array(Response(), Bool)
  |> zip_flow(response, owner):
    TaggedResponse():
      response: response
      owner: owner

def tagged_response = Array(memory_response, owners) |> response_join
```

Disconnected results are ordinary `InterfaceHandle` values from the frontend
interface layer, not a flow-specific graph. Inline adapters use local
`interface_link` wires and add no module hierarchy. Handles and sinks remain
linear; configured unary functions are reusable and construct a fresh stage on
each application.

`parallel` combines independent handles into one array-shaped handle, or
independent terminated sinks into one array-shaped sink. This lets fan-in,
buffering, fanout, heterogeneous projections, and their destinations form one
path. A call cannot mix handles and sinks:

```rhombus
def request_path:
  Array(Request(), Request())
  |> parallel(tag_fesvr, tag_processor)
  |> rr_arbiter()
  |> pipe(1)
  |> atomic_fork(2)
  |> parallel(
       (TaggedRequest() |> map_flow(tagged => tagged.request))
         |> memory_request,
       (TaggedRequest() |> map_flow(tagged => tagged.processor))
         |> owner_queue.ingress
     )

Array(fesvr_request, processor_request) |> request_path
```

The arrow form is especially useful for routing in the middle of a chain:

```rhombus
buffered
|> demux_flow(2, ~stable: #true, tagged => tagged.processor)
|> parallel(
     (Irrevocable(TaggedResponse()) |> map_flow(~stable: #true, tagged => tagged.response))
       |> fesvr_response,
     (Irrevocable(TaggedResponse()) |> map_flow(~stable: #true, tagged => tagged.response))
       |> processor_response
   )
```

The endpoint, handle, and sink shapes must match recursively. A
`handle |> endpoint` branch closes that handle's output immediately while
leaving its input available to the surrounding `parallel`. `zip_flow` is
deliberately binary; homogeneous multi-input rendezvous remains the role of
`Join(T, n)`.

## Stateful flows and completion tracking

`valid_pipe(stages)` infers its eventual input payload, instantiates in the
ambient `sync_circuit` domain, and delays every asserted cycle by exactly the
configured number of stages. There is no readiness or pending-offer state.
Its typed fixed-latency trace metadata lets the optional event compiler delay
parent references by the same number of cycles without changing the pipe RTL.

`ValidPipeAlwaysCapture(T, stages)` and `valid_pipe_always_capture(stages)`
register payload every cycle, independently of validity. Ordinary `ValidPipe`
retains each stage's payload when its incoming validity is false.
Always-capture preserves valid-token latency, throughput, and reset
behavior, while removing validity-dependent payload enables. Payload registers
are not reset; after the configured latency they also reflect invalid input
samples in always-capture mode. Such samples are not valid transactions.
Always-capture can increase payload switching during bubbles in exchange for
removing validity from the payload write-enable path.
Its configured helper carries the same fixed-latency trace metadata.

`valid_arbiter(n)` similarly infers its payload and, for a connected endpoint
array, its input count. Because `Valid` has no backpressure, callers must accept
that simultaneous unselected events are dropped.
`OfferRegister(T)` accepts `Valid(T)` state updates and exposes the current
state as a `Decoupled(T)` offer. An update replaces the offer even while it is
stalled; otherwise a transfer clears the slot. This makes replacement explicit
without claiming irrevocability.

`queue(depth, ...)` and `pipe(stages)` similarly infer their eventual
ready-valid input. `Pipe` and a non-flowing `Queue` produce an `Irrevocable`
endpoint; `queue(depth, ~flow: #true)` remains `Decoupled` because it may expose
its input offer directly. Use an explicit `Queue` instance when its `count`
output or instance handle is needed.

The corresponding `ctrl_queue(depth)`, `ctrl_pipe(stages)`, and
`ctrl_atomic_fork(n)` configured helpers accept `DecoupledCtrl` or
`IrrevocableCtrl` sources and retain the same dependent static information
without manufacturing a dummy payload. A disconnected control topology starts
with `DecoupledCtrl()` or `IrrevocableCtrl()`. Control-only streams carry
indistinguishable tokens; they are not a detachable control half of a
payload-bearing transaction. `CtrlPipe` and a non-flowing `CtrlQueue` produce
`IrrevocableCtrl`; flow-through queues remain `DecoupledCtrl`.

`Queue(T, depth)` defaults to a registered, non-flow-through FIFO.
`~pipe: #true` permits enqueue when a full queue dequeues in the same cycle;
`~flow: #true` lets an empty queue present its input directly. `count` has type
`Bits(index_width(depth + 1))`. Current queues use asynchronous reads, and
depths greater than one compose two `Counter(depth)` pointer instances. Those
queues assert that occupancy stays within the configured depth. Round-robin
arbiters similarly assert that their rotating priority remains in range.

`ShiftQueue(T, depth, ~pipe: ..., ~flow: ...)` offers the same handshake,
reset, count, and output-protocol contracts as `Queue`, using payload registers
with the oldest stored item always at index zero. Dequeue shifts the remaining
items; `mask: Bits(depth)` exposes registered occupancy with the low `count`
bits set. Prefer it for shallow, timing-sensitive FIFOs: it removes the
read-pointer mux at the cost of additional payload switching. With `pipe`
disabled, input readiness depends only on registered fullness; output readiness
still controls payload shifting. Both modules export the configured
`shift_queue(depth, ~pipe: ..., ~flow: ...)` stage for endpoint chains,
payload/protocol seeds, and reusable disconnected handles. Use an explicit
instance when `count` or `mask` is needed. Positive depth and host Boolean
options are required; reset empties occupancy without resetting payload.
`~flushable: #true` adds an explicit `flush: Bool` input; configured stages
accept `~flush: signal`. Flush empties occupancy on the edge, overriding
simultaneous insertion/removal without resetting payload. It does not suppress
pre-edge interface offers; callers must filter transfers they intend to cancel.
The intrinsic retained-window trace contract follows shifting slots and optional
empty bypass, including flush and full simultaneous replacement.

`CompletionQueue(Request, Response, depth)` couples a ready-valid request path
to a nonbackpressured implementation. Each request handshake reserves one
slot and appears immediately on the `Valid` `issue` endpoint. The implementation
must later produce exactly one `Valid` `completion`; completed responses emerge
in arrival order through the `Irrevocable` `response` endpoint. Assertions
detect unreserved completions, unavailable completion slots, and reservation
counts outside the configured depth.

## Examples

See [`../examples/std/flow-control.rhdl`](../examples/std/flow-control.rhdl) for
pipe, queue, fixed-priority arbitration, and chaining, and
[`../examples/std/flow-topology.rhdl`](../examples/std/flow-topology.rhdl) for
round-robin arbitration, demux, join, atomic fork, payload mapping, and
broadcast. The parallel token-only family is materialized in
[`../examples/std/ctrl-flow.rhdl`](../examples/std/ctrl-flow.rhdl).
