<!-- Documents Rhodium's mirrored test organization and focused verification commands. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rhodium test orchestration

Choose validation in two steps: start with the package that owns the change,
then add only the deeper toolchain stage needed to test its observable effect.
The root [`Makefile`](../../Makefile) defines the aggregate targets; package guides
own narrower workflows and evolving fixture details.

Contributors adding or reorganizing tests should read
[`DEVELOPING.md`](DEVELOPING.md) for placement, CI ownership, and maintenance
policy.

## Start with the change owner

Tests live with their owning implementation, not in this tooling directory.
The compiler layers use `rhodium/<layer>/tests/`; libraries, protocols, cores,
devices, and systems use their package-local `tests/` directories. This guide
maps those owners to aggregate commands:

| Owner | Focused target |
|---|---|
| Rhodium core, analysis, and frontend | `make frontend-test` |
| Standard library | `make std-test` |
| Flow library | `make flow-test` |
| Rhodium backend | `make backend-test` |
| Optional formal engine | `make formal-test` |
| Emacs mode | `make emacs-test` |

`make frontend-test` deliberately combines core, analysis, and frontend tests,
then exercises the intentional failures in `rhodium/frontend/tests/invalid/`. Use
`make analysis-test` when an analysis-only change does not need that wider
surface. `make backend-test` runs Rhombus tests for emission, diagnostics, and
backend policy; it does not invoke CIRCT or Verilator.

Other libraries and systems use their owning package targets:

| Change area | Host or package target | Owning guide |
|---|---|---|
| Language-layer equivalence | `make lop-test` | [Frontend](../../rhodium/frontend/README.md) |
| Logical diagrams | `make diagram-test` | [Diagrams](../../rhodium/diagram/README.md) |
| Event inference and instrumentation | `make event-test` | [Event graphs](../../rhodium/event/README.md) |
| Runtime event lineage (CIRCT/Verilator) | `make event-runtime-test` | [Event graphs](../../rhodium/event/README.md) |
| Device-tree model and encoders | `make devicetree-test` | [Device trees](../../devicetree/README.md) |
| RFPL views and constraints | `make rfpl-test` | [RFPL](../../rfpl/README.md) |
| Pure NoC model and hardware planning | `make noc-test` | [NoC](../../noc/README.md) |
| RISC-V model and instruction catalogs | `make riscv-test` | [RISC-V](../../riscv/README.md) |
| Platform devices | `make device-test` | [Devices](../../devices/README.md) |
| CHI protocol and invalid connections | `make chi-test` | [CHI](../../chi/README.md) |
| SoC composition and planning | `make soc-test` | [SoCs](../../socs/README.md) |
| HardFloat host semantics | `make hardfloat-host-test` | [HardFloat](../../hardfloat/README.md) |
| Reusable processor components and RV5Stage | `make rv5stage-host-test` | [Cores](../../cores/README.md) and [RV5Stage](../../cores/rv5stage/README.md) |

Canonical valid authoring programs live under
[`../examples/`](../../examples/README.md). Select the matching example group
instead of sweeping every example:

| Area | Target |
|---|---|
| Built-in language | `make examples-rhodium` |
| Clocking analysis | `make examples-clocking` |
| Standard library | `make examples-std` |
| NoC | `make examples-noc` |
| Authoring-layer comparisons | `make examples-lop` |
| RFPL | `make examples-rfpl` |
| RISC-V | `make examples-riscv` |
| CHI | `make examples-chi` |
| Processor components | `make examples-cores` |
| RV5Stage | `make examples-rv5stage` |

`make examples` runs every non-formal example. The optional Rosette example has
its own `make examples-formal` target. Use `make check-example-verilog` when the
only question is whether example designs have valid reference exports and
backend-manifest coverage; that check does not invoke CIRCT or Verilator.

## Increase validation depth deliberately

### Host and model checks

For one Racket or Rhombus test, use the repository wrapper so the run receives
the persistent compiled root owned by this worktree under
`.rhodium-cache/racket-build/`. Later invocations reuse project and dependency
bytecode incrementally. Set `RHODIUM_RACKET_CACHE_DIR` to relocate the complete
cache to another writable absolute directory; project roots remain separated by
absolute checkout path when several worktrees intentionally use the same base.

```sh
tools/run-racket-tests.sh rhodium/core/tests/verify-test.rhm
tools/run-racket-tests.sh rhodium/frontend/tests/interface-test.rhm
tools/run-racket-tests.sh rhodium/backend/tests/circt-test.rhm
```

