<!-- Compares Rhodium's explicit construction language with Clash's typed functional synthesis semantics. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rhodium and Clash: explicit construction versus typed functional synthesis

## Scope and thesis

*Snapshot: 2026-08-17.*

Rhodium and Clash both describe synchronous circuits without asking an HLS
scheduler to choose register placement. They differ in what their source
language makes primary. Rhodium executes Rhombus generators that construct an
explicit hardware graph. Clash compiles a synthesizable Haskell program in
which pure functions, algebraic data, and domain-indexed streams are the
hardware vocabulary.

Clash is more elegant when circuit structure follows functional composition:
map a function over a vector, lift it over time, or derive a state machine from
a pure transition function. Rhodium is more explicit when circuit construction
itself is the subject: introduce a destination, drive it once, instantiate a
register, and preserve a chosen module boundary. The core question is whether
functional uniformity or explicit graph-construction semantics gives the
better local model for the design at hand.

For elastic networks, the current `clash-protocols` `Circuit` abstraction and
Rhodium's standard flow layer make the comparison sharper. Clash provides the
more general typed algebra of reusable protocol circuits. Rhodium provides the
more source-visible topology language, with linear ownership of open paths and
explicit distinctions among combinational adapters, buffers, and instances.

## Summary

| Question | Rhodium | Clash |
|---|---|---|
| Source denotes | Host elaboration that constructs one verified operation graph | A normalizable Haskell description of combinational or synchronous functions |
| Core composition unit | Explicit definition/binding connections and circuit instances | Typed functions and function application |
| Time | Explicit registers, memories, clocks, and next-state drives | `Signal dom a` streams plus explicit state primitives and combinators |
| Static dimension | Realized hardware types checked during elaboration and verification | Widths, lengths, data types, and domains participate in Haskell types |
| Flow composition | One-shot interface handles; serial, parallel, and cardinality-changing `|>` paths | Reusable, protocol-indexed `Circuit a b` values with typed forward and backward channels |
| Patterns and decode | Typed aggregate cubes form an unordered, overlap-checked finite relation with sparse outputs | Haskell patterns destructure algebraic values and may bind nested fields in ordinary expressions |
| Concurrency | The constructed graph is concurrent; no scheduler | Pure signal network is concurrent; no general operation scheduler |
| Main source of locality | Source forms say which hardware object is being constructed | One functional syntax composes values, structures, and temporal signals |
| Syntactic compression | Direct for named RTL structure and local control | High for generic datapaths, vectors, products, and state machines |

## Denotation and staging

An Rhodium `circuit` is a Rhombus generator. During `elaborate`, ordinary host
functions, loops, collections, and parameters determine what hardware exists.
Hardware objects then create nodes in the
[single public IR](../../rhodium/core/README.md). Generation chooses structure,
while `when`, `switch`, and mux operations denote runtime hardware selection.

