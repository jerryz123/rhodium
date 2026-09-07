<!-- Compares the core denotation and authoring semantics of Rhodium and SpinalHDL. -->

# Rhodium and SpinalHDL

## Scope and thesis

*Snapshot: 2026-08-17.*

This comparison uses the current
[SpinalHDL documentation](https://spinalhdl.github.io/SpinalDoc-RTD/master/index.html)
and concentrates on the language's ordinary RTL model.

SpinalHDL and Rhodium both aim to make constructed hardware predictable rather
than emulate an event-driven HDL. SpinalHDL builds a mutable netlist through a
fluent Scala API and checks many structural mistakes after elaboration. Rhodium's
frontend presents a common hardware surface, while core factors readable
`Value`s from driveable `Place`s. SpinalHDL is syntactically economical; Rhodium
is more explicit about exact types, one final binding, conditional priority,
and current/next-state semantics.

## Summary

| Concern | Rhodium | SpinalHDL |
|---|---|---|
| Source denotation | Rhombus evaluation constructs one public core IR | Scala evaluation constructs an in-memory netlist that later passes transform/check phases |
| Hardware object | Common frontend hardware surface; core factors readable `Value` from driveable `Place` | Mutable-reference-like `Data` objects denote signals and destinations |
| Width policy | Positive explicit widths; exact types at connections | Widths may be explicit or inferred; assignment widths are checked; `.resized` is contextual |
| Assignment | One effective driver; alternatives become one selected drive | Same-scope overlap normally rejects; under conditional muxing the last active assignment wins |
| State | Current value plus distinct next-state place | `Reg` declaration makes state; assignment syntax remains uniform |
| Hierarchy | Circuit definitions and instances; pure helpers stay inline | `Component` makes hierarchy; `Area` groups inline structure |
| Interface relation | Nominal roles, refinement, support, and linear handles | `Bundle` structure, port directions, `IMasterSlave`, and inferred bulk connection |
| Flow composition | One-shot topology values compose serial, parallel, and cardinality-changing stages | Fluent `Stream` methods and connection operators compose concrete connected streams |
| Patterns and decode | Typed aggregate cubes form validated unordered relations with partial output care | `MaskedLiteral` and `DecodingSpec` provide flat masked tables and Boolean minimization |
| Primary abstraction tools | Rhombus functions, macros, classes, frontend layers, semantic type capabilities | Scala functions, classes, traits, generics, collections, `Area`, and library-defined data types |

## Denotation and staging

SpinalHDL is a regular Scala library. Running the Scala program calls
constructors and overloaded operators that build an in-memory graph. Once the
top-level `Component` has been instantiated, compiler phases transform, check,
and serialize that graph. The official
[Scala interaction guide](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Getting%20Started/Scala%20Guide/interaction.html)
states this model directly.

Scala computation chooses generated structure. Scala loops, collections,
methods, and ordinary booleans run during elaboration; Spinal `Bool`, `when`,
`switch`, and signal operators build circuit behavior. Because hardware
objects are references into the graph, passing one to a Scala function and
assigning it there affects the same signal.

The resulting graph denotes concurrent hardware. Scala execution order builds
that graph; it is not a cycle-by-cycle schedule, except where ordered
assignments deliberately encode mux priority.

Rhodium also runs its host program to construct hardware, but its completed
denotation is a documented [core IR](../../rhodium/core/README.md). Frontend
profiles and language layers present a common hardware vocabulary and all
produce that representation. Core then uses `Value` for a readable result and
`Place` for a destination. This makes ownership and direction explicit in the
completed graph, although a unified graph object with checked capabilities can
enforce the same policy.

Both systems are deterministic construction languages in the ordinary case.
SpinalHDL exposes more of elaboration as mutation of shared graph references;
Rhodium exposes more of the result as creation of typed values and final drives.

## Expressions, types, and widths

SpinalHDL provides `Bool`, raw `Bits`, arithmetic `UInt` and `SInt`, enums,
`Vec`, and `Bundle`. A hardware type combines a Scala class with the parameters
stored in its instance, so a parameterized bundle class is also a reusable
hardware type. Values can be cloned from an existing type object. This makes
ordinary object-oriented definitions a natural way to introduce structured
hardware.

Widths can be stated or inferred. For example, `Bits()` may obtain its width
from its assignments. SpinalHDL normally requires assignment bit counts to
match, automatically widening only weakly sized literals that fit. Explicit
`resize` may widen or narrow, while `.resized` asks the surrounding assignment
for the target width. These rules are documented in the
[`Bits` guide](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Data%20types/bits.html)
and
[assignment semantics](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Semantic/assignments.html).

Rhodium requires every width to be a positive host integer at construction.
Connections and ordinary operators require complete type equality; extension,
truncation, and representation casts are separate expressions. Its packed
types may expose bitwise, arithmetic, or signed-arithmetic capabilities while
remaining nominally distinct. Thus an enum, one-hot control, signed integer,
and raw bit vector need not become interchangeable merely because their widths
match.

SpinalHDL occupies a middle ground between pervasive inference and strict
explicitness: most mismatches are errors, yet declarations and contextual
`.resized` expressions can leave size to surrounding statements. Rhodium makes
the implemented width more local, while SpinalHDL saves repetition when the
destination is already the clearest size specification.

## Typed literals, patterns, and relational decode

SpinalHDL's [`MaskedLiteral`](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Data%20types/bits.html)
uses `-` bits in a flat `Bits` mask. It works directly in comparisons, `switch`,
and muxes, which is a compact fit for a local decoder. The higher-level
[`DecodingSpec`](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Libraries/utils.html#logic-simplification-utilities)
builds decode tables from masked inputs and outputs, applies
Quine–McCluskey-style simplification, and supports an explicit default. This is
a genuine decoder generator, not merely syntax around a `switch`.

Rhodium's [typed decode layer](../../rhodium/std/README.md#typed-decode-patterns)
uses semantic literals and recursive aggregate cubes to validate one unordered
decode relation before materializing it. Its sparse output patterns expose
don't-care freedom, and independently authored rows or output relations can be
combined before `DecodeGen` preserves them in one sparse backend operation.
Rhodium's distinction is therefore recursive semantic aggregate structure and
explicit relation composition, not the existence of masked tables or
minimization. SpinalHDL remains terser for a local `switch` and competitive
for a flat decode table.

Rhodium leaves output don't-cares and the complete relation available to
downstream synthesis, but it does not run a Boolean minimizer itself. It does
not guarantee better PPA than SpinalHDL's decoder minimization plus synthesis:
target mapping and the surrounding logic determine the eventual result.

## State, assignment, and priority

In SpinalHDL, declaration determines whether a signal is combinational or
sequential. `UInt(8 bits)` creates a combinational signal; wrapping the type in
`Reg(...)` creates a register. The same `:=` syntax drives either. A register
captures the ambient clock domain when it is created, so its clocking semantics
come from construction context rather than the later assignment site.

SpinalHDL's
[assignment rules](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Semantic/assignments.html)
distinguish two cases. Two ordinary `:=` assignments from the same scope are
treated as an overlap and rejected by default. Within `when` or other mux
construction, the last active assignment wins, which gives compact
default-then-override and priority logic. The separate `\=` operator updates a
combinational graph reference immediately for deliberately imperative
construction.

Rhodium instead gives each destination one final binding. `when` and `switch`
capture branch alternatives and lower them to one selected value or guarded
effect. Read and drive contexts select a register's current and next state;
core records those as a value and a place. A conditionally uncovered next state
holds. This removes connection order as an ambient source of priority, but
requires alternatives from separate helpers to be brought together explicitly.

SpinalHDL's `ClockDomain` makes clock, reset, edge, polarity, and enable policy
an ambient construction context, and `ClockingArea` scopes it. Rhodium registers
carry an explicit clock in core and may use the `sync_circuit` convention for
an ambient clock/reset pair. SpinalHDL's domain context is more expressive and
compact for groups of state; Rhodium's operand-level clock is simpler to recover
from an individual register node.

## Hierarchy, interfaces, and composition

A SpinalHDL `Component` creates a hardware module. Instantiating a component
inside another component creates hierarchy, and its ports are connected after
construction. An `Area` instead groups logic and naming without requiring a
module boundary. This gives authors a clear choice between structural hierarchy
and inline organization. The
[component guide](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Structuring/components_hierarchy.html)
also defines which parent and child ports may be read.

Rhodium makes a similar distinction through functions and circuits. Pure helpers
over values add operations directly to the containing circuit. Calling a
`circuit` generator creates a definition, and an explicit instance creates the
hierarchical relationship. Hardware crosses through ports; host parameters
shape the generated definition.

SpinalHDL `Bundle`s collect named hardware fields. Port directions live on the
members, `IMasterSlave` can assign complementary directions for a reusable
interface, and `<>` infers how matching members should connect. This is a
structural and directional model built from Scala classes and signal metadata.

Rhodium interfaces are nominal protocol descriptions. They define two named
roles, orient members by role, and may refine or support other protocol
identities. Linear handles can make a topology object single-use. SpinalHDL's
approach is lightweight and lets ordinary bundle methods become a fluent
interface API. Rhodium's approach carries more compatibility meaning than field
shape and direction alone.

## Ready-valid flow composition

SpinalHDL's [`Stream`](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Libraries/stream.html)
is the closest direct comparison to Rhodium's flow syntax. Methods such as
`queue`, `stage`, `m2sPipe`, `s2mPipe`, `haltWhen`, `throwWhen`, and `map`
return another connected stream, while dedicated operators insert particular
timing cuts during connection. Fork, join, arbitration, mux, and demux
utilities extend that fluent model to branching topologies. It is a coherent
stream language rather than merely a ready-valid bundle convention.

Rhodium moves one step further toward treating the entire topology as a value.
The same `|>` application accepts a concrete endpoint or a disconnected
payload/protocol seed, and the result may be an endpoint, endpoint array, or
linear `InterfaceHandle`. `parallel` combines independent or heterogeneous
branches, while fan-in, fanout, joins, and routing change cardinality without
leaving the notation. Configured unary stages are reusable, but each resulting
handle is deliberately consumed once so topology ownership is unambiguous.

The two systems expose ready-valid meaning differently, and their contracts do
not form a simple precision ordering. SpinalHDL's `Stream` keeps `valid`
asserted until acceptance but permits a stalled payload to change. Rhodium's
`Decoupled` makes a weaker pre-transfer promise, while `Irrevocable` requires
both the offer and payload to remain stable; Rhodium has no exact payload-bearing
name for SpinalHDL's intermediate contract. SpinalHDL also separately provides
nonbackpressured `Flow`; Rhodium separately models nonbackpressured `Valid` and
credited transport. Rhodium stages can make their chosen nominal strengthening or
weakening visible. It also keeps pure adapters inline, stateful stages as
module instances, and lowers both through generic interfaces rather than a
flow-specific core IR; its whole-design verifier checks the resulting
combinational graph across instance boundaries.

Rhodium's additional nominal contract is not backed by generated protocol
assertions today. Overall SpinalHDL remains at least as fluent for connected
streams and offers finer ready/valid timing-cut operators; Rhodium is cleaner for
detached topology construction, linear ownership, and explicit—though partly
author-enforced—protocol strength.

## Abstraction, locality, and predictability

SpinalHDL derives much of its elegance from the fact that hardware types are
Scala objects. Methods can be placed directly on bundles, constructors can
validate parameters, functions can return signals or `Area`s, and collections
can generate regular structure. Since operations mutate an in-memory graph,
an abstraction may contribute assignments to objects it receives instead of
returning a complete value.

The compiler compensates with broad design checks. Its documented checks
include assignment overlap, clock crossing, hierarchy violation,
combinational loops, latches, undriven signals, and width mismatch; see
[Design errors](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Design%20errors/index.html).
The language is therefore not merely permissive mutation: it accepts concise
construction idioms, then validates global graph properties.

Rhodium's frontend largely presents one hardware category, with driveability
checked when `<==` is elaborated. Core's source/sink factoring then gives the
verifier direct categories for ownership and direction. The stronger
language-level policies are that connection adaptation is explicit, each
destination has one final binding, and ordinary connection order cannot
override it. This improves local predictability but makes some incremental
construction patterns more verbose.

## Language-level judgment

SpinalHDL is the strongest compromise between terse embedded-HDL syntax and
predictable RTL among these Scala-style designs. Its overlap checks, width
checks, `Component`/`Area` distinction, and explicit resize forms show that a
unified `Data` surface can be disciplined. Rhodium is cleaner where exact type,
one final binding, explicit conditional priority, and current/next state should
be recoverable without replaying assignment order. Their stream surfaces are
close: SpinalHDL is exceptionally fluent over concrete streams, while Rhodium's
topology values make serial, parallel, detached, and cardinality-changing
composition more uniform. SpinalHDL wins syntactic economy; Rhodium wins locality
of denotation and explicit protocol-strength tracking.

## Lessons for Rhodium

1. Retain one effective driver, but keep conditional authoring concise enough
   that explicit priority does not become ceremony.
2. Preserve the distinction between inline helpers and hierarchy; SpinalHDL's
   `Area`/`Component` split shows the value of making both choices ergonomic.
3. If clock domains become first-class, make their construction context and
   register-level denotation equally inspectable.
4. Continue deriving rich protocol composition from nominal interfaces rather
   than accumulating unrelated connection operators.
5. Close the gap between nominal flow strength and what mapping or routing
   bodies actually guarantee before treating that distinction as enforced.

## Sources

- [SpinalHDL documentation](https://spinalhdl.github.io/SpinalDoc-RTD/master/index.html)
- [SpinalHDL and Scala interaction](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Getting%20Started/Scala%20Guide/interaction.html)
- [SpinalHDL assignments](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Semantic/assignments.html)
- [SpinalHDL bit vectors](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Data%20types/bits.html)
- [SpinalHDL masked literals](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Data%20types/bits.html)
- [SpinalHDL `DecodingSpec`](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Libraries/utils.html#logic-simplification-utilities)
- [SpinalHDL bundles](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Data%20types/bundle.html)
- [SpinalHDL components and hierarchy](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Structuring/components_hierarchy.html)
- [SpinalHDL clock domains](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Structuring/clock_domain.html)
- [SpinalHDL design checks](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Design%20errors/index.html)
- [SpinalHDL streams](https://spinalhdl.github.io/SpinalDoc-RTD/master/SpinalHDL/Libraries/stream.html)
- [Rhodium flow composition](../../flow/README.md#component-catalog)
- [Rhodium typed decode patterns](../../rhodium/std/README.md#typed-decode-patterns)
- [Rhodium core semantics](../../rhodium/core/README.md)
- [Rhodium frontend semantics](../../rhodium/frontend/README.md)
- [Rhodium frontend layers](../../rhodium/frontend/layers/README.md)
