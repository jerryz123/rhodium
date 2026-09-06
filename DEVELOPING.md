<!-- Guides contributors through Rhodium's repository architecture, change workflow, and validation ownership. -->

# Developing Rhodium

Read the user-facing [`README.md`](README.md) first for the language model,
quick start, public capabilities, and project navigation. This guide is for
changes to the repository itself. [`AGENTS.md`](AGENTS.md) contains the
mandatory execution and source-editing guardrails; this guide and the nearest
component `DEVELOPING.md` own architecture, workflow, and maintenance detail.

## Set up a development checkout

Install the Racket and Rhombus versions listed in the
[quick start](README.md#requirements). CIRCT, Verilator, Rosette, FESVR, PDKs,
and physical-design tools are optional until a change crosses their owning
boundary. Install the pinned CIRCT release when backend lowering or generated
SystemVerilog is in scope:

```sh
make setup-circt
```

Keep generated Racket bytecode, CIRCT output, SystemVerilog, Verilator builds,
and physical-flow artifacts out of version control unless an owning guide
explicitly defines a checked-in reference.

## Find the change owner

Choose the lowest package that owns the behavior before editing:

| Change | Start with |
|---|---|
| Public IR meaning, Builder behavior, or universal verification | [`rhodium/core/DEVELOPING.md`](rhodium/core/DEVELOPING.md) |
| Elaboration, profiles, or frontend extension machinery | [`rhodium/frontend/DEVELOPING.md`](rhodium/frontend/DEVELOPING.md) |
| One independently selectable authoring layer | [`rhodium/frontend/layers/DEVELOPING.md`](rhodium/frontend/layers/DEVELOPING.md) |
| Reusable public hardware library | [`rhodium/std/DEVELOPING.md`](rhodium/std/DEVELOPING.md) |
| Backend-independent analysis | [`rhodium/analysis/DEVELOPING.md`](rhodium/analysis/DEVELOPING.md) |
| CIRCT lowering | [`rhodium/backend/DEVELOPING.md`](rhodium/backend/DEVELOPING.md) |
| Logical diagram extraction or rendering | [`rhodium/diagram/DEVELOPING.md`](rhodium/diagram/DEVELOPING.md) |
| Rosette-backed formal semantics | [`rhodium/formal/DEVELOPING.md`](rhodium/formal/DEVELOPING.md) |
| Shared dependency-neutral annotations | [`support/DEVELOPING.md`](support/DEVELOPING.md) |
| Pure RISC-V model or ISA catalog | [`riscv/DEVELOPING.md`](riscv/DEVELOPING.md) |
| RISC-V-to-Rhodium adapter | [`riscv/rtl/DEVELOPING.md`](riscv/rtl/DEVELOPING.md) |
| HardFloat implementation | [`hardfloat/DEVELOPING.md`](hardfloat/DEVELOPING.md) |
| RFPL physical-view implementation | [`rfpl/DEVELOPING.md`](rfpl/DEVELOPING.md) |
| Pure NoC model, proofs, or plans | [`noc/DEVELOPING.md`](noc/DEVELOPING.md) |
| NoC hardware realization | [`noc/rtl/DEVELOPING.md`](noc/rtl/DEVELOPING.md) |
| AMBA CHI protocol library | [`chi/DEVELOPING.md`](chi/DEVELOPING.md) |
| Reusable platform device | [`devices/DEVELOPING.md`](devices/DEVELOPING.md) |
| Concrete SoC composition | [`socs/DEVELOPING.md`](socs/DEVELOPING.md) |
| Executable simulator harness | [`sims/DEVELOPING.md`](sims/DEVELOPING.md) |
| Reusable processor component or named core | [`cores/DEVELOPING.md`](cores/DEVELOPING.md) |
| RV5Stage pipeline or integration | [`cores/rv5stage/DEVELOPING.md`](cores/rv5stage/DEVELOPING.md) |
| RV5Stage decode | [`cores/rv5stage/decode/DEVELOPING.md`](cores/rv5stage/decode/DEVELOPING.md) |
| RV5Stage floating-point execution | [`cores/rv5stage/fp/DEVELOPING.md`](cores/rv5stage/fp/DEVELOPING.md) |
| RV5Stage CHI configuration or transaction engines | [`cores/rv5stage/chi/DEVELOPING.md`](cores/rv5stage/chi/DEVELOPING.md) |
| RV5Stage translation | [`cores/rv5stage/mmu/DEVELOPING.md`](cores/rv5stage/mmu/DEVELOPING.md) |
| RV5Stage private caches | [`cores/rv5stage/icache/DEVELOPING.md`](cores/rv5stage/icache/DEVELOPING.md), [`cores/rv5stage/dcache/DEVELOPING.md`](cores/rv5stage/dcache/DEVELOPING.md) |
| SRAM occurrence mapping or schemas | [`sram/DEVELOPING.md`](sram/DEVELOPING.md) |
| Sky130 SRAM catalog or model | [`sram/sky130/DEVELOPING.md`](sram/sky130/DEVELOPING.md) |
| VLSI prototype or physical handoff | [`vlsi/DEVELOPING.md`](vlsi/DEVELOPING.md) |
| Mapped VLSI simulation | [`vlsi/sim/DEVELOPING.md`](vlsi/sim/DEVELOPING.md) |
| Emacs integration | [`tools/emacs/DEVELOPING.md`](tools/emacs/DEVELOPING.md) |
| Example catalog or generated example Verilog | [`examples/DEVELOPING.md`](examples/DEVELOPING.md) |
| Comparison evidence or rubric | [`docs/comparisons/DEVELOPING.md`](docs/comparisons/DEVELOPING.md) |
| Package graph or allowed dependency direction | [`rhodium/DEVELOPING.md`](rhodium/DEVELOPING.md) |
| Test placement, CI ownership, or validation infrastructure | [`tests/DEVELOPING.md`](tests/DEVELOPING.md) |
| CIRCT fixture, Verilator bench, or exact Verilog reference | [`tests/backend/DEVELOPING.md`](tests/backend/DEVELOPING.md) |

Every documented directory keeps its public contracts in `README.md` and its
implementation architecture, source ownership, extension workflows, and
focused validation in the companion `DEVELOPING.md`.

## Preserve the architecture

Rhodium has one frontend-independent public hardware IR. Frontend notation and
ordinary libraries construct that IR; optional analyses, formal queries,
diagrams, and backends consume it. A change should move downward only when its
semantics must be preserved by verification and every backend.

The authoritative implementation graph, package responsibilities, and audited
direct-dependency inventories are in
[`rhodium/DEVELOPING.md`](rhodium/DEVELOPING.md). Do not add a reverse import to
avoid designing the correct public boundary. Run `make check-boundaries` after
moving a module or changing dependency direction.

## Make one owned change

1. Read the owning README for the current public contract and deliberate
   limits.
2. Read its DEVELOPING guide for source ownership, extension points, and
   focused tests.
3. Change the narrowest layer that owns the behavior. Keep reusable processor
   components, domain models, SoC policy, simulation policy, and backend policy
   in their existing packages.
4. Follow the test-quality policy in [`tests/DEVELOPING.md`](tests/DEVELOPING.md):
   prefer observable end-to-end behavior, and do not add a module-local test
   merely to prove that a circuit elaborates or passes `verify_design`.
5. Update the owning public contract only when observable behavior changes;
   update DEVELOPING when architecture, source ownership, or maintenance
   procedure changes.

## Validate at the owning boundary

The [test runner guide](tests/README.md) maps change areas to focused commands
and explains when CIRCT, Verilator, formal, or aggregate checks are useful. The
[test developer guide](tests/DEVELOPING.md) owns test placement, isolated
compiled roots, CI classification, fixtures, and checked-in artifacts.

Start with the smallest owning target. Add `make check-boundaries` for package
movement or import changes, and use broader targets only when the change spans
their scope. Follow the isolated Racket and Rhombus execution rules in
[`AGENTS.md`](AGENTS.md#verification).

Documentation-only changes require, at minimum, purpose-header, path, anchor,
code-fence, Mermaid-structure, and `git diff --check` validation. Confirm every
documented command and source path against the current repository rather than
copying a stale catalog.

## Maintain documentation ownership

Every documented directory uses two audiences:

- `README.md` owns user, library-consumer, integrator, or flow-operator
  guidance: entry points, public behavior, stable contracts, supported
  configurations, observable failures, and deliberate limits.
- `DEVELOPING.md` owns contributor guidance: implementation architecture,
  source maps, dependency enforcement, extension workflows, test ownership,
  CI, and generated-artifact maintenance.

Link to the owning document rather than copying a contract. A public import or
behavioral restriction remains in README even when its enforcement mechanism
is described in DEVELOPING. A command needed to use a tool remains in README;
test-authoring and change-validation detail belongs in DEVELOPING.

## Maintain compatibility and generated artifacts

Generated Verilog references are version-specific reviewed artifacts, not a
general build product. Follow
[`tests/backend/DEVELOPING.md`](tests/backend/DEVELOPING.md) before changing
them. Other generated Racket, CIRCT, Verilator, simulation, and physical-flow
outputs remain untracked.

Hardening work should favor clear diagnostics, deterministic output,
property-based and differential testing, and an explicit public-IR
compatibility policy. User-visible unsupported features remain listed in the
owning README; implementation plans and maintenance work belong in the owning
DEVELOPING guide or a dedicated plan document.
