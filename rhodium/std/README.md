<!-- Documents Rhodium's optional protocols and reusable circuit-generator library. -->

# Rhodium standard library

`rhodium/std` contains opt-in reusable hardware vocabulary written against the
public `#lang rhodium` language. It is not another language profile and adds no
core IR, elaboration, or backend behavior. Contributors changing or extending
the library should read [`DEVELOPING.md`](DEVELOPING.md), including its link to
the enforced package dependency contract.

## Choose a component

Import the narrow owning module when one component family is sufficient. Use
the facade modules only when a circuit composes several members of that family.

| Need | Start with | What it owns |
|---|---|---|
| Delays and balanced combinational trees | [`shift-register.rhdl`](shift-register.rhdl), [`reduction.rhdl`](reduction.rhdl) | Generic reusable generators |
| Safe stable-level clock crossing | [`cdc.rhdl`](cdc.rhdl) | `SyncLevel` and its CDC evidence |
| Typed sparse decode | [`decode.rhdl`](decode.rhdl) | Patterns, pattern sets, decode tables, and generators |
| Ready-valid protocol declarations | [`ready-valid.rhdl`](ready-valid.rhdl) | `Valid`, `Decoupled`, `Irrevocable`, and transfer detection |
| Credit-based transport | [`credited.rhdl`](credited.rhdl), [`flow/credit.rhdl`](flow/credit.rhdl) | Credited endpoints, monitoring, and ready-valid adapters |
| Packet/flit representation | [`flit.rhdl`](flit.rhdl), [`flow/flit.rhdl`](flow/flit.rhdl) | Flit types, packet serialization, reassembly, and checked framing conversions |
| Endpoint address and ID sets | [`interconnect.rhdl`](interconnect.rhdl) | Host-side interconnect parameters and validation |
| Bit operations and bounded counting | [`bits.rhdl`](bits.rhdl), [`counter.rhdl`](counter.rhdl) | Layout helpers and a synchronous counter |
| Occupancy and fixed-latency storage | [`scoreboard.rhdl`](scoreboard.rhdl), [`sync-ram.rhdl`](sync-ram.rhdl) | Scoreboarding and a shared 1RW RAM wrapper |
| Streaming composition | [`flow.rhdl`](flow.rhdl) | Pipes, queues, routing, arbitration, transforms, and boundaries |

The rest of this guide states the behavior that callers may rely on. Executable
examples live under [`../../examples/std/`](../../examples/std/), while source
files remain authoritative for complete exported-name lists.

## Foundational utilities

### Shift registers

[`shift-register.rhdl`](shift-register.rhdl) exports the `shift_register`
definition form, a generic ambient-clock delay line over any Rhodium `DataType`.
The declared name binds every stored stage as `Vec(stages, T)`, with the newest
value at index zero. Callers can therefore select only the final delayed value
or reuse all taps for filters and windows. Explicit naming makes multiple shift
registers in one circuit collision-free and preserves useful RTL names.

The register is resetless when `~init` is omitted. An initializer may be one
literal of `T`, replicated across every stage, or a `Vec(stages, T)` literal
that initializes stages independently. Optional `~enable` must be hardware
`Bool`; when omitted, the generated register advances directly without enable
muxing. The helper uses the ambient synchronous domain and therefore belongs
inside a `sync_circuit` or another ambient-clock scope:

```rhombus
import:
  lib("rhodium/std/shift-register.rhdl").shift_register

shift_register taps(sample, 4, ~init: bits(0, 8), ~enable: advance)
filtered <== taps[3]
```

### Reduction trees

[`reduction.rhdl`](reduction.rhdl) exports `tree_reduce(values, combine)`, an
elaboration-time balanced reduction over a nonempty host `List`. Each level
combines adjacent pairs in source order and carries an unmatched final value
to the next level. It therefore emits exactly `values.length() - 1` calls to
the supplied binary function with logarithmic tree depth:

