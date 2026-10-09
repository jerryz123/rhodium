<!-- Describes direct C++ component simulation, fixture selection, and cycle semantics. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Rsim component tests

Reusable hardware behavior is tested by compiling its elaborated program with
`rsim_target` and driving the emitted C++ model directly. No SystemVerilog,
CIRCT, DPI bridge, or Verilator driver is involved. Contributor policy and
ownership live in the [test development guide](../DEVELOPING.md).

From the repository root:

```sh
python3 tools/testing/rsim/run.py --fixture rv64i-alu
python3 tools/testing/rsim/run.py --fixture queue --fixture sync-memory-masked
python3 tools/testing/rsim/run.py --group std
python3 tools/testing/rsim/run.py --fixture event-queue
make rsim-component-test
```

Use `--list` with any selection to inspect it without compilation. Repeat
`--fixture` or `--group`; combining them selects their intersection. `FIXTURE`
and `FIXTURES` also select fixtures when no `--fixture` is supplied. Invalid
names and empty selections fail. `CXX` selects the host compiler, which must
support C++20; builds use `-O2`. `--jobs` bounds concurrent compiler processes.
Racket and Rhombus run through the repository's managed-cache wrapper.

## Driver contract

[`fixtures.tsv`](fixtures.tsv) records the fixture name, group, program source,
export, and package-owned C++ driver. Two optional trailing columns name a
compilation-target export from that source and a whitespace-separated list of
additional repository-relative C++ sources. An omitted target or `-` selects
`rsim_target`; an instrumented fixture can export
`rtl_pipeline_target(rsim_target, [event_trace_pass()])`. The target must emit a
standalone rsim model with its logical public signature. Additional sources are
linked into the test, and their parent directories are added to the header search
path. This permits linking the production collector without an HDL bridge.
[`emit.rhm`](emit.rhm) writes all compilation artifacts, including pass-produced
descriptors, and creates `ports.hpp` bindings for the public ports.
Authored port names alias typed model inputs and outputs in the `ports`
namespace, so they do not redeclare host-library globals. Use `ports::name`
when a port name conflicts with a host symbol (for example, `ports::select`).
`test.hpp` makes unambiguous short names available for convenience. Records and
vectors remain native aggregates; wide bit values use the emitted word representation.

A driver includes `test.hpp` and supplies an independent scoreboard or expected
values. `eval()` settles combinational outputs without advancing state.
`tick_model()` executes one model clock edge and refreshes the outputs. Sample
handshakes and update the reference model using pre-edge values. Drive reset
explicitly and tick it; model construction alone does not replace hardware
reset. There is no implicit clock thread or HDL delta-cycle scheduler.

`CHECK` remains active in optimized builds. `run_test` reports one `PASS` only
after every check completes. Use `expect_failure` with the exact hardware
assertion label for invalid transactions; a timeout is not an expected failure.
Loops have a shared cycle limit, and the runner bounds emission, compilation,
and execution time. Resetting `dut` to a new `Model` permits independent cases
without rebinding the port aliases.

A failed run retains generated sources, compiler logs, and execution logs and
prints their directory. `--work-dir DIRECTORY` retains successful runs too;
use a dedicated artifact directory. Reusing it replaces generated C++/headers
and binaries for the selected fixtures so stale regions cannot enter a build.

## Test boundaries

Rsim component lanes own portable language behavior, standard-library, Flow, reusable-core, HardFloat,
and portable controller behavior. The `cores-components` group also includes
RV5Stage register-file, FP pipeline, integer execution, writeback calendar,
memory arbiter, divider workload, and three vector-sequencer configurations.
RV2Wide core/FP, cache, disabled-fetch, assembly and frontend prediction, BHT,
MMU, enabled-fetch with retirement tracing, and RV32 integration also run in
this group.
RV5Stage composed core, control, vector, memory-routing, coherent-cache, and
LR/SC workloads use separate `cores-execution-*`, `cores-vector-*`,
`cores-memory`, and `cores-cache` groups. Each configuration keeps its complete
scoreboard and parameter-specific workload.
CIRCT and direct-SystemVerilog tests retain emitter diagnostics, exact Verilog
references, backend differential checks, and foreign ABI/context and HDL scheduling
checks. Hardware and instrumentation semantics belong on rsim even when their
implementation calls a foreign collector. The queue, pipeline, arbiter, demux,
atomic-fork, broadcast, join, stall/offer, retained-owner/window, crossbar,
feedback, branching, partial-tracing, OfferRegister, selected-parent,
runtime-identity, and retained-bank event suites run the trace pass and production
RHEG collector directly, preserving their independent graph oracles and reference
lanes. Traced Home/subordinate/FESVR, vector milestones, fetch assembly/source/
prediction/throughput, cache acknowledgement/copyback, page walks, and multiply
also run directly on rsim in their owning protocol/core groups. Retirement and
load-hit tracing, FP core integration, and vector configuration/memory observers
use the same direct path. Test-only clocked instrumentation samples nested public
component ports; it does not depend on generated C++ temporary names.
When migrating a suite, preserve its independent oracle, reset/stall timing,
parameter coverage, and expected assertion failures before retiring its SV
bench and behavioral manifest entry. Example-owned Verilog goldens may remain.

The protocol/controller migration has no remaining rsim feature blocker in the
ported inventory. The standalone FESVR MMIO requester runs through rsim too.
Remaining HDL workloads include runtime/elastic backend differential fixtures,
co-sim, foreign ABI/context and assertion scheduling, and system harness
integration. A foreign collector alone does not require HDL simulation.
Preserve independent graph checks when migrating tracing; an untraced
replacement does not cover a trace-runtime oracle.
