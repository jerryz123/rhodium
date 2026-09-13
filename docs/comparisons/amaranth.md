<!-- Compares the core denotation and authoring semantics of Rhodium and Amaranth. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rhodium and Amaranth

## Scope and thesis

*Snapshot: 2026-08-17.*

This comparison uses the current
[Amaranth documentation](https://amaranth-lang.org/docs/amaranth/latest/intro.html)
and focuses on language semantics and authoring syntax.

Amaranth and Rhodium are close in scale and intent. Both execute a host-language
program to construct explicit RTL. Amaranth centers its model on Python
`Value` expressions, assignable `Signal`s, and named control domains. Rhodium
presents one broad frontend `Hardware` surface, then factors readable `Value`s
from driveable `Place`s in its verified core IR. The substantive contrast is
between Amaranth's growing shapes and ordered assignments and Rhodium's exact
types, one final binding, explicit priority, and separate current/next-state
semantics.

## Summary

| Concern | Rhodium | Amaranth |
|---|---|---|
| Source denotation | Rhombus evaluation constructs one public core IR | Python evaluation constructs `Value` expressions and module fragments |
| Hardware object | Common frontend `Hardware`; core IR factors readable `Value` from driveable `Place` | A `Signal` is both a readable value and an assignment target |
| Width policy | Positive explicit widths and explicit adaptation | Shapes may be inferred; expressions grow; assignment may truncate |
| Assignment | One effective driver; conditional alternatives become one drive | One driving domain per bit; within a domain the last active assignment wins |
| State | Explicit register with current value, next place, clock, and optional synchronous reset | Synchronous domains hold state; domain assignments update it at the active edge |
| Hierarchy | Circuit definitions and explicit instances | `Elaboratable` objects return fragments; `Module.submodules` establishes hierarchy |
| Interface relation | Nominal roles, refinement, support, and linear handles | Immutable signatures, nested directions, flipping, equality, and structural connection |
| Flow composition | Linear topology values compose serial, parallel, and cardinality-changing stages | A deliberately minimal strong `stream.Interface` connected through ordinary wiring |
| Patterns and decode | Typed aggregate cubes and unordered decode relations can retain partial output care | `matches` and `Case` accept exact values or flat masked bit strings inside ordinary control flow |
| Primary abstraction tools | Rhombus functions, macros, classes, language layers, type capabilities | Python functions, classes, generators, castable protocols, and signatures |

## Denotation and staging

Amaranth hardware expressions are Python objects that form an abstract syntax
tree. Python executes first, creating values, assignments, modules, and
submodules; the resulting structure denotes hardware. The
[language guide](https://amaranth-lang.org/docs/amaranth/latest/guide.html)
draws this boundary explicitly: Python `if` and loops choose generated
structure, while `m.If`, `m.Switch`, and value operators describe circuit-time
behavior.

Rhodium uses the same two phases. Ordinary Rhombus computation chooses generated
structure; `when`, `switch`, and hardware operators create runtime logic.

The completed assignments in both systems denote concurrent logic. Python or
Rhombus statement order is construction order, not circuit execution order;
it becomes hardware priority only through the language's assignment and
conditional rules.

An Amaranth `Elaboratable` returns a `Module`, `Instance`, or another
elaboratable, and recursive preparation turns these fragments into a complete
design. Rhodium executes a circuit generator inside one active design builder and
immediately creates a module definition. All frontend forms converge on the
same [core representation](../../rhodium/core/README.md). Amaranth's fragment
protocol favors Python object composition; Rhodium's explicit design ownership
makes the completed semantic object easier to name and inspect.

## Expressions, types, and widths

Amaranth's central type descriptor is a *shape*: a width plus signedness.
Shapes may come from explicit declarations, Python integers and ranges, enums,
or user-defined `ShapeCastable` objects. Constants choose a sufficient width,
signals may infer a shape, and zero-width values are legal. User-defined
`ValueCastable` objects can present richer data abstractions while lowering to
ordinary Amaranth values.

The arithmetic rules aim to preserve the mathematical range of an
intermediate expression. Addition, subtraction, multiplication, comparisons,
and shifts derive result shapes from their operands. Assignment is a separate
boundary and may discard high bits when the destination is narrower. This
combination makes expressions concise and often prevents accidental
intermediate overflow, but the stored width is not determined by the expression
alone. The official guide documents these
[shape and value rules](https://amaranth-lang.org/docs/amaranth/latest/guide.html#shapes).

Rhodium requires positive, elaboration-known widths. Ordinary arithmetic is
fixed-width and modular; expanding arithmetic, extension, truncation, and casts
are different operations. Connections require complete type equality, not
only a compatible width. `Bits(8)`, `SInt(8)`, `Bool`, enums, one-hot controls,
records, and vectors therefore retain their semantic distinction through the
IR.

Amaranth's approach is elegant when the expression should follow mathematical
range rules. Rhodium's approach is elegant when each operation should expose the
implemented datapath width without consulting its eventual destination. Both
allow semantic types to be layered above bit vectors; Amaranth does so through
castable Python protocols, while Rhodium uses hardware-type objects with explicit
operation capabilities.

## Typed literals, patterns, and relational decode

Amaranth's [`Value.matches`](https://amaranth-lang.org/docs/amaranth/latest/guide.html#match-operator)
accepts exact constants or strings with `-` mask bits, and `m.Case` accepts the
same patterns. This is concise for local control, but a `Case` is ordered: the
first matching case supplies the active assignment. User-defined `ShapeCastable`
and `ValueCastable` objects can add semantic data views, but the masked pattern
itself is a flat value-level comparison rather than a recursively typed
aggregate relation.

Rhodium's [typed decode layer](../../rhodium/std/README.md#typed-decode-patterns)
instead represents exact typed literals and recursive aggregate `Pattern` cubes
as host data. `DecodeTable` validates an unordered, nonoverlapping relation;
sparse output patterns preserve which fields are free, and `DecodeGen` carries
the combined relation into one sparse backend operation. This is more concise
when several control fields or independently authored decoder fragments must
remain one relation. Amaranth remains more general for arbitrary
Python-generated tests and ordered conditional behavior.

The resulting Rhodium decoder leaves downstream synthesis free to share Boolean
work across outputs and use output don't-cares that a naive sequence of
comparisons might lose. Rhodium does not run a minimizer itself. An Amaranth
design can state the same Boolean function and a downstream synthesis tool may
recover the same or better implementation.

## State, assignment, and priority

Amaranth assignments belong to control domains such as `comb` or `sync`. A bit
may be driven from only one domain, but it may receive many assignments within
that domain. If several are active, the last assignment added wins. `m.If`,
`m.Elif`, and `m.Switch` establish conditional activity, so a common style is a
default assignment followed by increasingly specific overrides.

Inactive assignment has domain-specific meaning. A synchronously driven
signal retains its previous value. A combinationally driven signal falls back
to its initial value, which makes the combinational network total rather than
inferring a latch. These rules are described in the guide's
[assignment and control-domain sections](https://amaranth-lang.org/docs/amaranth/latest/guide.html#control-domains).

Rhodium gives every destination one final binding. Hardware conditionals collect
branch assignments and produce a mux or enable followed by one drive. Priority
is visible in the conditional chain, but ordinary connection order is not a
priority mechanism. Read and drive contexts select a register's current and
next state respectively; core records those facets as a value and a place. A
missing next-state branch means hold, whereas a combinational destination must
be fully covered.

The practical difference is syntactic. Amaranth's ordered assignments make
default-then-override control compact and allow separate helpers to contribute
updates to one signal. Rhodium requires those alternatives to meet at one
selection boundary, which is more explicit but can require more structure.
Amaranth also attaches state to named clock domains, including domain edge and
reset policy. Rhodium attaches a clock directly to each register and offers an
ambient synchronous-circuit convention; domain identity is not a general
property of every signal.

## Hierarchy, interfaces, and composition

Amaranth separates an object's Python API from its elaborated contents.
`Elaboratable.elaborate(platform)` returns hardware, and placing elaboratables
in `m.submodules` records hierarchy. `Component` adds a declared wiring
signature to that pattern. This is a natural fit for Python classes: a reusable
object can expose configuration and methods before or independently of its RTL
implementation.

Rhodium circuit calls create definitions, and explicit instances connect those
definitions inside parents. Hardware crosses hierarchy through ports. Pure
functions over current-circuit values stay inline; stateful or intentionally
structural abstractions use circuits. That distinction makes hierarchy a
deliberate hardware decision rather than an automatic consequence of every
host abstraction.

Amaranth's
[`wiring.Signature`](https://amaranth-lang.org/docs/amaranth/latest/stdlib/wiring.html)
contains immutable `In` and `Out` members, may nest other signatures, can be
flipped, and constructs introspectable interface objects. `connect()` checks
member names, dimensions, widths, initial values, and direction before adding
combinational assignments; signal signedness may differ when widths match.
Base signatures compare structurally; signature subclasses compare by identity
unless they override equality, which lets an abstraction define nominal or
domain-specific compatibility.

Rhodium interfaces begin from nominal protocol identity. They name two roles,
orient members by role, and can refine or declare support for other nominal
contracts. Linear handles add single-consumption semantics for composed
topologies. Amaranth offers the more general structural wiring object; Rhodium
puts protocol meaning and topology consumption directly into connection
semantics.

## Ready-valid flow composition

Amaranth's [`stream.Interface`](https://amaranth-lang.org/docs/amaranth/latest/stdlib/stream.html)
deliberately defines a minimal ready-valid protocol. Its contract is strong:
once `valid` is asserted, neither it nor the payload may change before
transfer, and the producer may not make `valid` combinationally depend on
`ready`. `always_valid` and `always_ready` record compatible restrictions in
the signature. Components and FIFOs then compose through ordinary
`wiring.connect` calls and explicit submodule construction.

Rhodium's standard flow layer provides a substantially broader composition
abstraction without adding flow nodes to its core IR. A configured stage is an
ordinary unary host function, so `source |> queue(4) |> pipe(2)` constructs a
serial path. Starting from a payload or protocol type constructs a detached
one-shot `InterfaceHandle`; `parallel` composes branches; arbiters,
demultiplexers, forks, joins, and zips change cardinality in that same
expression language. Pure transformations stay inline and stateful stages
remain explicit module instances. The completed ordinary graph is then checked
for combinational cycles, including paths through instances.

Rhodium also distinguishes weak `Decoupled` offers from stable `Irrevocable`
offers, and separately models nonbackpressured `Valid` and credited transport.
That is more expressive than Amaranth's single strong stream contract when a
network intentionally permits a stalled offer to change. It is not currently
a stronger guarantee because Rhodium emits no general stability assertions.
Amaranth's stream API is much less of a topology algebra, but the behavioral
rule it does state is simpler and more uniform.

## Abstraction, locality, and predictability

Amaranth benefits from Python's functions, classes, generators, containers,
metaclasses, and duck-typed protocols. `ShapeCastable`, `ValueCastable`, and
signature subclasses provide explicit extension seams without requiring every
semantic abstraction to be a built-in AST node. The source is often concise:
one signal object can be read, assigned, indexed, or passed through generic
helpers.

That economy moves some meaning into context. Exact width may depend on shape
inference or the assignment target; priority may depend on assignment order;
implementation may depend on the domain that owns the signal. These rules are
regular and documented, but they are not always visible in one expression.

Rhodium uses Rhombus functions, classes, macros, and language layers through a
common frontend hardware annotation. Core later records readable sources and
driveable destinations separately. That representation helps state the
invariants, but the important author-facing rules are explicit type adaptation,
one final binding, and priority expressed by conditional structure. More syntax
is required at some boundaries, but fewer facts must be recovered from
surrounding statements.

## Language-level judgment

Amaranth is cleaner when a design benefits from lightweight Python objects,
mathematically growing expressions, named domains, and concise ordered
assignment. Its unified `Signal` is a coherent abstraction, not a semantic
defect. Rhodium is cleaner when implementation width, semantic type, final
binding, and the point of priority should be apparent at the operation itself.
For ready-valid networks, Rhodium additionally supplies the more expressive and
uniform topology syntax, while Amaranth supplies a deliberately smaller and
more categorical stream contract. Amaranth optimizes low-friction
construction; Rhodium optimizes local reconstruction and explicit composition of
the elaborated circuit.

## Lessons for Rhodium

1. Keep exact connection types, while learning from Amaranth's concise and
   consistently documented shape rules.
2. If clock domains become first-class, define their denotation as one coherent
   object rather than adding unrelated register options.
3. Preserve nominal protocol relations and linear handles, but borrow the
   immutability and introspection discipline of Amaranth signatures.
4. Make `Irrevocable` transformations satisfy Amaranth's simple standard:
   preserve stalled payload stability by construction or do not claim it.

## Sources

- [Amaranth introduction](https://amaranth-lang.org/docs/amaranth/latest/intro.html)
- [Amaranth language guide](https://amaranth-lang.org/docs/amaranth/latest/guide.html)
- [Amaranth elaboration model](https://amaranth-lang.org/docs/amaranth/latest/guide.html#elaboration)
- [Amaranth pattern matching](https://amaranth-lang.org/docs/amaranth/latest/guide.html#match-operator)
- [Amaranth interfaces and connections](https://amaranth-lang.org/docs/amaranth/latest/stdlib/wiring.html)
- [Amaranth data streams](https://amaranth-lang.org/docs/amaranth/latest/stdlib/stream.html)
- [Rhodium flow composition](../../flow/README.md#component-catalog)
- [Rhodium typed decode patterns](../../rhodium/std/README.md#typed-decode-patterns)
- [Rhodium core semantics](../../rhodium/core/README.md)
- [Rhodium frontend semantics](../../rhodium/frontend/README.md)
- [Rhodium frontend layers](../../rhodium/frontend/layers/README.md)