```rhombus
import:
  lib("rhodium/std/reduction.rhdl").tree_reduce

def sum = tree_reduce(products, fun (left, right): left + right)
```

The combiner can be any binary host function that returns the next tree node,
including a named function over bundles. Since tree grouping is observable,
use an associative combiner when the result must agree with a sequential fold.
An empty list is rejected; callers that define an identity should supply it as
an explicit leaf.

### Clock-domain crossings

[`cdc.rhdl`](cdc.rhdl) exports the first standard crossing circuit,
[`cdc/level.rhdl`](cdc/level.rhdl)'s `SyncLevel`. It is a resetless two-stage
`sync_circuit` for a stable `Bits(1)` level. The sync-circuit contract wires and
certifies one ambient destination clock throughout the implementation, while
durable `cdc.sync_level` evidence lets core independently verify the direct
register chain for every producer of IR. Using `SyncLevel` is the semantic
promise that the source persists long enough to be observed; it is not an
event or pulse synchronizer.

## Typed decode

Decode descriptions are immutable host data. Only applying a decode generator
inside a circuit emits hardware.

### Typed decode patterns

[`decode/pattern.rhdl`](decode/pattern.rhdl) defines `Pattern`, an immutable
host-side bit cube over two `HardwareLiteral` values:

```rhombus
import:
  lib("rhodium/std/decode/pattern.rhdl") open

bundle Instruction():
  opcode: Bits(4)
  operands: Vec(2, Bits(4))

def instruction_pattern = pattern(Instruction()):
  opcode: bits(0b1010, 4)
  operands: pattern(Vec(2, Bits(4)), [bits(2, 4), _])
```

Within `pattern(T)`, a `HardwareLiteral` constrains the whole field, `_` leaves
the whole field unconstrained, and a nested `Pattern` preserves partial care.
Record fields are named, vector elements are positional, and both forms recurse
through nested aggregates. The underscore is host pattern syntax: it neither
creates hardware nor denotes a runtime X value.

`partial_pattern(T)` is the record form for sparse control descriptions. Every
listed field follows the same literal/nested-pattern rules, while omitted
fields are unconstrained:

```rhombus
def add_control = partial_pattern(Control()):
  result_select: ResultSelect.Arithmetic
  subtract: Bool(#false)
```

Unknown and duplicate fields remain errors.

`as_pattern(value)` normalizes values accepted by decode consumers: an
existing `Pattern` is retained, while a `HardwareLiteral` becomes a fully
cared exact pattern. Domain adapters can therefore accept both forms without
reimplementing packed care-mask construction.

The lower-level `Pattern(~value: ..., ~care: ...)` constructor remains useful
for partially cared scalar fields and extension libraries. A care bit of one
makes the corresponding value bit significant; zero makes it a don't-care.
`value` and `care` must have exactly equal hardware types, not merely equal
packed widths. Any `HardwareLiteral` type works, including `Bits`, `Bool`,
enums, `OneHot`, extension-defined flat data, and recursively nested records
and vectors. The generic `literal(T, packed_value)` form remains a low-level
escape hatch for arbitrary typed packed images; canonical aggregate examples
use named fields instead.

`Pattern` stores the common `type` and `width`, the packed `care_bits`, and the
canonical `value_bits = value & care`. It is ordinary host data rather than a
`HardwareLiteral`: declaring, comparing, or passing a pattern as a generator
parameter emits no hardware and it cannot be connected to a port.

The host-only relations support decode-table validation:

- `pattern.matches(literal)` tests membership.
- `pattern.overlaps(other)` reports whether two cubes share any value.
- `pattern.subsumes(other)` reports whether every value matched by `other` is
  also matched by `pattern`.

