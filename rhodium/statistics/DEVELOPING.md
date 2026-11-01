<!-- Routes declaration, occurrence-copy, runtime, and validation changes for RTL statistics. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing RTL statistics

Read [README.md](README.md) for the public contract and the
[package dependency graph](../DEVELOPING.md) for ownership.

## Architecture and implementation

- `frontend/layers/statistics.rhm` owns `stat_count` and its remappable metadata.
  Authoring has no dependency on compilation or simulator code.
- `pass.rhm` discovers declaration-bearing subtrees, specializes marked
  occurrences, certifies their clock/reset roots, and emits DPI calls and
  detached descriptors. It reuses the event package's graph copier; this does
  not couple counts to the event graph, trace schema, or RHEG runtime.
- `sims/statistics/collector.*` owns checked native accumulation. The generic
  simulator lifecycle brackets samples, freezes at exit, and reports only after
  successful finalization. Callbacks latch errors rather than throwing through
  the foreign ABI.
- `sims/program-test/performance.py` validates execution records and compares
  harness cycles and hierarchical RTL counter names uniformly. Catalogs, tolerances, and baseline
  policy belong there, not in the generic declaration, pass, or collector.

New instrumentation must preserve functional signatures and source metadata.
Do not derive counter identity from generated module names, merge repeated
instances, or add simulator switches to hardware generators. Change descriptors
and consumers together rather than adding format versions or compatibility paths.

## Validation

```sh
tools/run-racket-tests.sh rhodium/statistics/tests/statistics-test.rhm
python3 tools/testing/rsim/run.py --fixture statistics
make -C sims simulation-runtime-test
python3 sims/tests/check-statistics.py --backend circt --build .rhodium-cache/statistics-tests
make check-boundaries
```

The host checks cover immutable source metadata, descriptors, and rejected uses;
the rsim fixture checks actual edge counts and independent repeated instances
against the production collector. The simulator lifecycle fixture covers reset,
exit/drain, failed runs, and composition with trace/cosim. That target also runs
the generated statistics SoCHarness through rsim and direct SV using the
production TestDriver and native runtime, without a full SoC build. The explicit
CIRCT command checks that backend when its toolchain is installed.
