<!-- Explains CIRCT fixtures, event snapshot/stream tests, and Verilog references. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing CIRCT tests

Read the [CIRCT test guide](README.md) first for focused selectors, runner
modes, toolchain behavior, and failure interpretation. This guide owns fixture
and artifact structure, exact-reference maintenance, and changes to the runner.

The [backend package guide](../../../rhodium/backend/README.md) owns lowering
architecture and operation contracts. The
[example guide](../../../examples/README.md) owns the canonical example catalog.

## Architecture and implementation map

The manifest in [`run.sh`](run.sh) is authoritative for fixture
names, groups, example exports, direct emitters, Verilator tops, reference
eligibility, and expected failures.

| Path | Maintenance responsibility |
|---|---|
| [`run.sh`](run.sh) | Fixture manifest, selection, package-owned artifact resolution, CIRCT pipeline, exact diff, and Verilator orchestration |
| [`load-example.rhm`](load-example.rhm) | Compiling selected program exports through the CIRCT target in one process |
| `<package>/tests/circt/emit-*.rhm` | Package-owned program exports or custom compilation drivers without an example-owned reference |
| `<package>/tests/circt/verilog/` | Package-owned behavioral benches and optional local DPI companions |
| [`examples/`](../../../examples/README.md) | Canonical designs and their exact Verilog-reference exports |

The [repository test-development guide](../DEVELOPING.md) owns test placement,
authoring principles, and CI classification outside this backend-specific
fixture boundary.

Remaining RV5Stage HDL execution fixtures belong to `cores-execution-datapath`,
also selected by `cores-execution`. They cover co-sim integration; reusable
component emission and the `cosim-hooks` ABI fixture use `cores-components`.
Do not add empty HDL CI lanes.