`PatternSet` is a typed, host-only union of pairwise-disjoint `Pattern` cubes.
`pattern_set(...)` constructs a nonempty set, while `PatternSet(T, [])`
constructs a typed empty set for set-algebra results. Union, intersection,
subtraction, inverse, overlap, subsumption, and literal membership preserve the
hardware type and produce deterministic disjoint covers. This layer does not
minimize cubes. The backend preserves the sparse relation for downstream RTL
synthesis instead of choosing a Boolean cover itself.

Pattern don't-cares describe static matching or optimization freedom. They are
never runtime unknown or X values. `Pattern` remains neutral host data between
input matching and partially specified decode outputs.

[`decode/pattern-value.rhdl`](decode/pattern-value.rhdl) is the separate output
consumer. `pattern_value(pattern)` materializes one hardware member of the
cube: cared bits retain `value_bits`, while uncared bits receive synthesis
freedom from `dont_care`. Keeping this policy out of `Pattern` preserves its
host-only architecture and allows future matching or optimization consumers
to choose different interpretations.

### Typed decode generation

[`decode.rhdl`](decode.rhdl) is the public facade for `Pattern`, `PatternSet`,
`DecodeCase`, `DecodeTable`, and `DecodeGen`. A table requires at least one
case, one explicit default output pattern, exact common input types, and exact
common output types. Input cubes may not overlap: the relation has no hidden
row priority.

```rhombus
import:
  lib("rhodium/std/decode.rhdl") open

def cases = decode_groups(Control()):
  [add_input]:
    operation: Operation.Add
    write: Bool(#true)

  [sub_input]:
    operation: Operation.Subtract
    write: Bool(#true)

def decoder = DecodeGen(cases, ~default: default_control)

circuit ControlPath():
  input instruction: Instruction()
  output control: Control()

  control <== decoder(instruction)
```

`DecodeGen` is an ordinary callable host value and can be passed as a circuit
parameter. Construction validates and retains the typed relation; calling the
generator always emits one typed `rtl.decode` operation. The standard library
does not choose a physical implementation or invoke external tools. Raw
`hw_decode` emits the same core operation directly.

The CIRCT backend lowers `rtl.decode` to a sparse `sv.case casez`. Input-care
masks become wildcard positions, while uncared output bits remain explicit X
synthesis freedom. CIRCT emits the SystemVerilog and downstream RTL synthesis
chooses the physical logic implementation; Rhodium invokes no logic-minimizer
subprocess. `ValidDecodeGen` therefore keeps validity and payload in one
semantic relation while preserving their independently specified output bits.

Input and output patterns may use different scalar, aggregate, or
extension-defined hardware types. The core operation preserves output
don't-cares through backend lowering and SystemVerilog emission. See
[`../../examples/std/decode.rhdl`](../../examples/std/decode.rhdl)
for an aggregate input/output example.

`decode_groups(T)` constructs ordinary `DecodeCase` values while allowing one
sparse record output pattern to serve several inputs. Its optional `~input`
host function adapts domain descriptions into `Pattern` or `PatternSet` values
without making the decode library depend on that domain. A bracketed row
enumerates inputs directly. A `group inputs:` row accepts one pattern set or a
host `List` mixing literals, patterns, and pattern sets; every set expands into
its disjoint cube terms. Empty inputs contribute no rows. `decode_cases`
provides the same expansion without sparse-record syntax. `ValidDecodeGen`
treats the resulting cases as a partial mapping and returns `DecodeResult(T)`,
asserting `valid` for matches and leaving the unmatched value as synthesis
freedom.

Decode relations compose as ordinary case lists before constructing a
`DecodeGen`. Row extension is list concatenation; the final `DecodeTable`
rejects overlaps and inconsistent types. `overlay_decode_cases(fallbacks,
overrides)` assigns an input subregion to explicit extension rows by subtracting
their union from every fallback row. It returns a disjoint unordered relation,
so an extension can refine a broad fallback encoding without introducing row
priority. `lift_decode_inputs(cases, lift)` explicitly embeds every input cube
into a wider type while retaining its output cube. The lift function returns
the replacement input `Pattern`, so added input fields can be cared or
unconstrained without an inferred packing policy.

