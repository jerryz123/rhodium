<!-- Compares Rhodium's exact synchronous construction with Filament's timeline-typed pipeline language. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rhodium and Filament: explicit cycles versus timeline-typed composition

## Scope and thesis

*Snapshot: 2026-08-17.*

Rhodium and Filament are both structurally minded languages: neither treats a
software procedure as an unconstrained request for HLS. Their source programs
nevertheless state different structures. Rhodium constructs concrete operations,
registers, memories, and connections. Filament constructs component instances
and invokes them at symbolic events, with types saying when every port is
available and how frequently a resource may be reused.

Filament's timeline types are a genuine increase in language expressivity.
Rhodium can build a balanced pipeline or a resource-sharing controller, but it
cannot state their latency, initiation interval, or legal use windows as
compositional source contracts. Filament gains that power by specializing its
language around statically timed pipelines. Rhodium remains more direct for
general synchronous RTL and dynamically stalled protocols. In particular,
Rhodium's standard flow layer is a compact language for constructing exact
runtime-backpressured topologies rather than merely wiring ready and valid
signals by hand.

## Summary

| Question | Rhodium | Filament |
|---|---|---|
| Source denotes | One exact graph of operations, state, resources, and instances | A statically timed network of component instances and event-indexed invocations |
| Core composition unit | Explicit definition/binding connection | Component invocation at a symbolic event |
| Time | Emerges from explicit state edges and protocol logic | Appears in port availability intervals and event expressions |
| Resource sharing | Author constructs arbitration, enables, and state | Repeated invocations checked against a component's initiation interval |
| Elastic composition | Serial and parallel stages, fan-in, fanout, rendezvous, routing, buffering, and credit transport | No corresponding dynamic-flow algebra; event timelines describe a static schedule |
| Static guarantee | Concrete graph is typed, single-driver, and cycle-safe | Composed values and resource uses satisfy declared timeline constraints |
| Patterns and decode | Typed aggregate cubes form validated unordered relations with sparse outputs | Ordinary bit-vector logic is available inside a language centered on timed use |
| Main source of locality | Exact register and connection structure is visible | Latency and use windows travel with component signatures |
| Syntactic compression | Direct for arbitrary RTL | High for balanced, statically scheduled pipelines |

## Denotation and staging

Rhodium executes a Rhombus generator to construct the
[public hardware IR](../../rhodium/core/README.md). A register is a concrete
state element, a connection is a concrete driver relation, and module
instantiation creates a chosen structural boundary. If a result must arrive
two cycles later, the author builds the state and control that make that true.

