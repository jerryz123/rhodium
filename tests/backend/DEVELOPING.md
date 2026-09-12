<!-- Explains CIRCT fixtures, event snapshot/stream tests, and Verilog references. -->

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

The `event-runtime`, `event-pipeline`, `event-elastic`, `event-queue`, `event-arbiter`, `event-demux`, `event-atomic-fork`, `event-broadcast`, `event-join`, `event-stall`, and `event-offer` direct fixtures
additionally link the independent RHEG collector implementation. Each local DPI companion is a transfer scoreboard,
not a second implementation of the collector or ABI.
`event-window` additionally checks retained multi-entry contributions through
downstream elastic storage against a public fill/release model. `event-frontend`
runs the existing fetch bench against an instrumented production frontend;
its public memory/instruction scoreboard checks exact occurrence parents and
captures, not generated controls or payload-equality matching.
`event-home` runs the inclusive-Home bench with its production scoped contracts
and checks every emitted request/response/data occurrence against public port
transfers, with reused IDs, hit/miss responses, stalls, and pending reset.
`event-subordinate` checks intrinsic retained-request contracts on the shared
single-beat MMIO engine. Its public-transfer scoreboard requires exact parents
through DBID and delayed write data, ignores credit returns, and tests reset
in every retained phase plus read/write completion under backpressure.
`event-fesvr` instruments the production host access engine under the existing
MMIO bench. An independent public-transfer scoreboard checks every request
fragment, write-data transfer, and final host response against its accepted
command, including errors, delayed completion, and pending reset. The ordinary
and traced emitters share `sims/tests/fesvr-mmio-fixture.rhdl` service parameters.
`event-feedback` checks a queue/grant recirculation loop without an internal
checkpoint. Its public-control FIFO scoreboard preserves exact parent references
across multiple laps, repeated payloads, stalls, simultaneous transfers, and reset;
an uninstrumented lane checks functional equivalence. Host diagnostics cover
same-cycle cycles and unbounded parent accumulation.
`event-branching` extends this coverage through direct/configured 3x3 crossbars
with two fresh sources, a feedback queue, and two buffered exits. Its independent
FIFO model checks every exact parent occurrence, all source-to-exit routes,
repeated feedback, changed grants under stall, concurrent exits, and pending reset.
Keep uncertified feedback fanout under host rejection coverage; do not weaken
branching validation to make a cyclic fixture elaborate.
Pipeline, elastic, queue, arbiter, and broadcast coverage includes direct
self-described modules and configured adapters, with independent transaction
scoreboards and uninstrumented reference lanes. `event-instrument-test.rhm`
also checks named local regions, module/inline ownership equivalence, wrapper
delegation through checkpoints, and conflicting/foreign-control rejection.
The `rv5stage-load-hit` fixture also links RHEG and exports a matching descriptor;
its scoreboard checks D-cache S1/MEM and S2/WB alignment, public core/cache
admission, and retained S2-to-S3-to-S4 ancestry including direct-refill fields.
The existing bench checks functional load timing and architectural results.
Every refill receives RetryAck and PCrdGrant before retransmission, with request
backpressure; both attempts must retain the same S4 occurrence.
`event-retained` independently covers repeated output, release/replacement,
pending reset, and parentless detached traffic selected beside traced traffic.
Its scoped relation ends at an opaque child's attempt output before a normal
Flow mapper. Host coverage also preserves a downstream checkpoint, permits an
unrelated traced lane, and rejects summaries overlapping existing Flow.
`event-crossbar` links RHEG and checks direct/configured grant-controlled
crossbars with flow-through input queues. Its independent FIFO/transfer model
checks exact parent occurrence IDs for equal payloads, all routes, simultaneous
outputs, zero grants, changing stalled selections, full replacement, and reset.

An emitter may additionally export `event_manifest_cpp`, generated from the
same instrumented elaboration it prints. `load-example.rkt` writes this string
to `<fixture>_manifest.h` beside temporary MLIR. The runner supplies that directory
and the event runtime include directory to the C++ compiler. The join fixture
uses this path to bind its manifest before callbacks and export a validated
snapshot. These headers are generated artifacts, never checked-in references.
`../../rheg/tests/run-event-collector.sh` owns standalone C++ contract tests and is also invoked
by the `event-runtime` simulation fixture.
The optional `../../rheg/tests/run-event-perfetto.sh` builds `event-stream-test.cpp`,
`event-perfetto-test.cpp`, and the standalone converter, checks live/replay
parity, and queries the native Perfetto importer without Python. See the
[stream validation guide](../../rheg/DEVELOPING.md#focused-validation).
This compatibility test is separate from default CIRCT simulation so Perfetto
does not become a simulator build dependency.

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