`zip_decode_cases(left, right, combine)` forms an output product. Both inputs
must contain exactly the same input cubes, although their row order may differ.
The combiner receives the two output patterns and returns one aggregate output
pattern; nested patterns preserve each side's care bits. Missing, extra, or
duplicate counterparts are errors. This deliberately avoids implicit priority,
sparse joins, or input-partition refinement:

```rhombus
def extended_inputs = lift_decode_inputs(
  base_cases,
  fun (opcode):
    partial_pattern(ExtendedInstruction()):
      opcode: opcode
)

def combined_outputs = zip_decode_cases(
  base_cases,
  custom_cases,
  fun (base, custom):
    pattern(CombinedControl()):
      base: base
      custom: custom
)
```

[`../../examples/std/decode-composition.rhdl`](../../examples/std/decode-composition.rhdl)
shows reusable PatternSet input families alongside all three independent table
extensions: concatenated rows, zipped output fields, and lifted input fields.

## Protocols and transport

These modules separate transaction semantics and packet representation from
the flow-control circuits that implement buffering and routing.

### Ready-valid protocols

Import the protocol family directly:

```rhombus
import:
  lib("rhodium/std/ready-valid.rhdl") open
```

| Interface | Contract |
|---|---|
| `Valid(T)` | Every asserted cycle carries one payload; no backpressure |
| `DecoupledCtrl()` | Offer/accept control; transfer occurs when `ready` and `valid` are asserted |
| `IrrevocableCtrl()` | A decoupled offer cannot be withdrawn before transfer |
| `Decoupled(T)` | Payload-bearing decoupled transfer without a pre-transfer stability guarantee |
| `Irrevocable(T)` | A valid payload remains asserted and stable until transfer |

`Decoupled` refines `DecoupledCtrl`; `Irrevocable` refines
`IrrevocableCtrl`, which transitively refines `DecoupledCtrl`.
`Irrevocable(T)` also declares support for the weaker `Decoupled(T)` contract.
The temporal difference is currently documentation rather than generated
protocol assertions.

`endpoint.fire()` accepts any endpoint supporting `DecoupledCtrl` and returns
`endpoint.valid and endpoint.ready`. The exported receiver-first `fire`
function remains available for qualified calls and compatibility.

Ready-valid flow helpers classify protocols through the same nominal
refinement and `supports` relation used by interface connections. A differently
named refinement is accepted and normalized to its canonical `Decoupled(T)`,
`Irrevocable(T)`, or `Valid(T)` contract; an unrelated declaration is rejected
even if it reuses one of those display names. Control-only helpers additionally
require the exact payloadless control-plane member shape, so a payload-bearing
protocol is not silently treated as a `Ctrl` flow.

### Credited transport

[`credited.rhdl`](credited.rhdl) defines `Credited(T, credit_limit)` for a
bounded single-hop payload channel. The transmitter drives `valid` and `bits`;
the receiver returns one credit with each asserted `credit` pulse. Every valid
cycle transfers one payload and consumes a credit received on an earlier edge.
There is no ready signal, and a same-edge credit cannot authorize a transfer
from a zero pre-edge balance.

The positive host `credit_limit` is a semantic interface parameter rather than
a wire field, and connections require the same limit. Calling
`monitor_credited(endpoint)` explicitly instruments one endpoint: every
transfer must own a prior credit, grants must remain within the configured
limit, and the tracked balance must stay in range. Protocol monitors that own
several channel balances can call
`check_credited(valid, credit, credit_limit, balance)` to reuse the same checks
and update rule without allocating multiple identically named registers.

[`flow/credit.rhdl`](flow/credit.rhdl) provides the transport adapters.
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

### Flit formats

