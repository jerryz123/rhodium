<!-- Explains how to maintain Rhodium's CIRCT fixtures, simulations, and exact Verilog references. -->

# Developing backend tests

Read the [backend test guide](README.md) first for focused selectors, runner
modes, toolchain behavior, and failure interpretation. This guide owns fixture
and artifact structure, exact-reference maintenance, and changes to the runner.

The [backend package guide](../../rhodium/backend/README.md) owns lowering
architecture and operation contracts. The
[example guide](../../examples/README.md) owns the canonical example catalog.

## Architecture and implementation map

The manifest in [`run-circt.sh`](run-circt.sh) is authoritative for fixture
names, groups, example exports, direct emitters, Verilator tops, reference
eligibility, and expected failures.

| Path | Maintenance responsibility |
|---|---|
| [`*-test.rhm`](circt-test.rhm) | Host-side emission, diagnostic, and backend-policy tests |
| [`run-circt.sh`](run-circt.sh) | Fixture manifest, selection, CIRCT pipeline, exact diff, and Verilator orchestration |
| [`load-example.rkt`](load-example.rkt) | Materializing selected example exports in one process |
| [`emit-*.rhm`](emit-sync-ram.rhm) | Direct MLIR integration shapes without an example-owned reference |
| [`verilog/`](verilog/) | Behavioral benches and optional local DPI companions |
| [`../../examples/`](../../examples/README.md) | Canonical designs and their exact Verilog-reference exports |

The [repository test-development guide](../DEVELOPING.md) owns test placement,
authoring principles, and CI classification outside this backend-specific
fixture boundary.

## Fixture and artifact ownership

Example-backed entries name an example module, a concrete design export, an
optional Verilator top, and either a Verilog-reference export or `-`. A named
reference marks a compact fixture whose readable generated output is part of
the example; `-` keeps integration-scale fixtures under lowering and behavioral
coverage without a large exact snapshot. The ordinary golden pair is `design`
and `verilog_reference`; additional designs use the same prefix for both
exports, such as `cast_design` and `cast_verilog_reference`. References live
beside their designs so reviewers can see the authoring input and generated
result together.

Direct `emit-*.rhm` fixtures own backend integration shapes that do not belong
to one canonical example. They print MLIR for CIRCT verification and may name
a Verilator top, but they do not own example Verilog references. Add or rename
either kind through the manifest rather than relying on filename discovery.
`make examples` and `make check-example-verilog` check every concrete design's
manifest coverage and validate only declared golden exports without running
CIRCT or Verilator.

Behavioral benches live in [`verilog/`](verilog/). An example fixture with a
top uses `verilog/<fixture>_tb.sv`. The runner automatically links a matching
`verilog/<fixture>_dpi.cpp`; direct emitter fixtures can additionally link a
matching source from [`../../devices/dpi/`](../../devices/dpi/), with hyphens
in the fixture name changed to underscores. Assertion and protocol monitors
also have dedicated negative benches. Those checks pass only when simulation
fails and reports the expected assertion label, so an expected failure is not
treated as an unchecked crash.

The `event-runtime`, `event-pipeline`, and `event-elastic` direct fixtures
additionally link the event package's collector implementation. Each local DPI companion is a transfer scoreboard,
not a second implementation of the collector or ABI.

MLIR, generated SystemVerilog, Verilator object directories, and logs are
created in a temporary `/tmp/rhodium-circt.*` directory and removed when the
runner exits. They are diagnostic artifacts, not checked-in outputs. The only
source-writing mode is the intentional golden update described below.

## Verilog references

The [backend test guide](README.md#toolchain-and-exact-reference-behavior) owns
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

1. Keep the canonical design and its `verilog_reference` export together in
   the owning example source.
2. Add the fixture, group, export names, optional top, and expected behavior to
   the manifest in [`run-circt.sh`](run-circt.sh).
3. If simulation is required, add `verilog/<fixture>_tb.sv` and an optional
   matching `verilog/<fixture>_dpi.cpp`.
4. Run `make check-example-verilog`, then the narrow selector from the
   [backend test guide](README.md#choose-the-smallest-useful-run).
5. If the reference changed intentionally, update only that fixture with the
   pinned CIRCT tool and review the example-source diff.

### Add a direct emitter

1. Add `emit-<fixture>.rhm` for an integration shape that does not belong to a
   canonical example.
2. Declare it in the manifest; do not rely on filename discovery.
3. Add a matching bench only when the fixture needs behavioral validation.
   A direct emitter may use a local DPI companion or the fixture-name-matched
   source under [`../../devices/dpi/`](../../devices/dpi/).
4. Run the fixture first in `--verify-only` mode, then add simulation if the
   change has a runtime contract.

### Change or rename a fixture

Update the manifest, example exports or emitter, bench and DPI filenames, and
any expected-assertion label as one change. Re-run manifest coverage before the
focused external check. Do not check in generated MLIR, SystemVerilog,
Verilator object directories, or runner logs.
