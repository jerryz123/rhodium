<!-- Indexes Rhodium's language and compiler comparisons and defines the common rubric used by the suite. -->

# Rhodium comparison guide

This directory compares Rhodium with hardware languages, construction
libraries, research languages, and compiler IRs that make materially different
design choices. It is a guide to semantic tradeoffs, not a feature-count
leaderboard: each comparison asks what a source program denotes, which
properties compose, and which decisions remain visible to the hardware author.

Capability claims describe the repositories and public documentation examined
as of **2026-08-17**. All systems are evolving, so treat conclusions as a
qualified snapshot rather than a permanent ranking.

Contributors updating evidence, adding a comparison, or refreshing the shared
rubric should read [`DEVELOPING.md`](DEVELOPING.md).

## Choose a reading path

| If you want to understand... | Start with | Then read |
|---|---|---|
| The nearest direct RTL-construction alternatives | [Amaranth](amaranth.md), [SpinalHDL](spinalhdl.md), [Hardcaml](hardcaml.md) | [Chisel](chisel.md), [SystemVerilog](systemverilog.md) |
| Ready-valid and protocol composition | [SpinalHDL](spinalhdl.md) | [HazardFlow](hazardflow.md), [Clash](clash.md), [Bluespec](bluespec.md), [Filament](filament.md) |
| Typed patterns and decoder construction | [Chisel](chisel.md) | [SpinalHDL](spinalhdl.md), [Amaranth](amaranth.md), then [Bluespec](bluespec.md), [Clash](clash.md), and [DSLX/XLS](dslx.md) |
| Transactions, timing, or protocols as first-class semantics | [Bluespec](bluespec.md), [Filament](filament.md), [HazardFlow](hazardflow.md) | [Clash](clash.md) |
| Scheduling above exact RTL construction | [Calyx](calyx.md), [DSLX/XLS](dslx.md) | [PyMTL3](pymtl3.md) |
| The conventional-language baseline | [SystemVerilog](systemverilog.md) | The closest construction-language comparison for the topic you care about |

## Comparison map

| Comparison | Kind | Primary question for Rhodium |
|---|---|---|
| [Chisel](chisel.md) | Scala construction language | Which model makes widths, connection priority, hierarchy, and parameterized structure more direct and predictable? |
| [Amaranth](amaranth.md) | Python construction language | How do assignment, shape, domain, and structural abstractions compare when both systems remain transparent RTL construction languages? |
| [SpinalHDL](spinalhdl.md) | Scala construction language | How do data objects, assignment rules, direction inference, and clock-domain scopes compare with explicit destinations and roles? |
| [Hardcaml](hardcaml.md) | OCaml construction library | Can derived host-language interfaces and a direct signal graph offer a simpler compositional API? |
| [SystemVerilog](systemverilog.md) | Standard hardware language | Does a smaller expression-and-place model improve reasoning over continuous assignments, procedural blocks, nets, and variables? |
| [Bluespec](bluespec.md) | Rule-based hardware language | Should state conflicts remain explicit in wiring, or become guarded atomic actions scheduled by the compiler? |
| [Clash](clash.md) | Functional synthesizing language | Which invariants should live in a powerful static type system rather than an explicit construction IR? |
| [HazardFlow](hazardflow.md) | Typed dataflow research language | Can transfer, backpressure, and protocol transformation be one typed interface algebra? |
| [Filament](filament.md) | Timing-typed accelerator language | Should latency, initiation interval, and resource availability become statically checked authoring contracts? |
| [Calyx](calyx.md) | Accelerator compiler IR | Does the `cells` / `wires` / `control` split express scheduled actions more cleanly than constructing controller RTL directly? |
| [DSLX and XLS](dslx.md) | Functional dataflow language and HLS compiler | When should scheduling and pipelining be compiler decisions rather than exact source-visible structure? |
| [PyMTL3](pymtl3.md) | Multi-level Python modeling framework | Can update blocks and method interfaces unify functional, cycle-level, and RTL models without obscuring which model denotes hardware? |

## Method and evidence discipline

