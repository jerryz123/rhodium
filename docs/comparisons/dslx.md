<!-- Compares Rhodium's exact RTL construction with DSLX and the XLS functional HLS model. -->

# Rhodium and DSLX (XLS): exact RTL and functional HLS

## Scope and thesis

Snapshot: 2026-08-17. This comparison uses the checked-in Rhodium implementation
and the current official DSLX and XLS language and IR documentation.

DSLX is the hardware-oriented source language of XLS. Its functions describe
immutable, timeless computations; its `proc`s describe recurrent state and
communication through channels. XLS optimization and scheduling then decide
how those computations become combinational or pipelined RTL. Rhodium starts at a
lower level: its source elaborates explicit modules, registers, memories,
muxes, and protocol wiring.

The strongest resemblance is between Rhodium core and XLS *block* IR, not between
Rhodium and a DSLX function. Both can describe concrete ports, registers, and
instantiations. DSLX function and proc IR deliberately retain more freedom
above that boundary. DSLX is more elegant for algorithmic dataflow whose
pipeline is a compiler choice; Rhodium is more local and predictable when the
microarchitecture itself is the design.

## Summary

| Concern | Rhodium | DSLX / XLS |
|---|---|---|
| Semantic unit | Exact RTL module | Stateless function or recurrent communicating proc; concrete block after codegen |
| Time | Registers and memory ports fix cycle boundaries | Functions are timeless; procs recur; XLS schedules operations into cycles |
| State | Explicit physical registers and memories | Typed proc state until lowering; explicit registers in block IR |
| Control | Host structure plus hardware muxes and guarded effects | Immutable `if`, `match`, loops, channel operations, and proc recurrence |
| Types | Exact hardware types and nominal protocols | Fixed-size bits, arrays, tuples, nominal structs, enums, and parametrics |
| Patterns and decode | Typed aggregate cubes form an unordered, overlap-checked finite relation with sparse outputs | `match` is a typed, value-producing expression over structured values and literals |
| Composition | Modules and directional interface endpoints | Function calls, proc networks, and typed channels |
| Streaming topology | Exact serial/parallel ready-valid and credited paths with explicit buffers, routing, fanout, and rendezvous | Higher-level proc/channel network; metadata and code-generation policy can constrain its eventual buffering and handshake realization |
| Compiler freedom | Preserve author-selected stage boundaries | Optimize and pipeline before producing a concrete block |
| Syntactic center | Structural construction embedded in Rhombus | Rust-like expression language for analyzable dataflow |

## Denotation and staging

Rhodium is a deep embedding. Rhombus evaluation computes parameters, generates
hierarchy, and decides which operations exist. Circuit expressions and
connections become nodes and destination bindings in one frontend-independent
[core IR](../../rhodium/core/README.md). The generated graph already identifies
all sequential resources and therefore its cycle behavior.

