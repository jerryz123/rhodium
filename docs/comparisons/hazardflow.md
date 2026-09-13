<!-- Compares Rhodium's general construction semantics with HazardFlow's typed hazard-interface language. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rhodium and HazardFlow: explicit RTL versus typed protocol flow

## Scope and thesis

*Snapshot: 2026-08-17.*

HazardFlow makes a bidirectional communication protocol the central object of
hardware composition. A hazard interface carries an optional forward payload,
a backward resolver, a predicate defining transfer, and a type-level account
of whether forward signals depend on backward signals. Modules consume and
produce those interfaces through Rust-shaped functions and combinators.

Rhodium's core begins one level lower. Its source constructs arbitrary typed
expressions, explicit bindings, operations, state, and module ports. Its
standard flow layer nevertheless provides a substantial topology language:
one-shot interface handles pass through serial, parallel, fan-in, fanout, and
routing stages with `|>`, while every stage lowers through protocol-neutral
interfaces into the same minimal IR. HazardFlow remains more semantically
general for elastic pipelines; Rhodium is more structurally explicit and remains
orthogonal to protocols outside that authoring layer.

## Summary

| Question | Rhodium | HazardFlow |
|---|---|---|
| Source denotes | One verified RTL-style operation graph | A network of typed forward/backward interfaces and cycle functions |
| Core composition unit | Explicit definition/binding connections and circuit instances | A consumed interface transformed into another interface |
| Transfer semantics | Written as ordinary signal logic for a chosen protocol | Intrinsic `Hazard` payload, resolver, and ready predicate |
| State | Explicit registers and next-state bindings | Interface-attached FSM with explicit state transition callback |
| Dependency guarantee | Whole-design combinational-cycle verification after elaboration | Local `Helpful`/`Demanding` interface dependency types |
| Flow topology | Linear `InterfaceHandle` paths with serial, parallel, and cardinality-changing stages | Protocol-generic consumed-interface combinators and FSMs |
| Patterns and decode | Typed aggregate cubes form validated unordered relations with sparse outputs | Ordinary typed selection sits inside a language centered on hazard and dependency transformations |
| Main source of locality | Every driver and state edge is directly represented | Payload, backpressure, and transfer behavior travel together |
| Syntactic compression | High for explicit ready-valid topology; general RTL remains explicit | Very high for protocol-generic transformations and elastic pipelines |

## Denotation and staging

Rhodium elaboration executes Rhombus host code to build the
[public core graph](../../rhodium/core/README.md). Host parameters and loops choose
structure; hardware expressions and effects create runtime circuit behavior.
The graph is protocol-neutral: a Boolean operation, a register, and a memory
retain the same meaning whether or not they participate in a handshake.