The suite compares supported public models at the same semantic level and
separates circuit expressivity, abstraction expressivity, static guarantees,
and syntactic economy. Its evidence and maintenance rules are in
[`DEVELOPING.md`](DEVELOPING.md#method-and-evidence-discipline).

## Shared Rhodium baseline

All essays compare against the same model:

- Rhombus host computation elaborates exact-width hardware into one verified,
  backend-independent IR.
- The core distinguishes readable definitions from bindable endpoints and
  requires one effective driver per destination. Hardware alternatives become
  explicit muxes or guarded effects rather than implicit source-ordered
  overwrites.
- Frontend layers add authoring notation and policy without introducing a
  second hardware IR; the optional CIRCT backend consumes verified core IR.
- Nominal interfaces carry complementary roles, refinement, structural member
  checks, and linear topology handles. Standard flow and decode facilities
  elaborate through those generic frontend and core mechanisms.
- Current sequential policy is deliberately narrow: rising-edge clocks and,
  when present, active-high synchronous reset. Clock-domain identity is not a
  hardware type property.

The owning references are the
[`rhodium` architecture](../../rhodium/README.md),
[`core` semantics](../../rhodium/core/README.md),
[`frontend` model](../../rhodium/frontend/README.md), and
[`standard-library` contracts](../../rhodium/std/README.md). The essays refer
to those documents instead of redefining the architecture independently.

## Common evaluation rubric

Every essay uses the same evaluation questions and definitions. The complete
rubric and instructions for changing it are in
[`DEVELOPING.md`](DEVELOPING.md#common-evaluation-rubric).

## Cross-system conclusions

### Exact construction

Rhodium's strongest general idea is exact construction: exact semantic types,
one effective driver per destination, explicit priority, explicit state
relationships, and intentional module boundaries survive into one verified IR.
Among direct RTL construction languages, that makes meaning unusually local
and reconstructable. The cost is additional adaptation and selection syntax
relative to inferred widths, procedural update blocks, and
default-then-override assignment.

The single-driver rule is a deliberate language boundary, not deferred support
for general last-connect or unordered multiple-driver resolution. Rejecting
those models prevents elaboration order, helper composition, or code movement
from silently replacing a connection; competing drivers fail at their source,
priority remains explicit in control structure, and every downstream analysis
sees one unambiguous driver edge. The tradeoff is that defaults, overrides, and
arbitration must be composed into an explicit selected value before the final
drive. The owning contract is in the
[`Value`, `Place`, and binding model](../../rhodium/core/README.md#values-places-and-binding).

The core `Value`/`Place` split represents this discipline but is not itself an
expressivity advantage. A unified, capability-aware signal surface can enforce
the same reading, binding, ownership, typing, and driver rules; Rhodium's
frontend already presents registers, wires, and outputs this way.

Other systems expose genuinely stronger propositions in narrower domains:

| System | Stronger proposition exposed to authors |
|---|---|
| Hardcaml | One combinational description can be interpreted concretely or as a signal graph. |
| Clash | Dimensions and clock domains participate in a powerful static type system. |
| Bluespec | Guarded atomic actions compose before scheduling conflicts are resolved. |
| HazardFlow | Transfer and backpressure form a typed interface algebra. |
| Filament | Relative time and resource reuse are compositional types. |
| Calyx | Multi-cycle actions and their control schedules remain explicit above RTL. |
| DSLX/XLS | Behavioral dataflow can precede scheduling and pipelining decisions. |
| PyMTL3 | Multiple modeling levels coexist in one framework. |

Each idea would require a named semantic contract and lowering into exact
Rhodium hardware; none is just syntactic sugar.

### Flow composition

Rhodium's [`std/flow`](../../rhodium/std/README.md)
surface is a compact topology language. Configured stages are ordinary unary
host functions, so `|>` composes both concrete endpoints and detached paths.
The result can change cardinality, branch, terminate, route, or buffer selected
`Valid`, ready-valid, and credited protocols. Pure adapters elaborate to direct
wiring; stateful and structurally meaningful stages remain visible instances.
This uses the generic frontend `InterfaceHandle`, not a second flow IR.

| Comparison group | Bounded conclusion |
|---|---|
| SpinalHDL | The closest direct peer: its `Stream` composition is at least as fluent and more mature in timing control; Rhodium makes protocol strength and single-use topology ownership more explicit. |
| Chisel, Amaranth, PyMTL3 | Their standard ready-valid interfaces connect cleanly, but do not provide an equally broad, uniform topology algebra. Amaranth's minimal stream contract is stricter than Rhodium's `Decoupled`. |
| Hardcaml, Clash | Their host-typed arrows or `Circuit` abstraction are more general reusable circuit algebras; Rhodium makes nominal endpoints, buffering, ownership, and module structure more source-visible. |
| HazardFlow | Its payload/resolver/transfer model and generic FSM are a more general handshake algebra; Rhodium instead verifies the complete realized combinational graph. |
| Bluespec, Filament, Calyx, DSLX/XLS | These compose atomic transactions, static timelines, scheduled actions, or behavioral channel networks rather than exact continuously active token paths. |
| SystemVerilog | Interfaces and assertions can encode any chosen convention, but the language has no standard first-class ready-valid composition algebra. |

The claim is structural, not a claim of complete protocol verification.
`Irrevocable` stability is documented rather than assertion-backed, and
[`map_flow`](../../rhodium/std/flow/map.rhdl) plus
[`demux_flow`](../../rhodium/std/flow/demux.rhdl) conservatively weaken to
`Decoupled` unless the author asserts stability. The layer has no general
forward/backward protocol algebra or compositional latency,
initiation-interval, deadlock, fairness, or liveness contracts.

The clearest extension is static certification of payload-only transformations,
followed by protocol-polymorphic stages over the existing interface subsystem—not
a flow-specific core IR.

### Decode, patterns, and literals

Rhodium's decode surface combines three ordinary layers:

1. [`HardwareLiteral`](../../rhodium/frontend/support/hardware-literal.rhm)
   supplies one exact packed image for a semantic hardware type.
2. [`Pattern`](../../rhodium/std/decode/pattern.rhdl) represents a host-side
   typed bit cube, with recursive aggregate wildcards and sparse named record
   patterns.
3. [`DecodeTable`](../../rhodium/std/decode/table.rhdl) represents an unordered,
   non-overlapping typed relation with an explicit default and partially
   specified aggregate outputs. Relations compose before one decoder is
   elaborated.

| Comparison group | Bounded conclusion |
|---|---|
| Chisel | The closest peer: `BitPat`, `TruthTable`, and `DecodeTable` support partial inputs and outputs plus Boolean minimization. Rhodium adds recursive semantic aggregate patterns, exact relation types, relation lifting/zipping, and uniform overlap rejection; Chisel has stronger `BitSet` algebra and column-oriented fields. |
| SpinalHDL, Amaranth | Both offer concise flat masked matching; Rhodium's recursively typed aggregate relation and structured partial outputs are broader. |
| SystemVerilog | Packed aggregates and wildcard cases cover more local matching forms, but remain control-flow syntax rather than a reusable typed relation value. |
| Bluespec, Clash, DSLX/XLS | Their general pattern languages support features such as bindings, guards, alternatives, captures, or ranges. Rhodium is narrower but exposes the complete finite relation for global validation and sparse lowering. |
| Hardcaml, PyMTL3 | Host code can construct typed comparisons and muxes, but their standard authoring models provide no equivalent recursive masked relation with partial-output freedom. |
| HazardFlow, Calyx, Filament | Decoder hardware is constructible, but decode relations are not a central standard abstraction. |

This is a source-language and preservation claim, not universal Boolean or PPA
superiority. The CIRCT backend preserves one sparse `casez` with uncared output
positions as X; downstream tools choose product sharing, factoring, and target
mapping. Rhodium does not directly express wildcard captures, arbitrary guards,
primitive ranges, computed row results, or ordered priority among overlapping
rows.

Current seams remain explicit: `PatternSet` supports host-side set algebra, but
`zip_decode_cases` requires identical input partitions rather than refining
compatible cubes, and the low-level `Pattern(~value, ~care)` constructor gives
representation-level care information the value's semantic type. The natural
next step is partition-refining relation products while preserving the relation
and output freedom through lowering.