DSLX is a dedicated language whose restricted, immutable semantics make
whole-program analysis practical. The
[DSLX reference](https://google.github.io/xls/dslx_reference/) describes
fixed-size values, an analyzable call graph, parametric compile-time values,
and expression-oriented functions. A function denotes a feed-forward
calculation, not a sequence of clocked statements.

DSLX `proc`s add `config`, `init`, and `next`. Configuration constructs a proc
network and its channels at compile time; initialization supplies persistent
state; the next-state body receives channel data, produces channel data, and
returns new state. The resulting process meaning is still above physical RTL:
channel operations and state recurrence do not by themselves choose every
register boundary or external handshake signal.

XLS makes the abstraction levels explicit in its
[IR semantics](https://google.github.io/xls/ir_semantics/). Functions are
stateless single-result computations. Procs carry state, token-ordered effects,
and channels. Blocks are RTL-level units with ports, registers, and
instantiations. Comparing Rhodium core directly with function or proc IR would
mistake compiler freedom for missing RTL detail.

## Types and intrinsic guarantees

Rhodium requires positive elaboration-known widths and exact types at operation
and connection boundaries. `Bits`, `Bool`, `SInt`, nominal enums, `OneHot`,
clocks, resets, records, and vectors can retain different meanings even when
their packed representations have equal width. Extension, truncation, and
representation casts are explicit.

DSLX has arbitrary-width signed and unsigned bit types, fixed arrays, tuples,
nominal structs, enums, aliases, and parametric functions and types. Numeric
literals can carry explicit types, and most operations reject incompatible
widths or signedness. Parametric dimensions participate in type checking, so a
generic arithmetic function can state relationships between its operand and
result shapes directly in the source language.

DSLX's type system is especially expressive for reusable pure computation.
Rhodium's host language can calculate arbitrary shapes, but the relationship is
often checked when elaboration constructs hardware rather than proved as a
DSL-level parametric signature. Conversely, Rhodium's exact circuit types and
nominal multi-signal protocols express RTL-specific constraints that are not
data values in DSLX's functional type system.

## Typed literals, patterns, and relational decode

DSLX `match` is an immutable, value-producing decision expression. It can
match structured values such as tuples and enums, bind components, combine
alternatives, use ranges, and return arbitrary typed expressions. This is a
more general and more proportionate language for a semantic decision than
Rhodium's fixed-width bit-cube patterns; Rhodium cannot add bindings, guards, ranges,
or an arbitrary computation to a `Pattern` row.

Rhodium instead represents a decoder as data before it constructs a decision
network. `HardwareLiteral` provides exact typed scalar, aggregate, and
extension values. `Pattern` adds recursive care bits, and `DecodeTable` checks
that its typed input cubes are disjoint. A row's sparse output pattern records
which result bits are architecturally relevant, while input lifting and output
zipping compose finite relations before a `DecodeGen` consumes them. Zipping
does not infer a join: it deliberately requires matching input partitions.

Because the relation retains every output bit and its don't-care freedom,
Rhodium emits one sparse `casez` relation rather than spelling a separate
comparison tree per result. Downstream synthesis may share product terms and
exploit the preserved X-valued outputs, but Rhodium does not run a Boolean
minimizer itself. XLS can optimize a `match`-derived dataflow graph and its
scheduler and target flow can choose a different implementation. The
difference is explicit finite-relation algebra versus a general functional
decision expression, not a portable hardware-quality advantage.

## Time, state, and scheduling

A DSLX function expresses alternatives with value-producing `if` and `match`
expressions and repetition with bounded functional loops or folds. There is no
mutable variable whose update order defines hardware. XLS sees one dataflow
graph and can rewrite it or assign its nodes to pipeline stages
subject to scheduling constraints.

A proc makes state explicit as a value threaded from one activation to the
next. Channel sends and receives are effects whose ordering is represented by
tokens in XLS IR. This gives the compiler a behavioral recurrence and
communication graph while preserving latitude over their physical
realization.

Rhodium exposes the realization instead. A register has current state, an
explicit next-state binding, a clock, and optional synchronous reset. A memory
has a chosen port form. Hardware `when` and `switch` lower prioritized
alternatives to explicit mux and guard logic, and exactly one final binding is
verified for every destination. Moving a calculation across a register is
therefore a source-visible architectural change, not an ordinary scheduling
decision.

This distinction is deeper than imperative versus functional syntax. DSLX
functions are functional descriptions *before* cycle selection; Rhodium's
dataflow IR is also functional in its combinational regions, but those regions
are already separated by explicit state.

## Core composition unit and interfaces

DSLX functions compose by typed calls and values. Procs compose as networks of
typed directional channels created during `config`; send and receive express
communication without requiring the source to spell the final ready/valid or
FIFO implementation. This is a strong unit for schedule-tolerant streaming and
process composition.

Rhodium modules compose through ordinary ports and nominal interface endpoints.
An interface defines complementary roles, nested directional members,
refinement, and parameter compatibility. Ready-valid buffers, forks, joins,
arbiters, and transformations are concrete circuit elements. A channel stage
or backpressure path is visible because it is part of the author's
microarchitecture.

An XLS channel and an Rhodium ready-valid interface may eventually produce similar
signals, but they are not the same semantic object. The channel belongs to a
process model whose physical realization is chosen later. The Rhodium interface
describes a role over already physical signals, and its linear handles constrain
how elaboration consumes the topology.

## Elastic flow composition

Rhodium's standard flow layer turns those physical interfaces into a compact
topology language. Configured stages are unary elaboration-time functions, so
serial paths use ordinary `|>` composition. Parallel paths, arbitration and
joins, atomic or buffered fanout, rendezvous, routing, payload transforms,
queues, pipes, and credit adapters share the same model. A path may connect to
a concrete endpoint immediately or remain a detached, linearly consumed handle
until a caller attaches it. Pure transforms lower inline, while buffers and
other token-holding stages remain explicit instances.

The abstraction is powerful precisely because it does not make the flow graph
abstract. It determines the actual readiness dependencies, state placement,
queue capacities, and protocol conversions. Generic interfaces carry the
topology during elaboration, and the entire flow layer lowers into the same
minimal operations, connections, and modules as hand-built Rhodium.

An XLS proc network is higher-level. Typed channels default to expressing
communication and token ordering without a source-local commitment to a
particular ready path, finite FIFO placement, credit scheme, or port-level
handshake. Channel metadata and code-generation configuration can constrain
FIFO depth and buffering as XLS lowers the process network into concrete
blocks. This makes procs cleaner for behavioral, timing-insensitive
concurrency; Rhodium is cleaner when the buffering and backpressure architecture
is itself the design.

Rhodium does not derive the guarantees that this exactness might suggest. Its flow
types do not describe latency, throughput, liveness, or deadlock freedom. XLS
keeps more implementation freedom, but that freedom also enables global
scheduling and channel lowering that exact Rhodium deliberately declines.

## Locality, predictability, and syntax

DSLX syntax is compact when the intent is a mathematical transformation. `let`
bindings, expression-valued conditionals, pattern matching, arrays, and
parametrics expose data dependencies without assignment noise. Immutability
makes local reasoning about values strong: a name never silently changes.

The corresponding hardware cost and timing are intentionally less local.
Nothing in a normal function expression says whether an operation is duplicated,
folded away, or placed before or after a pipeline boundary. Scheduling options
and the surrounding graph determine that later. Proc syntax makes recurrence
and communication clear, but not every queue depth, handshake register, or
port shape.

Rhodium uses more construction vocabulary—circuits, ports, explicit registers,
connections, and hardware conditionals. Rhombus functions make generation
concise, while runtime selection, binding priority, and state remain explicit.
That source is more verbose for pure algorithms and more predictive for
microarchitecture: adding a register or queue locally adds a temporal boundary
locally.

## Language-level judgment

DSLX is more expressive and proportionate for pure algorithmic hardware. Its
immutable expressions, pattern matching, parametric types, and function
composition say more with less ceremony, while XLS preserves the freedom that
makes automatic pipelining meaningful. The nonlocality of final timing is not
an accident there; it is the abstraction's purpose.

Rhodium is more elegant once pipeline boundaries, memories, and physical
handshakes are the design rather than an implementation choice. Its source has
less algorithmic freedom but a tighter correspondence to reviewed hardware.
XLS block IR and Rhodium core converge near that lower level, so importing DSLX
syntax alone would not import DSLX's semantic leverage; that leverage comes
from retaining the higher function or proc denotation.

## Lessons for Rhodium

1. Function-level HLS would be a new semantic layer above Rhodium core. Exact RTL
   operations should not silently acquire freedom to move across cycles.
2. DSLX's immutable expression syntax is a useful model for combinational and
   whole-state descriptions, even when their lowering remains explicit Rhodium.
3. A future process layer should distinguish abstract channels from physical
   ready-valid endpoints and state how scheduling turns one into the other.
4. Rhodium should keep its exact-construction path for designs whose register,
   memory, and protocol placement is itself the architecture; automatic
   scheduling solves a different authoring problem.

## Sources

- XLS [project overview](https://google.github.io/xls/)
- XLS [DSLX language reference](https://google.github.io/xls/dslx_reference/)
- XLS [DSLX type system](https://google.github.io/xls/dslx_type_system/)
- XLS [IR overview](https://google.github.io/xls/ir_overview/)
- XLS [IR semantics](https://google.github.io/xls/ir_semantics/)
- XLS [scheduling](https://google.github.io/xls/scheduling/)
- XLS [code-generation options](https://google.github.io/xls/codegen_options/)
- XLS [primary repository](https://github.com/google/xls)
- Rhodium [architecture](../../rhodium/README.md), [core semantics](../../rhodium/core/README.md),
  [frontend staging](../../rhodium/frontend/README.md), and
  [typed decode relations](../../rhodium/std/README.md#typed-decode-patterns), plus the
  [ready-valid composition](../../flow/README.md#component-catalog)
