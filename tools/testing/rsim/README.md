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
export, and package-owned C++ driver. The source exports an elaborated program;
it does not select a compiler backend. [`emit.rhm`](emit.rhm) selects rsim,
writes its artifacts, and creates `ports.hpp` bindings for the public ports.
Authored port names alias typed model inputs and outputs. Records and vectors
remain native aggregates; wide bit values use the emitted word representation.

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

Rsim component lanes own standard-library, Flow, reusable-core, HardFloat,
and portable controller behavior. CIRCT and direct-SystemVerilog tests retain
emitter diagnostics, exact Verilog references, backend differential checks,
foreign ABI/event-runtime integration, and named-core/SoC harnesses. Those
checks exercise a different boundary and are not copies of component benches.
When migrating a suite, preserve its independent oracle, reset/stall timing,
parameter coverage, and expected assertion failures before retiring its SV
bench and behavioral manifest entry. Example-owned Verilog goldens may remain.

The protocol/controller migration has no remaining rsim feature blocker in the
ported inventory. The standalone FESVR MMIO requester runs through rsim too. Deliberate
HDL owners are:

| Remaining owner | Contract that keeps it on the HDL path |
|---|---|
| Event fixtures, including traced Home, FESVR, and page walks | Generated trace descriptors, DPI callbacks, and event ordering in the HDL runtime |
| UART DPI and co-sim hooks | Foreign ABI and production host integration |
| Compiler language/backend fixtures | Emitted HDL semantics, clock scheduling, exact references, and cross-backend differential oracles |
| Named-core and SoC fixtures | Existing architectural/integration harnesses outside this migration |

An untraced rsim Home or FESVR driver does not replace the trace-runtime
oracle. Their retained SV workload bodies live under the event test owner and
are selected only through its instrumented fixtures.
