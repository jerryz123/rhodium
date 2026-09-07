<!-- Documents the independently selectable Rhodium frontend layers and their authoring semantics. -->

# Rhodium frontend layers

Frontend layers add author-facing notation, types, static information, and
policy over Rhodium's shared core semantics. This document owns the contracts
of those independently selectable layers. It is a reference rather than a
start-to-finish tutorial. Contributors adding or changing a layer should read
[`DEVELOPING.md`](DEVELOPING.md).

## Using this reference

Start with the layer map below, then follow the contract links for the feature
you are using. The surrounding documentation has narrower ownership:

| Need | Owning document |
|---|---|
| Language profiles, circuit generators, elaboration, and host parameters | [Frontend guide](../README.md) |
| Layer implementation, extension machinery, and validation | [Layer contributor guide](DEVELOPING.md) |
| Package boundaries and authoritative direct-dependency inventories | [Rhodium contributor guide](../../DEVELOPING.md) |
| Ready-valid protocols and reusable hardware components | [Standard library](../../std/README.md) |
| Executable feature programs | [Examples](../../../examples/README.md) |

`#lang rhodium` imports the curated layer set, including clocking;
`#lang rhodium/base` programs can import only the layers they need.

### Layer map

| Area | Layer | Contract |
|---|---|---|
| Data and expressions | [`comb.rhm`](comb.rhm) | [Literals, combinational operators, muxes, and typed don't-cares](#literals-and-combinational-expressions) |
| Data and expressions | [`signed.rhm`](signed.rhm) | [Explicit-width signed integers](#signed-integers) |
| Data and expressions | [`expanding-arithmetic.rhm`](expanding-arithmetic.rhm) | [Lossless `+&` and `*&`](#literals-and-combinational-expressions) |
| Data and expressions | [`bool.rhm`](bool.rhm) | [Booleans, masks, reductions, ordering, and optional one-hot values](#boolean-reductions-and-ordering) |
| Data and expressions | [`enum.rhm`](enum.rhm) | [Nominal hardware enums](#hardware-enums) |
| Data and expressions | [`tagged-union.rhm`](tagged-union.rhm) | [Nominal tagged unions](#tagged-unions) |
| Data and expressions | [`one-hot.rhm`](one-hot.rhm) | [Exact one-hot selection](#one-hot-values) |
| Aggregates | [`bundle.rhm`](bundle.rhm) | [Named records and fields](#bundles-and-records) |
| Aggregates | [`vector.rhm`](vector.rhm) | [Fixed-length vectors](#fixed-length-vectors) |
| State and effects | [`wire.rhm`](wire.rhm) | [Internal single-driver wires](#wires) |
| State and effects | [`sequential.rhm`](sequential.rhm) | [Explicit and ambient registers](#registers-and-synchronous-circuits) |
| State and effects | [`memory.rhm`](memory.rhm) | [Asynchronous-read memories](#asynchronous-read-memories) |
| State and effects | [`sync-memory.rhm`](sync-memory.rhm) | [Fixed-port synchronous memories](#synchronous-memories) |
| State and effects | [`assertion.rhm`](assertion.rhm) | [Clocked assertions](#clocked-assertions) |
| State and effects | [`dpi.rhm`](dpi.rhm) | [Clocked DPI](#clocked-dpi) |
| Control and hierarchy | [`conditional.rhm`](conditional.rhm) | [`when`, `switch`, and guarded effects](#conditional-assignment-and-effects) |
| Control and hierarchy | [`hierarchy.rhm`](hierarchy.rhm) | [Instances and child access](#hierarchy) |
| Clock domains | [`sync.rhm`](sync.rhm) | [Ambient clocks and reset derivation](#registers-and-synchronous-circuits) |
| Clock domains | [`clocking.rhm`](clocking.rhm) | [Timing contracts and CDC enforcement](#clocking-environments-and-cdc-enforcement) |
| Interfaces | [`interface.rhm`](interface.rhm) | [Directional protocols and topology](#interfaces-and-topology) |

## Shared extension surface

### Hardware types

Extension libraries can define a scalar descriptor and its hardware annotation
with one declaration:

```rhombus
hardware_type Token(width :: PosInt):
  implements FlatDataType
  width: width
  describe: "token<" ++ to_string(width) ++ ">"

fun pass(value :: Token(8)) :~ Token(8):
  value

fun pass_any_width(value :: Token) :~ Token(value.type.width):
  value

circuit TokenPass():
  input source: Token(8)
  output result: Token(8)
  result <== source.pass_any_width()
```

The generated descriptor is nominal by declaration. Its parameters form the
default equality key; `type_key:` can instead provide a canonical host value
when parameter objects need semantic rather than identity equality. The bare
type name annotates any member of the nominal family, while `value.type`
projects the concrete descriptor and its original host parameters.
`Hardware.of(type_expression)` remains available when the descriptor itself is
dynamic or generic, but a named extension type should normally expose its own
annotation through `hardware_type`.

### Hardware methods

An ordinary visible function can be called as an import-scoped hardware
extension method. When a hardware dot call is not a built-in or a type-owned
method, `receiver.operation(arguments...)` resolves directly to
`operation(receiver, arguments...)`. Bare dot names remain hardware field
projections. Because the expansion is a direct function call, receiver checks
and dependent result annotations come from the function declaration, and an
extension exists only where its function binding is in lexical scope.
Receiver-owned methods take precedence, followed by built-ins and compatible
receiver-first functions; specialized field providers fall through to that
same common resolution path. The static-information and dispatch machinery is
described in the [layer contributor guide](DEVELOPING.md#static-information-and-hardware-surfaces).

## Clock domains and CDC analysis

### Clocking environments and CDC enforcement

Import `clocking.rhm` explicitly and use `elaborate_with_clocking` when a
completed design should be analyzed in a top-level temporal environment:

```rhombus
import:
  lib("rhodium/frontend/layers/clocking.rhm") open

circuit Top():
  input clock: Clock
  input data: Bits(8)
  synchronous_input(data, clock)

def clocked = elaborate_with_clocking(Top())
def design = clocked.design
def report = clocked.summary
```

`synchronous_input`, `asynchronous_input`, and `unknown_input_timing` attach a
contract to a top data input or aggregate subtree. `identical_clocks`,
`derived_clock`, `asynchronous_clocks`, and `exclusive_clocks` describe pairs
of top `Clock` inputs. These declarations are legal only while the wrapper is
elaborating its explicit top circuit; child-owned and hardware-conditional
declarations are rejected. The result retains the `DesignElaboration`, the
validated `TemporalEnvironment`, and the resolved `DesignTemporalSummary`.

`elaborate_with_clocking` remains report-only. `elaborate_with_cdc` applies the
first conservative policy: static, exact-clock, and declared-identical inputs
are safe; other sampled data requires recognized crossing evidence. Reset
inputs remain inventory-only pending RDC semantics. Both forms expose the
complete structured list through `summary.cdc_violations`; the strict form
raises one aggregate error after the full hierarchy has been classified.

The retained summary also diagnoses distinct verified crossing identities
that later reach one clocked sink through `summary.reconvergences`. These
findings preserve source and hierarchy lineage but do not make otherwise legal
crossings fail strict CDC verification.

The low-level `sync_level_crossing(source, stages)` hook records a stable-level
promise around an ordinary resetless register chain using the certified
ambient `sync_circuit` clock. An explicit destination clock may be supplied as
`sync_level_crossing(source, destination_clock, stages)` outside that ambient
context. Core verification requires `Bits(1)`, at least two direct
destination-clock stages, and no functional fanout from intermediate stages.
Most authors should instantiate
[`../../std/cdc/level.rhdl`](../../std/cdc/level.rhdl) rather than calling the
evidence hook directly.

## Data types and expressions

### Literals and combinational expressions

Integer hardware literals always state their width:

```rhombus
def zero = bits(0, width)
def one = bits(1, width)
```

These are immutable host-side `HardwareLiteral` shadows. They allocate IR only
when consumed by hardware. The generic `literal(T, packed_value)` form covers
every bit image of a packable `DataType`, including frontend-defined types,
records, and vectors. Materialization creates a canonical `Bits` constant and,
for non-`Bits` data, an explicit equal-width cast. `Clock`, `Reset`, and other
non-data types are not literal domains.

`dont_care(T)` is the corresponding typed synthesis-freedom source for any
packable `DataType`. It materializes a `Bits(T.packed_width())` core don't-care
and casts it to `T` when needed. It is not a literal, cannot be inspected as a
host packed value, and gives Rhodium no runtime X-propagation semantics. A backend
may expose an X carrier to downstream tools, so use it only where every
synthesized bit choice is legal and do not rely on backend simulation of it.

`hw_decode(selector, input_type, output_type, cases, default_value,
default_care)` is the low-level authoring bridge to the core decode relation.
Its cases use packed integer value/care images. Ordinary designs should prefer
the typed `DecodeGen` standard-library wrapper, which constructs those images
from `Pattern` values.

The standard combinational surface includes modular `+`, `-`, `*`, shifts,
and the hardware-only bitwise operators `&`, `|||`, and `^`. Prefix `-`
performs fixed-width two's-complement arithmetic negation on `Bits` and `SInt`
and ordinary numeric negation on host values. Prefix `!` is a width-preserving
hardware inversion for both `Bool` and `Bits`; on host values it retains
Rhombus negation. The three-character OR spelling avoids conflicting with
Rhombus's use of bare `|` for block alternatives. Host bit manipulation remains
available explicitly through `rhombus.bits`. `Bits` arithmetic is unsigned.
Rhombus `&&` and `||` remain host short-circuit operators; hardware conjunction
and disjunction use `&` and `|||`.
`SInt` arithmetic uses a signed interpretation where the operation depends on
it. Both remain fixed-width. Arithmetic and shift operators keep their ordinary
Rhombus meaning on host values; the symbolic bitwise family requires hardware
operands. A hardware value may be shifted by either an unsigned hardware `Bits`
amount or a nonnegative host `Int`; a host amount becomes a minimally wide
constant during elaboration. Negative host amounts are rejected.
Bitwise and shift expressions retain the left operand's authoring surface, so
type-owned indexing and methods remain available on their results. `mux` and
`mux_lookup` likewise retain the selected value surface.

Expanding arithmetic is explicit:

```rhombus
sum <== a +& b
product <== a *& b
```

For unsigned `Bits`, `+&` widens both operands to
`max(width(a), width(b)) + 1`; `*&` widens them to the sum of their widths.
Signed arithmetic values also support `*&`: both operands are sign-extended to
the summed width, and the result preserves their compatible signed type.
Mixed signed/unsigned multiplication is rejected. Each form then uses the
existing modular core operation. Expanding subtraction and signed expanding
addition remain undefined because their result-width policies are not implied.

The canonical selection form is N-way lookup:

```rhombus
result <== mux_lookup(op, ~default: !a):
  0: a & b
  1: a ||| b
  2: a ^ b
```

Cases may come from a host list. Keys are checked and normalized during
elaboration. Binary `mux(sel, when_true, when_false)` is a `Bool` specialization
of the same core `rtl.mux_lookup`; there is no separate core mux operation.

### Boolean, reductions, and ordering

`Bool` is a nominal one-bit frontend `BitwiseType`, not a core special case:

```rhombus
output ready: Bool
ready <== Bool(#true)
equal <== a === b
different <== a =/= b
less <== a < b
active <== state.is_one_of(State.Refill, State.Respond)
encoding_valid <== enum_valid(state)
nonzero <== or_reduce(value)
all_set <== and_reduce(value)
set_count <== popcount(value)
first_index <== priority_encoder(value)
first_mask <== priority_encoder_oh(value)
```

`Bool(#true)` lowers to a one-bit constant plus an explicit cast. Equality
and inequality work on exactly equal flat types and return `Bool`; `=/=` is
the Boolean complement of `===` and lowers to equality followed by negation.
`value.is_one_of(alternative, ...)` accepts alternatives of the same exact flat
type either variadically or as one host list and lowers to typed equalities
joined by hardware OR. Membership in an empty host list is false. Membership is
receiver-only; there is no parallel free-function form.
`enum_valid(value)` derives the complete runtime membership test from a
hardware enum's declared encodings. This matters at ports and cast boundaries:
the enum type prevents unrelated typed operations, but its physical wire can
still carry an unused packed encoding. The predicate works for sequential,
explicit, and one-hot enums; a non-enum operand is rejected.
`or_reduce(value)` and `and_reduce(value)` accept any packable hardware
`DataType` or a host list of packable hardware values, reduce the canonical
packed representation, and return `Bool`. Empty host lists use the Boolean
identities: `or_reduce([])` is false and `and_reduce([])` is true. Nonempty
operands lower respectively to inequality with zero and equality with an
all-ones value, expressed through existing equality and inversion operations
without adding core reduction operations or zero-width hardware types.
`popcount(value)` counts asserted bits in the same canonical packed
representation. For an `n`-bit representation it returns
`Bits(index_width(n + 1))`; an empty host list returns one-bit zero. Population
count lowers to a balanced tree of capacity-sized additions, keeping
logarithmic depth without widening small subtrees beyond the values they can
represent.
`priority_encoder(value)` returns the index of the lowest asserted bit in the
canonical packed representation, while `priority_encoder_oh(value)` returns a
`MaybeOneHot(n)` containing only that bit. Both use packed zero for an all-zero
operand; `MaybeOneHot.valid` distinguishes absence and `MaybeOneHot.index`
returns the same zero-defaulted binary index. A `Vec`'s logical element zero is
its lowest packed lane, so the same lower-index-first rule applies directly to
vectors.
Host code continues to use `!=`. `<`, `>`, `<=`, and `>=` require equal-width
operands with the same numeric type and return `Bool`. `Bits` uses unsigned
ordering while `SInt` uses signed ordering; the layer derives all forms from
the corresponding core less-than operation.

`Mask(n)` is a nominal `n`-lane set whose every packed encoding is legal.
`Mask(n)(value)` constructs a host-known packed lane set; the value is not a
lane index. Mask intersection, union, symmetric difference, and complement use
`&`, `|||`, `^`, and `!`, and `mask[index]` returns `Bool`. Generic reductions,
population count, and priority encoding accept masks through their canonical
packed representation. `Mask` implements `BitwiseType`, but deliberately does
not implement `ArithmeticType`: addition, subtraction, multiplication, and
both shifts are rejected.

Equal-width representation changes remain explicit. `Bits`, `OneHot`, and
`MaybeOneHot` values widen to an unrestricted lane set with `.into(Mask(n))`.
A `Mask` does not safely refine to an exactly-one or optional-one selector;
such a cast is an explicit unchecked representation assertion rather than a
validation operation. See [`../../../examples/rtl/masks.rhdl`](../../../examples/rtl/masks.rhdl).

### Signed integers

`SInt(width)` is a nominal frontend `SignedArithmeticType` with an explicit
positive width. Signed literals accept only the two's-complement range for that
width and retain a canonical packed image:

```rhombus
input(a, b): SInt(8)
output result: SInt(8)

result <== (a + sint(-3, 8)) >> 1
```

Fixed-width `+`, `-`, and `*` wrap modulo `2^width`, just as their packed
two's-complement representations do. `>>` is arithmetic for `SInt`; `<<`
preserves the signed type. Dynamic shift amounts remain unsigned `Bits`;
constant amounts may be nonnegative host `Int` values. Mixed widths and mixed
`Bits`/`SInt` arithmetic are rejected rather than coerced.

`value.sext(target_width)` explicitly sign-extends and preserves the receiver's
signed arithmetic type family. `strunc(value, target_width)` explicitly retains
the low bits and returns the same signed type at the narrower width. Equal-width
representation changes between `Bits` and `SInt` continue to use `cast` or
`.into`. Signed expanding multiplication uses `*&`; expanding signed addition,
division, remainder, and implicit width inference are not part of this layer.

See [`../../../examples/rtl/signed-integers.rhdl`](../../../examples/rtl/signed-integers.rhdl).

### Hardware enums

```rhombus
hardware_enum Opcode(~width: 4):
  Add = 0
  Sub = 1
  Load = 8
```

Hardware enums are nominal `FlatDataType`s without arithmetic or bitwise
capability. Automatic encodings use declaration order and the minimum positive
width. Explicit encodings require a stable width, unique names and values, and
all-or-none explicit values.

An enum may own multiple hardware methods:

```rhombus
hardware_enum CacheState:
  Invalid
  SharedClean
  SharedDirty
  UniqueClean
  method is_shared(state) :: Bool:
    state.is_one_of(CacheState.SharedClean, CacheState.SharedDirty)
  method is_invalid(state) :: Bool:
    state.is_one_of(CacheState.Invalid)

input state: CacheState
output shared: Bool

shared <== state.is_shared()
```

The first method parameter is the receiver and is omitted at the call site.
Method bodies use ordinary Rhodium operations and return hardware. An optional
result type preserves the exact authoring surface for chained calls; an
unannotated result remains generic `Hardware`. Methods are supported on
automatic, explicit-width, explicit-value, and one-hot enums.
The exact enum authoring surface follows members through typed ports and
wires, explicit or inferred registers, `mux`, parameterized bundle fields,
vector elements, asynchronous memory reads, interface members, child-instance
ports, and explicit `cast` or `.into` representation changes. Generic hardware methods
such as `.into` remain available on the same values.

A declaration can instead make its encoding policy explicit and derive one
selector bit per member:

```rhombus
hardware_enum ResultKind(~encoding: one_hot):
  Arithmetic
  Shift
  Compare
  Xor
  Or
  And

result <== select.mux({
  ResultKind.Arithmetic: arithmetic_result,
  ResultKind.Shift: shift_result,
  ResultKind.Compare: compare_result,
  ResultKind.Xor: xor_result,
  ResultKind.Or: or_result,
  ResultKind.And: and_result
})
```

This produces a six-bit nominal enum with encodings `1`, `2`, `4`, `8`, `16`,
and `32`. It is distinct from both `OneHot(6)` and every other enum declaration,
but its packed representation can directly drive `.mux`. Typed-key map arms
are checked against that nominal enum and must cover each member exactly once. See
[`../../../examples/rtl/one-hot-enum.rhdl`](../../../examples/rtl/one-hot-enum.rhdl).

Each evaluated declaration has distinct nominal identity. Members lower to
`Bits(width)` constants plus casts. Enum-selected `mux_lookup` requires keys
from exactly the selector's enum and retains a mandatory default for unused
encodings. See [`../../../examples/rtl/enum-state.rhdl`](../../../examples/rtl/enum-state.rhdl).

### Tagged unions

Tagged unions are nominal packed `DataType`s whose leading tag uses the same
encoding machinery as hardware enums. Variants may be nullary or carry one
typed payload:

```rhombus
bundle WritePayload():
  address: Bits(8)
  data: Bits(8)

tagged_union Request:
  Idle
  Read(Bits(8))
  Write(WritePayload())

def request = Request.Read(bits(0x5A, 8))
def read = request.view(Request.Read)
```

`value.tag` returns the union's nominal `tag_type`. `value.is(Variant)` tests
the tag, while `value.view(Variant)` returns a view with `.valid` and, for a
payload variant, a typed `.payload`. The payload is meaningful only when
`.valid` is true. Variants must belong to the viewed union. Payload
constructors require the declared payload type, and nullary variants are
reusable hardware literals.

Automatic tags use declaration order and the minimum positive width. As with
hardware enums, `~width` supports all-or-none explicit encodings and
`~encoding: one_hot` derives one tag bit per variant. The packed union reserves
enough payload bits for its widest variant; shorter payloads are zero-extended
and nullary variants use an all-zero payload image. Nested tagged unions,
bundles, vectors, ports, and state retain the exact union authoring surface.

See [`../../../examples/rtl/tagged-union.rhdl`](../../../examples/rtl/tagged-union.rhdl)
and [`../../../examples/rtl/nested-tagged-union.rhdl`](../../../examples/rtl/nested-tagged-union.rhdl).

### One-hot values

`OneHot(n)` is a structurally sized `FlatDataType`. Calling the type with a
host index produces the corresponding power-of-two literal. A one-hot encoded
hardware enum provides the same selector contract while retaining named,
nominal members:

```rhombus
def Grant = OneHot(4)
next_grant <== current.mux([
  Grant(1),
  Grant(2),
  Grant(3),
  Grant(0)
])
```

`one_hot(index)` converts a hardware `Bits(n)` index to `OneHot(2^n)`. Inferring
the result width makes the conversion total: every index encoding selects
exactly one lane. Use `one_hot(index).as_bits()` when subsequent mask operations
may produce zero or multiple asserted bits.

One-hot values deliberately do not implement `BitwiseType`, because bitwise
operations do not preserve the exactly-one invariant. Equality, aggregates,
ports, state, and explicit casts work normally. `.mux` lowers exact selectors
to the partial `rtl.onehot_mux` operation. Exactly one selector bit must be set;
zero-hot and multi-hot results are unspecified. This contract needs no default
value and allows the backend to use selector-bit gating and a balanced OR tree.
Structural values use `selector.mux(choices)`. One-hot enums use typed-key map
arms so member-to-lane mappings stay explicit and independent of call order.

`MaybeOneHot(n)` is the compact optional counterpart: zero represents no
selection and every present value has exactly one asserted bit. Calling the
type without an argument constructs absence; calling it with a host index
constructs that lane. Native values support lane indexing, `.valid`, `.index`,
and `.mask(mask)`, where the operand must be an equal-width `Mask`. Masking may
turn a present selection into absence but cannot create a second selected lane.

Because absence does not satisfy the partial exact-one-hot contract,
`selector.mux(choices, ~default: value)` requires an explicit default and is
total for every legal `MaybeOneHot` encoding. Exact and optional selectors
share the `.mux` method name while the selector type and presence of a default
determine the contract. `MaybeOneHot` does not implement
`BitwiseType`; arbitrary bitwise OR could violate its at-most-one invariant.
External ports remain contracts and can still require a formal assumption when
their environment could physically drive a multi-hot encoding.

### Packing and width operations

- `a ++ b` concatenates packable hardware values and places `a` in the
  most-significant bits; chains such as `a ++ b ++ c` retain that ordering.
  Ordinary host strings, lists, maps, and sets keep Rhombus's append behavior.
- `concat(a, b, ...)` is the variadic form and also accepts a host-generated
  list, making it suitable when the operands are constructed programmatically.
- `bits_value[index]` produces `Bits(1)`.
- `value[low..high]` uses a half-open host range; `low..=high` is inclusive.
  Both forms lower to the core's fixed bit-extraction operation.
- `bits_value.zext(target_width)` adds most-significant zeroes and returns wider
  `Bits`; use `.as_bits()` to expose other packable data first.
- `signed_value.sext(target_width)` sign-extends any `SignedArithmeticType` and
  returns the same nominal signed type family at the wider width.
- `trunc` retains low bits.
- `value.into(TargetType)` preserves packed width and bit pattern while
  changing the hardware type.
- `value.as_bits()` exposes any packable data value as
  `Bits(value.type.packed_width())`; an existing `Bits` value is unchanged.
- `value.as_vec(ElementType)` splits any packable data representation into a
  `Vec` whose inferred length exactly covers the source width. Element zero
  receives the least-significant element-width chunk, and an existing vector
  of the inferred type is unchanged.

Indices and ranges are host values known during elaboration. Width changes are
always explicit. Width extension is receiver-only; there are no parallel
author-facing `zext(value, width)` or `sext(value, width)` free functions.

## State and clocked effects

### Registers and synchronous circuits

A register reads as its current state and drives as its next state:

```rhombus
reg state(Bits(width), ~clock: clk, ~reset: reset, ~init: zero)
state <== state + one
```

The same rule follows statically selected aggregate paths. Reading
`state.field` or `state[index]` selects current state, while using that path as
the direct target of `<==` selects the corresponding next-state place.
Author-level RTL should always use this direct form. Hardware-selected dynamic
indices are values rather than places; express those updates as a
whole-register drive with `.updated`.

The type can be inferred from `~init` or `~next`. Supplying `~next` drives the
register immediately; another drive is an error. `~init` is an active-high
synchronous reset value, not power-up initialization. Register state may be
any `DataType`. Register keyword options follow ordinary Rhombus calling rules
and may appear in any order; their existing legal combinations are unchanged.

`sync_circuit` supplies real `clock: Clock` and `reset: Reset` inputs plus an
ambient domain. Those ports are present in the core IR and emitted RTL, but
`clock` and `reset` are not source bindings inside the body:

```rhombus
sync_circuit Counter(width):
  output count: Bits(width)
  reg state(~init: bits(0, width))
  state <== state + bits(1, width)
  count <== state
```

A resetless ambient register uses the clock only. An initializer is never
invented merely because ambient reset exists. Explicit `~clock` and `~reset`
controls are rejected while a sync domain is active; use an ordinary `circuit`
for an explicit clock boundary.

`reset_when(condition)` creates a nested domain whose reset is the active
ambient reset OR a readable one-bit data or `Reset` condition. Nested scopes
compose by ORing each added condition:

```rhombus
reset_when(flush):
  reg pending(~init: Bool(#false))
  pending <== next_pending
```

A sync child that needs the derived reset but remains in use after the scoped
expression uses the equivalent instance option:

```rhombus
inst responses(Queue(Response(), 2), ~reset_when: flush)
```

The child's clock remains ambient; `~reset_when` is not an explicit control
override and is valid only for a `sync_circuit` child. When hardware or a
simulation boundary must observe the active reset as data, `reset_active()`
returns it as `Bits(1)` and respects the innermost reset scope. There is no
corresponding clock-value escape hatch.

### Asynchronous-read memories

```rhombus
mem storage(depth, Bits(width))
read_data <== storage[read_address]
storage.write(write_address, write_data,
              ~enable: write_enable, ~clock: clock)
```

Inside a `sync_circuit`, the clock is omitted. Memory depth is a positive
host integer; elements may be any `DataType`; addresses are exactly
`Bits(index_width(depth))`. Reads are asynchronous physical ports. Writes are
independent rising-edge synchronous ports and share one clock per memory.

Initial contents, dynamic out-of-range addresses, read-during-write results,
and simultaneous writes to one address are unspecified. Resets, masks,
initialization, and combined read/write ports are not part of the current
contract.

### Synchronous memories

Synchronous memories are separate circuit-shaped primitives with declared,
fixed ports:

```rhombus
sync_mem storage(depth, Bits(width)):
  read
  write

storage.read.address <== read_address
storage.read.enable <== read_enable
read_data <== storage.read.data

storage.write.address <== write_address
storage.write.data <== write_data
storage.write.enable <== write_enable
```

Alternatively, one shared physical port performs either a read or a write per
cycle:

```rhombus
sync_mem storage(depth, Bits(width)):
  read_write

storage.read_write.address <== address
storage.read_write.enable <== enable
storage.read_write.write <== write
storage.read_write.write_data <== write_data
read_data <== storage.read_write.read_data
```

Write-capable memories can opt into uniform packed-lane masks:

```rhombus
sync_mem storage(depth, Bits(32), ~mask_granularity: 8):
  read_write

storage.read_write.address <== address
storage.read_write.enable <== enable
storage.read_write.write <== write
storage.read_write.write_data <== write_data
storage.read_write.write_mask <== byte_mask
read_data <== storage.read_write.read_data
```

The granularity is a positive host `Int` that divides the packed element
width. The mask is `Bits(packed_width / granularity)` and may select any subset
of the fixed lanes on each write. Separate write ports expose `.mask`; shared
ports expose `.write_mask`. Bit zero controls the least-significant packed
lane. Omitting `~mask_granularity` preserves the unmasked port shape.

The supported shapes are 1R, 1W, 1R1W, and 1RW. A `read_write` declaration
cannot yet be combined with separate `read` or `write` declarations. Port
declarations are not inferred from use, the primitive cannot be indexed, and
the fixed port names cannot be renamed. Address fields are
`Bits(index_width(depth))`, data fields use the element `DataType`, and
`enable` and `write` are `Bits(1)`.

Inside a `sync_circuit`, `sync_mem` uses the ambient clock and rejects
`~clock`. An ordinary `circuit` supplies `~clock: clock` in the declaration. `~clock` and
`~mask_granularity` may appear in either order. An enabled read samples
its address on a rising edge and presents the corresponding data after that
edge. Data while read enable is false is unspecified. The primitive has no
reset or initialization; out-of-range addresses and same-cycle read/write
collisions between separate ports are also unspecified. On an enabled shared
port, `write = 0` reads and `write = 1` writes; `read_data` after a write or
while disabled is unspecified. An all-zero write mask preserves storage but
does not turn a shared write-mode cycle into a read. Port arrays remain outside
this contract.

### Clocked assertions

```rhombus
sync_circuit CheckedQueue():
  input invariant: Bool
  input request: Bits(1)

  assert(invariant, "queue_invariant")
  when request:
    assert(invariant, "request_invariant")
```

An assertion samples a readable one-bit `FlatDataType` on each rising clock
edge. It is disabled while the active-high reset is asserted. `sync_circuit`
supplies the ambient clock and reset and rejects explicit controls. An ordinary
circuit must supply both `~clock` and `~reset`; supplying only one is an error.
Host Booleans are not hardware conditions.

The optional second positional argument is an ASCII identifier label. The
current form has no formatted message operands or temporal-property syntax.
Inside `when`, an assertion is active only when its effective priority branch
is selected. `elsewhen` guards exclude earlier conditions, `otherwise` covers
the unmatched case, and nested chains combine their guards. The public
assertion form has no `~enable` option.

### Clocked DPI

```rhombus
dpi_import procedure rhodium_trace(value: Bits(8))
dpi_import function rhodium_step(value: Bits(8)) -> result: Bits(8)
dpi_import function rhodium_step_pair(value: Bits(8)) -> (
  out doubled: Bits(8),
  return incremented: Bits(8)
)

rhodium_trace.call(value, ~enable: enable)
dpi_reg step_result = rhodium_step(value, ~enable: enable)
dpi_reg (doubled_result, incremented_result) = rhodium_step_pair(value, ~enable: enable)
```

A procedure is a clocked side effect with no result. A DPI function has one C
return and may place `out` results before it; every input and result must be a
flat hardware type. A single-result function uses one `dpi_reg` binding. A
multi-result function uses parenthesized `dpi_reg` bindings in declaration
order. The bindings are separate hardware values rather than a packed bundle.
Each result is visible state: it holds while disabled, has unspecified initial
value, and has no reset. A `sync_circuit` supplies the ambient clock and
rejects `~clock`, while ordinary circuits pass it explicitly. `dpi_reg` accepts
`~clock` and `~enable` in either order. There is no unclocked DPI form,
`inout`, or `ref` support.

## Control and structural composition

### Conditional assignment and effects

`when` accepts only a readable one-bit `FlatDataType`. Host values are rejected:

```rhombus
when load:
  state <== data_in
elsewhen clear:
  state <== zero
otherwise:
  state <== fallback
```

The first true branch wins. Nested chains lower each destination to priority
mux lookups plus one final drive. Register-next destinations may omit
`otherwise` and implicitly hold current state. Outputs, wires, and instance
inputs require exhaustive assignment.

`switch` selects unordered, unique exact keys from one hardware selector:

```rhombus
switch state:
  case State.Idle:
    state <== State.Running
  case State.Running:
    state <== State.Done
  otherwise:
    state <== State.Idle
```

Bits selectors use nonnegative host `Int` keys. Typed mux-key selectors such as
hardware enums and `OneHot` require keys from that selector's own type. Case
order carries no priority, fallthrough is unsupported, and keys must be unique
and fit the selector width. Each assigned destination lowers directly to one
`rtl.mux_lookup`. As with `when`, outputs, wires, and instance inputs require
`otherwise`, while register-next destinations may omit it and hold.

Conditional memory writes are effects rather than place assignments. Branch
guards combine with explicit local enables, and corresponding write positions
across mutually exclusive branches share one physical port with muxed address
and data. Independent chains remain independent ports.

Assertions participate in conditional-body lowering: each assertion receives
the effective priority or exact-match branch as a derived activation guard.
Clocked DPI calls and DPI result registers do not participate; put those effects
outside `when` or `switch` and pass the hardware condition through `~enable`.

A destination may appear once per branch, and separate chains do not implement
last-connect semantics. Primitive register reset remains expressed through
`~reset` and `~init`.

### Hierarchy

Equivalent circuit calls reuse one elaboration-local module specialization, so
instances can call a generator directly:

```rhombus
inst u0(Adder(8))
inst u1(Adder(8))
```

Different stable parameters create distinct definitions. Binding the result
once remains valid but is not required for reuse.

An `inst` binding supplies a suggested name; collisions receive deterministic
suffixes. `InstanceArray` is a host collection reducer that retains checked
static information for concise child-port access. Child inputs are places,
child outputs are values, and all communication uses ports.

A marked sync child instantiated inside a sync parent receives the ambient
clock and reset. Ordinary children never participate implicitly. Outside a
sync parent, both signals must be supplied explicitly.

### Bundles and records

The bundle layer gives concise declarations and fields over core structural
records:

```rhombus
bundle Pair(T):
  left: T
  right: T

def swapped = Pair(Bits(8)):
  left: source.right
  right: source.left

result.left <== source.right
result.right <== source.left
```

A concrete zero-parameter bundle may also own multiple hardware methods:

```rhombus
bundle CacheEnvelope():
  state: CacheState
  valid: Bool
  method is_shared(envelope) :: Bool:
    envelope.valid & envelope.state.is_shared()
  method state_value(envelope) :: CacheState:
    envelope.state

shared <== envelope.is_shared()
state_shared <== envelope.state_value().is_shared()
```

The receiver is omitted at the call site, just as for enum methods. Exact
bundle methods follow values through typed ports, literals, wires, explicit
registers, `mux`, vectors, memories, and child-instance ports. A typed method
result retains enum or bundle methods for further dot calls. Field and method
names share one dot namespace and must be distinct. Parameterized bundles do
not yet support method declarations because their specialization-dependent
field surfaces are not available to method bodies.

Calling the declared name with its parameters and no field block produces a
`RecordType`; attaching a field block constructs a value of that type. Fields
are named, order-independent, and must be supplied exactly once with matching
hardware types. The bare type family remains an ordinary host value, so it can
be passed to a function and called later. The declaration also supplies a
preferred emitted type name without changing structural type equality. When
multiple concrete specializations request that name, the backend retains the
first and adds numeric suffixes to the rest.

A bundle can conditionally include one or more fields using a host `if` group:

```rhombus
bundle TaggedPayload(T, include_tag):
  payload: T
  if include_tag:
    tag: Bits(4)
```

The condition is evaluated during elaboration and must produce a host
`Boolean`; a hardware `Bool` is rejected. `TaggedPayload(Bits(8), #false)` is
an eight-bit record containing only `payload`, while the `#true`
specialization is a twelve-bit record that also requires `tag`. An absent
field is not evaluated, consumes no width, cannot be projected, and must not be
supplied during construction. Remaining fields retain source order, duplicate
realized names are rejected, and every concrete specialization must contain at
least one field. Structural equality continues to compare the resulting
record fields rather than the conditions that produced them.

Complete field-wise drives canonicalize to nested record construction and one
whole-record drive. Partial assignment and mixing whole with field-wise drives
are errors. When every field of a named bundle construction is a
`HardwareLiteral`, the form creates a reusable recursive `RecordLiteral`;
otherwise it creates runtime `rtl.record_create` hardware. The lower-level
`record(type_value): fields` form remains available when the `RecordType` is a
dynamically computed host value instead of a directly named bundle family.

### Fixed-length vectors

`Vec(n, T)` constructs the public structural `VectorType` descriptor, while
`vec(...)` layers concise construction over it. Literal-only construction
produces a reusable `VectorLiteral`; live elements produce a runtime vector
value. Element zero occupies the least-significant packed slot.

Static host indexing works for readable values and driveable places. A hardware
`Bits(index_width(n))` index may read a `Vec(n, T)` with ordinary brackets; an
out-of-range encoding is undefined and makes the result unconstrained.
Complete element-wise drives canonicalize to one vector construction and whole
drive. Total dynamic read and functional replacement remain explicit:

```rhombus
chosen <== values.lookup(selector, ~default: fallback)
next_value <== current.updated(selector, replacement)
```

Bracket selection lowers to core `rtl.vector_index`. Lookup projects all
elements and builds a mux with its explicit default. `updated` reconstructs the
vector with per-element muxes, and an out-of-range selector leaves the original
vector unchanged.

A direct `Reg<Vec(n, T)>` is the one dynamically assignable exception:

```rhombus
state[selector] <== replacement
```

Repeated assignments describe independent write ports without tuple syntax:

```rhombus
when write_enable_0:
  state[address_0] <== data_0
when write_enable_1:
  state[address_1] <== data_1
```

All writes to one register lower together to core `rtl.vector_write_set` and
one whole next-state driver. Writes in mutually exclusive branches share a
port; writes in independent conditionals remain independent ports. Enabled
indices must be in range and pairwise distinct. A collision has undefined
behavior and no port priority, allowing symmetric decode and merge logic.
Multiple total functional replacements may still be composed explicitly with
chained `updated` calls. Dynamically selected ordinary vector places remain
read-only.

### Wires

`wire temporary: T` creates an internal single-driver connection. Its value is
readable before its one driver is declared, so wiring order does not impose
dataflow order. Verification still requires one complete effective driver.
Aggregates may be assembled leaf by leaf;
all leaves must be driven, and whole and projected drive modes cannot mix.
Conditional assignment may synthesize one exhaustive priority driver.

Core retains `rtl.wire` for inspection. CIRCT lowering aliases the read value
to its driver, so a SystemVerilog wire is not necessarily emitted.

## Interfaces and topology

### Declaring and connecting interfaces

Interfaces are frontend protocol descriptors over directional record ports,
not core `DataType`s:

```rhombus
interface ingress(Decoupled(T), ~role: consumer)
interface egress(Decoupled(T), ~role: producer)
egress <=> ingress
```

Each root direction becomes one record-typed core port. `<=>` atomically
connects exact flows or compatible provider-to-peer contracts; operand order
is irrelevant. Individual fields remain accessible, and frontend metadata
reconstructs endpoints through instances without guessing from port names.

### Read-only observations

Interfaces describe connectivity and do not carry monitor factories or
instrumentation policy. `endpoint.observe()` explicitly creates a read-only view
containing fields from both directions, so ordinary library functions can
correlate a forward `valid` with a backward `ready`:

```rhombus
interface Stream(T):
  role producer
  role consumer
  producer -> consumer:
    valid: Bool
    bits: T
  consumer -> producer:
    ready: Bool

fun monitor_stream(link :: Observation):
  assert(!(link.ready & !link.valid), "ready_requires_valid")

sync_circuit Producer():
  interface stream(Stream(Bits(8)), ~role: producer)
  stream.valid <== false
  stream.bits <== 0
  monitor_stream(stream.observe())
```

The observation capability prevents instrumentation from becoming another
driver. Protocol packages can offer several independent monitor functions, and
an implementation or verification wrapper chooses which ones to elaborate at a
specific endpoint. A monitor that uses `assert`, registers, or other ambient
sequential operations must be called in a `sync_circuit`.

### Identity and connection compatibility

Each root interface declaration has a stable nominal identity. By default,
repeated calls to one parameterized declaration compare that identity plus
their realized member structure; independently declared same-named interfaces
remain distinct. A declaration with a custom connection rule also preserves
its arguments as part of exact-specialization matching. `Endpoint.of(protocol)`
checks one exact nominal specialization.
`Endpoint.supports(protocol)` accepts that protocol, a transitive refinement,
or a declared supported contract while retaining endpoint static information.
Ordinary libraries can apply the same nominal relation to type descriptors
with `interface_type_supports(type, protocol)`; interface display names are not
part of either check.

A parameterized root interface may explicitly define a directional connection
rule after its provider declaration:

```rhombus
interface CapacityLink(capacity, width):
  role producer
  role consumer
  provider producer
  connection_compatible producer (provided, required):
    provided[0] >= required[0]

  producer -> consumer:
    payload: Bits(width)
```

`provided` and `required` are Lists containing the corresponding declaration
arguments. The provider-owned predicate runs at elaboration time, must return a
host `Boolean`, and is directional; `<=>` operand order does not affect which
specialization is provided. The predicate governs every connection, including
two endpoints with equal arguments. `Endpoint.supports(protocol)` uses the
rule, while `Endpoint.of(protocol)` remains exact. A rule applies only within
one nominal interface family. It may relax semantic parameter compatibility,
but member names, directions, and recursively realized hardware types must
still match exactly. Width conversion or field translation therefore requires
an explicit adapter. Nested interface members and endpoint arrays apply the
same rule recursively.

The first declared role is the compatibility provider by default. A root
interface whose provider is the other role declares it explicitly after its
roles, for example `provider sender`. Endpoint provenance records whether the
connected component is a module peer or an instance, so compatible connection
does not infer orientation from the presence of a particular member.

### Forwarding, refinement, and nesting

Bulk connection also forwards one endpoint through a hierarchy boundary when
the boundary and child endpoint have the same declared role, complementary
represented roles, and the same nominal interface family. Forwarding requires
the same specialization and recursively wires its complete shape; it does not
apply the peer compatibility predicate or permit specialization relaxation.
Different nominal contracts continue to use their declared refinement or
support compatibility. The ordinary `outer <=> child.port` spelling therefore
works for interfaces with custom connection policies as well as structurally
exact interfaces without them.

An interface can declare one nominal parent with `refines` and additional,
role-qualified, structurally checked contracts such as
`supports producer: Decoupled(T)`. The support role must be the interface's
provider role. Parenthesized `<=>` forms
merge a parent endpoint with its refinement delta or split a richer source
into parent and delta destinations. The frontend checks the parent, complete
field set, types, and directions.

Interface members may themselves be interfaces. Composition, field access,
bulk connection, and instance reconstruction work recursively. Nested
directions lower to nested `RecordType` fields and then CIRCT `hw.struct`
without core or backend interface cases.

### Endpoint arrays

Declared endpoint arrays bulk-connect directly. A parenthesized sequence on
the right connects individual endpoints positionally to an array on the left:

```rhombus
egress <=> (first_ingress, second_ingress)
```

The sequence must have the same length as the array, and every pair receives
the ordinary protocol, type, and direction checks. This parenthesized form is
connection-only syntax, not a general host tuple, and cannot appear on the
left of `<=>`. Scalar endpoint connections keep using the same syntax for
interface refinement merge and split.

### Links, transforms, and pipelines

`interface_link(protocol)` creates a local pair of complementary endpoint
views over forward-readable, exactly-one-driver wires. An `InterfaceHandle`
owns a left endpoint shape and a right endpoint shape. Endpoint shapes are an
`Endpoint` or recursively matching `Array`s of endpoints.

`interface_transform(input_links, output_links)` adopts already-wired local
links as one generic transform. Its input can be one link or an array-shaped
product of links, and its output can independently be one link or an array.
The transform exposes input links' left sides and output links' right sides,
supporting 1-to-1 protocol conversion as well as N-to-M topology without
adding a protocol concept to core IR. Protocol libraries implement the wiring
between the internal link ends before constructing the transform.

A binding annotated as `Handle` exposes `.left` and `.right` as endpoint
shapes: either one `Endpoint` or a recursively matching endpoint `Array`.
Static information preserves field access on scalar sides and indexing on
array sides. This supports named local links such as `Valid(T)` sidebands:
drive the fields on `.left` and read the corresponding fields on `.right`
without materializing a circuit instance.

- `endpoint |> handle` connects the endpoint to the left end and returns the
  right end while preserving endpoint-shape static information.
- `endpoint |> endpoint` connects the source to the terminal endpoint and
  returns `#void`, allowing a complete topology to remain one pipeline.
- `first_handle |> second_handle` connects their adjacent ends and returns a
  handle owning the two outside ends.
- `handle |> endpoint` performs the same partial termination and returns its
  `InterfaceSink`, so a detached topology can be assembled from right to left.
- `endpoint |> sink` completes the topology and returns `#void`; an earlier
  `handle |> sink` retains that handle's left end in a new sink.
- `parallel_handles(Array(...))` creates one array-shaped handle or sink from
  a homogeneous collection. Handles and sinks cannot be mixed.

Handles and sinks are interface-generic: they contain no ready-valid policy,
buffering, or new core IR. They are linear host objects because consuming one
twice would attempt to drive an interface destination twice. `<=>` between
concrete endpoint shapes performs the hardware wiring effect and returns
`#void`; piping a handle into an endpoint performs partial termination and
returns a sink.

Configured standard flow stages use the interface topology static-information
key through `InterfaceTransformResult` to select a dependent result. A known
endpoint or endpoint array exposes the connected transform's exact result
shape, a handle remains a handle, and a generic expression receives a
conservative topology annotation that permits endpoint fields, array indexing,
and handle-side access until elaboration chooses the concrete shape. This is
frontend information only; it creates no protocol-specific runtime graph or
core IR type.

Inspection consumers can require explicit transaction-flow semantics through
`InterfaceTraceModel`. Its routes map top-level transform input indices to
possible output indices independently of the transform's display `kind`.
`describe_interface_event` additionally records a labeled one-to-one event
checkpoint. These records are nonsemantic module metadata: they do not modify
hardware, verification, or backend lowering. A compiler analysis must reject an
unmodeled transform instead of inferring routes from its label.

### Injection and ejection boundaries

`inject_interface(protocol, ...)` creates an endpoint from ordinary hardware,
and `eject_interface(...)` terminates an endpoint back into ordinary hardware.
Every declared member is supplied by name; `interface_fields(...)` groups the
bindings of a nested interface member. These boundaries derive direction only
from the interface roles and provider declaration. They do not inspect names
such as `valid`, `ready`, `bits`, or `credit`.

The interface layer therefore owns composition, topology, and generic circuit
boundaries. `Valid`, `Decoupled`, `Irrevocable`, and `Credited` are optional
standard-library protocols above it. User libraries can declare unrelated
bidirectional protocols and configured transforms without importing those
standard protocols.

Ready-valid protocols and reusable flow circuits are documented in
[`../../std/README.md`](../../std/README.md). Canonical feature programs live
in [`../../../examples/`](../../../examples/README.md). See
[`DEVELOPING.md`](DEVELOPING.md) to change or add a frontend layer.
