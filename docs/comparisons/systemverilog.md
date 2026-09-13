<!-- Compares Rhodium's exact construction semantics with SystemVerilog's core design language. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rhodium and SystemVerilog

## Scope and thesis

Snapshot: 2026-08-17. “SystemVerilog” here means the core design semantics of
[IEEE Std 1800-2023](https://standards.ieee.org/ieee/1800/7743/). The standard
also defines event-driven testbench, assertion, coverage, object, and foreign
interface facilities. Those are outside this core-design comparison except
where assertions directly illuminate flow-contract enforcement below.

Two levels must remain separate. Full SystemVerilog denotes event-driven
simulation with four-state values and many constructs that do not synthesize.
Its common synthesizable RTL subset denotes gates, state, memories, and
hierarchy at roughly the same level as Rhodium. Synthesis support is tool- and
target-dependent; the IEEE language definition is not itself a promise that
every construct maps to hardware.

Within the common RTL subset, SystemVerilog is compact and extraordinarily
expressive, but meaning often depends on procedural context, event controls,
assignment form, sizing, signedness, and four-state rules. Rhodium deliberately
accepts a smaller model: deterministic host elaboration, exact hardware types,
one effective driver, dataflow combinational logic, and explicit state. The
trade is syntactic breadth for local predictability.

## Summary

| Concern | Rhodium | SystemVerilog |
|---|---|---|
| Semantic unit | Elaborated module in a verified two-state dataflow IR | Module/interface design plus event-driven process semantics |
| Staging | Arbitrary Rhombus host computation constructs a concrete design | Parameters, constant expressions, generate, macros, and elaboration |
| Time | Explicit rising-edge registers and memory/effect operations | Event controls, procedural regions, blocking/nonblocking assignment, delays in full language |
| Assignment | One final binding per driveable destination | Variables follow procedural-driver rules; nets can resolve multiple drivers |
| Types | Exact semantic hardware types and explicit conversion | Rich two-/four-state, signed, packed/unpacked, aggregate, and context-sized types |
| Patterns and decode | Typed aggregate cubes form an unordered, overlap-checked finite relation with sparse outputs | `case`, `casez`, and `case inside` are general selected-control forms over packed values |
| Composition | Modules plus nominal complementary protocol roles | Modules, interfaces/modports, packages, and retained parameters |
| Elastic flow | First-class serial/parallel topology with fan-in, fanout, rendezvous, routing, explicit buffering, and credits | All structures are encodable, but there is no standard ready-valid composition algebra |
| Compiler freedom | Preserve explicit cycle and resource structure | RTL synthesis preserves sequential boundaries; full behavioral syntax is broader than synthesis |
| Syntactic center | Circuit construction forms inside Rhombus | Declarations plus continuous and procedural HDL statements |

## Denotation and staging

Rhodium executes Rhombus while elaborating a design. Host `if`, loops, functions,
and data structures decide which hardware exists. Hardware `when`, `switch`,
registers, memories, and connections create operations in one
[core IR](../../rhodium/core/README.md). The resulting module graph is the
semantic design; generated SystemVerilog is a lowering artifact.

SystemVerilog has its own compile-time and elaboration language. Parameters,
local parameters, constant functions, generate conditionals and loops, module
instances, interfaces, and preprocessor macros specialize hierarchy before
simulation. Unlike Rhodium's host evaluation, retained parameters remain part of
the HDL module abstraction and can be selected by downstream instantiators.

After elaboration, SystemVerilog processes execute in standardized event
regions. Continuous assignments, `always_comb`, `always_ff`, `always_latch`,
`initial`, tasks, functions, and assertion sampling have distinct rules.
Synthesizers recognize a disciplined subset of those meanings as hardware.
Rhodium has no authored event scheduler: operation order is not runtime order,
combinational logic is a graph, and state changes only through explicit
sequential operations.

This difference remains even when both sources synthesize to the same circuit.
SystemVerilog describes that circuit through a mixture of structural and
procedural language semantics. Rhodium constructs a structural/dataflow object
directly.

## Types and intrinsic guarantees

Synthesizable SystemVerilog includes two-state and four-state integral values,
signed and unsigned packed arrays, unpacked arrays, structs, unions, enums, and
user typedefs. Nets and variables are separate categories. Full SystemVerilog
adds dynamic and object-oriented types that are not ordinary hardware data.

Expression width and signedness can be self-determined or supplied by context.
Unsized literals, assignment targets, casts, packed layouts, and mixed signed
operands all participate. This permits terse arithmetic and close control over
representation, but the meaning of an expression is not always readable from
its operands alone.

Rhodium uses elaboration-known positive widths and exact types. `Bits`, `Bool`,
`SInt`, nominal enums, `OneHot`, clocks, resets, records, and vectors remain
distinct when their layers require it. Connections and ordinary operations do
not implicitly resize or reinterpret; extension, truncation, and equal-width
representation casts are explicit.

SystemVerilog's four-state values carry runtime `X` and `Z` behavior, including
defined equality, case, conditional, and edge rules. Rhodium's normal data model
is two-state. Its synthesis don't-care denotes implementation freedom rather
than a source-level runtime unknown, even if the eventual SystemVerilog uses an
`X` token to convey that freedom to later tools.

## Typed literals, patterns, and relational decode

SystemVerilog has compact, general selection syntax. `case` selects exact
packed values; `casez` provides wildcard-oriented matching; and `case inside`
can express wildcard sets and ranges. Packed structs and enums let an arm
construct a semantic result, while `unique` and `priority` state additional
selection intent. Those arms remain ordinary control flow, however: their
order matters when more than one can match, and the language does not make the
complete set of masks a typed reusable relation with a portable overlap check
or a multi-output minimization contract.

Rhodium separates that finite-relation problem from selected control. An exact
`HardwareLiteral` may represent a scalar, recursive aggregate, or extension
type; a `Pattern` adds field-level care while remaining typed host data.
`DecodeTable` rejects overlapping input cubes, preserves sparse output care,
and accepts composition by row extension, explicit input lifting, and output
zipping. The resulting `DecodeGen` is one callable decoder. Output zipping is
purposefully strict: both relations must already use the same input cubes,
instead of introducing an implicit priority or partition refinement rule.

For a large unordered control decoder, this is often more concise and gives a
PLA implementation direct access to cross-output sharing and output
don't-cares. It is not a claim that Rhodium always synthesizes better hardware.
A disciplined SystemVerilog `case` or comparison network can express the same
function, and target synthesis may optimize it as well as or better than a
source-level PLA. Nor is a SystemVerilog runtime `X` the same thing as Rhodium's
static synthesis freedom; using `casex` or assignment `X` as though it were
would change simulation semantics.

## Time, state, and scheduling

SystemVerilog offers two complementary RTL styles. Continuous assignments and
instances are structural/dataflow-like. Procedural blocks use statements,
temporaries, `if`, `case`, and loops to describe combinational or sequential
behavior. `always_comb` supplies implicit sensitivity and intent restrictions;
`always_ff` restricts event controls and competing writers; `always_latch`
states intended level-sensitive storage.

Blocking versus nonblocking assignment is semantic, not merely stylistic. A
blocking assignment updates a procedural variable immediately within its
process, while a nonblocking assignment schedules an update for a later event
region. Nonblocking assignment is the standard sequential-RTL convention for
simultaneous state updates. Multiple processes remain concurrent even though
statements within a process are ordered.

Rhodium replaces these procedural scheduling rules with explicit graph state. A
register has distinct current-state use and next-state binding. Hardware
alternatives become explicitly prioritized muxes and guarded effects; every
driveable destination has exactly one final binding. The current sequential
primitives choose rising-edge clocks and optional active-high synchronous
reset. Latches, falling-edge state, and asynchronous reset are not alternate
spellings of the same primitive in the current language.

Neither ordinary synthesizable SystemVerilog nor Rhodium is HLS at this level.
Once a register boundary is present, synthesis can optimize logic while
preserving observable sequential behavior, but it does not generally reinterpret
the source as an unconstrained algorithm and choose a different pipeline.

## Core composition unit and interfaces

The SystemVerilog module is both a reusable design unit and an elaborated
hierarchy boundary. Parameters allow one definition to retain a family of
specializations. Packages share types, constants, and functions. Interfaces
can bundle signals, parameters, continuous logic, tasks, functions, assertions,
and clocking blocks; modports expose named direction and access views.

Rhodium host functions and circuit constructors specialize fresh concrete modules
during elaboration. The resulting core module does not retain a parameterized
source family. Frontend interfaces are separate nominal descriptors with two
complementary roles, nested directions, refinement, declared support, and
parameter compatibility. They lower away to records and ports before the
backend.

SystemVerilog interfaces are more syntactically self-contained: signal bundle,
local behavior, timing view, and verification statements can live together.
Rhodium interfaces are narrower and more declarative. They state protocol identity
and direction while executable behavior remains in modules or reusable flow
constructors. Modports do not by themselves define Rhodium-style nominal
refinement or elaboration-time linear consumption; Rhodium interfaces do not
retain SystemVerilog-style behavioral members.

## Elastic flow composition

Rhodium's standard flow layer supplies a language abstraction that SystemVerilog
leaves to project conventions and libraries. A configured stage is a unary
elaboration-time function, so `|>` builds serial paths. The same model covers
parallel paths, fan-in and joins, atomic or buffered fanout, rendezvous,
routing, payload transforms, queues, pipes, and credit transport. Detached
handles let a path be named before it is attached and enforce single
consumption during elaboration; configured stage functions remain reusable.
Combinational transforms lower inline; token-holding stages remain explicit
modules.

The interface subsystem is intentionally only the substrate. Flow composition
ultimately becomes ordinary ports, connections, operations, and instances in
the protocol-independent core. This preserves exact visibility of readiness
dependencies, state placement, and buffer capacity without adding a flow graph
to the IR.

SystemVerilog can encode every one of these circuits. Interfaces and modports
can bundle a ready-valid protocol, modules can implement stages, and functions,
generate constructs, or macros can reduce repetition. What the standard does
not provide is one first-class algebra that makes serial, parallel,
cardinality-changing, and terminating flow paths compose through a common
typed value. Each codebase must choose its own conventions for ownership,
protocol stability, and stage construction.

SystemVerilog Assertions provide a major complementary advantage: an author
can assert that `valid` and payload remain stable while stalled and can state
bounded progress or environmental fairness properties. Rhodium's `Irrevocable`
contract is currently not automatically checked. Ordinary `map_flow` and
`demux_flow` stages conservatively weaken to `Decoupled`; their explicit
`~stable: #true` preservation mode remains an unchecked author assertion. Rhodium
also has no latency, throughput, liveness, or deadlock types. Its advantage is
therefore the cleanliness of constructing an exact elastic topology, not a
stronger temporal verification language.

## Locality, predictability, and syntax

Disciplined SystemVerilog RTL can be exceptionally concise. `always_comb` with
local temporaries expresses a decode or arithmetic datapath directly;
`always_ff` places related state updates in one visible block; packed structs,
enums, interfaces, and generate forms reduce wiring repetition. For engineers
reading the synthesizable idiom, the surface is close to the resulting RTL.

Its breadth creates contextual load. The same `if` can run during constant
elaboration, procedural simulation, or a synthesizable process. An incomplete
combinational assignment can imply retained state; assignment operators change
event timing; expression context can change width or signedness; nets and
variables obey different driver rules. The source is predictable when a
project follows a well-defined subset and style, less so when the full language
is treated as one uniform semantic space.

Rhodium makes dataflow policy explicit. Conversions are named, conditional
driving has explicit priority and is resolved before core verification, and
each destination receives one final binding. Rhodium remains more verbose than a
compact procedural block, but a local operation has fewer ambient language
rules. Rhombus abstraction can remove repetition without changing the graph
that the expansion constructs.

## Language-level judgment

A disciplined synthesizable SystemVerilog subset is both expressive and
elegant. Procedural blocks, packed types, retained parameters, and interfaces
give common RTL proportionate notation, and explicit register boundaries keep
microarchitecture under author control. Its difficulty is not procedural syntax
alone; it is the interaction of several assignment, sizing, event, net, and
elaboration models within one language.

Rhodium is more orthogonal within the circuits it denotes. Explicit conversion,
one final binding per destination, explicit priority, and explicit graph state
reduce ambient rules. It is not simply a cleaner spelling of all SystemVerilog
RTL: the current primitive model excludes legitimate circuit semantics such as
latches, alternative clock/reset events, and resolved nets. The smaller model
is compelling only if extensions preserve its local rules.

## Lessons for Rhodium

1. The useful SystemVerilog comparison is its disciplined synthesizable RTL
   subset, not the union of every event-simulation and verification construct.
2. Procedural combinational syntax can be elegant, but Rhodium should preserve a
   lowering in which ordering disappears and one final drive remains explicit.
3. If Rhodium broadens clock, reset, latch, net, or four-state semantics, each
   should be a distinct primitive contract rather than an incidental backend
   spelling.
4. Retained HDL parameters and elaborated concrete modules serve different
   composition needs; adding one should preserve the meaning of the other.
5. Rhodium's nominal protocol descriptors and SystemVerilog's behavior-bearing
   interfaces are complementary designs, not two syntaxes for an identical
   abstraction.

## Sources

- [IEEE Std 1800-2023 overview](https://standards.ieee.org/ieee/1800/7743/)
- [IEEE Xplore entry for IEEE Std 1800-2023](https://ieeexplore.ieee.org/document/10458102)
- [Accellera access to IEEE 1800-2023](https://www.accellera.org/downloads/ieee)
- Rhodium [architecture](../../rhodium/README.md), [core semantics](../../rhodium/core/README.md),
  [frontend staging](../../rhodium/frontend/README.md), and
  [interface layer](../../rhodium/frontend/layers/README.md#interfaces-and-topology),
  [typed decode relations](../../rhodium/std/README.md#typed-decode-patterns), plus the
  [standard flow composition model](../../flow/README.md#component-catalog)
