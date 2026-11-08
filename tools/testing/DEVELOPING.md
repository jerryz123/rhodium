<!-- Defines test ownership, CI artifact reuse, and incremental bytecode validation. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing Rhodium tests

Read the [test-running guide](README.md) first for validation targets, focused
selectors, toolchain depth, and failure interpretation. This guide owns the
contributor rules for adding and maintaining tests.

## Test placement and ownership

There is no repository-level test package. Every test, fixture, emitter, bench,
and package-specific helper belongs under the lowest package that owns the
behavior, normally in `<package>/tests/`. Compiler tests therefore live under
`rhodium/core/tests/`, `rhodium/analysis/tests/`,
`rhodium/frontend/tests/`, `rhodium/backend/tests/`, and
`rhodium/formal/tests/`; domain tests remain under `flow/tests/`, `noc/tests/`,
`riscv/tests/`, `sw/tests/`, `cores/*/tests/`, and the corresponding package directories.

This directory owns only repository-wide test policy and reusable
orchestration. [`run-negative.rkt`](run-negative.rkt) is a package-neutral
diagnostic harness. [`circt/`](circt/DEVELOPING.md) owns external-tool
orchestration for HDL contracts. [`rsim/`](rsim/README.md) owns direct C++
orchestration; package-local `tests/rsim/` directories own behavioral drivers
and program-export fixtures. Package-local `tests/circt/` directories retain
HDL-specific benches and DPI companions. Canonical valid authoring programs belong under
[`../../examples/`](../../examples/README.md); invalid programs stay beside the
frontend or domain suite that owns their diagnostics.

When moving or adding implementation, move its tests with it and run
`moon run rhodium:check-boundaries`. Do not create a new top-level `tests/` directory or
move package fixtures into `tools/testing/` merely because one runner consumes
them.

Representative compiler integration tests may reuse package-owned event and
UART DPI fixtures. Keep those artifacts with their original owners; their
foreign/runtime boundary requires HDL integration. Scalar and aggregate memory
semantics have dedicated backend differential oracles. The portable SyncRam
behavior suite belongs to rsim and has no duplicate SV bench.

Rsim behavioral fixtures are selected from `rsim/fixtures.tsv`. The
CI lanes cover standard-library/Flow and portable language behavior,
reusable and standalone named-core components, protocols/controllers, and HardFloat. They require a
C++20 compiler but neither CIRCT nor Verilator. Package changes select their
rsim owners; shared rsim or orchestration changes select all rsim checks. HDL golden
and instrumentation lanes remain independently selected. Language behavior
includes arithmetic, aggregates, hierarchy, synchronous clocking, memories,
and assertion enable/reset behavior; the same examples retain their HDL
goldens. RV2Wide core/FP, cache, RV3Wide fetch, prediction, BHT, and MMU
scoreboards, enabled-fetch tracing, and RV32 integration belong to the core lane. RV5Stage composed behavior has separate frontend, control,
datapath, vector, memory, and cache groups, preserving the previous workload
split without HDL dependencies. Use the fixture inventory to select a group;
named-core tracing and FP/vector internal-port observers also run directly on
rsim; co-sim retains its separate HDL integration coverage. Hardware and instrumentation semantics belong on rsim;
retain HDL simulation for emission-sensitive contracts, including DPI ABI/context
and assertion/edge scheduling. Queue, pipeline, arbiter, demux, atomic-fork,
broadcast, join, stall/offer, retained-owner/window, crossbar, feedback, branching,
partial-tracing, OfferRegister, selected-parent, runtime-identity, and
retained-bank event suites use the language rsim group with the production
collector and exact public-transfer graph oracles. Traced Home/subordinate/FESVR, vector,
fetch, cache, page-walk, and multiply workloads use their corresponding rsim
protocol/core groups. Trace and collector changes select those groups as well.
Their optional targets and
link sources are declared in the same fixture inventory.

The host/backend lane also runs the small standalone rsim C++ fixtures, using
the runner's C++ compiler without HDL tools. The backend differential lane
compares the same fixtures through direct SV/Verilator. Rsim's sources, driver,
and independent oracles remain in `rhodium/backend/tests/rsim/`; its generated
models and native binaries are temporary artifacts. The rsim suite also runs
the production PTY UART integration; its UART/host/helper dependencies select
both host/backend and backend differential lanes.
The differential suite also checks the generated SV/DPI binding for rsim
using the unchanged `sims/TestDriver.v` and its native lifecycle runtime; changes
to that driver or runtime select the
backend differential lane. This binding proof requires Verilator and remains
outside the HDL-free rsim suite.

## Authoring principles

- A new hardware module does not require a dedicated test merely because it
  elaborates or passes `verify_design`. Successful compilation and elaboration
  are compiler responsibilities and should be covered once at representative
  integration boundaries, not repeated for every library component.
