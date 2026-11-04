<!-- Guides contributors through Rhodium's repository architecture, change workflow, and validation ownership. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing Rhodium

Read the user-facing [`README.md`](README.md) first for the language model,
quick start, public capabilities, and project navigation. This guide is for
changes to the repository itself. This guide and the nearest component
`DEVELOPING.md` own architecture, workflow, and maintenance detail.

## Set up a development checkout

Install the Racket and Rhombus versions listed in the
[quick start](README.md#requirements). CIRCT, Verilator, Rosette, FESVR, PDKs,
and physical-design tools are optional until a change crosses their owning
boundary. Install the pinned CIRCT release when backend lowering or generated
SystemVerilog is in scope:

```sh
moon run rhodium:setup-circt
```

Keep generated Racket bytecode, CIRCT output, SystemVerilog, Verilator builds,
and physical-flow artifacts out of version control unless an owning guide
explicitly defines a checked-in reference.

Repository-owned serialized formats and internal adapter contracts are
unversioned. Change producers, consumers, and fixtures together; regenerate
stale artifacts instead of maintaining format-version fields, numbered adapter
identities, compatibility readers, or migration paths. Keep structural and
semantic validation, format-kind discriminators, and source/build/configuration
fingerprints. Versions required by external formats, architectural
specifications, and dependency/toolchain pins remain part of those contracts.

## Find the change owner

Choose the lowest package that owns the behavior before editing:

| Change | Start with |
|---|---|
| Public IR meaning, Builder behavior, or universal verification | [`rhodium/core/DEVELOPING.md`](rhodium/core/DEVELOPING.md) |
| Elaboration, profiles, or frontend extension machinery | [`rhodium/frontend/DEVELOPING.md`](rhodium/frontend/DEVELOPING.md) |
| One independently selectable authoring layer | [`rhodium/frontend/layers/DEVELOPING.md`](rhodium/frontend/layers/DEVELOPING.md) |
| Reusable public hardware library | [`rhodium/std/DEVELOPING.md`](rhodium/std/DEVELOPING.md) |
| Streaming components and configured topology stages | [`flow/DEVELOPING.md`](flow/DEVELOPING.md) |
| Backend-independent analysis | [`rhodium/analysis/DEVELOPING.md`](rhodium/analysis/DEVELOPING.md) |
| Portable program materialization | [`rhodium/lowering/DEVELOPING.md`](rhodium/lowering/DEVELOPING.md) |
| Explicit target compilation | [`rhodium/compile/DEVELOPING.md`](rhodium/compile/DEVELOPING.md) |
| CIRCT lowering | [`rhodium/backend/DEVELOPING.md`](rhodium/backend/DEVELOPING.md) |
| Logical diagram extraction or rendering | [`rhodium/diagram/DEVELOPING.md`](rhodium/diagram/DEVELOPING.md) |
| Event dependency inference or compiler instrumentation | [`rhodium/event/DEVELOPING.md`](rhodium/event/DEVELOPING.md) |
| C++ event collection or Perfetto export | [`rheg/DEVELOPING.md`](rheg/DEVELOPING.md) |
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
| Target software sources, ports, patches, or ELF builders | [`sw/DEVELOPING.md`](sw/DEVELOPING.md) |
| Reusable processor component or named core | [`cores/DEVELOPING.md`](cores/DEVELOPING.md) |
| Shared physical cache, geometry, or cache-side CHI engine | [`cores/cache/DEVELOPING.md`](cores/cache/DEVELOPING.md) |
| RV5Stage pipeline or integration | [`cores/rv5stage/DEVELOPING.md`](cores/rv5stage/DEVELOPING.md) |
| RV5Stage instruction fetch or prediction | [`cores/rv5stage/fetch/DEVELOPING.md`](cores/rv5stage/fetch/DEVELOPING.md) |
| RV5Stage decode | [`cores/rv5stage/decode/DEVELOPING.md`](cores/rv5stage/decode/DEVELOPING.md) |
| RV5Stage floating-point execution | [`cores/rv5stage/fp/DEVELOPING.md`](cores/rv5stage/fp/DEVELOPING.md) |
| RV5Stage CHI configuration or transaction engines | [`cores/rv5stage/chi/DEVELOPING.md`](cores/rv5stage/chi/DEVELOPING.md) |
| RV5Stage translation | [`cores/rv5stage/mmu/DEVELOPING.md`](cores/rv5stage/mmu/DEVELOPING.md) |
| Shared physical instruction/data caches and RV5Stage fetch metadata | [`cores/cache/DEVELOPING.md`](cores/cache/DEVELOPING.md), [`cores/rv5stage/icache/DEVELOPING.md`](cores/rv5stage/icache/DEVELOPING.md) |
| SRAM occurrence mapping or schemas | [`sram/DEVELOPING.md`](sram/DEVELOPING.md) |
| Sky130 SRAM catalog or model | [`sram/sky130/DEVELOPING.md`](sram/sky130/DEVELOPING.md) |
| VLSI prototype or physical handoff | [`vlsi/DEVELOPING.md`](vlsi/DEVELOPING.md) |
| Mapped VLSI simulation | [`vlsi/sim/DEVELOPING.md`](vlsi/sim/DEVELOPING.md) |
| Emacs integration | [`tools/emacs/DEVELOPING.md`](tools/emacs/DEVELOPING.md) |
| Example catalog or generated example Verilog | [`examples/DEVELOPING.md`](examples/DEVELOPING.md) |
| Comparison evidence or rubric | [`docs/comparisons/DEVELOPING.md`](docs/comparisons/DEVELOPING.md) |
| Package graph or allowed dependency direction | [`rhodium/DEVELOPING.md`](rhodium/DEVELOPING.md) |
| Test placement, CI ownership, or validation infrastructure | [`tools/testing/DEVELOPING.md`](tools/testing/DEVELOPING.md) |
| CIRCT fixture, Verilator bench, or exact Verilog reference | [`tools/testing/circt/DEVELOPING.md`](tools/testing/circt/DEVELOPING.md) |

## Contribution and provenance

Except where a file or directory states otherwise, contributions intentionally
submitted for inclusion in Rhodium are accepted under Apache-2.0. Every commit
must certify the [Developer Certificate of Origin](DCO) with a real-name trailer:

```text
Signed-off-by: Contributor Name <contributor@example.com>
```

Create that trailer with `git commit -s`. A contributor must have the right to
submit the complete contribution, including employer- or institution-owned work,
material produced with automated tools, and modifications derived from other
projects. Identify third-party provenance and preserve its notices before the
work is incorporated. Do not add code under terms incompatible with the owning
directory's license.

The complete `hardfloat/` package retains its documented BSD license and source
headers. Git submodules retain the terms of their pinned upstream repositories.
See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for the current exception
inventory. The project does not currently require a separate contributor license
agreement.

New original source and documentation files must carry
`SPDX-License-Identifier: Apache-2.0` in the format's native comment syntax,
after any mandatory first line and the file-specific purpose comment. Preserve
a different SPDX identifier and its notices when modifying separately licensed
material. Exact license and notice texts without a comment syntax are exempt
from the source-header requirement. Run `moon run rhodium:check-license-headers` to check
the tracked-file inventory. CI runs that complete check, while the supplied
pre-commit hook checks the exact staged contents through
`moon run rhodium:check-license-headers-staged`.

## Preserve the architecture

Rhodium has one frontend-independent public hardware IR. Frontend notation and
ordinary libraries construct that IR; optional analyses, formal queries,
diagrams, and backends consume it. A change should move downward only when its
semantics must be preserved by verification and every backend.

The authoritative implementation graph, package responsibilities, and audited
direct-dependency inventories are in
[`rhodium/DEVELOPING.md`](rhodium/DEVELOPING.md). Do not add a reverse import to
avoid designing the correct public boundary. Run `moon run rhodium:check-boundaries` after
moving a module or changing dependency direction.

## Make one owned change

For first-party hardware and its elaboration logic, follow the repository
[`RTL style guide`](RTL_STYLE.md). It owns general RTL authoring conventions;
the nearest component guide owns local architecture and timing invariants.

1. Read the owning README for the current public contract and deliberate
   limits.
2. Read its DEVELOPING guide for source ownership, extension points, and
   focused tests.
3. Change the narrowest layer that owns the behavior. Keep reusable processor
   components, domain models, SoC policy, simulation policy, and backend policy
   in their existing packages.
4. Follow the test-quality policy in [`tools/testing/DEVELOPING.md`](tools/testing/DEVELOPING.md):
   prefer observable end-to-end behavior, and do not add a module-local test
   merely to prove that a circuit elaborates or passes `verify_design`.
5. Update the owning public contract only when observable behavior changes;
   update DEVELOPING when architecture, source ownership, or maintenance
   procedure changes.

## Validate at the owning boundary

The [test runner guide](tools/testing/README.md) maps change areas to focused commands
and explains when CIRCT, Verilator, formal, or aggregate checks are useful. The
[test developer guide](tools/testing/DEVELOPING.md) owns test placement, managed
compiled roots, CI classification, fixtures, and checked-in artifacts.

Start with the smallest owning target. Add `moon run rhodium:check-boundaries` for package
movement or import changes, and use broader targets only when the change spans
their scope.

Use [Moon 2.6.5](https://github.com/moonrepo/moon/releases/tag/v2.6.5) for repository
tests, examples, suites, and named build products.
From a fresh checkout, generate the
ignored catalog before invoking Moon; it constructs its graph before running a task.

```sh
python3 -m tools.ci.catalog
moon run rhodium:check--host-hygiene
moon run rhodium:frontend-test
echo '{"files":["cores/superscalar/core.rhdl"]}' | moon query affected --downstream deep
```

For a configured benchmark, install the [simulator prerequisites](sims/README.md#build-a-simulator)
and put `riscv64-unknown-elf-gcc` on `PATH`. Then prepare its software sources
and run the named product:

```sh
moon run rhodium:coremark-setup rhodium:target--simple-rv2wide-rva23
python3 -m tools.ci.catalog --soc simple-rv2wide-rva23
moon run rhodium:program--simple-rv2wide-rva23--coremark
```

Repeat `--soc` to share compatible ELF builds across configurations, running each
`rhodium:target--SOC` first. The catalog command only reads those descriptors; it
does not build them. Refresh the descriptors and catalog after changing branches,
inputs, pins, or SoC configuration. `python3 -m tools.ci.catalog` without options
returns to the base graph; it needs neither descriptors nor a guest
toolchain for host checks. Local runs need no CI environment variables.
Invoke Moon directly from the repository root, with configuration options in the
environment. Multiple targets enter one Moon graph. Moon owns test inventories,
commands, and suite dependencies in
[`.moon/tasks/tests.yml`](.moon/tasks/tests.yml), while
[`.moon/tasks/checks.yml`](.moon/tasks/checks.yml) assigns hosted coverage.
Setup commands live in [`.moon/tasks/setup.yml`](.moon/tasks/setup.yml), and
software-builder recipes live in [`.moon/tasks/software.yml`](.moon/tasks/software.yml).
Use the managed Racket wrappers or fixture runners for individual files or fixtures.
Simulator execution templates
live in [`.moon/tasks/execution.yml`](.moon/tasks/execution.yml); native binary
checks and local physical lint live in
[`.moon/tasks/native.yml`](.moon/tasks/native.yml). Moon owns descriptor emission,
RTL lowering, native objects/archives/links, firmware stages, SRAM mapping, physical
flows, and test aggregation. The `*-build.yml` task files declare those stages;
[`catalog.py`](tools/ci/catalog.py) instantiates configured model and workload families.
Make/Ninja remain only inside upstream builds and compiler-generated Verilator/CMake
trees. Build-time artifact integrity checks remain with their producer.

Moon caches portable stage outputs beneath `.rhodium-cache/moon/`. Simulator
compiler work directories retain the existing `BUILD_ROOT` (default
`/tmp/rhodium-sims`): cache misses reuse their object files, and cached emission is
copied only when bytes differ. Publication verifies or records the exact simulator
identity and stages its descriptor. Do not cache entire compiler trees or place
Moon output declarations at absolute external paths. Physical outputs remain under
`vlsi/build/`; CMake retains its incremental SRAM and co-simulation build trees.
The [CI ownership guide](tools/testing/DEVELOPING.md#ci-ownership)
explains how hosted selection and artifact handoffs differ.

Documentation-only changes require, at minimum, purpose-header, path, anchor,
code-fence, Mermaid-structure, and `git diff --check` validation. Confirm every
documented command and source path against the current repository rather than
copying a stale catalog.

## Maintain documentation ownership

Keep public usage, behavior, and limits in README files. Keep architecture,
source ownership, change workflow, and validation procedures in DEVELOPING
guides. Link between the pair instead of duplicating their content.

## Maintain compatibility and generated artifacts

Generated Verilog references are version-specific reviewed artifacts, not a
general build artifact. Follow
[`tools/testing/circt/DEVELOPING.md`](tools/testing/circt/DEVELOPING.md) before changing
them. Other generated Racket, CIRCT, Verilator, simulation, and physical-flow
outputs remain untracked.

Hardening work should favor clear diagnostics, deterministic output,
property-based and differential testing, and an explicit public-IR
compatibility policy. User-visible unsupported features remain listed in the
owning README; implementation plans and maintenance work belong in the owning
DEVELOPING guide or a dedicated plan document.
