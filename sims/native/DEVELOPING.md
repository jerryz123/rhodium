<!-- Owns native harness adapters, generated-code tooling, and benchmark validation. -->

# Developing native harnesses

Read the [usage guide](README.md), [simulator ownership rules](../DEVELOPING.md)
and [runtime architecture](../../rhodium/sim/DEVELOPING.md). Keep hardware policy
in the owning library or SoC and implementation-independent simulation in
`rhodium/sim/`.

## Source ownership

| Files | Responsibility |
|---|---|
| `library.rhm`, `cpu.rhm`, `contracts.rhm` | Explicit circuit-identity registrations and semantic bindings |
| `emit-*.rhm`, `*-harness.rhdl` | Executable extraction and diagnostic fixtures |
| `host-model.rhm`, `workload-host.h`, `mini-loader.h` | Sized HTIF binding, acknowledged boot publication and MiniSoC loader |
| `compile-model.c`, `specialize-empty.cpp` | C emission driver and proven constant/empty-region specialization |
| `vvadd-loader.h`, `chase-workload.h` | Shared coherent protocol, liveness checks and workload oracles |
| `native-vvadd.cpp`, `verilator-vvadd.cpp` | Matched cycle drivers and timing boundaries |
| `vvadd-build.sh`, `*-benchmark.sh`, `*-report.cpp` | Reproducible builds, isolated timings and signature comparison |
| `soc-noc-*` | Actual-network extraction, boundary replay and software reference |
| Flow, payload, routing and partition fixtures | Small independent models for semantic and performance diagnosis |

## Adapter correctness

Select replacements by declaration identity and validated parameters, never by
printed names. A resolver observes a completed module and must not create hardware.
Keep target/fallback graph equivalence, invalid previews, reset writes and update
priority explicit when recognizing a semantic object. Unsupported cases retain
ordinary IR. Follow upstream module ownership when importing moved flow primitives.

Constant/empty specialization consumes matching source JSON, generated plan and
emitter-owned storage markers. Only total pure expressions may be replaced under
proven current-state facts. Retain unknown computations, stores, assertions and
host effects. Current-state aliases must identify the correct bank and width.
Generated state requires its declared alignment and initial import; clearing
bytes is not a substitute for initializing cache metadata.

## SoC and NoC verification

The shared workload loader verifies every hart's progress, retirement, execution
interval and final result. The pointer-ring oracle owns address geometry and
independent arithmetic. Keep coherent transactions and timing boundaries identical
between engines; a faster host shortcut would change the workload.

Network extraction follows occurrence/interface provenance and rejects dependencies
on excluded state. Compare every boundary output against the observed whole SoC
before treating an extracted graph as the reference. Trace replay includes input
delivery but excludes decoding and separate correctness checking from timing.
Software references must preserve arbitration, invalid previews, queue occupancy
and backpressure; they are not instruction-level functional substitutes.

Borrowed payloads require an old-snapshot lifetime proof. Prefer compact indices
when a known owner supplies the storage base. Reset, stalled outputs, delayed
workers and buffer reuse must remain safe. A local fixture's protocol does not
implicitly authorize a whole-SoC publication change.

## Validation and measurement

[tests/sim](../../tests/sim/README.md) owns runtime/compiler regressions. Run focused
specialization tests when changing source markers, aliases or branch placement.
Use complete output replay for fixture changes, including reset, backpressure,
failed publication and reattachment. Run Racket commands with fresh compiled roots
and `-y`; retained JSON permits subsequent offline iteration.

Builds and validation hold the shared performance lock; timing holds it exclusively.
Freeze models, generated libraries, drivers and flags. Compare actual worker counts,
cycles, traces and results before reporting speed. Keep model construction,
compilation, loading and workload execution boundaries explicit. Preserve sample
counts and raw evidence externally. The [benchmark guide](BENCHMARKS.md) owns
operator commands and interpretation.