[`flit.rhdl`](flit.rhdl) separates packet representation from transport. A
`VariableFlit(T)` carries explicit `head`, `tail`, and `payload` fields.
`FramedFixedFlit(T)` carries structurally distinct `first`, `last`, and
`payload` fields whose markers must agree with one fixed packet length.
`FixedFlit(T)` carries only `payload`; its packet
boundaries are implicit in the successful-transfer sequence. The packet
length is therefore an elaboration-time argument to conversions and monitors,
not a payload field or a count of clock cycles.

[`flow/flit.rhdl`](flow/flit.rhdl) supplies the safe ready-valid conversion
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

### Generic interconnect parameters

[`interconnect.rhdl`](interconnect.rhdl) owns protocol-neutral sets used to
describe interconnect endpoints. `IdRange(start, end)` is a nonempty
half-open range of nonnegative IDs. `AddressSet(base, mask)` describes every
nonnegative address formed by varying the one bits of `mask`; canonical bases
keep those bits clear. `TransferSizes(min_bytes, max_bytes)` is an inclusive
power-of-two byte-size range. The set objects are immutable elaboration-time
values and do not themselves create hardware. `IdRange.fits_unsigned_width(width)` and
`AddressSet.fits_unsigned_width(width)` report whether every represented value
fits a nonnegative unsigned host width. Both set types provide `overlaps` for
host-time topology validation. `AddressSet.matches(address)` turns one set
into a hardware predicate, while `address_sets_match` OR-reduces any list of
sets and returns false for an empty list. `allocate_id_ranges` assigns exact contiguous
global ranges to a nonempty list of local ranges and returns reversible
`IdRangeMap` records without requiring the local ranges to begin at zero.

## Data paths and storage

### Bit-vector utilities

[`bits.rhdl`](bits.rhdl) provides reusable ordering, layout, and alignment
operations over hardware `Bits` values:

```rhombus
import:
  lib("rhodium/std/bits.rhdl") open

def aligned = is_aligned(address, 8)
def base = align_down(address, 8)
def reversed = reverse_bits(address)
def leading_zeros = count_leading_zeros(address)

fun bank_index_width(bank_count :: Pow2Int):
  alignment_bits(bank_count)
```

`Pow2Int` composes Rhombus's built-in `PosInt` annotation with the
power-of-two predicate. Annotated generator parameters remain ordinary
integers, so host arithmetic and specialization use the original value without
wrapping.
`power_of_two(value)` is the corresponding predicate for conditional queries;
it returns false for non-integers, zero, negative integers, and other
non-power-of-two values.
`unsigned_value_count(width)` returns the number of distinct values represented
by a nonnegative unsigned width, including one value for width zero.
`reverse_bits(value)` reverses the positions of a nonempty `Bits` value while
preserving its width. `count_leading_zeros(value)` returns
`Bits(index_width(width + 1))`; its result ranges from zero through the operand
width, with an all-zero operand returning the operand width.
The alignment is a positive power-of-two host parameter and must fit the
value's width. `is_aligned` checks that the corresponding low bits are zero;
`align_down` clears them while preserving the input width. Alignment to one is
the identity for `align_down` and always true for `is_aligned`.
`alignment_bits(alignment)` exposes the exact host-side base-two width for
protocols and generators that need to size or remove those low bits.

The same module provides `masked_merge(original, replacement, mask)`, which
selects replacement bits where the mask is set and retains original bits
elsewhere. All three operands must be compatible `Bits` values; the result
retains the original width.

### Scoreboard

[`scoreboard.rhdl`](scoreboard.rhdl) provides a reusable occupancy bitmap with
one nonbackpressured `Valid` set operation, one nonbackpressured `Valid` clear
operation, and a combinational `busy` bitmap:

```rhombus
import:
  lib("rhodium/std/flow.rhdl") open
  lib("rhodium/std/scoreboard.rhdl") open

inst hazards(Scoreboard(32))
def reserve_filter = filter_valid(operation => operation.reserve)
def reserve_index = map_valid(operation => operation.destination)
def completion_index = map_valid(completion => completion.tag)
reservations |> reserve_filter |> reserve_index |> hazards.set
completions |> completion_index |> hazards.clear
def permitted = requests |> gate_flow(!scoreboard_busy(32, hazards.busy, source))
```