HazardFlow gives modules a more specialized denotation. Its
[tutorial](https://kaist-cp.github.io/hazardflow/book/tutorial/tutorial.html)
models one module cycle as a function from ingress forward signals, egress
backward signals, and current state to egress forward signals, ingress backward
signals, and next state. An interface transformation therefore denotes both
directions of a communicating pipeline stage at once.

The Rust-like surface should not be mistaken for ordinary Rust values driving
an external circuit builder. Hardware signal types, interface traits, and the
`fsm` operation have specialized HDL meaning. Its source vocabulary is built
around one recurring pipeline equation, so functions and method chains are
smoother than Rhodium's general graph-construction forms.

## Core composition unit and syntax

Rhodium composes a circuit by binding source expressions to explicit destinations.
The expression `destination <== source` has one direction and one ownership
effect. This definition/binding normal form is not itself a protocol
abstraction. A bidirectional protocol is represented by an interface descriptor
with complementary endpoint roles, but connecting it ultimately performs a set
of ordinary bindings. The general circuit model does not define a transfer
event.

HazardFlow treats a function implementing `FnOnce(Ingress) -> Egress`, where
both sides implement `Interface`, as a module. The idiomatic syntax chains
transformations on a consumed interface: payload maps, resolver maps, forks,
joins, registers, and FSMs all return another typed interface. This is an
elegant use of method chaining because the receiver is not incidental data;
it is the communication edge being consumed and replaced.

The [`Hazard` trait](https://kaist-cp.github.io/hazardflow/book/lang/interface.html)
is the key semantic compression. It associates a payload type `P`, a resolver
type `R`, and `ready(P, R)`. A transfer occurs exactly when a valid payload is
present and that predicate holds. Ready/valid becomes one instance of a more
general algebra instead of three related wires whose meaning must be restated
by each combinator.

Rhodium's nominal protocol identity answers a different question: which named
contract do these two endpoints claim to implement? HazardFlow's hazard type
answers how a transfer is computed. Nominality and transfer algebra are
orthogonal; neither makes the other redundant.

## Flow composition

Rhodium's standard flow layer closes much of the syntactic gap without changing
the core denotation. Each configured stage is an ordinary unary host function,
so a concrete endpoint or a disconnected `InterfaceHandle` can pass through
`pipe`, `queue`, mapping, filtering, arbitration, rendezvous, fork, demux, and
crossbar stages with `|>`. `parallel` forms the structural product of
independent paths, while joins and routing stages change cardinality. A handle
or terminated sink can be consumed at most once, making ownership of the open
topology local during elaboration; a configured stage function itself remains
reusable and constructs fresh topology on every application.

That is a coherent embedded topology DSL, not just shorthand for a component
catalog. Pure combinational adapters elaborate as local interface links, while
stateful or structurally meaningful stages remain explicit instances. Stages
also state meaningful protocol-strength transitions: storage may establish an
`Irrevocable` output, while an adapter observing live control conservatively
returns `Decoupled`. The entire layer is implemented by the generic interface
subsystem and ordinary core bindings rather than flow-specific IR nodes.

HazardFlow is more general at the same authoring level. Its
[combinators](https://kaist-cp.github.io/hazardflow/book/lang/combinator.html)
operate over the `Hazard` algebra rather than a fixed ready-valid family, and
its generic `fsm` describes stateful protocol transformations without requiring
a new stage abstraction for each pattern. Rhodium's flow language is therefore
stronger in source-visible structural ownership, while HazardFlow is stronger
in protocol-parametric reuse.

## Time, state, and concurrency

Both languages remain cycle-explicit. HazardFlow does not schedule abstract
operations into an arbitrary number of cycles. Its generic FSM callback
computes forward output, backward output, and next state for the current cycle,
and the chosen combinator determines when state advances under transfer or
stall. Backpressure is therefore part of the state-transition expression, not
an external convention.

Rhodium state is an explicit graph primitive. A register separates current state
from one selected next-state binding; guarded updates become muxes or enables.
An author writes `valid`, `ready`, and state-hold equations directly. That is
more verbose for a standard elastic stage, but it also exposes the precise
hardware when state does not advance on the same event as an interface
transfer.

Concurrency in both systems is the concurrency of the resulting network, not
a Bluespec-style rule schedule. The major difference is which cycle invariant
is factored out. HazardFlow packages the relation among payload, resolver,
transfer, and state. Rhodium packages only the lower-level ownership and operation
semantics, leaving transfer coupling to the protocol definition and circuit.

## Types and intrinsic guarantees

HazardFlow signal types include arbitrary-width `U<N>`, Boolean values, enums,
tuples, structs, and arrays. Its distinctive guarantee comes from
`I<H, D>`: the hazard `H` defines transfer, while dependency type `D` describes
the backward-to-forward path. `Helpful` means forward signals do not depend on
backward signals. `Demanding` permits that dependence and additionally requires
that a present payload satisfy the ready predicate.

This type information can rule out some compositions that would create a
combinational loop. A sink that drives its backward result from the forward
payload, for example, can require a `Helpful` input. The
[dependency documentation](https://kaist-cp.github.io/hazardflow/book/advanced/dependency.html)
also states the limit precisely: the classification records only
intra-interface dependency. It does not detect every inter-interface loop,
including the documented fork/join pattern, and one invariant relies on using
the standard combinators correctly.

Rhodium has no corresponding dependency type. It instead verifies the complete
elaborated dependency graph, including hierarchy and aggregate paths, and
rejects combinational cycles there. That catches loops outside HazardFlow's
two-point classification, but only after composition; it cannot tell a caller
from an interface type alone whether a proposed transformation is safe.

Rhodium's other guarantees are broader and lower-level: exact hardware types,
one effective driver, legal resource ownership, and explicit conversions.
HazardFlow's guarantee is narrower and more protocol-aware. The strongest
language design would not confuse these layers: a local dependency type is a
composition aid, while whole-graph cycle verification remains the final fact.

Rhodium's nominal flow contracts still exceed what it proves. `Irrevocable`
stability is documented rather than automatically asserted. Ordinary
`map_flow` and `demux_flow` stages conservatively weaken to `Decoupled`, while
their explicit `~stable: #true` preservation mode remains an unchecked author
assertion. HazardFlow's dependency types do not solve every temporal property
either, but its generic protocol signature states more of the transfer
relation. Rhodium still needs static dependency certification or generated
assertions to prove a retained promise.

## Typed literals, patterns, and relational decode

Rhodium's decode vocabulary forms a second, independent algebra above its exact
data types. `HardwareLiteral` supplies exact values for scalar, aggregate, and
extension-defined types. A recursive `Pattern` adds cared and unconstrained
parts without erasing record or vector structure, and the same representation
can specify both input regions and partial output values.

An Rhodium `DecodeTable` is an unordered, nonoverlapping typed relation rather
than a prioritized control construct. Ordinary table values can be combined by
adding rows, lifting their selector patterns into a wider input type, or
zipping independently authored output relations. `DecodeGen` retains the
combined sparse relation and all output don't-cares in one backend operation;
downstream synthesis owns minimization and product sharing.

HazardFlow has typed literals, structs, tuples, arrays, enums, and Rust-shaped
expressions for selection, but its standard language abstraction is the hazard
interface and its transfer function, not a reusable masked decode relation.
Authors can express or generate comparisons and conditional outputs, including
the exact same Boolean function. What is absent is a standard value that
retains recursive typed patterns, validates unordered coverage regions, and
composes input and output dimensions before circuit generation.

Rhodium is consequently more concise for instruction and control decoding;
HazardFlow remains more expressive for transformations whose central fact is
forward/backward protocol behavior. Preserving the relation gives Rhodium better
optimization information than a naive collection of separate compares and
muxes, but does not guarantee better PPA than HazardFlow plus an effective
downstream optimizer.

## Locality and predictability

HazardFlow gives excellent protocol locality. The payload, feedback information,
transfer test, and dependency direction appear in one interface type, and a
combinator signature states how that package changes. A fluent pipeline can be
read left to right without manually tracing `valid` and `ready` through every
stage.

That economy weakens when behavior crosses several interfaces. Forward and
backward paths create a graph whose dependencies are not always captured by
the local `Helpful`/`Demanding` label. The fork/join limitation is important
because it shows that a compact interface type is an abstraction of the graph,
not the graph itself.

Rhodium has the opposite profile. The final dependencies are explicit and checked,
but protocol meaning is distributed over ordinary equations unless a higher
authoring abstraction gathers it. Its linear interface handles make topology
consumption local during elaboration, and nominal roles prevent accidental
shape-only compatibility, yet the type does not carry a generic transfer
predicate or ready-dependency fact.

## Language-level judgment

HazardFlow's hazard interface is a strong, orthogonal abstraction for elastic
hardware. Payload, resolver, validity, readiness, and dependency are not five
unrelated features; they describe one communication event. Encoding them
together produces both syntactic economy and reusable semantic guarantees.
The fluent `Interface -> Interface` style matches that denotation unusually
well.

Its specialization is also its boundary. Not every register, memory port, or
control relation is most naturally understood as a stream transformation, and
the two-valued dependency summary cannot replace full circuit analysis. Rhodium's
flow surface is less protocol-general but preserves more of the selected
buffering, routing, module boundaries, and final dependency graph.

Rhodium can compose sophisticated ready-valid and credited topologies, but it
cannot currently state “this interface's transfer predicate is part of its
type” or “forward data is independent of backward resolution.” Those are real
language-level contract gaps, not missing components or Boolean expressivity.

## Lessons for Rhodium

1. Represent a transfer protocol as one coherent contract containing payload,
   backward information, and the transfer predicate. Do not make each adapter
   rediscover their relationship from field names.
2. Add compositional dependency metadata only as a conservative summary, and
   retain whole-design combinational-cycle verification as the authority.
3. Keep nominal protocol identity and hazard behavior separate. One states
   meaning and compatibility; the other states transfer mechanics.
4. Provide an expression-oriented `Interface -> Interface` vocabulary for
   protocol transformations while leaving stateful or structurally meaningful
   boundaries explicit.
5. Do not force the general Rhodium circuit model into a universal stream model.
   HazardFlow's elegance comes from a focused semantic domain, not from making
   every hardware object an interface.

## Sources

- Rhodium [core semantics](../../rhodium/core/README.md),
  [frontend model](../../rhodium/frontend/README.md), and
  [flow protocols and composition](../../flow/README.md)
- Rhodium [typed decode patterns](../../rhodium/std/README.md#typed-decode-patterns)
  and [decode generation](../../rhodium/std/README.md#typed-decode-generation)
- [HazardFlow project](https://github.com/kaist-cp/hazardflow)
- [HazardFlow signal types](https://kaist-cp.github.io/hazardflow/book/lang/signal.html)
- [HazardFlow interface and hazard model](https://kaist-cp.github.io/hazardflow/book/lang/interface.html)
- [HazardFlow module model](https://kaist-cp.github.io/hazardflow/book/lang/module.html)
- [HazardFlow dependency types and limitations](https://kaist-cp.github.io/hazardflow/book/advanced/dependency.html)
