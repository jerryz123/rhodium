<!-- Compares the core denotation and authoring semantics of Rhodium and Hardcaml. -->

# Rhodium and Hardcaml

## Scope and thesis

*Snapshot: 2026-08-17.*

This comparison uses the current Hardcaml manual and
[Hardcaml 0.17.1 API](https://ocaml.org/p/hardcaml/latest/hardcaml/Hardcaml/index.html).
It focuses on the language's structural RTL semantics.

Hardcaml and Rhodium are unusually close architectural comparisons. Both execute
a functional host language to build an explicit graph, expose that graph for
inspection, use exact vector widths for primitive operations, and avoid an
implicit hardware scheduler. Hardcaml's most elegant abstraction is a shared
combinational signature implemented by both concrete `Bits.t` values and deep
`Signal.t` expressions. Rhodium's frontend is also broadly uniform, while its core
factors readable `Value` from driveable `Place`. That factoring usefully names
sources and sinks, but the stronger distinction is Rhodium's complete semantic
types, one final binding, explicit priority, and current/next-state semantics.

## Summary

| Concern | Rhodium | Hardcaml |
|---|---|---|
| Source denotation | Rhombus evaluation constructs one public core IR | OCaml evaluation constructs a `Signal.t` graph; a `Circuit.t` is traced from outputs |
| Expression model | Common frontend hardware surface; core factors typed `Value` nodes from `Place` destinations | Deep `Signal.t` expressions sharing `Comb.S` with shallow `Bits.t` values |
| Width/type policy | Exact complete hardware types; explicit conversion | Exact vector widths checked while constructing; signedness usually belongs to the chosen operator |
| Assignment | One effective driver per place; conditional alternatives become one drive | Ordinary wires accept one driver; `Always` variables use defaults and last-executed assignment priority |
| State | Current value and next-state place; explicit clock | `Signal.reg` and memories are graph nodes configured by `Reg_spec` |
| Hierarchy | Circuit definitions and explicit instances | Circuits formed from named outputs; scopes can flatten calls or record module instances |
| Interface relation | Nominal roles, refinement, support, and linear handles | PPX-derived polymorphic records carrying field names and widths |
| Flow composition | Linear interface topologies with protocol-aware serial, parallel, and cardinality-changing stages | A separate handshake library supplies pure typed arrows and serial composition |
| Patterns and decode | Typed aggregate cubes form validated unordered relations and preserve partial output care | `Signal` muxes and enum matches construct selection; there is no comparable standard masked relation |
| Primary abstraction tools | Rhombus functions, macros, classes, language layers, type capabilities | OCaml functions, modules, functors, records, PPX derivation, and shared module signatures |

## Denotation and staging

Hardcaml is an OCaml hardware construction library. Executing the OCaml
program creates a graph of `Signal.t` nodes; it does not execute the circuit.
The manual's
[quick overview](https://docs.hardcaml.org/hardcaml-docs/introduction/quick_overview/)
describes `Signal.t` as a deep embedding that records computation. A circuit is
created by naming outputs and tracing their dependencies back to inputs,
registers, memories, constants, and instantiations.

Host computation and hardware computation use different OCaml types. OCaml
integers, booleans, lists, functions, and modules generate structure.
`Signal.t` operators, muxes, registers, and the `Always` DSL construct runtime
hardware. No scheduling pass turns an algorithm into cycles; registers and
memories are explicit in the source graph.

The finished signal graph denotes concurrent hardware. OCaml evaluation order
constructs dependencies; it becomes circuit priority only inside an explicitly
ordered abstraction such as `Always`.

Rhodium has the same broad staging model. Rhombus computation chooses structure,
while hardware forms create operations inside an active circuit. Its completed
denotation is a module-owned [core IR](../../rhodium/core/README.md) containing
ports, values, places, operations, registers, memories, instances, and effects.
The verifier checks that graph before any later interpretation consumes it.

Hardcaml's graph is naturally expression-rooted: `Circuit.create_exn` discovers
what is reachable from outputs. Rhodium's builder records the whole module as it
is constructed, including driveable destinations and effects. The former is a
compact denotation for pure dataflow; the latter makes ownership and effectful
hardware part of the first-class structure.

## Expressions, types, and widths

Hardcaml's fundamental value is a finite bit vector with a width. `Bits.t`
computes with concrete vectors; `Signal.t` records the same operations as graph
nodes. Both implement the combinational `Comb.S` signature, so one OCaml
functor or higher-order function can describe a datapath once and interpret it
as either computation or hardware construction. This shallow/deep symmetry is
a particularly strong form of executable specification.

Primitive operators have precise width rules checked while the OCaml program
runs. Addition and subtraction require equal-width operands and preserve that
width; multiplication returns the sum of operand widths; selection,
concatenation, mux, and comparison have similarly explicit rules. Resizing is
requested directly. The
[combinational-logic guide](https://docs.hardcaml.org/hardcaml-docs/designing-circuits/combinational_logic/)
documents the complete model.

Signedness is usually not stored in the vector itself. Instead, signed and
unsigned operator names choose the interpretation. `Typed_math` modules can
wrap that policy and support mixed-width arithmetic with growing results. This
keeps the core vector type uniform, but the meaning of a comparison or resize
must be recovered from the selected operation or module.

Rhodium attaches a complete semantic type to every value. Raw `Bits`, `SInt`,
`Bool`, enums, one-hot controls, records, and vectors remain distinct. Core
capability interfaces determine whether a type supports packing, bitwise
operations, modular arithmetic, or signed arithmetic. Width-changing and
representation-changing operations remain explicit.

Both languages therefore make primitive datapath widths predictable.
Hardcaml's uniform vectors maximize reuse through `Comb.S`; Rhodium's nominal and
structural hardware types maximize preservation of author intent after the
host abstraction has disappeared.

## Typed literals, patterns, and relational decode

Hardcaml's `Comb.S` supplies constants, comparisons, muxes, priority selectors,
and enum-specific `match_` helpers, so an ordinary decoder composes naturally
as a `Signal.t` expression. The [enum support](https://docs.hardcaml.org/hardcaml-docs/using-interfaces/enums_in_hardcaml/)
also gives an exhaustive algebraic-case surface when the selector is an enum.
It does not provide a standard object equivalent to a masked, multi-output
decode relation that preserves partial output specifications for later
minimization.

Rhodium's [typed decode layer](../../rhodium/std/README.md#typed-decode-patterns)
makes exact typed literals and recursive aggregate cubes reusable host data.
A nonoverlapping relation can be assembled from independently authored rows or
output fragments, then passed once to `DecodeGen`. This is more concise for a
large control decoder whose meaningful outputs vary per instruction; Hardcaml's
ordinary functional composition is more general for a decoder interleaved with
arbitrary Boolean or arithmetic computation.

Rhodium preserves the combined sparse relation and its output don't-cares for
downstream synthesis instead of constructing separate mux and comparison
trees. Rhodium does not run a Boolean minimizer itself. Hardcaml's graph and a
downstream synthesizer can realize the same function differently, and
target-specific optimization decides the final PPA.

## State, assignment, and priority

Most Hardcaml construction is functional: operators return new `Signal.t`
nodes. A `Signal.wire` is the deliberate exception. It is created before its
driver is known, allowing feedback to pass through a register, and later
receives one driver with `<==`. Assigning it twice or assigning a value of a
different width raises during construction. Circuit creation also detects
unassigned wires and, by default, combinational cycles. The
[sequential-logic guide](https://docs.hardcaml.org/hardcaml-docs/designing-circuits/sequential_logic/)
uses this pattern to define register feedback.

For control-heavy logic, Hardcaml provides a second authoring mode. The
[`Always` DSL](https://docs.hardcaml.org/hardcaml-docs/more-on-circuit-design/always/)
creates wire or register variables with defaults, then accepts assignments,
`if_`, and `switch` statements. `Always.compile` converts the program to muxes
and registers. Within that procedural description, the last assignment
executed determines the next value. Priority is therefore local to the ordered
`Always` program rather than to ordinary signal wiring.

Rhodium uses one assignment model for both straightforward and conditional
construction. Every destination has one final binding. `when` and `switch`
capture alternatives and emit one selected drive; a missing register branch
means hold, while combinational destinations require full coverage. This is
close to Hardcaml's one-driver wires after `Always` has compiled. The important
difference is when priority becomes explicit, not whether the destination is
represented by a separate object class.

Hardcaml state is configured by `Reg_spec`, which groups clock, edge, reset,
clear, enable, and associated values. `Signal.reg` consumes a spec and a data
input. Rhodium gives a register an explicit `Clock`, distinct current- and
next-state semantics, and an optional synchronous reset value; core represents
current as a value and next as a place, while its `sync_circuit` layer supplies
ambient convenience. Hardcaml centralizes state policy in a reusable record;
Rhodium makes the temporal direction directly inspectable in the graph.

## Hierarchy, interfaces, and composition

A common Hardcaml component is an OCaml function from an input record of
signals to an output record. Turning the outputs into a `Circuit.t` creates a
module-level object. `Scope` and `Hierarchy.In_scope` can call such a function
directly for a flattened design or elaborate it separately and insert an
instantiation. The
[module-hierarchy guide](https://docs.hardcaml.org/hardcaml-docs/using-interfaces/module_hierarchies/)
therefore treats hierarchy as a selectable interpretation of the same
component function.

Rhodium separates those choices at the authoring boundary. A pure function over
values is inline by construction. A `circuit` call creates a module definition,
and an explicit instance creates hierarchy; binding can reuse a definition.
This is less interchangeable than Hardcaml's scope-controlled interpretation,
but it makes structural intent stable at the call site.

Hardcaml interfaces are polymorphic OCaml records whose fields carry names and
widths. `ppx_hardcaml` derives mapping, folding, packing, port creation, wire
assignment, register, and naming operations over the record. Separate input
and output interface modules plus a conventional `create` function provide a
concise, statically typed component signature. The official
[interface guide](https://docs.hardcaml.org/hardcaml-docs/using-interfaces/hardcaml_interfaces/)
shows how the same record shape is reused across construction and circuit
access.

Rhodium interfaces describe directional protocols rather than only port records.
They have nominal identity, two named roles, nested members, refinement, and
declared support relations. Linear handles can restrict topology values to one
consumption. Hardcaml's record functors are more general for uniform host
operations over fields; Rhodium's interface semantics carry more information
about compatibility and direction between endpoints.

## Ready-valid flow composition

The separate
[`hardcaml_handshake`](https://github.com/janestreet/hardcaml_handshake/blob/master/src/handshake.mli)
library gives Hardcaml an unusually principled answer to composition. A
handshake circuit is a typed reusable arrow; pure lifting with `arr` and serial
composition with `>>>` obey an ordinary functional shape. Unlike a mutable
connected endpoint, the arrow value can be named, reused, and composed before
it is interpreted as signals. On this narrow axis, its abstraction is purer
than Rhodium's linear topology handle.

Rhodium's flow language carries considerably more hardware semantics in the
composed value. Configured unary stages apply through `|>` to either concrete
endpoints or detached protocol seeds. `parallel` forms products, and arbiters,
demultiplexers, forks, joins, and zips change cardinality while preserving the
same notation and dependent endpoint shape. The resulting
`InterfaceHandle` is intentionally one-shot, although the configured function
that creates it is reusable. This trades the equational reuse of a pure arrow
for explicit ownership of a physical topology.

Rhodium also tracks `Valid`, `Decoupled`, `Irrevocable`, and credited protocols,
keeps pure transformations inline and stateful stages as modules, and checks
the realized graph for combinational cycles across hierarchy. None of this
requires flow-specific core IR; the abstraction erases through generic
interfaces. The Hardcaml arrow is the more elegant skeleton, while Rhodium is the
more complete language for actual elastic topology and protocol-strength
changes.

Rhodium's richer protocol description is not itself a temporal proof:
`Irrevocable` currently lacks generated stability assertions. A pure typed
arrow gives composition laws about shape rather than temporal correctness too,
but Hardcaml's design is a useful reminder to distinguish algebraic structure
from the properties established by an implementation.

## Abstraction, locality, and predictability

Hardcaml uses OCaml's module system unusually well. Functions and functors can
be parameterized over `Comb.S`, interfaces can be abstracted behind module
signatures, and PPX derivation removes repetitive record traversal. The same
datapath can often be reused for concrete evaluation, signal construction, or
a customized implementation simply by changing a module argument.

Its local expression semantics are compact: every signal exposes one vector
API and every primitive has a stated width rule. Semantic distinctions such as
signedness or protocol meaning live in the chosen operator, module signature,
or record abstraction rather than in each graph node's base type. Those
abstractions are strong while present in OCaml, but some distinctions are
erased in the underlying signal graph.

Rhodium uses Rhombus functions, classes, macros, and language layers, and presents
readable and driveable hardware through a common frontend surface. Core's
source/sink factoring helps verification. More consequentially, a complete
semantic type remains attached to each core value, and nominal interface
relations are checked before endpoints lower to records and ports. This makes
the resulting hardware easier to interpret without its source abstraction, at
the cost of less uniform reuse across bit-vector meanings.

## Language-level judgment

Hardcaml has the more elegant expression-level abstraction: the same
combinational program can run shallowly over `Bits.t` or elaborate deeply over
`Signal.t`, and its unified signal vocabulary still supports disciplined
one-driver wires. Its handshake arrow is also the purer reusable serial
composition value. Rhodium is cleaner after elaboration where complete semantic
types, one final binding, explicit priority, current/next state, modules, and
the ownership and strength of an elastic topology should remain directly
inspectable. Hardcaml optimizes reuse across interpretations; Rhodium optimizes
preservation of hardware intent through interpretation.

## Lessons for Rhodium

1. Preserve semantic types and one-final-binding semantics. Keep `Value` and
   `Place` as an internal factoring while useful, but learn from Hardcaml's
   principled unified combinational signature and shallow/deep reuse.
2. Keep hierarchy intentional while making it easy to choose between an inline
   helper and a reusable module definition from one implementation body.
3. Derive generic traversal, packing, and naming views from protocol member
   declarations rather than hand-writing parallel record machinery.
4. Maintain one-driver semantics even if a more procedural control notation is
   added; compile ordered syntax to one explicit selected drive.
5. Preserve the richer flow semantics, but learn from `hardcaml_handshake`'s
   pure arrow: reusable topology descriptions should be distinct from their
   one-shot materialization.

## Sources

- [Hardcaml manual](https://docs.hardcaml.org/)
- [Hardcaml quick overview](https://docs.hardcaml.org/hardcaml-docs/introduction/quick_overview/)
- [Hardcaml combinational logic](https://docs.hardcaml.org/hardcaml-docs/designing-circuits/combinational_logic/)
- [Hardcaml enums and `match_`](https://docs.hardcaml.org/hardcaml-docs/using-interfaces/enums_in_hardcaml/)
- [Hardcaml sequential logic](https://docs.hardcaml.org/hardcaml-docs/designing-circuits/sequential_logic/)
- [Hardcaml `Always` DSL](https://docs.hardcaml.org/hardcaml-docs/more-on-circuit-design/always/)
- [Hardcaml interfaces](https://docs.hardcaml.org/hardcaml-docs/using-interfaces/hardcaml_interfaces/)
- [Hardcaml module hierarchy](https://docs.hardcaml.org/hardcaml-docs/using-interfaces/module_hierarchies/)
- [Hardcaml Handshake interface](https://github.com/janestreet/hardcaml_handshake/blob/master/src/handshake.mli)
- [Hardcaml API](https://ocaml.org/p/hardcaml/latest/hardcaml/Hardcaml/index.html)
- [Rhodium flow composition](../../flow/README.md#component-catalog)
- [Rhodium typed decode patterns](../../rhodium/std/README.md#typed-decode-patterns)
- [Rhodium core semantics](../../rhodium/core/README.md)
- [Rhodium frontend semantics](../../rhodium/frontend/README.md)
- [Rhodium frontend layers](../../rhodium/frontend/layers/README.md)