The entry count may be any positive integer. `busy` exposes only the registered
bitmap; consumers that need same-cycle update visibility implement that bypass
as part of their own timing policy. Clear wins when both updates target one
entry. Out-of-range indices are assertion failures for updates and read as not
busy through `scoreboard_busy`. Reset empties the scoreboard. Assertions reject
setting an occupied entry and clearing a free entry, except for a simultaneous
opposite operation on that entry. The component has no reserved-entry policy:
callers that treat an index such as register zero as permanently free must
filter that policy at their own boundary.

### Fixed-latency synchronous RAM

[`read-write.rhdl`](read-write.rhdl) defines
`ReadWritePort(address_width, T, n)`. `T` is one data-lane type and `n` is the
number of lanes. Its single `request` flow carries a `Bits(address_width)`
address, a read/write selector, `Vec(n, T)` data, and a `Mask(n)` lane set. The
`response` flow returns `Vec(n, T)`. Request data and mask are meaningful only
for writes; response data is meaningful only for reads.

[`sync-ram.rhdl`](sync-ram.rhdl) applies that interface to a word-indexed shared
1RW physical memory. `SyncRam1RW(depth, T, n)` stores `n` lanes of `T` per
word, and each mask bit controls the corresponding lane:

```rhombus
import:
  lib("rhodium/std/read-write.rhdl") open
  lib("rhodium/std/sync-ram.rhdl") open

inst tags(SyncRam1RW(64, Bits(20), 1))
tags.port.request.valid <== lookup ||| update
tags.port.request.bits <== ReadWriteRequest(6, Bits(20), 1):
  address: index
  write: update
  data: vec(new_tag)
  mask: Mask(1)(1)
```

Every asserted request is accepted; there is no readiness or retry state. A
read produces `response.valid` exactly one cycle later. Writes produce no
response, and response bits are meaningful only while valid is asserted.
Addresses are word indices. A one-lane RAM uses `n = 1`; asserting its sole
mask bit writes the complete `T` value.

The wrapper maps each lane to a raw-memory mask granularity of `T.bit_width()`.
It neither initializes storage nor changes the raw primitive's behavior for
dynamically out-of-range addresses. A separate 1R1W wrapper remains deferred
until its read-during-write collision policy is explicit.

Use raw `sync_mem` when physical port shape or timing must remain explicit.
Use this wrapper for fixed-latency internal arrays whose callers naturally
produce `Valid` accesses. Domain protocols such as CHI own externally visible
request, response, ordering, and error semantics.

### Counter

The bounded counter is independent of ready-valid flow control:

```rhombus
import:
  lib("rhodium/std/counter.rhdl") open

inst timer(Counter(10))
timer.enable <== tick
expired <== timer.wrap
```

`Counter(n)` synchronously resets to zero and, while enabled, counts through
the `n` states from zero to `n - 1`. `value` has type
`Bits(index_width(n))`. `wrap` is asserted during an enabled cycle at
`n - 1`, immediately before the next edge returns the value to zero. An
internal assertion checks that the state remains within that range.

## Flow-control circuits

The flow library has two authoring levels. Instantiate a named generator when
you need its additional ports or instance identity. Use a lowercase configured
stage when the component should participate in a `|>` topology and its payload
or protocol can be inferred.

### Component catalog

[`flow.rhdl`](flow.rhdl) re-exports the independently importable generators
under [`flow/`](flow/):

| Valid-only generator | Behavior |
|---|---|
| `ValidPipe(T, stages)` | Fixed-latency registered valid/payload pipeline with no backpressure |
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
  lib("rhodium/std/flow.rhdl") open

