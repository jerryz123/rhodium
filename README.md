<!-- Introduces Rhodium, its authoring model, quick start, public capabilities, and user-facing documentation. -->

# Rhodium

> All the code and text in this repository was written by a LLM. The only text not produced by a LLM is this disclaimer. I worked with a coding agent to implement everything here to my personal preferences.

Rhodium is an experimental hardware description language hosted by
[Rhombus](https://docs.racket-lang.org/rhombus/). Ordinary Rhombus computation
generates hardware through concise, typed notation; elaboration produces one
public, backend-independent hardware IR and verifies it before any downstream
tool consumes it.

Normal designs use `#lang rhodium`. Authors who want to assemble a smaller
language can start from `#lang rhodium/base` and import only the frontend layers
they need. Both profiles create exactly the same core hardware model.

This page is the user and integrator entry point. Contributors changing Rhodium
itself should start with [`DEVELOPING.md`](DEVELOPING.md).

Rhodium does not emit SystemVerilog itself. Its optional backend lowers verified
IR through CIRCT, which owns RTL generation.

## Core principles that set Rhodium apart

### Explicit hardware, simple IR

Hardware connectivity should be explicit, not something readers and tools must
recover by mentally executing a sequence of assignments. Rhodium rejects
last-connect and competing-driver semantics: every place has one effective
driver, and priority is represented in the hardware graph itself. Author-level
`when` and `switch` forms become explicit selection, guarded effects, and final
drives; the IR has no conditional-connect operations or general control-flow
regions. Registers and memories still model state over time, but understanding
what drives a signal does not require replaying procedural assignment order.
This gives readers, verification, analysis, and backends the same unambiguous
dataflow graph. See the [IR contract](rhodium/core/README.md).

### Extend the language with hardware types

Equal bit widths do not make two hardware values interchangeable. Extensible
types let libraries enforce domain distinctions in user-level connections and
operations, catching incompatible uses during elaboration instead of relying
on naming conventions. The supplied layers already build a rich vocabulary:
`OneHot`, `MaybeOneHot`, multi-hot lane sets through `Mask`, nominal enums, and
tagged unions. Libraries can add their own types, methods, and notation using
the same mechanisms without adding a core primitive for every abstraction.
The normal language and an explicitly extended `#lang rhodium/base` still
converge on the same small hardware IR. See the
[type extension surface](rhodium/frontend/layers/README.md#shared-extension-surface).

### Declarative decoding

Specify the behavior that matters and leave synthesis free to optimize what
does not. Typed decode tables express input patterns and partially specified
outputs: an output bit marked don't-care gives synthesis freedom to choose
whichever value simplifies the circuit, instead of preserving an unnecessary
designer-supplied constant. Rhodium retains that intent as an unordered,
non-overlapping relation in the IR. The CIRCT backend lowers it to sparse
`casez` logic with don't-care outputs, preserving optimization freedom for
downstream tools. See [decode generation](rhodium/std/README.md) and the
[lowering contract](rhodium/backend/README.md#selection-and-relations).

## What layers and libraries make possible

### Domain-specific hardware vocabulary

The libraries put that extensibility to work: CHI defines types such as
`CHIReqOpcode`, `CHITransferSize`, and `CHIMemAttr()`, while the RISC-V adapter
defines `Sv39Access` and `Sv39Pte()`. Protocol encodings and page-table fields
become typed values with domain operations, built using ordinary public
Rhodium declarations. See the [CHI types](chi/protocol/flits.rhdl) and
[RISC-V translation types](riscv/rtl/sv39.rhdl).

### Protocol-aware flow composition

The [flow library](flow/README.md) composes buffers, arbitration, routing,
joins, and splits with `|>`. Connections check payload, direction, and protocol
compatibility, including the distinction between `Valid`, `Decoupled`, and
`Irrevocable`. Joins consume inputs atomically; atomic forks transfer to all
selected recipients together, while buffered broadcasts allow independent,
exactly-once acceptance. Adapters preserve or explicitly weaken protocol
guarantees rather than silently promising stability under backpressure.

### Transaction-aware Perfetto traces

Annotate flow checkpoints and the [event compiler](rhodium/event/README.md)
derives transaction ancestry through supported queues, arbitration, routing,
forks, and joins. Instrumented simulations track the actual contributing
transfers; RHEG collects and exports their event graph to Perfetto. Optional
stall observations and typed captures, including instruction disassembly,
connect timing to what the hardware was doing. Unsupported ancestry is rejected
rather than guessed, and tracing leaves the original synthesis design unchanged.

### Validated NoC generation

The [NoC library](noc/README.md) starts with symbolic topology and routing
descriptions, checks reachability and routing deadlock under documented VC
acquisition assumptions, and produces validated plans for
[hardware generation](noc/rtl/README.md). Routing analysis happens before RTL
construction, with failure witnesses tied back to the authored network.
These routing guarantees do not imply fairness or whole-protocol correctness.

### Clock-crossing safety from signal provenance

Clock-domain safety should follow where a signal comes from, not its name or
the module boundary it passes through. Rhodium's
[clock-crossing checker](rhodium/analysis/README.md#review-or-enforce-cdc-violations)
traces clock provenance through logic and hierarchy, down to individual record
and vector fields. Opt-in `elaborate_with_cdc` rejects unsafe or unknown-timing
sampling unless recognized, structurally verified crossing evidence permits it.
The current evidence supports stable one-bit synchronizers; it is not a blanket
guarantee for buses, handshakes, or reset-domain crossings, and the checker does
not silently insert synchronization hardware.

## Quick start

### Requirements

- Racket 9.2 or a compatible current release
- Rhombus 1.1
- Device Tree Compiler for DTB interoperability tests and FESVR simulations
- CIRCT and Verilator only for external backend integration tests
- Rosette only for optional equivalence, reachability, and output-property tests

On a Homebrew-based macOS setup:

```sh
brew install minimal-racket dtc
raco pkg install --auto rhombus
```

On x86-64 Linux or Apple Silicon macOS, install the pinned CIRCT release into
the ignored `.tools` directory:

```sh
make setup-circt
```

For simulation, build the pinned Verilator 5.038 (requires a C++ toolchain,
Autoconf, Bison, Flex, Make, Perl, and Python 3):

```sh
make setup-verilator
export PATH="$PWD/.tools/verilator/bin:$PATH"
```

Root Make targets also prefer this installation automatically. CI uses the
same installer and verifies the version on cache hits as well as fresh builds.

On other platforms, install CIRCT separately and set `CIRCT_OPT` to the path of
`circt-opt` when running backend integration tests.

### First circuit

```rhombus
#lang rhodium

circuit Adder(width :: PosInt):
  input(a, b): Bits(width)
  output sum: Bits(width)
  sum <== a + b

def design = elaborate(Adder(8))

export:
  Adder
  design
```

Run the standard adder example from the checkout:

```sh
tools/run-racket-tests.sh examples/lop/adder-standard.rhdl
```

Run all canonical examples:

```sh
make examples
```

The [test runner guide](tests/README.md) explains the available validation
levels. Contributor setup and change validation are in
[`DEVELOPING.md`](DEVELOPING.md#validate-at-the-owning-boundary).

## Mental model

A Rhodium source file contains two kinds of computation:

- **Host computation** is ordinary Rhombus. Host values, functions, loops, and
  conditionals decide what hardware is generated during elaboration.
- **Hardware computation** describes runtime signals, state, memories, and
  hierarchy. Hardware values have explicit types and cannot control ordinary
  host conditionals.

Calling `elaborate` runs the host program, constructs the selected hardware,
and verifies the completed design. Macro expansion and frontend layers do not
create intermediate hardware languages: every authoring path converges on the
same public core IR.

### Authoring profiles

| Profile | Intended use | Provides |
|---|---|---|
| `#lang rhodium` | Normal design work | Rhombus, the foundational circuit surface, and the curated frontend layers |
| `#lang rhodium/base` | Language composition and focused extensions | Rhombus and the foundation; the program explicitly imports any additional layers |

The foundation supplies circuits, ports, connections, elaboration, and basic
hardware types. Selectable layers add notation, types, static information, and
authoring policy. Ordinary standard and domain libraries build on the public
language; they are not compiler layers.

## Architecture

```mermaid
flowchart TB
    standard["#lang rhodium<br/>foundation + curated layers"]
    base["#lang rhodium/base<br/>foundation + explicit layer imports"]
    libraries["Standard and domain libraries<br/>ordinary public Rhodium code"]
    frontend["Frontend notation, types,<br/>static information, and policy"]
    kernel["Elaboration kernel<br/>context-sensitive construction"]
    core["Verified public core IR<br/>types, values, places, operations, and resources"]
    analysis["Analysis and diagrams"]
    formal["Formal engine"]
    circt["Optional CIRCT backend"]
    sv["SystemVerilog"]

    standard --> frontend
    base --> frontend
    libraries --> frontend
    frontend --> kernel
    kernel --> core
    core --> analysis
    core --> formal
    core --> circt
    circt --> sv
```

The architectural boundary is semantic rather than syntactic. A concept belongs
in core only when verification and every backend must preserve its hardware
meaning. Notation, organization, reusable host descriptions, and policy over
existing operations belong in frontend layers or ordinary libraries. Optional
derived facts and reports belong in analysis packages.

The public package map is in [`rhodium/README.md`](rhodium/README.md). Its
enforced implementation graph and direct-dependency inventories are maintained
in [`rhodium/DEVELOPING.md`](rhodium/DEVELOPING.md).

## Design commitments

- One public, inspectable hardware IR rather than frontend-specific IRs.
- Explicit widths and conversions, fixed-width arithmetic, and deterministic
  host elaboration.
- Readable values and driveable places with exactly one effective driver.
- Frontend-defined types and notation that reuse core semantics whenever
  possible.
- Backends consume verified IR and never import frontend syntax.
- CIRCT owns SystemVerilog generation.

The [Rhodium comparison guide](docs/comparisons/README.md) places these choices
alongside construction languages, rule-based and functional HDLs, timing-typed
research languages, compiler IRs, multi-level modeling systems, and
SystemVerilog.

## Explore the project

### Learn the language

- [`examples/README.md`](examples/README.md) — executable language walkthrough
  and example catalog
- [`rhodium/frontend/README.md`](rhodium/frontend/README.md) — elaboration,
  profiles, and extension boundaries
- [`rhodium/frontend/layers/README.md`](rhodium/frontend/layers/README.md) —
  frontend feature and syntax guide
- [`rhodium/std/README.md`](rhodium/std/README.md) — host utilities, protocols,
  and reusable circuit generators

### Inspect or extend the implementation

- [`DEVELOPING.md`](DEVELOPING.md) — contributor entry point and change workflow
- [`rhodium/README.md`](rhodium/README.md) and
  [`rhodium/DEVELOPING.md`](rhodium/DEVELOPING.md) — public package map and
  enforced implementation architecture
- [`rhodium/core/README.md`](rhodium/core/README.md) and
  [`rhodium/core/DEVELOPING.md`](rhodium/core/DEVELOPING.md) — public IR
  semantics and core implementation guidance
- [`rhodium/analysis/README.md`](rhodium/analysis/README.md) — clock/reset
  inventory and temporal provenance
- [`rhodium/diagram/README.md`](rhodium/diagram/README.md) — logical hierarchy,
  interface, and flow diagrams
- [`rhodium/event/README.md`](rhodium/event/README.md) — event dependency
  inference and compiler instrumentation
- [`rheg/README.md`](rheg/README.md) — C++ event collection and streaming or
  standalone Perfetto export
- [`rhodium/backend/README.md`](rhodium/backend/README.md) — CIRCT lowering and
  SystemVerilog generation
- [`rhodium/formal/README.md`](rhodium/formal/README.md) — Rosette equivalence,
  reachability, and output properties
- [`tests/README.md`](tests/README.md) and
  [`tests/DEVELOPING.md`](tests/DEVELOPING.md) — running validation and
  maintaining the test architecture

### Explore hardware libraries and systems

- [`flow/README.md`](flow/README.md) — streaming buffers, arbitration, routing,
  packet adapters, and typed pipeline composition
- [`devicetree/README.md`](devicetree/README.md) — validated host-side device
  trees with native DTS and DTB encoding
- [`noc/README.md`](noc/README.md) — graph-validated NoC authoring and hardware
  bridge
- [`riscv/README.md`](riscv/README.md) — RISC-V instruction model and Rhodium
  adapter
- [`hardfloat/README.md`](hardfloat/README.md) — Berkeley HardFloat port
- [`chi/README.md`](chi/README.md) and [`devices/README.md`](devices/README.md) —
  AMBA CHI and platform devices
- [`cores/README.md`](cores/README.md) and [`socs/README.md`](socs/README.md) —
  reusable processors and SoC composition
- [`sims/README.md`](sims/README.md) — executable SoC simulation harnesses

### Physical and development tooling

- [`rfpl/README.md`](rfpl/README.md) — physical views over existing Rhodium
  circuits
- [`sram/README.md`](sram/README.md) — technology-independent SRAM mapping
- [`vlsi/README.md`](vlsi/README.md) — physical integration and mapped simulation
- [`support/README.md`](support/README.md) — dependency-neutral Rhombus
  refinements
- [`tools/emacs/README.md`](tools/emacs/README.md) — project-aware Emacs
  integration

## Contributing

Read [`DEVELOPING.md`](DEVELOPING.md) before changing implementation packages,
tests, generated references, or documentation ownership. Package-level
`DEVELOPING.md` files refine that repository-wide workflow without redefining
their sibling README's public contract.

## Current status

The current vertical slice includes:

- A public, backend-independent IR with explicit-width types, structural
  aggregates, state, memories, assertions, DPI simulation operations,
  single-driver verification, and combinational-cycle detection.
- Standard and compositional profiles with host-only generation,
  frontend-defined scalar and aggregate types, combinational and sequential
  constructs, hierarchy, directional interfaces, and reusable protocols.
- Deterministic CIRCT lowering, example-owned SystemVerilog references, and
  Verilator simulations.
- Optional Rosette-backed equivalence, reachability, and universal output
  properties over verified IR.
- Backend-independent clock/reset inventory, temporal-provenance reports, and
  durable crossing evidence.
- Logical diagrams plus RFPL physical annotations that leave logical IR and
  generated RTL unchanged.
- RV5Stage, a five-stage RV32/RV64 processor with floating point, privilege and
  trap state, private coherent L1 caches, and CHI integration. See
  [`cores/rv5stage/README.md`](cores/rv5stage/README.md) for its current contract.

## Deferred work

- Memory initialization, masks on asynchronous-read memories, general
  multi-port synchronous memories, and defined inter-port collisions
- Asynchronous reset, reset-polarity metadata, and policy/approval semantics
  for crossings identified by multi-domain temporal analysis
- Runtime-loaded operation dialects
- Multi-role protocols, optional interface fields, and generated protocol
  assertions
- User-authored IR mutation and rewriting before a concrete transformation
  defines transaction and handle-validity requirements

Contributor-facing hardening priorities are tracked in
[`DEVELOPING.md`](DEVELOPING.md#maintain-compatibility-and-generated-artifacts).
