<!-- Describes native simulator regression coverage and focused test entry points. -->

# Native simulation tests

Run `make sim-test` with Racket/Rhombus, C/C++ compilers, Python 3 and the
[standalone compiler dependencies](../../rhodium/sim/compiler/README.md).
The runner creates temporary model and compiled-bytecode directories. Select tools
with `RACKET`, `CC`, `CXX` and optionally a prebuilt `RDS_OPTIMIZER`.
`RDS_SANITIZER_FLAGS=-fsanitize=undefined` instruments the runtime.

The suite compares original graphs, optimized graphs, interpreted execution and
compiled C against independent arithmetic and transition models. It covers:

- Narrow and wide arithmetic, word recovery, selectors, byte writes and snapshots.
- FIFO, scoreboard, matcher, ALU, TLB and whole-flow replacements, including invalid
  previews, backpressure, reset priority and malformed inputs.
- Payload pooling and lifetime/exchange proofs, including intermediate readers,
  exact capacities, rollback and library reattachment.
- Static partitions, bounded replication, repeated instances, persistent workers,
  both synchronization implementations and state-bank parity.
- Code-generation layouts, lazy guards, stable-value caches, specialized decoders,
  empty-region specialization and debug/release execution.
- Coherent workload loading, sized host transactions and acknowledged boot release.

## Focused compiler and runtime checks

Most `*-test.sh` scripts accept an existing runtime build directory as their first
argument. They build native fixtures without repeating hardware elaboration.
Run the script owning the changed behavior; its matching C++ source defines the
reference oracle and edge cases. Examples:

```sh
bash tests/sim/contract-kernel-test.sh /path/to/runtime-build
bash tests/sim/payload-lifetime-test.sh /path/to/runtime-build
bash tests/sim/bulk-cycles-test.sh /path/to/runtime-build
bash tests/sim/decoder-specialization-test.sh /path/to/runtime-build
```

`workload-host-test.cpp` independently checks the sized-memory adapter's loading,
boot acknowledgement, polling, reset and error behavior. `loader-trace-test.cpp`
checks the workload loader's transaction state machine.

## RTL differential checks

`make sim-differential-test` additionally needs CIRCT and Verilator. It compares
10,000 complete cycle observations for the hierarchy fixture, then times rotated
runs of each execution mode. Set `CIRCT_OPT` and `VERILATOR` to select tools.
This focused fixture uses Verilator O2 and generated C++ O2.

`RDS_BENCH_MODEL=pipeline make sim-differential-test` selects eight independent
arithmetic recurrences. `RDS_BENCH_CYCLES` overrides the timed cycle count.
These fixtures diagnose runtime behavior; full-SoC performance and matched
multithreaded comparisons belong to the
[native benchmark workflow](../../sims/native/BENCHMARKS.md).

Follow the [runtime development guide](../../rhodium/sim/DEVELOPING.md) when adding
coverage. Keep generated models, RTL, binaries and reports outside version control.