inst buffered(Queue(Bits(8), 4, ~pipe: #true, ~flow: #false))
```

### Building pipelines

Every lowercase flow-stage helper is configured first and receives its input only
through Rhombus `|>`. This makes every stage an ordinary unary host function:

```rhombus
ingress |> queue(4, ~pipe: #true) |> pipe(2) |> egress
```

`trace_event(label)` inserts a transparent compiler-visible checkpoint on a
`Decoupled` or `Irrevocable` payload flow. `trace_valid_event(label)` provides
the same annotation for `Valid`. These helpers do not add state or runtime
effects; `rhodium/event` consumes their metadata to infer possible nearest
dependencies. Labels are nonempty and unique within a module definition.

```rhombus
ingress
  |> trace_event("accepted")
  |> queue(4)
  |> trace_event("issued", ~terminal: #true)
  |> egress
```

The [event package](../event/README.md) can also rebuild a separate design with
synthesizable lineage and DPI emission for linear combinational paths and
fixed-latency `valid_pipe` and elastic `pipe` paths. Queues and branching transforms
remain static-analysis-only boundaries.
`pipe` publishes its actual per-stage load and input-valid controls as typed
metadata; the instrumenter observes them to keep shadow references aligned
under stalls without changing functional ready/valid/payload behavior.

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

### Static typing and reusable topologies

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

### Virtual channels and fanout

`VcMux(T, n)` and `VcDemux(T, n)` let `n` independently backpressured logical
flows share one physical ready-valid flow. The mux fairly selects only lanes
whose corresponding `vc_ready` bit is asserted and emits `VcBeat(T, n)` with
the selected lane index. The demux validates that index, delivers the payload
to exactly one output, and exposes every output's readiness for the upstream
mux. `VcLink(T, n)` groups that multiplexed beat flow with the reverse
per-channel readiness vector at a typed physical boundary. Neither component
allocates per-VC buffering or gives the lanes routing, reservation, credit, or
deadlock semantics; domain libraries and callers own those policies.

### Replicated state

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

### Mapping and protocol conversion

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

### Circuit boundaries

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

### Filtering, gating, and routing

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

`demux_flow(n, payload => selector)` is a one-to-many routing stage whose
selector may observe the offered payload and ambient hardware. The ordinary
form returns an array of `n` `Decoupled` endpoints. Use
`demux_flow(n, ~stable: #true, ...)` only when the selector remains stable for
the entire stalled offer; that explicit assertion preserves an `Irrevocable`
input contract. An out-of-range selector blocks the input. This distinguishes
exclusive routing from `atomic_fork(n)`, which requires every output to accept
the same item.

### Allocation and grant-controlled routing

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

### Joining and branching topologies

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

### Stateful flows and completion tracking

`valid_pipe(stages)` infers its eventual input payload, instantiates in the
ambient `sync_circuit` domain, and delays every asserted cycle by exactly the
configured number of stages. There is no readiness or pending-offer state.
Its typed fixed-latency trace metadata lets the optional event compiler delay
parent references by the same number of cycles without changing the pipe RTL.
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

`CompletionQueue(Request, Response, depth)` couples a ready-valid request path
to a nonbackpressured implementation. Each request handshake reserves one
slot and appears immediately on the `Valid` `issue` endpoint. The implementation
must later produce exactly one `Valid` `completion`; completed responses emerge
in arrival order through the `Irrevocable` `response` endpoint. Assertions
detect unreserved completions, unavailable completion slots, and reservation
counts outside the configured depth.

### Examples

See [`../../examples/std/flow-control.rhdl`](../../examples/std/flow-control.rhdl) for
pipe, queue, fixed-priority arbitration, and chaining, and
[`../../examples/std/flow-topology.rhdl`](../../examples/std/flow-topology.rhdl) for
round-robin arbitration, demux, join, atomic fork, payload mapping, and
broadcast. The parallel token-only family is materialized in
[`../../examples/std/ctrl-flow.rhdl`](../../examples/std/ctrl-flow.rhdl).