Each absolute checkout path receives a separate mutable project root and lock.
The wrapper hashes source content and invalidates changed modules plus their
transitive project dependents before Racket recompiles them. It clears the
complete checkout bytecode subtree when the source-path inventory changes, so
branch switches and module moves cannot retain orphaned project modules. A
dependency snapshot contains no checkout bytecode and becomes immutable when
published. Supplying `PLTCOMPILEDROOTS` explicitly is reserved for an external
harness that deliberately owns and bypasses the managed cache. Cache setup and
lock failures stop before the requested command; they never retry against an
uncached root. Use `tools/racket-build-cache.sh status` to inspect a lock and
`make clean-racket-cache` to discard the current worktree's project bytecode.

Then move to the owning target from the tables above. Add
`make check-boundaries` after moving modules or changing dependency direction.
These checks do not require CIRCT or Verilator. Some package targets can still
exercise ordinary native helpers; for example, `make device-test` includes the
standalone UART DPI C++ check.

### External CIRCT and Verilator checks

Use external checks when a change can affect CIRCT MLIR, generated
SystemVerilog, or runtime hardware behavior. The neutral CIRCT runner owns
fixture selection, grouping, tool discovery, exact-reference policy, temporary
artifacts, and expected-failure simulations; packages own the selected
emitters and benches. See the [CIRCT test guide](circt/README.md).

| Target | External work |
|---|---|
| `make circt-verify-test` | Lower the curated backend spine through CIRCT without golden comparison or simulation |
| `make circt-test` | Lower the curated spine, compare eligible references, and run available Verilator simulations |
| `make verilator-test` | Lower and simulate the curated fixtures that have benches, without golden comparison |
| `make verilog-golden-test` | Compare every example-backed fixture with its exact reference using the pinned CIRCT version |
| `make circt-full-test` | Lower every backend-manifest fixture, compare all eligible references, and run every available backend-manifest simulation |
| `make rfpl-circt-test` | Run RFPL's separately owned CIRCT and Verilog-reference fixture |
| `make hardfloat-circt-test` | Run HardFloat's separately owned CIRCT and Verilator fixtures |
| `make rv5stage-test` | Run RV5Stage host checks, then its focused backend fixture set |

For one backend fixture, use `FIXTURE=name`; for a small batch, use the
space-separated `FIXTURES` selector:

```sh
FIXTURE=bundle bash tools/testing/circt/run.sh
FIXTURES='bundle interface-array' bash tools/testing/circt/run.sh
```

Do not copy the changing fixture catalog here. The manifest and all supported
selectors and modes are documented in
[`circt/README.md`](circt/README.md#choose-the-smallest-useful-run).

Executable FESVR transport, DPI binding, reusable SoC simulators, and mapped
MiniRV5StageSoC smoke tests are a separate external-toolchain boundary owned by the
[simulation guide](../../sims/README.md), not by the backend fixture runner.

### Optional formal checks

Formal validation is solver-backed and remains outside ordinary host and
the broad `make test` aggregate:

| Target | Work performed |
|---|---|
| `make formal-test` | Probe Rosette and Z3, then run the formal API, semantics, reachability, property, witness, and fail-closed suite |
| `make examples-formal` | Run the optional formal example |
| `make formal-differential-test` | Replay selected formal models independently through CIRCT and Verilator |

The [formal guide](../../rhodium/formal/README.md) owns supported semantics,
dependency versions, and troubleshooting.

### Aggregate targets

Use aggregate targets only when the change spans their full scope:

| Target | Scope |
|---|---|
| `make unit-test` | Core, analysis, frontend, invalid-frontend, and backend host tests |
| `make host-checks` | Host tests, package models, protocols, cores, SoCs, and repository hygiene without the explicit example sweep |
| `make host-test` | `host-checks` plus every non-formal example |
| `make test` | `host-test`, the curated backend CIRCT spine, RFPL CIRCT, and HardFloat CIRCT/Verilator checks |
| `make ci-plan-test` | Validate CI path selection, tracked executable coverage, and the stable gate contract |

`make test` is the broad repository aggregate, not an exhaustive superset. It
excludes the optional formal and Emacs suites, the full backend manifest, and
executable SoC simulation. Run those owners explicitly when the change requires
them.

## Contributing tests

See [`DEVELOPING.md`](DEVELOPING.md) for test placement, authoring principles,
CI classification, fixture maintenance, and generated-artifact policy.
