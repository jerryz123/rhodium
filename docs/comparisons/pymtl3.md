<!-- Compares Rhodium with PyMTL3 across embedded RTL syntax and multi-level modeling semantics. -->

# Rhodium and PyMTL3

## Scope and thesis

Snapshot: 2026-08-17. This comparison uses the checked-in Rhodium implementation
and PyMTL3's current official documentation and primary repository.

At the synthesizable RTL level, Rhodium and PyMTL3 are close peers: both execute a
host language to elaborate hierarchy and both describe cycle-accurate hardware
rather than asking an HLS scheduler to invent a pipeline. Their semantic
centers differ. Rhodium elaborates into one explicit hardware graph with exact
types, final connectivity, and explicit state boundaries. PyMTL3 elaborates an
executable Python component model whose concurrent update blocks are scheduled
for simulation and translated from a defined Python RTL subset.

PyMTL3 also supports functional-level and cycle-level models. That wider
modeling vocabulary matters for composition and syntax, but it should not be
mistaken for automatic refinement: an FL or CL component is a different model,
not a high-level program that PyMTL3 necessarily synthesizes into an RTL
component. Rhodium is narrower and more explicit; PyMTL3 is more uniform across
executable modeling levels.

## Summary

| Concern | Rhodium | PyMTL3 |
|---|---|---|
| Semantic unit | Static hardware module in a verified dataflow IR | Executable Python `Component` with connectivity and update blocks |
| Staging | Host forms generate structure; hardware forms construct graph nodes | `construct` elaborates; decorated blocks describe model behavior |
| Time | Explicit register and memory operations | `@update_ff` state transition plus simulator ticks; CL methods can model cycle timing |
| Control | Hardware `when` / `switch` lower to muxes and guards | Python `if` / `for` in a decorated, translatable block |
| Types | Exact semantic hardware types and explicit conversions | Concrete `BitsN`, `BitStruct`, ports, wires, and Python values |
| Patterns and decode | Typed aggregate cubes form validated unordered relations with sparse outputs | Update-block conditions and host-generated tables construct ordinary comparison and selection logic |
| Composition | Modules plus nominal two-role interface descriptors | Components plus hierarchical signal or method interfaces |
| Flow composition | One-shot topology values compose serial, parallel, and cardinality-changing stages | Ready-valid interfaces and components compose through explicit construction and wiring |
| Scheduling | Source fixes every sequential boundary | RTL source fixes boundaries; simulator schedules concurrent update blocks |
| Syntactic center | Dedicated circuit forms inside Rhombus | Familiar Python statements under decorators and overloaded operators |

## Denotation and staging

Rhodium is a deep embedding in Rhombus. Ordinary Rhombus values decide widths,
module structure, and generator control. Circuit expressions and connections
build the same [core IR](../../rhodium/core/README.md) from every frontend profile.
Within that IR, `Value` and `Place` factor computed results from destinations
that receive bindings.

