<!-- Documents Rhodium's mirrored test organization and focused verification commands. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rhodium test orchestration

Choose validation in two steps: start with the package that owns the change,
then add only the deeper toolchain stage needed to test its observable effect.
Use [the Moon quick start](../../DEVELOPING.md#validate-at-the-owning-boundary)
to run a named check, simulator, or benchmark, or to inspect affected products.
For CI artifact and selection rules, see [CI ownership](DEVELOPING.md#ci-ownership).
[`tests.yml`](../../.moon/tasks/tests.yml) defines test commands and suites;
[`checks.yml`](../../.moon/tasks/checks.yml) defines their hosted coverage.
Package guides own narrower workflows and evolving fixture details.
Run `moon run rhodium:<test-name>` from the repository root. Supply configuration
options as environment variables before the command.

Contributors adding or reorganizing tests should read
[`DEVELOPING.md`](DEVELOPING.md) for placement, CI ownership, and maintenance
policy.

## Run hardware behavior directly

Rsim is the default for portable component behavior:

```sh
python3 tools/testing/rsim/run.py --fixture queue
python3 tools/testing/rsim/run.py --group cores-components
moon run rhodium:hardfloat-rsim-test
moon run rhodium:rsim-component-test
```

The [rsim guide](rsim/README.md) describes fixture selection, compiler
requirements, direct `eval()`/`tick_model()` semantics, and retained failure
artifacts. CIRCT remains responsible for HDL emission references and HDL-specific
integration; named-core and SoC simulation retain their existing harnesses.

## Start with the change owner

Tests live with their owning implementation, not in this tooling directory.
The compiler layers use `rhodium/<layer>/tests/`; libraries, protocols, cores,
devices, and systems use their package-local `tests/` directories. This guide
maps those owners to aggregate commands:

| Owner | Focused target |
|---|---|
| Rhodium core, analysis, and frontend | `moon run rhodium:frontend-test` |
| Standard library | `moon run rhodium:std-test` |
| Flow library | `moon run rhodium:flow-test` |
| Rhodium backend | `moon run rhodium:backend-test` |
| Optional formal engine | `moon run rhodium:formal-test` |
| Emacs mode | `moon run rhodium:emacs-test` |

`moon run rhodium:frontend-test` deliberately combines core, analysis, and frontend tests,
then exercises the intentional failures in `rhodium/frontend/tests/invalid/`. Use
`moon run rhodium:analysis-test` when an analysis-only change does not need that wider
surface. `moon run rhodium:backend-test` runs Rhombus tests for emission, diagnostics, and
backend policy; it does not invoke CIRCT or Verilator.

Other libraries and systems use their owning package targets:

| Change area | Host or package target | Owning guide |
|---|---|---|
| Language-layer equivalence | `moon run rhodium:lop-test` | [Frontend](../../rhodium/frontend/README.md) |
| Logical diagrams | `moon run rhodium:diagram-test` | [Diagrams](../../rhodium/diagram/README.md) |
| Event inference and instrumentation | `moon run rhodium:event-test` | [Event graphs](../../rhodium/event/README.md) |
| Runtime event lineage (CIRCT/Verilator) | `moon run rhodium:event-runtime-test` | [Event graphs](../../rhodium/event/README.md) |
| Device-tree model and encoders | `moon run rhodium:devicetree-test` | [Device trees](../../devicetree/README.md) |
| RFPL views and constraints | `moon run rhodium:rfpl-test` | [RFPL](../../rfpl/README.md) |
| Pure NoC model and hardware planning | `moon run rhodium:noc-test` | [NoC](../../noc/README.md) |
| RISC-V model and instruction catalogs | `moon run rhodium:riscv-test` | [RISC-V](../../riscv/README.md) |
| Platform devices | `moon run rhodium:device-test` | [Devices](../../devices/README.md) |
| CHI protocol and invalid connections | `moon run rhodium:chi-test` | [CHI](../../chi/README.md) |
| SoC composition and planning | `moon run rhodium:soc-test` | [SoCs](../../socs/README.md) |
| HardFloat host semantics | `moon run rhodium:hardfloat-host-test` | [HardFloat](../../hardfloat/README.md) |
| Reusable processor components and RV5Stage | `moon run rhodium:rv5stage-host-test` | [Cores](../../cores/README.md) and [RV5Stage](../../cores/rv5stage/README.md) |

Canonical valid authoring programs live under
[`../examples/`](../../examples/README.md). Select the matching example group
instead of sweeping every example:

| Area | Target |
|---|---|
| Built-in language | `moon run rhodium:examples-rhodium` |
| Clocking analysis | `moon run rhodium:examples-clocking` |
| Standard library | `moon run rhodium:examples-std` |
| NoC | `moon run rhodium:examples-noc` |
| Authoring-layer comparisons | `moon run rhodium:examples-lop` |
| RFPL | `moon run rhodium:examples-rfpl` |
| RISC-V | `moon run rhodium:examples-riscv` |
| CHI | `moon run rhodium:examples-chi` |
| Processor components | `moon run rhodium:examples-cores` |
| RV5Stage | `moon run rhodium:examples-rv5stage` |

`moon run rhodium:examples` runs every non-formal example. The optional Rosette example has
its own `moon run rhodium:examples-formal` target. Use `moon run rhodium:check-example-verilog` when the
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
`moon run rhodium:clean-racket-cache` to discard the current worktree's project bytecode.

Then move to the owning target from the tables above. Add
`moon run rhodium:check-boundaries` after moving modules or changing dependency direction.
These checks do not require CIRCT or Verilator. Some package targets can still
exercise ordinary native helpers; for example, `moon run rhodium:device-test` includes the
standalone UART DPI C++ check.

### External CIRCT and Verilator checks

Use external checks when a change can affect CIRCT MLIR, generated
SystemVerilog, or HDL-specific runtime behavior. The neutral CIRCT runner owns
fixture selection, grouping, tool discovery, exact-reference policy, temporary
artifacts, and expected-failure simulations; packages own the selected
emitters and benches. See the [CIRCT test guide](circt/README.md).

| Target | External work |
|---|---|
| `moon run rhodium:circt-verify-test` | Lower the curated backend spine through CIRCT without golden comparison or simulation |
| `moon run rhodium:circt-test` | Lower the curated spine, compare eligible references, and run available Verilator simulations |
| `moon run rhodium:verilator-test` | Lower and simulate the curated fixtures that have benches, without golden comparison |
| `moon run rhodium:verilog-golden-test` | Compare every example-backed fixture with its exact reference using the pinned CIRCT version |
| `moon run rhodium:circt-full-test` | Lower every backend-manifest fixture, compare all eligible references, and run every available backend-manifest simulation |
| `moon run rhodium:rfpl-circt-test` | Run RFPL's separately owned CIRCT and Verilog-reference fixture |
| `moon run rhodium:hardfloat-rsim-test` | Run HardFloat's rsim C++ fixtures |
| `moon run rhodium:rv5stage-test` | Run RV5Stage host checks, then its focused backend fixture set |

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
the broad `moon run rhodium:test` aggregate:

| Target | Work performed |
|---|---|
| `moon run rhodium:formal-test` | Probe Rosette and Z3, then run the formal API, semantics, reachability, property, witness, and fail-closed suite |
| `moon run rhodium:examples-formal` | Run the optional formal example |
| `moon run rhodium:formal-differential-test` | Replay selected formal models independently through CIRCT and Verilator |

The [formal guide](../../rhodium/formal/README.md) owns supported semantics,
dependency versions, and troubleshooting.

### Aggregate targets

Use aggregate targets only when the change spans their full scope:

| Target | Scope |
|---|---|
| `moon run rhodium:unit-test` | Core, analysis, frontend, invalid-frontend, and backend host tests |
| `moon run rhodium:host-checks` | Host tests, package models, protocols, cores, SoCs, and repository hygiene without the explicit example sweep |
| `moon run rhodium:host-test` | `host-checks` plus every non-formal example |
| `moon run rhodium:test` | `host-test`, all rsim component suites, the curated backend CIRCT spine, and RFPL CIRCT checks |

`moon run rhodium:test` is the broad repository aggregate, not an exhaustive superset. It
excludes the optional formal and Emacs suites, the full backend manifest, and
executable SoC simulation. Run those owners explicitly when the change requires
them.

## Contributing tests

See [`DEVELOPING.md`](DEVELOPING.md) for test placement, authoring principles,
CI classification, fixture maintenance, and generated-artifact policy.