Filament components are parameterized by symbolic events. In a declaration
such as `comp C<'G: 1>(x: ['G, 'G+1] 32)`, `'G` names a start event, the
interval says when `x` is available, and the event delay states the component's
initiation interval. Instantiating a component creates hardware; invoking that
instance at an event states when the hardware performs one use. The
[Filament overview](https://filamenthdl.com/) presents those distinctions as
the language's central model.

The source therefore contains a schedule, but not as a flat list of absolute
cycles. Event expressions describe relative time and component signatures
abstract over a caller's start event. The compiler checks and realizes that
schedule; it is not free to choose arbitrary operation latency or register
placement. Rhodium fixes time by explicit graph construction, while Filament fixes
time by a checked symbolic contract that later determines graph control.

## Core composition unit and syntax

Rhodium syntax names hardware ownership directly. `output out: Bits(32)` creates
a destination, `out <== value` supplies its one binding, and `reg state(...)`
introduces a state element. `when` and `switch` describe runtime selection that
lowers to muxes and enables. Composition is uniform because all of these forms
end in the same exact operation graph. Its definition/binding normal form is
not an extra timing abstraction.

Filament syntax separates three ideas that conventional structural HDLs often
conflate: component declaration, physical instantiation, and timed invocation.
`A := new Add[32]` creates one adder; `a0 := A<'G>(x, y)` uses that particular
adder at event `'G`. Port types carry intervals such as `['G, 'G+1]`, so the
invocation result brings its valid time into subsequent composition.

This separation is syntactically economical precisely when hardware is reused
over time. A second invocation communicates sharing without manually exposing
the mux, enable, and controller at every use site, while the event argument
makes the scheduling decision visible. Rhodium requires those implementation
objects to be constructed explicitly. Filament writes less control structure,
but its source is less literal about the final registers and controller states.

## Elastic flow composition

Rhodium's standard flow layer gives exact elastic hardware a higher-level syntax
without changing what the circuit denotes. A configured flow stage is an
ordinary unary elaboration-time function, so `|>` expresses serial topology.
The same model covers parallel branches, arbitration and joins, atomic or
buffered fanout, demultiplexing and crossbars, payload transforms, queues,
pipes, and credit adapters. A path may be connected immediately or constructed
as a detached, linearly consumed handle and attached later. Pure combinational
transforms lower inline, while stages that hold tokens remain explicit module
instances.

This is unusually powerful for exact RTL because it composes both topology and
state placement. The generic interface subsystem carries the endpoints, and
the flow vocabulary elaborates into ordinary ports, connections, operations,
and instances; the minimal core IR needs no flow node or protocol-specific
semantics.

Filament's event, interval, and invocation algebra solves a different problem.
It checks that a statically scheduled value exists when used and that a resource
is not invoked more frequently than its initiation interval permits. It does
not provide an equivalent algebra for a token whose transfer may be delayed an
unbounded, runtime-dependent number of cycles by downstream readiness. Such a
network can be implemented as explicit component logic, but it falls outside
the central timeline abstraction.

Rhodium's advantage here should not be mistaken for temporal proof. Flow types do
not state latency, initiation interval, throughput, liveness, or deadlock
freedom, and `Irrevocable` stability is not automatically asserted. Filament
is therefore much stronger for static time and resource safety even though
Rhodium is more expressive for dynamically elastic topology.

## Time, state, and concurrency

Rhodium has conventional synchronous semantics. Registers advance on explicit
clocks, combinational logic relates current values within a cycle, and guarded
next-state drives determine holds and updates. Parallel operations are simply
parallel regions of the graph. There is no language-level latency associated
with an ordinary circuit port.

Filament makes relative time part of every component boundary. An output can be
consumed only during an interval covered by its availability, and a shared
instance cannot be invoked at overlapping times forbidden by its initiation
interval. A register is used when a value must remain available into a later
interval. Timeline checking therefore catches unbalanced pipeline paths and
structural resource hazards as composition errors.

This is not the same abstraction as dynamic backpressure. A timeline contract
says at which relative cycles a value exists and when new work may begin. A
ready/valid protocol decides at runtime whether a transfer occurs and may stall
for an unbounded, data-dependent number of cycles. Rhodium directly expresses the
latter through signal and state equations; Filament's most elegant core model
addresses the former. Treating one as a weaker version of the other obscures
their different denotations.

## Types and intrinsic guarantees

Filament's distinctive type is a bit width paired with an availability
interval over events. Type checking establishes that each source value is
available for the destination's required interval and that uses of a component
respect its declared delay. The declaration checks an implementation against
the latency and initiation interval it promises, making timing compositional
across component boundaries.

The guarantee extends beyond one concrete unrolling. Filament parameters,
constraints, generative `if`/`for`, and index-dependent
[bundles](https://filamenthdl.com/docs/meta/loops-and-bundles.html) let timing
relationships be expressed symbolically. The language checks allowed
parameterizations with arithmetic constraints rather than merely testing one
expanded circuit. This is a family-level timing guarantee that unrestricted
Rhodium host generation does not attempt.

Rhodium's type system records a different set of facts: exact packed and
aggregate structure, semantic distinctions such as signed, enum, and one-hot
values, operation capabilities, and binding legality. The verifier checks those
facts plus single-binding ownership and combinational acyclicity in the
realized design. Its types do not carry temporal intervals, and module
signatures do not promise latency or initiation interval.

The representational boundary is important. Rhodium can implement the same
pipeline and even generate host-side checks for a particular instance, but the
public language cannot quantify over a symbolic event or require callers to
respect a time-indexed port contract. Filament's advantage is not a more
convenient register helper; it is a different static proposition.

## Typed literals, patterns, and relational decode

Rhodium's finite-relation vocabulary composes exact types along an axis that is
orthogonal to time. `HardwareLiteral` supplies exact values for scalar,
aggregate, and extension-defined types. A recursive `Pattern` can constrain
selected record or vector parts while leaving others free, and it serves both
as an input region and as a partially specified output value.

An Rhodium `DecodeTable` requires exact common types and pairwise nonoverlapping
inputs, making its rows an unordered relation rather than a priority-ordered
case statement. Tables can be extended by list concatenation subject to
overlap validation, lifted into a wider input domain, and zipped across
separately authored output relations. `DecodeGen` preserves that complete
sparse relation and its don't-care outputs in one backend operation.

Filament has bit-vector constants, expressions, conditions, and components
from which the same Boolean decoder can be constructed. Its standard language
abstraction instead tracks event-indexed availability and legal resource use;
it has no comparable standard recursively typed masked pattern or
partial-output decode relation. A user can generate repeated comparisons or
encapsulate them in a component, but the timeline type does not preserve
decoder cubes as a composable authoring object.

Rhodium is consequently more expressive and concise specifically for structured
control decoding, while Filament is far more expressive for stating when the
decoder's inputs and outputs may be used. Giving downstream synthesis the
complete sparse relation may produce better logic than naive compare-and-mux
construction, but it does not establish universal PPA superiority over an
equivalent Filament design.

## Locality and predictability

Filament makes timing local at component boundaries. A caller can see when an
input is required, when an output appears, and how soon the component can be
invoked again without inspecting its internal pipeline. Relative events make
this information compose through nested invocations instead of devolving into
comments about cycle numbers.

The price is a more abstract relationship to the final control graph. A short
sequence of timed invocations can imply guards, pipeline enables, and controller
state that are not named in the source. Constraint errors may also involve
symbolic interval relationships spanning several calls. The schedule is still
author-visible, but the mechanism implementing it is compiler-derived.

Rhodium has the inverse locality. Registers, muxes, and enables are easy to locate
and their connectivity is predictable, but latency is an emergent path property
that does not survive as an interface contract. A module can be structurally
transparent yet temporally opaque to its caller.

## Language-level judgment

Timeline types are elegant because Filament does not bolt independent
`latency`, `valid_from`, and `initiation_interval` annotations onto a structural
language. Events, intervals, invocations, and component delay form one algebra:
the same event expression explains value availability and legal resource reuse.
That orthogonality gives a small syntax unusually high leverage for static
pipelines.

Rhodium's construction language is more general and more literal. Its explicit
bindings, state boundaries, and control accommodate elastic protocols,
arbitrary state machines, memories, and ordinary datapaths without translating
them into a timeline. The cost is that timing intent above the register level
is not expressible as a checked contract.

The right comparison is therefore not which language has “more timing.” Rhodium
owns exact cycle implementation; Filament owns symbolic timing composition.
Filament is more expressive and concise where a design admits a static
schedule. Rhodium is more predictable where runtime control, not a timeline, is
the natural specification.

## Lessons for Rhodium

1. If Rhodium adds fixed-latency contracts, base them on composable symbolic events
   or equivalent relations, not disconnected integer annotations.
2. Distinguish physical instantiation from logical invocation when a language
   layer intentionally shares resources. The distinction exposes reuse without
   requiring scheduling to masquerade as wiring.
3. Verify any timing contract against the constructed implementation; a type
   that callers trust must not be documentary metadata.
4. Keep statically timed and elastic interfaces as distinct regimes. A
   fixed-latency event contract and a dynamically stalled transfer protocol
   make different promises.
5. Preserve exact Rhodium IR beneath any timeline layer. Inferring control from
   events should be a named elaboration step, not a silent change to ordinary
   connection semantics.

## Sources

- Rhodium [core semantics](../../rhodium/core/README.md),
  [frontend model](../../rhodium/frontend/README.md), and
  [frontend layers](../../rhodium/frontend/layers/README.md), plus the
  [standard flow composition model](../../flow/README.md#component-catalog)
- Rhodium [typed decode patterns](../../rhodium/std/README.md#typed-decode-patterns)
  and [decode generation](../../rhodium/std/README.md#typed-decode-generation)
- [Filament language overview](https://filamenthdl.com/)
- [Filament language tutorial](https://filamenthdl.com/docs/lang/tutorial.html)
- [Filament loops and timed bundles](https://filamenthdl.com/docs/meta/loops-and-bundles.html)
- [Modular Hardware Design with Timeline Types](https://www.cs.cornell.edu/~asampson/media/papers/filament-pldi2023-preprint.pdf)