Clash is not a deep embedding with a separate signal AST presented as a
library value. It uses Haskell syntax and GHC's typed program representation,
then normalizes the definitions reachable from a synthesizable top entity.
The [Clash FAQ](https://docs.clash-lang.org/compiler-user-guide/general/faqs.html)
therefore describes ordinary `if`, guards, pattern matching, higher-order
functions, and algebraic data as potential circuit description. The types and
normalization context determine which expressions become structure and which
are evaluated during compilation.

Clash's uniformity is a genuine language advantage: the same abstraction
mechanisms used for pure software functions organize hardware. Its staging
boundary is correspondingly less syntactically prominent. Rhodium requires more
hardware-specific notation, but a reader can identify elaboration-time and
runtime constructs without first following normalization and specialization.

## Core composition unit and syntax

Rhodium composition is constructive. `sum <== a + b` reads as a structural claim:
the output destination `sum` receives its one binding from this expression.
`reg state(...)` introduces a state element. A circuit call creates a fresh
module definition; `inst` creates an instance boundary. Internally, Rhodium records
definition results and binding destinations separately. That is the exact IR's
normal form, not a counterpart to Clash's functional abstractions.

Clash composition is applicative. A combinational circuit commonly has type
`a -> b`; a synchronous circuit has a type involving `Signal dom a`. Function
composition wires components, higher-order functions generate repeated
structure, and `Vec n a` makes regular hardware look like ordinary functional
programming. `Bundle` relates products of signals to signals carrying products,
so structural rearrangement remains typed function composition.

This syntax has exceptional economy for regular datapaths. A vector `map` or
`fold` states the mathematical structure without naming every intermediate
wire. Rhodium can use host functions and loops to generate the same network, but
the result is expressed through construction actions rather than by giving the
network a pure functional denotation. Conversely, Rhodium's explicit destination
syntax is clearer when output ownership, final connectivity, or a chosen
module boundary is the important fact.

## Typed literals, patterns, and relational decode

Clash inherits Haskell's general pattern language. Pattern matching over
algebraic data can name constructors, destructure nested products and sums,
bind fields, and produce an arbitrary expression; compiler normalization then
turns the reachable decision into hardware. That is the more expressive and
often more elegant abstraction when a decoder is really a semantic function
over an algebraic value. Clash also provides
[`bitPattern`](https://hackage-content.haskell.org/package/clash-prelude-1.8.2/docs/Clash-Sized-BitVector.html),
whose `0`, `1`, and wildcard positions can match and bind selected bits in a
flat `BitVector` encoding.

Rhodium addresses a narrower but common RTL problem. `HardwareLiteral` is an
open exact-value protocol for scalar, aggregate, and extension-defined types.
`Pattern` is a recursive typed bit cube, so a record field or vector element
may be exact, partly cared, or unconstrained without flattening the source
description. `DecodeTable` validates a finite unordered relation by rejecting
overlapping input cubes; sparse output patterns preserve which result bits are
irrelevant. Its input-lift and output-product operations make relations
composable before one `DecodeGen` consumes them, although output zipping
intentionally requires identical input partitions.

That representation can expose sharing and output don't-cares to a
multi-output PLA implementation more directly than separately written
comparisons and muxes. It is an optimization opportunity, not a general QoR
guarantee: Clash can express the same Boolean relation, and normalization plus
target synthesis may recover equal or better structure. Rhodium gains a checked,
decoder-specific relation; Clash retains the broader and more uniform
functional pattern language.

## Flow composition

Rhodium's standard flow layer gives ready-valid topology an expression-oriented
surface without changing the core language. A configured stage is an ordinary
unary function applied with `|>` to either a live endpoint or a disconnected
`InterfaceHandle`. Serial stages, `parallel` products, arbitration, joins,
forks, demultiplexing, and crossbars can therefore form one path even as its
endpoint cardinality changes. Handles and terminated sinks are consumed once,
whereas configured stage functions are reusable and build fresh topology when
applied.

The abstraction retains unusually direct structural meaning. Payload maps and
other combinational adapters become local interface links; stateful or
structurally meaningful stages remain explicit instances. Stages also make
protocol-strength changes visible, such as storage establishing an
`Irrevocable` output or live control weakening one to `Decoupled`. All of this
lowers through Rhodium's generic interface subsystem and ordinary bindings, so the
minimal IR needs no flow graph or protocol operation.

[`clash-protocols`](https://github.com/clash-lang/clash-protocols) offers the
more general mathematical abstraction. A `Circuit a b` relates the `Fwd` and
`Bwd` representations of arbitrary protocols, and `Df dom a` supplies one
dataflow protocol with acknowledgment and stability laws. Circuit values are
typed, reusable descriptions that compose independently of a particular
connection; serial and product structure are instances of the same general
protocol algebra rather than special ready-valid topology operations. This is
more uniform and protocol-parametric than Rhodium's fixed flow family.

The static types do not by themselves prove every `Df` law. The project states
that its harnesses check invariants where possible and explicitly notes that
stable data until acknowledgment is not yet checked. Rhodium has a corresponding,
unproven contract: `Irrevocable` stability is documentary. Ordinary `map_flow`
and `demux_flow` stages now weaken to `Decoupled`; preserving an irrevocable
contract requires an explicit author assertion that is not itself proven.
Clash has the cleaner reusable circuit algebra; Rhodium has clearer source-level
ownership of the realized buffering, readiness paths, hierarchy, and
cycle-verified whole-design dependency graph.

## Time, state, and concurrency

Clash gives synchronous time a first-class type constructor. The tutorial
describes `Signal dom a` as an infinite sequence of `a` samples, one per tick in
domain `dom`. `register` delays a signal by a cycle, while `mealy` and `moore`
lift pure state-transition functions into sequential machines. Feedback is
expressed through the functional network, with state primitives breaking the
cycle.

Rhodium instead represents the state object directly. A register exposes current
state separately from its single next-state binding. Guarded writes become
enables or muxes; absence of a guarded register write means hold. The clock is
an explicit hardware value, although `sync_circuit` can provide an ambient
clock/reset policy as authoring shorthand.

Neither language performs general latency scheduling. In both, adding a
register changes the authored cycle behavior. Clash's compiler normalizes the
functional program into a netlist, so exact sharing and hierarchy are not the
primary source contract. Rhodium constructs those graph and hierarchy choices
directly. Clash gives a cleaner equational account of time; Rhodium gives a more
literal account of the state elements implementing it.

## Types and intrinsic guarantees

Clash's strongest language-level advantage is the reach of its static types.
`Unsigned n`, `Signed n`, `BitVector n`, `Index n`, and `Vec n a` put widths and
cardinalities in types. Algebraic data types and `BitPack` relate semantic
values to packed representations. Polymorphic functions can quantify over
sizes, element types, and constraints, so one definition can state
relationships among an entire family of circuits.

The domain parameter of `Signal dom a` is equally important. A synthesis domain
records an identity, period, active edge, reset synchrony and polarity, and
initialization behavior. Distinct domain indices cannot be directly confused,
and crossings require an operation whose type explicitly relates the domains.
That is a strong composition guarantee; it does not by itself prove the analog
correctness or metastability behavior of an arbitrary CDC construction.

Rhodium uses elaboration-time hardware type objects such as `Bits(width)`, records,
vectors, signed values, enums, and one-hot selectors. Connections require exact
types, conversions are explicit, and the verifier checks the concrete graph.
Its open capabilities state which operations a type supports without reducing
every type to bits. But widths and clock-domain relationships are not
propositions in a static source type system, and current `Clock` and `Reset`
values carry no domain index.

The difference is not simply “more typing.” Clash proves generic relationships
before netlist construction; Rhodium verifies ownership and realized types after
construction. Clash's inferred types can make a compact definition unusually
powerful, while Rhodium's explicit type objects make the final representation
easy to inspect.

## Locality and predictability

Clash offers semantic locality at the function boundary. A pure function's
result depends only on its arguments, and a transition function can be tested
without procedural update ordering. Strong inference removes annotation noise.
The cost is structural distance: specialization, normalization, inlining, and
sharing decisions stand between a source expression and the eventual netlist.

Rhodium offers structural locality. A `<==` names a destination and its driver;
register creation and hierarchy are explicit; one effective driver is a global
invariant. The cost is syntactic ceremony for transformations that are
mathematically just function composition. Host-generated repetition can also
look less declarative than a typed `map` over `Vec n a`.

Protocol composition exposes another boundary. Rhodium has nominal two-role
interfaces whose directions and identities survive authoring composition.
Clash's base `Signal` does not imply a handshake discipline, but
`clash-protocols` adds a first-class typed `Circuit` layer over it. Rhodium's flow
surface is more specialized and structurally owned; Clash's protocol circuit
surface is more generic and equational.

## Language-level judgment

Clash has the more unified surface language. Pure functions, products,
algebraic data, higher-order structure, and temporal signals form a small set
of orthogonal concepts with unusually high reuse. Its best abstractions feel
like mathematics that happens to synthesize. Domain-indexed signals also make
an important physical distinction statically visible.

Rhodium has the more explicit construction semantics. One final binding per
destination, explicit priority and state boundaries, and dedicated hardware
control form a compact policy for saying exactly which RTL graph should exist.
It preserves intentional structure without depending on normalization; the
particular core representation is secondary to those guarantees.

The systems therefore optimize different forms of elegance. Clash minimizes
the conceptual distance between reusable functions and circuits. Rhodium
minimizes the conceptual distance between source construction and the realized
hardware graph. Their flow abstractions preserve the same divide: Clash gives
the cleaner general composition algebra, while Rhodium gives the clearer exact
elastic topology. Neither should be judged by whether it can manually
reproduce the other's final Boolean network.

## Lessons for Rhodium

1. Make clock domains semantic descriptors rather than unrelated clock/reset
   values. Domain identity and reset policy should compose as one contract.
2. Let generic circuit APIs state relationships among widths and shapes, not
   merely validate each concrete host parameter after entry.
3. Preserve Rhodium's explicit realized types and graph even if richer static
   contracts are added; they answer a different and valuable question.
4. Prefer pure, expression-oriented helpers for combinational transformations.
   Clash shows how much syntax disappears when reusable datapaths denote
   functions instead of construction procedures.
5. Do not adopt `Signal` as decorative terminology. Making time part of a type
   is a semantic decision that requires domain and state semantics.

## Sources

- Rhodium [core semantics](../../rhodium/core/README.md),
  [frontend model](../../rhodium/frontend/README.md), and
  [typed decode relations](../../rhodium/std/README.md#typed-decode-patterns), plus
  [frontend layers](../../rhodium/frontend/layers/README.md)
- [Clash compiler model](https://docs.clash-lang.org/compiler-user-guide/general/index.html)
- [Clash FAQ](https://docs.clash-lang.org/compiler-user-guide/general/faqs.html)
- [Clash Prelude: signals, domains, and state](https://docs.clash-lang.org/compiler-user-guide/developing-hardware/prelude.html)
- [Clash first-circuit tutorial](https://docs.clash-lang.org/tutorial/first-steps/first-circuit.html)
- [Clash `bitPattern`](https://hackage-content.haskell.org/package/clash-prelude-1.8.2/docs/Clash-Sized-BitVector.html)
- [`clash-protocols` circuits, `Df`, and invariants](https://github.com/clash-lang/clash-protocols)
