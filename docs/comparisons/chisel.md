<!-- Compares the core denotation and authoring semantics of Rhodium and Chisel. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rhodium and Chisel

## Scope and thesis

*Snapshot: 2026-08-17.*

This comparison is about the languages' central authoring models. It uses the current
[Chisel documentation](https://www.chisel-lang.org/docs) and
[Chisel 7.14 API](https://www.chisel-lang.org/api/latest/).

Rhodium and Chisel share the same broad denotation: a host-language program runs
to construct synchronous hardware. Chisel presents bound `Data` objects whose
connections accumulate during Scala elaboration. Rhodium's frontend presents a
similarly common `Hardware` surface, while its core IR factors readable
`Value`s from driveable `Place`s. The sharper differences are Rhodium's exact
types, one final binding per destination, explicit conditional priority, and
separate current- and next-state semantics.

## Summary

| Concern | Rhodium | Chisel |
|---|---|---|
| Source denotation | Rhombus evaluation constructs one public core IR | Scala evaluation constructs a hardware graph lowered through FIRRTL/CIRCT |
| Hardware object | Common frontend `Hardware`; core IR factors readable `Value` from driveable `Place` | `Data` object whose binding determines whether it is a type, wire, port, or register |
| Width policy | Positive explicit widths; exact types at operations and connections | Widths may be unknown and inferred; operators and connections have sizing rules |
| Assignment | One effective driver; alternatives become one selected drive | Repeated connections are legal and the last connection wins |
| State | Current value plus a distinct next-state place | `Reg` is read and connected through the usual `Data` surface |
| Hierarchy | Circuit generators create definitions; instances cross explicit ports | Scala `Module` construction creates hierarchy; `RawModule` removes ambient clock/reset |
| Interface relation | Nominal identity, named roles, refinement, and support | Aggregate structure, orientation, `Flipped`, and `Connectable` relations |
| Flow composition | One-shot `InterfaceHandle` topologies compose serially, in parallel, and across cardinality changes | `DecoupledIO`, connections, and components compose through ordinary Scala construction and explicit wiring |
| Patterns and decode | Typed literals and aggregate cubes form validated unordered relations that `DecodeGen` preserves as sparse backend operations | `BitPat` and `TruthTable` form multi-output bit-vector tables; `DecodeTable` organizes structured fields and `decoder` selects a minimizer |
| Primary abstraction tools | Rhombus functions, macros, classes, frontend layers, semantic type capabilities | Scala functions, classes, traits, generics, collections, and compiler-supported `Data` APIs |

## Denotation and staging

Chisel is a Scala library: executing constructors and operators builds a
hardware graph rather than computing hardware results. Its
[introduction](https://www.chisel-lang.org/docs) describes a Scala program as
constructing a circuit graph. Scala `if`, loops, collections, and methods run
during elaboration; `when`, `Mux`, and Chisel operators construct runtime
hardware.

Rhodium makes the same phase distinction with Rhombus. Ordinary Rhombus control
and data choose generated structure, while `when`, `switch`, and hardware
operators construct circuit behavior.

After elaboration, both graphs denote concurrent hardware. Host statement
order does not schedule operations across cycles; it matters only where an API
uses elaboration order to define structure or connection priority.

The object models differ more sharply. Chisel distinguishes a *Chisel type*
from hardware bound to a circuit, but both are represented by `Data` objects.
Binding supplies direction and location, and some invalid combinations are
therefore diagnosed when the Scala program elaborates rather than by Scala's
type checker. The official
[Chisel type versus Scala type guide](https://www.chisel-lang.org/docs/explanations/chisel-type-vs-scala-type)
documents this distinction.

Rhodium host descriptions and type objects are not circuit values. Frontend
profiles expose one broad `Hardware` vocabulary, then read and drive contexts
select the appropriate facet. In the resulting [core IR](../../rhodium/core/README.md),
a `Value` belongs to a module and denotes a readable result, while a `Place`
denotes a legal destination. This source/sink factoring makes the completed
graph explicit, but Chisel could enforce comparable capabilities through the
binding state of its unified `Data` objects; the two-class representation is
not what determines the languages' expressiveness.

## Expressions, types, and widths

Rhodium operations consume complete hardware types. `Bits(8)`, `SInt(8)`, a
particular enum, a one-hot control, and an eight-bit packed record remain
distinct even when their representations have the same width. Ordinary
arithmetic is modular and preserves its declared result type. Growth,
truncation, and equal-width representation casts are requested explicitly.
Open core capabilities determine which operations a frontend-defined type may
support.

Chisel provides `Bool`, `UInt`, `SInt`, clocks, resets, `Vec`, `Bundle`, enums,
and user-defined aggregate types. Widths can be explicit or initially unknown;
the compiler solves width constraints using the documented
[width-inference rules](https://www.chisel-lang.org/docs/explanations/width-inference).
Operators encode intent through variants such as modular `+` and expanding
`+&`. This is concise for parameterized arithmetic because intermediate widths
can follow the expression, but a reader may need operator and connection rules
to recover the exact result width.

Connection syntax also reflects Chisel's preference for contextual
composition. General connections can pad or truncate according to Chisel's
rules. The newer
[`Connectable`](https://www.chisel-lang.org/docs/explanations/connectable)
operators are more explicit about aligned and flipped members and reject a
potentially truncating aligned connection unless the author opts into
squeezing. Rhodium instead requires exact type equality at the connection itself;
adaptation is a separate expression.

Rhodium's nominal one-hot and enum types preserve control intent through the IR.
Chisel can encode the same hardware with `ChiselEnum`, wrappers, `Mux1H`, or a
project type, but its ground `Data` operations are the more general building
blocks. The tradeoff is semantic specificity versus a smaller number of
widely composable primitives.

## Typed literals, patterns, and relational decode

Rhodium's [typed decode layer](../../rhodium/std/README.md#typed-decode-patterns)
builds on `HardwareLiteral`: an exact, typed host value for scalars,
extensions, records, and vectors. `Pattern` then describes a typed cube rather
than a runtime value, so it can leave recursively nested input or output fields
unconstrained. A `DecodeTable` is an unordered finite relation: overlapping
input cubes are rejected, partial outputs supply optimization freedom, and
ordinary host operations can extend rows, lift a relation into a wider input,
or zip independently authored output relations on their shared input cubes.
`ValidDecodeGen` adds validity as part of the same partial relation.

Chisel is the close technical peer, not a counterexample that Rhodium must
outgrow. Its [decoder API](https://www.chisel-lang.org/docs/explanations/decoder)
uses `BitPat` and `TruthTable` for bit-vector cubes with input and output
don't-cares. `DecodePattern`, `DecodeField`, `DecodeTable`, and `DecodeBundle`
also support an instruction-like host description whose independently defined
fields become a decode result. Chisel's public minimizers handle a multi-input,
multi-output truth table, using Espresso when available or QMC as an
alternative. Rhodium preserves the same essential Boolean-optimization opportunity
as sparse CaseZ logic for target-aware downstream synthesis.

The authoring difference is where the relation lives. Chisel associates a
structured host pattern and output fields with a packed `BitPat` table; its
field-oriented API is concise when a decoder grows one output column at a
time. Rhodium makes the typed input and partially cared aggregate output cubes
the relation itself. This makes sparse aggregate controls, semantic-type
checking, and explicit input/output relation composition direct, without
requiring a separate packed encoding. Both decoder APIs treat their tables as
relations rather than priority-ordered cases; Rhodium additionally rejects every
pair of distinct overlapping input cubes. Conversely, Rhodium currently has only
exact-cube zipping despite its host-side `PatternSet` algebra; it does not
automatically refine nonidentical input partitions while composing relations.

Multi-output minimization can share product terms across output fields and
exploit unconstrained outputs. Chisel performs an explicit host-side
minimization, while Rhodium emits one sparse CaseZ relation whose X-valued output
positions remain available to downstream RTL synthesis. Neither approach is a
portable PPA guarantee: optimization quality depends on the selected minimizer,
target, and synthesis flow. Rhodium's advantage is therefore more precise source
meaning and preserved optimization freedom, not an inherent hardware-quality
ceiling above Chisel.

## State, assignment, and priority

Rhodium treats connection as one final binding. Hardware conditionals collect
alternatives and lower them to a mux, enable, or guard followed by that single
drive. Priority is therefore attached to `when`/`else` structure, not to
unrelated statement order. A register's read context selects current state and
its drive context selects next state; the core represents those as a value and
a place. An uncovered next-state branch means hold, while a combinational
destination must be covered.

Chisel uses repeated connection as a control idiom. The normal rule is that
the last connection to a sink wins, including connections nested under
`when`. A default followed by conditional overrides is compact and familiar,
and source order deliberately expresses priority. The same economy means that
moving a connection can change the selected driver without changing the local
expression at the sink. Chisel's
[probe documentation](https://www.chisel-lang.org/docs/explanations/probes)
contrasts ordinary last-connect hardware with probes, which require one
definition.

Registers fit naturally into each model. Chisel `Reg`, `RegInit`, and
`RegEnable` use the ordinary expression/connection vocabulary and a `Module`'s
implicit clock and reset unless a more explicit form is selected; see the
[sequential-circuit guide](https://www.chisel-lang.org/docs/explanations/sequential-circuits).
Rhodium registers carry an explicit clock and optional synchronous active-high
reset in core, with `sync_circuit` providing ambient convenience. Chisel's
unified syntax is economical and can still enforce disciplined state updates;
Rhodium's separate current/next semantics make the state boundary directly
visible in the completed graph.

## Hierarchy, interfaces, and composition

Chisel hierarchy follows Scala object construction. A class extending
`Module` defines hardware, and `Module(new Child(...))` creates a child
instance. Ordinary modules receive implicit clock and reset; `RawModule`
removes them. Ports are bound `Data`, usually grouped in `Bundle`s. This model
is direct and works naturally with Scala constructor parameters and object
composition. The
[module guide](https://www.chisel-lang.org/docs/explanations/modules) explains
the hierarchy and binding rules.

An Rhodium `circuit` call during elaboration creates a module definition, and an
instance is a separate IR object with input places and output values. Hardware
crosses generator boundaries through ports; host values may be captured while
constructing definitions. Explicit binding can reuse one definition. This
separates definition generation from instantiation more visibly than Chisel's
constructor idiom.

For aggregate connection, Chisel combines `Bundle`, member orientation,
`Flipped`, and `Connectable`. Compatibility is principally structural and
relative to member alignment, with explicit operators for directional or
bidirectional bulk connection. Rhodium interfaces are nominal protocol
descriptors: they name two roles, orient members, and can refine or support
other interface contracts. Linear handles add a single-consumption discipline
for topology-building APIs. Chisel's model adapts structural aggregates more
freely; Rhodium's model can state that two similarly shaped interfaces mean
different protocols or that one nominal protocol supports another.

## Ready-valid flow composition

Chisel's standard [`ReadyValidIO` and `DecoupledIO`](https://www.chisel-lang.org/docs/explanations/interfaces-and-connections)
give ready-valid channels a uniform signal vocabulary, and utilities such as
`Queue` and the arbiters package common transport structures. These pieces
compose through ordinary module construction, `Bundle` connection, or a
project-defined Scala function. The standard abstraction does not make a
whole serial or branching topology one uniform value: the author normally
creates each component and wires the intermediate `DecoupledIO` objects.

Rhodium's standard flow layer treats topology itself as a composable value.
`source |> queue(4) |> pipe(2)` is serial composition; `parallel` composes
independent branches; arbiters, demultiplexers, forks, joins, and zips change
cardinality in the same notation. A path may begin with a payload or protocol
type and remain disconnected until later. The result is a linear, one-shot
`InterfaceHandle`, while each configured stage function remains reusable.
Dependent static information preserves whether the current result is an
endpoint, endpoint array, or open handle.

This is a frontend interpretation of Rhodium's generic interface mechanism, not
a flow graph added to core IR. Pure maps, filters, gates, and routing adapters
stay inline; stages with storage or intentional structure remain module
instances. Verification then checks the realized graph, including
combinational cycles that cross instances.

Rhodium also distinguishes `Valid`, `Decoupled`, `Irrevocable`, and credited
transport, so a stage can explicitly preserve, weaken, or strengthen its
contract. Chisel's `IrrevocableIO` is likewise a convention rather than an
automatically enforced property. Rhodium's nominal distinction is more expressive
but does not itself prove temporal behavior: it generates no general stability
assertions. The flow syntax is therefore substantially more compositional than
Chisel's standard ready-valid surface, while its strongest temporal label still
depends partly on author discipline.

## Abstraction, locality, and predictability

Chisel inherits Scala's mature abstraction vocabulary. Higher-order
functions, collections, inheritance, traits, implicit parameters, and generic
classes can all generate hardware. Libraries can also wrap module creation in
an `apply` method so a component looks like a function, as shown in the
[functional module creation guide](https://www.chisel-lang.org/docs/explanations/functional-module-creation).
This yields very compact generators, although the meaning of a `Data` value may
depend on binding, direction, inferred width, and prior connections.

Rhodium inherits Rhombus functions, classes, pattern matching, macros, and
language composition. Frontend layers may add surface syntax and semantic
types, but hardware operations still end in the same public IR.
Pure combinational helpers can return values without introducing hierarchy;
stateful or intentionally structural boundaries remain circuits. Exact-width
connections, one final binding, and explicit priority make the resulting
circuit locally reconstructable. The internal `Value`/`Place` factoring helps
the verifier express those rules directly.

## Language-level judgment

Chisel is cleaner when syntactic economy and fluent generator construction are
the priority: one `Data` vocabulary, contextual widths, and ordered connections
make common RTL compact. Rhodium's frontend is also intentionally uniform; its
language-level advantage is not that core uses two object classes, but that
exact result types, one final binding, explicit priority, and current/next
state remain straightforward to recover. For ready-valid networks, Rhodium also
has the more coherent topology abstraction: the same expression covers direct
connection, detached path construction, parallel structure, and cardinality
changes. Chisel's host abstractions are broader and smoother; Rhodium's ordinary
denotation is smaller and less dependent on replaying surrounding construction
and connection order.

## Lessons for Rhodium

1. Keep exact types and one-final-binding semantics. Retain `Value`/`Place` in
   core while it remains the clearest internal source/sink factoring, without
   requiring that distinction to burden ordinary authoring.
2. Add concise authoring forms only when they still elaborate to one visible
   driver and do not make priority depend on ambient statement order.
3. Preserve the separation between inline value composition and intentional
   module hierarchy while making reusable-definition syntax economical.
4. Keep semantic type and protocol invariants visible in ordinary authoring,
   rather than relying on project conventions over raw aggregates.
5. Make every advertised flow-strength preservation true by construction or
   conservatively weaken the result; nominal `Irrevocable` should not outpace
   what an adapter can establish.

## Sources

- [Chisel documentation](https://www.chisel-lang.org/docs)
- [Chisel types versus Scala types](https://www.chisel-lang.org/docs/explanations/chisel-type-vs-scala-type)
- [Chisel width inference](https://www.chisel-lang.org/docs/explanations/width-inference)
- [Chisel modules](https://www.chisel-lang.org/docs/explanations/modules)
- [Chisel sequential circuits](https://www.chisel-lang.org/docs/explanations/sequential-circuits)
- [Chisel interfaces and connections](https://www.chisel-lang.org/docs/explanations/interfaces-and-connections)
- [Chisel `Connectable`](https://www.chisel-lang.org/docs/explanations/connectable)
- [Chisel decoders](https://www.chisel-lang.org/docs/explanations/decoder)
- [Chisel experimental decode API](https://www.chisel-lang.org/api/latest/chisel3/util/experimental/decode/index.html)
- [Rhodium flow composition](../../flow/README.md#component-catalog)
- [Rhodium typed decode patterns](../../rhodium/std/README.md#typed-decode-patterns)
- [Rhodium core semantics](../../rhodium/core/README.md)
- [Rhodium frontend semantics](../../rhodium/frontend/README.md)
- [Rhodium frontend layers](../../rhodium/frontend/layers/README.md)
