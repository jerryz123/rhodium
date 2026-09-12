<!-- Documents native simulator harnesses, workload selection, and performance tooling. -->

# Native simulator harnesses

These tools connect the [native runtime](../../rhodium/sim/README.md) to explicit
standard-library/processor models and complete SoC workloads. Synthesizable topology
and platform policy remain in `socs/`; loading, callbacks and benchmarking live here.

## Smoke and offline compilation

```sh
bash sims/native/run.sh
bash sims/native/compare.sh
```

The first command runs a native MiniSoC smoke. The second compares its transactions
and cycles with Verilator, including generated one/multi-worker execution. Racket,
Rhombus, the host C/C++ toolchain and CIRCT/Verilator are required as appropriate.
Scripts create isolated compiled roots and keep generated output outside the checkout.
Use `RACKET`, `CC`, `CXX` and the documented tool-specific environment overrides
when selecting installed tools.

For an already exported model, use the [standalone compiler](../../rhodium/sim/compiler/README.md).
`compile-model.c` emits generated C and an optional plan for a selected worker/flag
configuration. `library.rhm`, `cpu.rhm` and `contracts.rhm` own explicit replacement
and semantic metadata adapters; ordinary unsupported logic remains in the graph.

## Multicore workloads

`vvadd-build.sh` supports `rtl`, `extract`, `optimize`, `native` and `verilator`
stages for the banked 1/2/4/8-hart RV5Stage configurations. Set
`RDS_VVADD_BUILD_DIR` to an external build directory. The native and Verilator
stages take hart and worker/thread counts; they build matched O3/native binaries.
See [benchmark commands and policy](BENCHMARKS.md).

Both drivers share coherent loading, synchronized release, progress polling and
result verification. These workload adapters support RV64 and the standard
`0x1000` boot-address register; they wait for boot-write acknowledgement before
polling. `RDS_WORKLOAD=vvadd` is the default, with 32–65536 rounds.
`RDS_WORKLOAD=chase` selects eight private mutable pointer rings striped across
all eight LLC address stripes, with 2–256 traversals and 8 KiB of data per hart.
The chase workload checks independent accumulated results and sampled final nodes.
Both workloads require progress and retirement from every configured hart.

After building all matching models and `harts-8/chase.bin`:

```sh
bash sims/native/chase-benchmark.sh BUILD_DIR 2 1
```

The report requires all 1/2/4/8-worker/thread cells, identical simulated behavior
and matching artifact hashes. It reports workload and full-run time separately.

## Optimization controls

The generic runtime and compiler defaults are documented by their owning guides.
The banked native harness additionally chooses these explicit policies:

| Code-generation policy | One worker | Multiple workers |
|---|---|---|
| Automatic inlining, direct state packs and typed scratch | Enabled | Enabled |
| Wide/semantic regions, guarded one-hot work and union demand | Enabled | Enabled |
| Stable payload/field reuse, flow caches and cold regions | Enabled | Enabled |
| Batched FIFO preparation | Enabled | Enabled |
| Word primitive and transition lifting | Enabled | Disabled |
| Spin synchronization, parallel state staging and publication | Disabled | Enabled |
| Additional dependency reordering, scratch coalescing and assembly kernel selection | Disabled | Disabled |

- One worker uses partial empty-region specialization by default;
  `RDS_VVADD_SPECIALIZE=none` disables it. Parallel builds use no empty specializer.
- Parallel builds use `model-parallel.rsim` when available, while one worker uses
  the unduplicated image. The optimize stage generates bounded snapshot prefixes.
- `RDS_FLAGS` overrides the generated runtime plan flags. The chosen numeric flags,
  specialization mode and artifact hashes are saved with each build.
- Decoder specialization, idle/stationary-object folding, indexed datapaths,
  matcher grant reuse, contract lifting and handshake sharing are individually
  opt-in through the `RDS_VVADD_*` settings in `vvadd-build.sh`.
- Matcher-mask simplification defaults on only for explicitly contract-lifted
  single-worker builds. Other builds leave it off unless selected.
- Standard banked comparisons disable PGO and LTO and use
  `-O3 -march=native -DNDEBUG` for both engines.

A transformed image must match its source JSON before a further pass is applied.
Do not select options using graph size alone; compare their generated code and
measured performance on the intended worker count.

## Inspection and diagnostic models

`vvadd-report.cpp` compares signatures, aggregates timings and correlates IR with
plans. `scratch-inspect.cpp`, `benchmark-single.cpp` and profiling helpers inspect
storage and generated code. `profile-window.h` enables optional counters only at
the measurement boundaries.

`soc-noc.sh` extracts and validates the actual SoC network from retained typed IR.
Replay and software-reference tools preserve its boundary timing, queueing and
arbitration. Their measurements isolate the NoC and are distinct from closed-loop
SoC execution. Small flow, payload, routing and mesh fixtures provide independent
oracles and diagnostic comparisons; they do not replace the complete SoC benchmark.

[DEVELOPING.md](DEVELOPING.md) describes ownership and validation requirements.
