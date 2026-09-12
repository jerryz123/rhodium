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
| Ready-valid protocol declarations | [`ready-valid.rhdl`](ready-valid.rhdl) | `Pulse`, `Valid`, `Decoupled`, `Irrevocable`, and transfer detection |
| Credit-based transport | [`credited.rhdl`](credited.rhdl) | Credited endpoints and monitoring |
| Packet/flit representation | [`flit.rhdl`](flit.rhdl) | Packet payload types independent of transport |
| Endpoint address and ID sets | [`interconnect.rhdl`](interconnect.rhdl) | Host-side interconnect parameters and validation |
| Bit operations and bounded counting | [`bits.rhdl`](bits.rhdl), [`counter.rhdl`](counter.rhdl) | Layout helpers and a synchronous counter |
| Occupancy and fixed-latency storage | [`scoreboard.rhdl`](scoreboard.rhdl), [`sync-ram.rhdl`](sync-ram.rhdl) | Scoreboarding and a shared 1RW RAM wrapper |
| Streaming composition | [Flow library](../../flow/README.md) | Pipes, queues, routing, arbitration, transforms, and boundaries |

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
enums, `OneHot`, extension-defined scalar data, and recursively nested records
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
| `Pulse()` | Every asserted cycle is one payloadless control event; no backpressure |
| `Valid(T)` | Every asserted cycle carries one payload; no backpressure |
| `DecoupledCtrl()` | Offer/accept control; transfer occurs when `ready` and `valid` are asserted |
| `IrrevocableCtrl()` | A decoupled offer cannot be withdrawn before transfer |
| `Decoupled(T)` | Payload-bearing decoupled transfer without a pre-transfer stability guarantee |
| `Irrevocable(T)` | A valid payload remains asserted and stable until transfer |

A `Pulse()` endpoint's `valid` field may be asserted on consecutive cycles;
each cycle is a separate event, with no edge detection or required gap.

`Valid(T)` refines `Pulse()`. `Decoupled` refines `DecoupledCtrl`; `Irrevocable` refines
`IrrevocableCtrl`, which transitively refines `DecoupledCtrl`.
`Irrevocable(T)` also declares support for the weaker `Decoupled(T)` contract.
The temporal difference is currently documentation rather than generated
protocol assertions.

`endpoint.fire()` accepts any endpoint supporting `DecoupledCtrl` and returns
`endpoint.valid and endpoint.ready`. The exported receiver-first `fire`
function remains available for qualified calls and compatibility.

Flow stages consume these nominal contracts; see their
[normalization rules](../../flow/README.md#protocol-normalization).

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

For ready-valid adapters and timing contracts, see the
[flow library](../../flow/README.md#credited-transport-adapters).

### Flit formats

[`flit.rhdl`](flit.rhdl) separates packet representation from transport. A
`VariableFlit(T)` carries explicit `head`, `tail`, and `payload` fields.
`FramedFixedFlit(T)` carries structurally distinct `first`, `last`, and
`payload` fields whose markers must agree with one fixed packet length.
`FixedFlit(T)` carries only `payload`; its packet
boundaries are implicit in the successful-transfer sequence. The packet
length is therefore an elaboration-time argument to conversions and monitors,
not a payload field or a count of clock cycles.

For serialization, reassembly, and checked framing conversions, see the
[flow library](../../flow/README.md#packet-and-framing-adapters).

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

`transfer_in_range(address, transfer_bytes, base_address, window_bytes)` tests
whether a nonempty byte transfer fits wholly within a contiguous, nonwrapping
window. The first three arguments are unsigned `Bits` of the same width;
`window_bytes` is a positive host integer at most the address-space size.
Zero-length transfers and runtime windows extending beyond the address space
return false. Exclusive ends are calculated with an extra bit, so ending
exactly at the address-space limit is valid. Alignment and allowed transfer
sizes remain caller policy. This combinational helper adds no state or assertions.

`StripedAddressLayout(local_bytes, stripe_bytes, bank_count)` describes equal
power-of-two banks interleaved at a power-of-two byte granularity. Stripes must
fit within a bank. Its `total_bytes`, `bank_mask`, and `global_mask` properties
describe the layout; `fits_unsigned_width(width)` checks its relative extent.
`address_set(global_base, bank_index)` requires an aligned global base and an
in-range bank index and returns that bank's sparse address set.
`project(address, global_base, bank_index, ~local_base: 0)` checks membership
and returns a dense host address. `project_offset(relative)` removes bank bits
from a hardware `Bits` offset, retaining its width; it checks the layout fits
that width but does not check runtime membership. Callers own base translation
and address acceptance. A single bank is an identity projection. None of these
operations adds storage, a handshake, or protocol policy.

## Data paths and storage

### Bit-vector utilities

[`bits.rhdl`](bits.rhdl) provides reusable ordering, layout, alignment,
and lane-mask operations over hardware `Bits` values:

```rhombus
import:
  lib("rhodium/std/bits.rhdl") open

def aligned = is_aligned(address, 8)
def transfer_aligned = is_aligned_log2(address, size_bits, ~max_log2: 6)
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
`is_aligned_log2(value, exponent, ~max_log2: bound)` takes a hardware `Bits`
exponent and checks alignment to `2 ** exponent`. The host bound defaults to
the value's bit width and must be a natural number no larger than that width.
Exponent zero is always aligned; exponent equal to the value's width requires
an all-zero value. Encodings above the bound return false, even for zero.
The exponent's width may differ from the value's width. Protocol-specific
encoding validity and maximum transfer sizes remain caller policy.
`alignment_bits(alignment)` exposes the exact host-side base-two width for
protocols and generators that need to size or remove those low bits.

`expand_mask(mask, lane_width)`, also callable as `mask.expand_mask(lane_width)`,
replicates each enable bit into a lane of `lane_width` bits. Input bit `i`
controls output bits `[i * lane_width .. (i + 1) * lane_width)`, so the
least-significant enable controls the least-significant lane. The result is
`Bits(mask_width * lane_width)`. The mask must be a nonempty `Bits` value and
the lane width a positive host integer; lane width one preserves the value.
For example, `bits(0b0101, 4).expand_mask(8)` produces `bits(0x00ff00ff, 32)`.
This combinational operation is independent of any bus protocol or byte size.

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
  lib("flow/main.rhdl") open
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

The wrapper maps each lane to a raw-memory mask granularity of `T.packed_width()`.
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