- For reusable hardware, use rsim tests of cycle-visible behavior:
  outputs, state transitions, reset, handshakes, backpressure, arbitration,
  ordering, faults, and protocol interactions. Drive the emitted C++ model
  directly, following the [rsim driver contract](rsim/README.md#driver-contract).
- Keep host tests for pure host algorithms and configuration, public
  elaboration-time rejection, specialization that removes or changes a public
  interface, and structural metadata consumed by another tool.
- Inspect exact operations, internal instance names, register names, or counts
  only when that IR is the compiler feature under test or an explicitly stable
  downstream contract. Incidental implementation shape is not behavior.
- Test supported behavior and invalid uses of supported features.
- Prefer semantic structure, opcodes, and types over generated temporary names.
- Use language-layer equivalence tests when syntax should lower to existing
  kernel or core meaning.
- Update Verilog references only when backend output changes intentionally, and
  review the example-source diff.
- Keep generated Racket, C++, CIRCT, SystemVerilog, and Verilator output out of
  version control.
- Reserve broader suites for cross-layer, shared-infrastructure, or complete
  backend-pipeline changes.
- Do not add tests of CI planning, workflow text or shell steps, Makefile
  recipes, mocked compilation, test-runner orchestration, build caches, or
  artifact packaging and reuse, or self-tests of repository hygiene checkers.
  Validate infrastructure changes through the
  actual affected build and behavioral test path. Keep production integrity
  checks and failure handling in the tools themselves.

## CI ownership

Moon is the sole product graph. [Shared file groups](../../.moon/tasks/inputs.yml),
[executable products](../../.moon/tasks/products.yml)
and [static checks](../../.moon/tasks/checks.yml) are native moon declarations.
[Tests and suites](../../.moon/tasks/tests.yml) own the executable test
commands, positive-test/example file groups, and suite dependencies formerly
kept in the root Makefile. The CI bytecode task passes Moon-expanded file groups
directly to the compiler wrapper; it does not query Make for another manifest.
Moon's `*-build.yml` tasks own first-party stage dependencies; upstream and generated
compiler builds retain their native Make/Ninja engines.
[Configured execution](../../.moon/tasks/execution.yml) invokes the native and
ACT runners directly, preserving simulator attestation, suite bounds, shards,
resume behavior, and failure reports. [Native checks](../../.moon/tasks/native.yml)
and products declare their binary producers as Moon dependencies. Hosted execution
options are environment variables. Build jobs request upstream execution; staged
consumer jobs explicitly use `--upstream none`, and harness jobs run direct payload
dependencies only when they are not consuming a downloaded ELF archive.
[catalog.py](../ci/catalog.py) declares each configured task family's command, inputs,
and prerequisites together. With resolved ELF requests it declares archive producers
and bindings first, so a consumer can use its final manifest directly. Normal and
instrumented models instantiate the same stage templates; an instrumented model
does not build a normal model as a prerequisite.
[inputs.py](../ci/inputs.py) expands source and fixture groups from the owning inventories.
It reads rsim's validated manifest records without requiring the source paths to
exist, and asks the CIRCT runner for its input patterns; neither manifest format
is parsed a second time by CI. Hardware source selection comes from
[the simulator inventory](../../sims/model_inputs.py).
[catalog.py](../ci/catalog.py) also exposes configuration generation as a CLI.
[moon.py](../ci/moon.py) is a query-only library for reading Moon's task metadata and graph;
it has no execution wrapper or command-line entry point.
[policy.py](../ci/policy.py) owns hardware/software enrollment, not GitHub jobs or worker assignment.
[actions.py](../ci/actions.py) asks Moon which endpoints a diff affects, then uses
Moon's action graph to collect their prerequisites. It returns task IDs, tags, and
expected receipts, never job matrices or dependency edges. Setup prepares the catalog
once; selection reads it without regenerating task families. Removed diff paths
refresh only the input groups before querying task metadata. The selector does not
run selection regression tests or start an outer Moon task to invoke itself.
The lightweight receipt-gate check runs directly in Python. The checked-in
[workflow](../../.github/workflows/ci.yml) owns static jobs, matching producer/consumer
dependencies, and fixed worker matrices. Selection uses `ubuntu-24.04` with a
ten-minute budget; each Moon query reports start and elapsed time on stderr so
the selection JSON stays machine-readable and a timeout identifies the active query.
Mapped Mini smoke has its own job: it consumes bytecode, SRAM mapping and native tools, not an
ordinary simulator, target descriptor, or Sail build.
ACT coverage follows the model and ACT's own platform/ELFs. Its execution
also requires the attested simulator: changing only the native descriptor writer
can require new simulator attestations, but does not select ACT shards.
The RV5Stage RVA23 Sail alias check depends directly on its configured ACT task;
hosted dependencies must not invoke local selector wrappers or start nested Moon runs.
Moon queries changed files between explicit revisions. CI uses a direct Git tree diff when
histories diverge or gitlinks change: the former needs both sides of the divergence, and the
latter needs the submodule path, independent of whether the runner populated its checkout. Unavailable revisions
and unowned paths select full CI, even when mixed with known changes. Docs-only edits are inert.

Moon tasks declare two independent CI roles: `ci-root` makes a task selectable;
`ci-receipt` requires evidence when it is in the selected execution graph. Program and
ACT suites are roots only; their executable shards produce receipts only. A hosted simulator
does both. Workers collect receipts for their explicitly selected endpoints from Moon's
execution report; the gate checks them against the tagged, selected graph and rejects
missing or unexpected receipts. Local-only targets need neither tag.

Declare each hosted check once. In `checks.yml`, give it both roles, tag it `ci-check`, assign one `pool-<key>`,
and add the required `needs-dtc`, `needs-circt`,
or `needs-verilator` tags. Worker timeout budgets live in `ci-checks.yml`, not task
tags. The four pools are host, rsim, SystemVerilog, and CIRCT. Selected
members determine each pool's tool setup; rsim has a 60-minute budget for its wider
pool. YAML declares two worker slots per pool. Each receives the same selected
target list; Moon assigns targets to workers. An unaffected pool skips at job
level. A singleton pool skips its second worker before allocating a runner.

Each worker invokes `moon exec --ci --job INDEX --job-total COUNT` with that list.
This uses Moon's native CI partitioning without running affected selection again:
the plan already includes dependency closure and fail-closed handling for unknown
paths and revision failures. Do not use `--force`, which would also disable caches,
or include downstream tasks outside the selected pool. Checks in the same pool
must not depend on another pool member. Moon runs assigned tasks serially and
continues after failures. Each check retains its logs and successful-task receipt.

Use two workers for pools with at least two selected targets and one otherwise.
Moon 2.6.5 leaves the full target list in place when an automatic partition starts
past its end: five targets over four workers reruns all five on worker three.
The two-worker limit avoids empty partitions. Requalify this behavior before
increasing the limit or upgrading Moon; do not reproduce its partition algorithm
in Python. The receipt collector rejects a multi-target shard that ran the entire
pool, and the final gate still requires every selected endpoint.

The simulator, shared ELF, ACT, and platform producer/consumer workflows retain
their artifact boundaries. Native checks and software adapters remain single-worker
batches; this change does not duplicate their shared setup/build dependencies.
Host check receipts belong to aggregates. Hosted host shards run their direct
test leaves (with per-leaf logs) against verified bytecode; local Moon runs the
full dependency graph. The rsim checks invoke the component fixture runner directly.
Host aggregates also include the common boundary check where required, so
direct-upstream hosted execution retains it without repeating it per leaf within
a worker. Shared prerequisites may run once on each worker that needs them.
Program and ACT hosted matrices declare fixed shard slots matching the executable
Moon children. Keep those slots aligned when changing enrollment or shard counts;
the receipt gate rejects omitted selected shards. Unselected program slots run
only a shell preflight, before checkout, Moon installation, toolchain setup,
submodule initialization, or artifact downloads. Unselected ACT slots skip at job
level. Do not filter a selected graph a second time with a different Git diff.
Native checks and software adapters in `products.yml` use `ci-native-check` and `ci-adapter`
with their own `needs-*` setup tags; those tags do not create build dependencies. Each of
these two jobs also batches its selected tasks serially after setup. Native checks share
the configured Verilator root when needed.
For each fixture group, reference `@group(fixtures--<key>)` on its enrolled check. Planning
rejects fixture groups without that owner.

Install moon 2.6.5 and run these commands at the repository root:

```sh
python3 -m tools.ci.catalog
moon query tasks --tags ci-check
moon task rhodium:check--host-hygiene
moon action-graph rhodium:program--simple-rv2wide-rva23--coremark --json
moon run rhodium:check--host-hygiene
python3 -m tools.ci.actions --paths cores/superscalar/core.rhdl --pretty
python3 -m tools.ci.actions --all --pretty
```

The workspace enforces the same exact Moon version as the CI installer; update both
when qualifying a new release. Regenerate the ignored catalog after changing configuration,
fixture inventories, or submodule revisions. After setup has generated the catalog,
hosted cache-key steps use the `product-input` composite action with a full
`rhodium:hash--...` task ID. The action verifies its `ci-hash` contract, invokes
`moon run` directly, and reads the successful task's hash from the fresh report
with `jq`. It never regenerates the catalog or translates product names.
Jobs that receive resolved target descriptors explicitly regenerate configuration
with `PROGRAM_TARGETS`.

Locally, run `moon run rhodium:target--SOC` first, then
`python3 -m tools.ci.catalog --soc SOC`. Repeat both targets and `--soc` for
multiple configurations. Catalog generation only reads the descriptors already
produced by Moon, resolves workload recipes, and embeds the immutable snapshot
location in archive and binding commands. Missing descriptors are an error;
refresh them through Moon before preparing the catalog after configuration changes.
Prepare the base catalog with `python3 -m tools.ci.catalog` before hosted selection
queries. Use Moon's native affected query to inspect a prepared local graph.
Use Moon for first-party stage prerequisites and cached outputs. Use the managed
Racket cache for all elaboration. Formal, trace, diagram, SRAM and physical-design tasks
are available locally without enrolling new CI coverage; golden updates are explicit.

Use task dependencies for commands that must execute first and file groups or direct inputs
for source ownership. The commandless program and ACT suites collect their executable
shards. Only the `hash--*` tasks requested by CI exist solely to produce a cache key;
they are tagged `ci-hash` and have no task dependencies. [Fixed keys](../../.moon/tasks/cache-keys.yml) share inputs with their preparation tasks;
keys that share generated recipe inputs stay with those recipes. Indexed gitlink revisions enter the key as explicit file
inputs even before submodule checkout. Software adapters and ELF producers use the same
`build--<suite>` file groups: [inputs.yml](../../.moon/tasks/inputs.yml) declares the suite
sources and [inputs.py](../ci/inputs.py) adds their shared tools and pins. The Linux recipe
includes the full OpenSBI recipe and its Buildroot inputs. FESVR consumers use
`fesvr-runtime`; the installer key uses `fesvr-library` so runtime-only edits do not invalidate it.
[Scoped source groups](../ci/inputs.py) keep
implementation exclusions local: a shared-core rule must not suppress a fixture that a
check owns separately. The same groups account for removed files when planning a diff.

To diagnose a cache miss, run `moon hash OLD NEW` at the workspace root, replacing
`OLD` and `NEW` with full Moon task hashes for the two runs (not Git SHAs or GitHub
cache keys). Both hash records must be available in the local Moon cache. Use
`moon hash HASH` to inspect a single record.

In the `targets` job, [archives.py](../ci/archives.py) groups selected ELF requests once
by actual descriptor and recipe. That grouping produces archive identities and
consumer bindings, not job matrices. Each fixed SoC/suite build slot receives its
resolved recipe or null. Only the representative of a compatible group builds;
the other slots skip at job level without starting a runner. This preserves one
shared archive per compatible recipe without constructing GitHub jobs at runtime.
The resulting `elf-requests.json` and `elf-bindings.json` travel with the target
descriptors; set `PROGRAM_TARGETS` to that artifact directory when materializing the resolved
catalog. Portable native build outputs are cacheable. Platform and Linux builds keep
tool-specific caches and mandatory artifact checks. [Shared harness recipes](../ci/harness.py)
own the harness test, ELF suite and build options, Moon builder, and output subdirectory.
The simulator binds the selected archive from `harness-plan.json` to its own SoC; a missing
or ambiguous shared binding is an error, and ordinary harness tests need no archive.
A hosted worker invokes `moon run` or `moon exec` directly, using full target IDs,
ordinary environment variables, and explicit `--upstream` depth. Run-only consumers use
`none` against staged artifacts; host aggregates and local harness payloads use `direct`;
builders and native checks follow their declared graphs. Shards are explicit Moon targets,
not launcher arguments. Local `moon run` follows ordinary dependencies.

Before an endpoint invocation, remove `.moon/cache/runReport.json`. Afterwards, run
`python3 -m tools.ci.receipts` with exactly the same target IDs, including after a batch
failure. The collector validates the report's primary targets and records only successful
or cached endpoint actions; it neither launches Moon nor translates arguments. Preserve
Moon's exit status as well as collector failures. Do not collect receipts for setup or
build-only tasks without `ci-receipt`. Never treat a Moon cache hit as a performance or
simulation verdict: run tasks retain their uncached execution policy. The gate compares
receipts against expected endpoints in addition to checking job status; it consumes the
selection and receipts without installing Moon.

For sharded static-check pools, pass `--sharded` and the full pool target list to
the collector. It validates `initialTargets` against the pool and records only
Moon's assigned `primaryTargets`, never receipts for unassigned candidates.
`moon exec --ci` still writes `runReport.json`; a direct `moon ci` invocation writes
`ciReport.json`, which the collector can read with `--report`.

The Moon catalog enrolls configs from the
[`simulator inventory`](../../sims/test-configs.txt); policy selects workloads
by shape and ISA, independently of core. Static workflow slots must cover that
inventory. Simulator job configuration additionally carries
backend, cosim selection, and artifact identity. rsim is the default for every
enrolled config; the full shape/ISA workloads remain unchanged.
`BACKEND_SMOKE_VARIANTS` in `policy.py` adds a Simple RVA23 direct-SV variant
with three smoke tests and a CIRCT variant with smoke, boot,
host MMIO, UART, and ISA smoke. Only these explicit variants use narrowed
workload policy; ordinary backend selection never reduces coverage.
All simulator rows select `OPT_FAST=-O2`; initialization and Verilator runtime
compilation inherit it. Prebuilt consumers carry backend provenance explicitly
through software, ACT, platform, and harness jobs. Preserve optimization
propagation, bounded arguments, and nonzero status through log capture in the
actual build and run commands. CI retains build and harness logs.

All RTL cores receive both variants with the same selection. RV3Wide mirrors
RV2Wide enrollment and ACT shard counts; software still depends only on shape/ISA.
All Mini/Simple RV5Stage, RV2Wide, and RV3Wide rsim rows, plus their HDL
variants, publish a cosim-enabled simulator under their config artifact name. Propagate
that selection to native ISA, ACT, and firmware consumers; verify the variant
and install GMP. Native and ACT runners require nonempty successful checking.
Cosim enrollment does not add matrix rows or change workload selection.
The existing RVA23 benchmark jobs also collect
[whole-ELF cycle measurements](../../sims/README.md#whole-elf-cycle-regressions).
Native shards and Tiled multihart harness jobs upload compact `performance-…`
artifacts and job summaries even on failure. With no checked-in config/suite
baseline they collect and report results without comparison; an existing baseline enforces the
per-ELF ±5% gate by config, suite, and test name, without baseline fingerprints.
RVA23 Simple/Tiled RTL simulator rows enable statistics in that same binary;
retirement, issue width, demand cache misses, and classified winning branch
recoveries follow the [counter baseline contract](../../sims/README.md#rtl-statistics).
The catalog's `simulator_options` owns `cosim`, `stats`, and the exact `variant`.
Keep static simulator, native, ACT, and firmware job settings aligned with that
contract; consumers still verify the exact artifact rather than reconstructing suffixes.
This adds no benchmark runs,
guest instrumentation, or trace jobs. Baselines are promoted explicitly from a
successful run's complete artifacts, never written by CI; their ownership and
qualification live in the [simulator contributor guide](../../sims/DEVELOPING.md#whole-elf-performance-baseline-maintenance).
When the plan selects any downstream work, CI
compiles the positive Racket entrypoint manifest once for reuse by the selected
jobs. Pull requests and pushes classify changed paths; a full manual dispatch
selects every matrix shard.

When any selected simulator enables cosim or ACT generation is selected, one
shared Sail producer runs alongside Racket compilation. It restores an exact-input
cross-run cache, builds or verifies the installed model, and saves the completed
cache before packaging or downstream tests can fail. The cache includes the
pinned compiler and installed model, keyed by the runner platform, Sail revisions,
ordered patches, installer, and CMake package definition. Simulator and ACT
generation jobs download the same exact-commit model artifact; they do not build
Sail or save competing caches. When cosim is selected, this producer also builds
the config-independent native runtime archives once, without test executables.
Simulator consumers verify archive checksums, native sources, platform, checkout,
and absolute link paths before linking. Generated runtime configuration and
`runtime/session.cc` remain config-specific. The stable gate requires the selected
Sail producer to succeed.

One target producer emits each selected SoC descriptor once.
Simulator, harness, program, and platform jobs consume these shared
descriptions; none may silently regenerate a missing artifact. Harness ISA smoke
and eight-hart benchmark builds use the existing suite-specific ELF projection
to group compatible configs, then bind each shared archive to the execution
target. Local build-and-run targets remain available; CI uses builder-free run
targets. Shared build failures retain diagnostics and do not cancel other groups.

Linux image production is selected independently of simulator execution.
Selected `target:*` tasks include both simulator and build-only software
requirements; a Linux-only change needs bytecode and target descriptions, not
Sail, simulators, native runtime builds, or software execution jobs.
`targets` groups Simple RVA23 descriptors by
their Linux build specification and retain every compatible binding target.
`ci-linux-builds.yml` builds each group on `ubuntu-24.04` through `linux-elfs`,
using Buildroot's own Linux toolchain and the pinned OpenSBI source/patches.
It caches only hash-verified upstream downloads, starts compilation fresh, and
has a provisional 180-minute cold-build budget. Measure a successful cold build
before tuning the budget or introducing compiled-output caching.
The producer packages the common manifest archive, extracts it, and binds it
to every group member without executing any image. Successful bundles and
always-uploaded available build logs/configurations have seven-day retention;
the stable gate requires targets and Linux image production when selected.
Linux port/source/builder/workflow changes select only this producer chain;
shared target, firmware, and build-contract inputs also retain their existing
coverage. Full manual CI selects the producer too. No Linux boot, simulator,
or shutdown consumer is enrolled. Compilation and static artifact checks must
not be reported as Linux boot qualification.

One native-check job owns config-independent DPI, transport, runtime, cosim-hook,
and architectural projection tests. A separate lightweight software-adapter job
runs the graph-selected host checks and initializes only their required sources.
Native checks run their selected Moon dependency graphs once and install tools from their
prerequisites. A branch-analysis edit runs its Python check without Racket,
FESVR, Verilator, or simulator construction; Spike ABI checks still use the
native toolchain, Racket, CIRCT and Verilator. Per-config hardware elaboration
and execution remain in their own jobs. The gate requires all selected shared
producers and native checks, including cancellation or unexpectedly skipped jobs.

Local runner scripts preserve the same freshness boundary with a persistent
compiled root owned by each worktree under `.rhodium-cache/`. The cache is keyed
by the Racket version, installed package metadata and checksums, operating
system, architecture, and absolute checkout path. The wrapper hashes repository
sources and uses Racket's recorded dependency graph to remove changed modules
and their transitive project dependents before incremental compilation. It also
records the source path inventory and clears only the checkout's mirrored
bytecode subtree when a source is added, removed, or moved, preventing orphaned
modules from surviving structural changes.

Mutable project bytecode and its lock are never shared across worktrees. An
explicit common `RHODIUM_RACKET_CACHE_DIR` still separates project roots by
absolute checkout path; only a completed external-dependency snapshot can be
shared, and checkout bytecode is removed before that snapshot is published.
Cache access is serialized within one worktree. The wrapper supervises the
cached command, releases its lock on interruption, reports long-running phases,
and fails without an uncached retry when the cache is unavailable or busy.
Callers that deliberately provide `PLTCOMPILEDROOTS` retain full ownership of
that root. Invalidation reads each reachable module's direct `.dep` record once
and walks reverse edges from changed sources or unreadable project metadata;
it deletes only project bytecode, and does not recursively rescan external
libraries for every source. Its summary reports cached, inspected, invalidated,
retained, and unreadable module counts.

CI first restores a checksum-pinned full Racket installer through the shared
`setup-racket` action. The compile job saves a verified installer immediately,
before downstream jobs start, so cold caches do not trigger parallel upstream
downloads. Every job verifies the SHA-256 again before installing at the same
Unix-style `/usr` paths; the runtime cache is separate from Rhombus packages and
project bytecode. Update the version, official checksum, and installer script
together when upgrading Racket; its content hash scopes the installer cache.

Local Moon checks depend on `racket-cache`, which prepares the worktree cache;
the test wrappers compile what they need. Give tasks that use that cache the
`racket-cache` Moon mutex so independent elaborations wait in the scheduler
instead of timing out on the worktree lock. The hosted plan maps that prerequisite
to a dedicated `compile-racket` job. Only that job compiles the whole manifest
and publishes attested bytecode; downstream hosted checks still verify it.

CI caches the entire completed compiled root and source manifests across runs,
including external bytecode updated by project compilation. The key scopes
reuse to the Racket/Rhombus environment, absolute workspace path, and cache
implementation. Restoring only project bytecode over an older dependency seed
would mix imported-dependency fingerprints and defeat warm reuse. The package
installation cache seeds cold builds; a completed-root cache replaces its
bytecode with the coherent snapshot from the previous build. Source-path changes
still clear only the checkout subtree, preserving external bytecode. CI then
applies the same content invalidation as the local wrapper and records total
compilation time. The cross-run cache is only a compilation starting point.
Downstream jobs consume a newly published exact-commit artifact, including hidden
package-cache paths, and verify its environment and absolute workspace path.

```mermaid
flowchart TD
    Changes["Pull request or push paths"] --> Classifier["Moon affected-task selection"]
    Manual["Manual dispatch"] --> All["Select every shard"]
    Unknown["Unknown path or unavailable base"] --> All
    Classifier --> Docs{"Documentation or<br/>repository metadata only?"}
    Docs -->|yes| None["Static functional jobs skip"]
    Docs -->|no| Selected["Dependency-aware selection"]
    All --> Selected
    Selected --> Compile["Compile positive Racket entrypoint manifest once"]
    Selected --> Sail["Build or restore Sail model once<br/>and shared cosim runtime archives"]
    Compile --> Checks["Capability matrix<br/>host and CIRCT;<br/>auxiliary examples run only on host"]
    Compile --> Native["Config-independent native and adapter checks once"]
    Compile --> Targets["Generate each selected target descriptor once"]
    Compile --> Mapped["Mapped Mini smoke: SRAM check, mapped simulator and test"]
    Targets --> HarnessBuilds["Compile harness ISA / multihart ELFs once per group"]
    Targets --> Simulators["Per-config simulator builds"]
    HarnessBuilds -->|only configs using shared harness ELFs| Simulators
    Sail -->|only cosim configs| Simulators
    Simulators --> Simulation["Same runner: publish simulator,<br/>bind shared ELFs and run shape/ISA selection"]
    HarnessBuilds --> Simulation
    Targets --> LinuxBuilds["Build compatible Linux images<br/>archive and bind only; no boot"]
    Targets --> PlatformBuilds["Shared litmus / OpenSBI build artifacts"]
    PlatformBuilds --> Platform["Per-config platform tests<br/>litmus histograms; OpenSBI handoff"]
    Simulators -->|matching config only| Platform
    Targets --> ProgramBuilds["Compile program suites once per compatible group"]
    ProgramBuilds -->|matching workload group only| Programs["Bind shared ELFs to each exact target<br/>per-config execution shards"]
    Simulators -->|matching config only| Programs
    Compile --> ActBuild["Generate ACT ELFs per single-core profile"]
    Sail --> ActBuild
    Simulators -->|matching config only| ActRun["Per-config ACT execution<br/>unchanged resource-policy shards"]
    ActBuild -->|matching config only| ActRun
    Checks --> Gate["Stable CI gate"]
    Native --> Gate
    Targets --> Gate
    LinuxBuilds --> Gate
    HarnessBuilds --> Gate
    Sail --> Gate
    Simulators --> Gate
    Simulation --> Gate
    Platform --> Gate
    PlatformBuilds --> Gate
    ProgramBuilds --> Gate
    Programs --> Gate
    ActBuild --> Gate
    ActRun --> Gate
```

Known dependency paths can select several branches. For example, NoC, RISC-V,
CHI, core, and shared standard/flow library changes also select the SoC host shard
when their behavior feeds system composition. Backend implementation changes
retain differential coverage; fixture ownership comes from the runner inventories.
The direct lane
requires Verilator without CIRCT and runs the independent memory oracle; the
differential lane installs both and owns the full compiler behavioral families. Its
Builder fixtures, oracle, and runner live under
[`rhodium/backend/tests/verilog/`](../../rhodium/backend/tests/verilog/); see the
[backend contributor guide](../../rhodium/backend/DEVELOPING.md#validation). The
example manifest partitions execution: CIRCT loads and checks each declared
example source once in its owning lane, while `ci-host-examples-test` derives
the remaining non-formal examples by subtracting the manifest's source list.
Keep the local example targets for focused authoring checks; do not add a
second CI lane that merely reloads the same concrete designs. The foundation
host lane also executes standard-library, Flow, event, and diagram contracts;
the frontend and backend host owners execute their equivalence tests once.

CI uses the display groups `Checks`, `SoC / <config>`, `Platform / <config>`, and
`Software / <config> / <group>`, with short build and run names beneath each group. ACT execution
names include the config and shard. Shared ELF builds identify their suite and
representative slot; all compatible targets still consume one archive. The final `CI`
gate name remains stable.

The root workflow statically calls `ci-simulator.yml` once per config. Each job
publishes its simulator, then runs that config's harness on the same runner,
reusing installed tools, native libraries, bytecode, and target descriptions.
Build and harness execution retain separate step budgets; the job result covers
both phases for the stable gate. Configs with downstream software or platform
tests have named root jobs so each consumer depends on its own simulator,
not an entire simulator matrix. All other simulator slots also have explicit
Sail and shared-harness-ELF dependencies. Non-cosim builds do not wait
for Sail; configs without shared harness software do not wait for harness ELFs.
Named dependency edges are necessary because Actions cannot express a
matching-row dependency between matrices. Edit the checked-in jobs and fixed
matrices when changing enrollment; there is no workflow generator. Update the
gate's static dependency list and result collection together. The gate rejects
failed or canceled jobs and requires one successful receipt per selected endpoint
and shard, even when a mistakenly skipped job otherwise looks successful.

Native and platform planning/builds start from the target descriptions,
independently of simulator completion. Native producers are separated into
ISA, short suites, CoreMark, Bringup-Bench, SKL, and Highway; short RVA23 runs wait only
for ISA and short-suite producers, while RV32 runs wait only for ISA.
OpenSBI and litmus have separate producer dependencies. Shared compatible ELF
groups within a producer still finish before that producer's consumers start;
there is no wait for another workload family. ACT generation starts alongside
simulator builds, once bytecode and Sail are ready, and each config's shards
wait only for that config's generator and simulator. No runner polls for an
artifact whose producer is still running.
Producers upload build logs and Verilator pass statistics independently of their
simulator artifacts, including on failure. `ci-platform-builds.yml` owns shared
platform compilation and `ci-platform.yml` owns execution, independently of backend
fixtures. Each OpenSBI test has its own
job budget and uses the matching exact-commit single-core simulator artifact,
so firmware execution cannot consume another SoC's or the harness job's budget.
That workflow groups litmus builds by software requirements and OpenSBI builds
by minimum firmware ISA and derived boot layout. Each execution supplies its
own BootROM DTB; firmware neither embeds nor binds to a separate DTB. Litmus7 and target compilers
belong only to build jobs, and the common ELF archive/binder feeds all compatible
execution configs. Each `tiled4core` config runs the same bounded, model-checked
litmus7 selection with its independent 90-minute budget; OpenSBI retains a
45-minute budget per single-core config. Both retain build diagnostics and
runner results even on failure. The full litmus inventory is manual only and has
no CI job or schedule. Do not remove smoke cases because they expose a failure.
The `tiled8core` configs retain platform smoke, ISA smoke, and eight-hart
benchmarks, but do not repeat litmus execution. Keep named simulator and
consumer dependencies explicit for both tiled sizes.

Core rsim coverage retains frontend, control, datapath, both functional vector
shards, alternate vector configurations, memory, and cache selections. Compatible
selected targets share runner groups; fixture inventories and local targets are
unchanged. HardFloat uses the same rsim runner through its package-owned target.
Retained CIRCT fixtures share an HDL job for emission, trace/runtime, and
observation contracts; selected direct and differential SystemVerilog checks
share their own job. Only the selected members determine tool installation.

All Simple RVA23 single-core software matrices independently select ISA tests,
benchmarks, both CoreMark variants, Embench-IoT, SKL and Highway vector execution workloads,
and the complete bounded Bringup-Bench inventory. SKL uses the upstream capability-filtered
catalog without X390 or standalone scalar suites. SKL retains all test-input and
benchmark cases but disables numerical verification and reference-buffer setup.
All RTL jobs require Sail cosim for architectural checking; Spike is an
execution smoke test, not an independent numerical oracle.
Highway uses repository-owned bounded drivers linked with the pristine upstream
library, not modified hosted benchmarks. All benchmark families use the same
single profile on each core. Its CMake/G++ build runs only in the shared producer;
execution consumes the ordinary checksum-bearing ELF archive and bound manifest.
`programs.py` projects generated target descriptors into suite build groups;
`ci-program-builds.yml` compiles each group once and distributes one ELF archive to all compatible
configs. Each run binds the archive to its own full target fingerprint and
checks its simulator attestation. Compiler installation and ELF caches belong
only to the build jobs; execution shards retain every selected test.
Short ISA, benchmark, and Embench suites share one
execution job per exact config. Each suite still consumes its own shared ELF
archive and retains its manifest, runner limits, results, logs, and performance
identity. A preparation or execution failure does not skip later suites; the
group fails after all selected suites have been attempted. SKL, Highway, and
Bringup-Bench keep independent execution jobs. Grouped artifact names identify
the config and group; their directory trees retain the separate suite records.

**CoreMark qualification.** Dispatch CI with `scope: coremark` to run
both CoreMark variants on the four Simple RVA23 simulators, with the normal
Sail/cosim and statistics prerequisites for the RTL cores. Inspect that
selection offline with `python3 -m tools.ci.actions --coremark --pretty`.
The same jobs run for affected changes and full CI. Group actual descriptors
by ELF build specification (including the iteration count) before starting
producers. Ordinary and archived builds inherit the same
[Moon software recipes](../../.moon/tasks/software.yml). The
[build-option contract](../ci/programs.py) translates canonical manifest options
into recipe environment values, not commands or Make goals. Interactive Moon
tasks retain their environment defaults. Python validates and packages completed
artifacts; it does not dispatch software builds.
A producer checks the pinned source with `tools.ci.archives check-source`, materializes
the resolved catalog, then invokes `moon run rhodium:archive--BUILD_ID` directly. It builds
or restores its portable archive, checks the suite and build ID with
`tools.ci.archives verify`, and uploads the archive as a required Actions artifact.
Only trusted main builds publish moon's
portable output cache. A cache miss does not prevent a cold build.

Before portable archive lookup, a Moon fingerprint check probes the current
toolchain for the actual producer descriptor. For SKL and Highway this is the
same identity used by their builders, including CMake and target-selected runtime
library hashes (and G++ for Highway). Do not capture it at catalog generation:
a warm checkout must notice a changed runtime without regenerating the graph.

Each selected core/variant has a separate execution row: its evidence key
must not depend on whether its sibling variant was selected. Consumers verify
the exact simulator, check the archive identity and bind to their own target
before running the existing software runner through moon. Consumer tasks
are deliberately uncached: Rhodium may reuse verified raw execution evidence,
but always evaluates the current expectations, reports cosim/statistics and
uploads results. `force_fresh` still forces new simulation. No shared ELF
producer waits for an implementation-specific simulator. Simulator builds and
exact-commit attestations remain with the existing workflow; moon's portable
cache for native software contains portable ELFs, not simulator binaries or verdicts.

Bringup-Bench uses four shards and two workers per shard on every eligible
config, including Spike. `policy.py` owns the suite's shard count;
The static workflow declares its four execution slots; artifact grouping does not
create or change execution shards.
Each shard publishes separate diagnostics, and all shards are required for the
CI gate to succeed. Do not add core-specific selections or rebuild
the suite per shard.
Software adapter contracts run once in their independent selected host job. A failed build does
not prevent execution jobs from attempting other published groups.
Each selected OpenSBI execution
validates its enrolled single-core config and
publishes separate diagnostics. A failed platform build does not prevent
execution jobs from attempting other published build groups.
All Simple RV32 configs select upstream ISA tests;
the existing RV64-only benchmark ports remain outside their coverage.
Selected Simple RV32/RVA23 configs retain their own ACT generation and execution.
ACT shard counts are unchanged by runner grouping. Counts live in `tools/ci/policy.py` and
only split execution; each profile still generates one shared ELF archive and
builds one simulator. Shared SoC dependencies
(including CHI, NoC, devices, and RISC-V support) select these lanes; suite-only
adapter/source changes select the owning lane. Enrolled Mini and Tiled
configs receive capability-filtered ISA smoke. The `rv64max`, `rv64imacb`, and
`rv64imafdcb` presets enroll their paired Simple configs; all RTL cores also
enroll Mini for the two lean presets. These run only ISA smoke, without ACT,
benchmarks, or platform tests. All RTL cores use the same shape/ISA inventory,
enforced by the enrollment policy, with cosim enabled on Mini/Simple. All Tiled cores run the
eight-hart benchmark manifests in CI. Harness execution reuses the simulator
producer's hardware tools and native libraries; only standalone platform payloads
need the RISC-V compiler on that runner. ISA smoke consumes shared ELF archives
and the producer's target descriptor, retaining ordinary manifest and simulator
checks. Mapped MiniRV5Stage checks retain their additional compiler dependencies.
Native DPI and transport checks run once in the shared native job.
Software selection is a
function of SoC shape and ISA, never core identity. No timeout or prior failure
removes a workload from one core. None receives the full
single-hart suite matrices. ACT configuration
generation uses the exact compiled root; ISA/benchmark/CoreMark/Embench-IoT/Bringup-Bench execution needs only the
shared ELF archive and native simulator artifact. Bare-metal software builds use the same pinned
GCC/Newlib toolchain; Linux image production uses Buildroot's Linux/musl toolchain.
ACT execution consumes its shared ELF archive without installing
the compiler or reference-model toolchain again. Generation can reuse only an
exact-input, checksum- and inventory-verified complete bundle; execution still
verifies the current commit's simulator. Native software and ACT shards may reuse
checksum-verified raw execution evidence under the
[simulation reuse contract](../../sims/DEVELOPING.md#content-addressed-execution-evidence).
Current functional checks, Sail completion, inventory accounting, and performance
expectations still run; a cache hit never skips the job's verdict. Only trusted
`main` runs publish complete functional evidence, with exact-key restore and a
force-fresh dispatch option. Optional cache transfer failures may continue; test
failures may not. Platform, harness, multihart, and litmus jobs execute freshly.
The configured deterministic shards cover
the full generated inventory, including failures. They run independently with bounded process parallelism and
upload logs plus JSON/JUnit results even when execution fails. Do not add
`continue-on-error` or pass-based exclusions to make new suites green. Measure a
complete Linux run and resolve baseline failures before marking the new jobs
required in branch protection. Platform/privileged ACT coverage limits remain
in the [simulation guide](../../sims/README.md#architectural-certification-tests).
The program suites and ISA-smoke jobs publish manifest-selected ELF archives
with their diagnostics; package by manifest entry rather than filename suffix,
since upstream ISA binaries have no extension. The ordinary simulation jobs
also attach their standalone hand-written ELFs.

Recognized documentation and inert repository metadata select no functional
test jobs. The optional Emacs integration and most of `vlsi/` have no
functional CI lane; Rhodium sources there still receive source hygiene, while
`vlsi/sim/` and the mapped MiniRV5StageSoC flow select simulation. Unrecognized
paths fail closed by selecting every lane; tracked executable inputs must select
their affected owners. The root workflow owns only triggers,
affected selection, the shared Racket and Sail artifacts, reusable-workflow calls, and the stable
`CI` gate. The reusable workflows separately own capability checks, simulator
construction, simulation, and bare-metal software. Composite actions own only
repeated toolchain setup, so test steps and their failures remain visible as
workflow jobs.

The hygiene lane runs `moon run rhodium:check-license-headers` over the complete tracked
source, test, script, configuration, and documentation inventory. It requires
Apache-2.0 identifiers for original Rhodium material and BSD-3-Clause
identifiers within `hardfloat/`, while exempting exact legal texts and external
submodule contents.

When adding or moving executable source, update the moon catalog if its existing
dependency rules do not select every affected owner. Inspect the plan with
`python3 -m tools.ci.actions --paths PATH... --pretty` and run the affected build
and behavioral tests.

## Change workflow

1. Put the test beside the layer or package that owns the behavior.
2. Before adding a host test, state what regression it catches beyond successful
   elaboration. If the answer is only internal shape, add or extend an
   end-to-end simulation instead.
3. Add valid executable examples to the owning example group and invalid
   language uses to the frontend-invalid suite.
4. Add external lowering or simulation coverage through the
   [CIRCT fixture workflow](circt/DEVELOPING.md) only when the change crosses
   that toolchain boundary.
5. Confirm `python3 -m tools.ci.actions --paths PATH... --pretty` selects every
   affected package, example, CIRCT, simulation, or software lane.
6. Run the smallest owner target first, then the broader target required by the
   changed dependency surface. The [test-running guide](README.md) lists those
   targets and their scope.