PyMTL3 runs a component's `construct` method to create children, ports, wires,
interfaces, connections, and update blocks. An `@update` block denotes
combinational behavior; an `@update_ff` block denotes edge-triggered state
updates. Simulation passes derive an execution schedule for those concurrent
blocks. The [quick start](https://pymtl3.readthedocs.io/en/latest/intro/quickstart.html)
therefore reads like ordinary executable Python while still denoting hardware
inside the decorators.

Translation introduces a second boundary. Only the documented
[translatable RTL subset](https://pymtl3.readthedocs.io/en/latest/ref/passes-translation-intro.html)
of Python types, expressions, statements, loops, helper calls, and update
blocks denotes synthesizable RTL. A model can be valid Python and executable in
PyMTL3 without belonging to that subset. Rhodium's dedicated circuit forms map
directly to graph construction, while PyMTL3's executable Python surface is
broader and its synthesizability more contextual.

PyMTL3's functional and cycle-level models use the same component framework to
express less structural behavior, including method-level transactions. Their
relationship to an RTL implementation is supplied by the designer and tests,
not by an automatic FL-to-RTL or CL-to-RTL lowering. Rhodium currently standardizes
only the exact RTL denotation.

## Types and intrinsic guarantees

Rhodium requires exact hardware types at operations and connections. `Bits`,
`Bool`, `SInt`, enums, `OneHot`, clocks, resets, records, and vectors can remain
distinct despite equal packed widths. Extension, truncation, and
representation casts are explicit. Its core `Value` and `Place` classes make
definition/use and destination binding convenient to verify, but this is an IR
factoring rather than a distinctive type-system capability.

PyMTL3's [`Bits` types](https://pymtl3.readthedocs.io/en/latest/ref/datatypes.html)
are fixed-width concrete values usable both in models and tests. `BitsN`
operations have defined width behavior, implicit truncation is rejected, and
some Python-integer or narrower-value contexts are inferred or zero-extended.
`BitStruct` gives packed fields a named aggregate type, while explicit helpers
cover concatenation, extension, and truncation.

PyMTL3 gains syntactic continuity from using the same concrete values during
execution, message construction, and RTL modeling. Rhodium represents circuit
expressions as graph-owned symbolic values. Its nominal protocol identity also
lives outside the packed data type, while a PyMTL3 interface is primarily a
Python composition object containing signals or methods.

## Typed literals, patterns, and relational decode

Rhodium applies one typed abstraction across exact constants and decoder tables.
A `HardwareLiteral` is the exact packed image of any supported packable type,
including records, vectors, and extension-defined types. A `Pattern` adds a
recursive care mask: named aggregate fields can be constrained, partially
constrained, or left unconstrained without flattening the author's type. The
same pattern representation describes selector regions and partially specified
outputs.

`DecodeTable` then treats rows as an unordered relation, requires exact common
input and output types, and rejects overlapping input patterns rather than
giving source order an accidental priority meaning. Tables can be extended by
list concatenation subject to overlap validation, lifted into a wider input
type, and zipped across independently authored output relations. `DecodeGen`
preserves the combined sparse relation and its output don't-cares in one
backend operation.

PyMTL3 has excellent concrete `BitsN` and `BitStruct` values, but its standard
synthesizable vocabulary has no corresponding typed partial-pattern and
decode-relation abstraction. A decoder is normally written as Python
comparisons, conditions, and assignments in an update block, or generated from
Python data. That is more general for computed predicates and explicitly
prioritized behavior; Rhodium is more concise and analyzable for an unordered
finite relation.

Both can generate equivalent Boolean logic, and a downstream synthesis tool
may optimize either form well. Rhodium's advantage is that the complete relation
and its don't-care freedom remain explicit inputs to downstream optimization
rather than facts a translator must recover from procedural RTL. Rhodium itself
does not choose a cover, so this is not a universal area, timing, or power
advantage.

## Time, state, and scheduling

PyMTL3 groups combinational behavior into `@update` blocks. Ordinary Python
assignments to local temporaries and Python `if` or bounded `for` statements
provide behavioral RTL syntax; `@=` updates combinational signals. `@update_ff`
and `<<=` state the next value installed at a clock step. Component clock and
reset signals supply the conventional temporal context.

Those blocks are concurrent even though Python executes them sequentially in a
derived simulation schedule. Dependencies between reads and writes, plus any
explicit scheduling constraints, determine a valid order. The schedule is an
execution technique for a cycle-accurate model; it does not authorize the RTL
translator to move logic across an `@update_ff` boundary.

Rhodium removes procedural block scheduling from its hardware denotation.
Combinational operations form a dataflow graph, operation listing order has no
runtime meaning, and pure combinational cycles are rejected. A register has an
explicit current value and next-state binding. Conditional assignments are
captured with explicit priority and become one final muxed binding; effects
receive explicit guards.

Thus both RTL languages preserve author-chosen cycles, but they make
combinational intent differently local. PyMTL3 presents a process body that is
convenient for algorithms and direct execution. Rhodium presents use/definition
and destination ownership relationships that are convenient for inspecting
the constructed circuit.

## Core composition unit and interfaces

PyMTL3 `Component`s compose hierarchically. Signal-level `Interface` objects
can contain ports, nested interfaces, arrays, and messages, and `//=` expresses
structural connections. At higher modeling levels, method ports and
method-based interfaces can represent transactions without committing to an
RTL handshake bundle.

Rhodium separates hierarchy from protocol description. Modules own physical
ports and instances. A frontend interface descriptor adds nominal identity,
two complementary named roles, nested directions, refinement, supported
contracts, and parameter compatibility. Its endpoint lowers to ordinary
records and ports; linear handles can additionally require a ready-valid
topology value to be consumed once during elaboration.

PyMTL3's structural interface objects make it easy to keep a recognizable
component shape across FL, CL, and RTL models. Rhodium's nominal descriptors make
same-shaped but semantically different RTL protocols noninterchangeable. The
former favors model substitution by convention; the latter favors static
protocol identity after physical signals have been chosen.

## Ready-valid flow composition

PyMTL3's standard stream interfaces provide conventional ready-valid message
ports, and stream queues and other components can share that shape. Composition
remains ordinary PyMTL3 hierarchy construction: instantiate components, place
them on the parent, and connect each intermediate interface with `//=`. Python
functions can package the pattern, but the RTL stream interface itself is not a
uniform serial, parallel, or branching topology value. PyMTL3's method-level
interfaces provide a higher-level transaction abstraction for CL models, but
that is a different denotation rather than a ready-valid RTL algebra.

Rhodium makes the RTL topology itself composable. A configured stage is an
ordinary unary function used with `|>`; starting with a payload or protocol
type creates a detached `InterfaceHandle`, while starting with an endpoint
connects immediately. `parallel` composes independent branches and the same
notation covers fan-in, fanout, rendezvous, and routing. Dependent static
information retains endpoint fields and array shape through those cardinality
changes. Handles are consumed once, whereas the functions that materialize
them remain reusable.

The abstraction preserves physical intent. Pure transformations are inline,
storage remains in explicit module instances, and everything lowers through
the generic interface subsystem into the ordinary core IR. Whole-design
verification checks the realized combinational graph across instances. Rhodium
also distinguishes `Valid`, `Decoupled`, `Irrevocable`, and credited transport,
where PyMTL3's common ready-valid shape leaves stability meaning to the
component contract. Rhodium therefore has the stronger language-level abstraction
for exact elastic RTL; PyMTL3 instead offers explicit wiring plus the separate
advantage of substitutable FL, CL, and RTL models.

## Locality, predictability, and syntax

PyMTL3's greatest syntactic advantage is familiarity. A combinational block
looks like a short Python procedure, loops and conditions reuse Python grammar,
and concrete `BitsN` values print and execute naturally. Decorators and the
`@=` / `<<=` distinction provide a compact visual marker for combinational and
sequential intent.

The cost is contextual meaning. Python `if` in `construct` chooses generated
structure; Python `if` in `@update` denotes a mux-like hardware alternative;
the same code in an unrestricted helper may be simulation-only. Whether a
construct is translatable depends on the enclosing block and accepted subset.
Understanding drive ownership or block ordering can also require examining the
whole component schedule rather than only one assignment.

Rhodium uses less familiar graph-construction forms. Runtime alternatives use
dedicated selection forms, conversions are explicit, and a source-level
connection immediately constructs an IR edge contributing to one final
binding for its destination. The
generated structure is therefore predictable. This adds ceremony for simple
behavioral logic, while Rhombus functions and macros provide abstraction
without changing the final RTL semantics.

## Language-level judgment

PyMTL3 offers the more economical surface for executable behavioral RTL. A
small amount of Python syntax covers elaboration, update logic, concrete
values, and multiple modeling levels, and decorated blocks are a natural unit
for related equations. That reuse is powerful, but it is context-sensitive:
the enclosing phase and accepted subset determine what otherwise ordinary
Python denotes.

Rhodium is more orthogonal as an exact RTL construction language. Exact types,
one final binding per destination, explicit conditional priority, and visible
state boundaries determine connectivity without consulting a simulator
schedule or translation subset. Its standard flow topology syntax is also more
compositional than PyMTL3's explicit ready-valid component wiring. PyMTL3's
wider modeling continuum is genuine abstraction expressivity, however; Rhodium's
smaller core does not replace the ability to state useful non-RTL models in the
same component vocabulary.

## Lessons for Rhodium

1. PyMTL3 demonstrates that executable FL, CL, and RTL models can share a
   component vocabulary without claiming that the higher levels synthesize to
   the lower one. Rhodium could adopt such models as separate denotations.
2. Update-block syntax is economical for behavioral combinational logic, but a
   compatible Rhodium form should still lower immediately to an exact graph with
   final bindings, muxes, and guarded effects.
3. Concrete executable bit values improve examples, testing, and direct
   interpretation; they can coexist with graph-owned symbolic values.
4. Structural model substitution and nominal protocol compatibility solve
   different composition problems. Rhodium should preserve the latter even if it
   adds higher-level executable models.
5. Keep flow composition independent of hierarchy: inline transformation and
   deliberate stateful module boundaries should remain visible after the
   topology syntax elaborates.

## Sources

- PyMTL3 [official documentation](https://pymtl3.readthedocs.io/)
- PyMTL3 [quick start](https://pymtl3.readthedocs.io/en/latest/intro/quickstart.html)
- PyMTL3 [hardware data types](https://pymtl3.readthedocs.io/en/latest/ref/datatypes.html)
- PyMTL3 [RTL translation language](https://pymtl3.readthedocs.io/en/latest/ref/passes-translation-intro.html)
- PyMTL3 [primary repository](https://github.com/pymtl/pymtl3)
- PyMTL3 [standard stream interfaces](https://github.com/pymtl/pymtl3/blob/master/pymtl3/stdlib/stream/ifcs.py)
- PyMTL3 [multi-level modeling paper](https://www.csl.cornell.edu/~cbatten/pdfs/batten-pymtl3-nvidia2023.pdf)
- Rhodium [standard flow composition](../../flow/README.md#component-catalog)
- Rhodium [typed decode patterns](../../rhodium/std/README.md#typed-decode-patterns)
  and [decode generation](../../rhodium/std/README.md#typed-decode-generation)
- Rhodium [architecture](../../rhodium/README.md), [core semantics](../../rhodium/core/README.md),
  [frontend staging](../../rhodium/frontend/README.md), and
  [interface layer](../../rhodium/frontend/layers/README.md#interfaces-and-topology)