Portable language, library, core, cache, vector, and tracing behavior belongs
to the package-owned rsim groups in [`../rsim/fixtures.tsv`](../rsim/fixtures.tsv).
RV2Wide enabled fetch and RV32 integration also use rsim. Example-backed entries
retain their CIRCT lowering/reference checks without a Verilator top.
The [RV5Stage guide](../../../cores/rv5stage/DEVELOPING.md#focused-validation)
owns the migrated internal-port observers and architectural workloads.

## Fixture and artifact ownership

Example-backed entries name an example module, an elaborated program export, an
optional Verilator top, and either a Verilog-reference export or `-`. A named
reference marks a compact fixture whose readable generated output is part of
the example; `-` keeps integration-scale fixtures under lowering and behavioral
coverage without a large exact snapshot. The ordinary golden pair is `program`
and `verilog_reference`; additional programs use the same prefix for both
exports, such as `cast_program` and `cast_verilog_reference`. References live
beside their designs so reviewers can see the authoring input and generated
result together.

Examples export `elaborate(...)` programs without preparing RTL eagerly. The
runner selects the CIRCT target with `compile_program`. Tests that inspect a
concrete graph prepare it locally; emission drivers should not prepare a graph
and reconstruct an elaborated program from it. Traced drivers compose
`event_trace_pass` with `rtl_pipeline_target`, so RTL and trace descriptors come
from the same compilation.

Package-local `emit-*.rhm` fixtures own integration shapes that do not belong
to one canonical example. Ordinary fixtures export an elaborated `program`;
the shared runner selects `circt_target`, compiles it, and writes its MLIR.
Keep compiler imports and artifact extraction out of those fixtures. A fixture
that inspects prepared RTL or generates instrumentation-specific descriptors
may instead own its compilation and print MLIR. Neither form owns an example
Verilog reference. Add or rename either kind through the manifest. The runner resolves each declared direct
emitter and each selected `<fixture>_tb.sv` or optional `<fixture>_dpi.cpp`
from exactly one package-local `tests/circt/` directory; zero or multiple owners
are errors.
An optional third field in a direct-fixture entry names an expected assertion
label. A fourth field names a program export, normally `program`; omitting
that field selects a custom emitter. For example,
`cosim-hooks|cosim_hooks_tb||program` uses ordinary shared
compilation, while a traced emitter that exports scoreboard constants owns its
compilation. An expected-failure fixture passes only when its bench fails with
the declared label. Use separate emitters for independent compilation
roots rather than collecting uninstantiated circuit references in a suite.
`make examples` and `make check-example-verilog` check every elaborated program's
manifest coverage and validate only declared golden exports without running
CIRCT or Verilator.

`--list-example-sources` exposes the manifest's unique source paths without
loading Racket or external tools. CI subtracts those paths from the non-formal
example inventory to select its auxiliary host examples; the owning CIRCT
lane already executes each declared source's top-level checks and elaboration.
Keep example checks at top level so materialization exercises them. Local
example targets still run complete groups independently.

Behavioral benches live under the owning package's `tests/circt/verilog/`.
An example fixture with a top uses `verilog/<fixture>_tb.sv`. The runner
automatically links a matching `verilog/<fixture>_dpi.cpp`; direct emitter
fixtures can additionally link a matching source from
[`devices/uart/dpi/`](../../../devices/uart/dpi/), with hyphens
in the fixture name changed to underscores. Assertion and protocol monitors
also have dedicated negative benches. Those checks pass only when simulation
fails and reports the expected assertion label, so an expected failure is not
treated as an unchecked crash.
The failure runner accepts optional trailing native sources for DPI-backed
assertion benches. Selected-parent and identity-stability failures now belong
to the rsim event drivers.

The `event-runtime` and `event-elastic` direct fixtures
additionally link the independent RHEG collector implementation. Each local DPI companion is a transfer scoreboard,
not a second implementation of the collector or ABI. Migrated lineage workloads
live in `rhodium/event/tests/rsim/` and link that collector directly; the
[event validation guide](../../../rhodium/event/DEVELOPING.md#focused-validation)
owns their coverage and site-binding helpers.
An emitter may additionally export `event_manifest_cpp`, generated from the
same target compilation that emits its RTL. `load-example.rhm` writes this string
to `<fixture>_manifest.h` beside temporary MLIR. The runner supplies that directory
and the event runtime include directory to the C++ compiler. A runtime can bind
this descriptor before callbacks and export a validated snapshot. These headers are generated artifacts, never checked-in references.
`rheg/tests/run-event-collector.sh` owns standalone C++ contract tests and is also invoked
by the `event-runtime` simulation fixture.
The optional `rheg/tests/run-event-perfetto.sh` builds `event-stream-test.cpp`,
`event-perfetto-test.cpp`, and the standalone converter, checks live/replay
parity, and queries the native Perfetto importer without Python. See the
[stream validation guide](../../../rheg/DEVELOPING.md#focused-validation).
This compatibility test is separate from default CIRCT simulation so Perfetto
does not become a simulator build dependency.

MLIR, generated SystemVerilog, Verilator object directories, and logs are
created in a temporary `/tmp/rhodium-circt.*` directory and removed when the
runner exits. They are diagnostic artifacts, not checked-in outputs. The only
source-writing mode is the intentional golden update described below.

The runner reports `[circt]` elapsed timings on stderr for each fixture's
materialization, CIRCT lowering, native build, and simulation, plus the complete
materialization batch. Batch timing includes managed-cache preparation and
shared module startup; per-fixture materialization includes loading, elaboration,
emission, and artifact writes. Successful builds also print Verilator's model
size and generation/build timing summary; full build logs remain available on
failure. Compare warm runs on the same toolchain when assessing an optimization.

Frequently inlined stimulus helpers can use scoped
`/* verilator unroll_disable */` annotations to avoid expanding byte-access
and ROM-setup loops at every call site. Case loops containing timing controls
may already remain runtime loops; measure generated C++ before annotating them.
Keep all scenarios and their order, and use explicit `begin`/`end` around
nested loops so the annotation immediately precedes its intended loop at the
same statement level. Do not change global DUT optimization to shrink a bench.

## Verilog references

The [CIRCT test guide](README.md#toolchain-and-exact-reference-behavior) owns
the exact-output contract, normalization boundary, tool discovery, and
alternate-version behavior. This section covers maintaining those references.

When a backend change intentionally changes generated SystemVerilog, update
only the affected reference and review the example-source diff:

```sh
FIXTURE=bundle make update-verilog-goldens
```

Without `FIXTURE`, that target rewrites every example-owned reference. The
update mode uses whichever `circt-opt` the runner resolves and does not reject
an alternate version, so use the pinned toolchain unless the version transition
itself is intentional. Never use golden updates merely to make an unexplained
diff disappear.

## Change workflows

### Add an example-backed fixture

1. Keep the canonical program and its `verilog_reference` export together in
   the owning example source.
2. Add the fixture, group, export names, optional top, and expected behavior to
   the manifest in [`run.sh`](run.sh).
3. Put portable component behavior in the [rsim inventory](../rsim/README.md).
   Add `<package>/tests/circt/verilog/<fixture>_tb.sv` and an optional matching
   `<fixture>_dpi.cpp` only for an HDL-specific or retained integration contract.
4. Run `make check-example-verilog`, then the narrow selector from the
   [CIRCT test guide](README.md#choose-the-smallest-useful-run).
5. If the reference changed intentionally, update only that fixture with the
   pinned CIRCT tool and review the example-source diff.

### Add a package-local fixture

1. Add `<package>/tests/circt/emit-<fixture>.rhm` for an integration shape that
   does not belong to a canonical example.
2. Define `program = elaborate(...)`, export it, and name `program` in the
   manifest entry's fourth field. The shared driver owns compilation and output. Use
   a custom emitter without that field only when the fixture needs its own
   prepared-graph checks or instrumentation artifacts.
3. Add a matching bench only for behavior that exercises the HDL integration boundary.
   A direct emitter may use a local DPI companion or the fixture-name-matched
   source under [`devices/uart/dpi/`](../../../devices/uart/dpi/).
4. Run the fixture first in `--verify-only` mode, then add simulation if the
   change has a runtime contract.

### Change or rename a fixture

Update the manifest, example exports or emitter, bench and DPI filenames, and
any expected-assertion label as one change. Re-run manifest coverage before the
focused external check. Do not check in generated MLIR, SystemVerilog,
Verilator object directories, or runner logs.
