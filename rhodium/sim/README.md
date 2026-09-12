<!-- Defines the native simulator's execution model, API, and optimization controls. -->

# Native simulation

The native simulator executes verified Rhodium IR using two-state, fixed-width
values and synchronous read-current/write-next semantics. It is a cycle simulator:
flow contracts can replace implementation logic, but preserve latency,
backpressure, arbitration, invalid-cycle outputs and state publication.

The frontend exports typed JSON once. A standalone C++ compiler optimizes it and
writes a versioned binary model. The C17 runtime can interpret that model or emit
C for compilation into a shared library. The compiled cycle path uses fixed
operations, storage offsets and worker ownership; it does not dispatch an IR
interpreter or allocate objects each cycle.

## Build and use

See [compiler usage](compiler/README.md) for offline compilation and the
[native harness guide](../../sims/native/README.md) for executable examples.
Dependencies are Racket/Rhombus for extraction, a C17 compiler and pthreads for the
runtime, and a C++17 compiler with nlohmann/json and Boost headers for the optimizer.
Generated models, source and binaries belong outside the checkout.

`main.rhm` exports compilation, inspection and serialization. Pass
`~preserve_semantics: #true` to retain records, vectors and source conditional
groups in JSON. `~optimize: #false` permits saving this semantic snapshot before
running the standalone compiler. Explicit model and contract resolvers recognize
registered circuit identities; display names never authorize replacement.

The [C header](runtime/include/rhodium_sim.h) defines the runtime API:

1. Load with `rds_load_with_options`, then bind any clocked host callbacks.
2. Resolve ports once and set their input words, least-significant word first.
3. Call `rds_eval` before reading outputs. Evaluation does not publish RTL state.
4. Call `rds_advance` to validate effects and commit one rising edge.
5. Evaluate again before observing the new outputs.

`rds_emit_c` writes source without invoking a compiler. Compile it as a shared
library and attach it with `rds_use_compiled`. Attachment checks model, execution
plan and storage compatibility. Replacement preserves current state.
`rds_advance_cycles` repeats edges with public inputs held fixed; zero is a no-op.
It preserves callbacks and stops at a failing edge after any preceding commits.
The caller must exclusively own a simulator during API calls.

## State and semantic kernels

Flip-flops use two banks whose roles swap at publication. SRAM has stable backing
storage and sparse pending writes; it is not copied between banks each cycle.
Semantic objects have independently owned state and preparation/publication steps.
Supported kernels include queue variants, pipelines, offer/credit controls,
scoreboards, counters, arbiters, broadcasts, matchers and explicitly registered
ALU/TLB models. Unsupported compositions retain ordinary IR execution.

A contract is permission to analyze dependencies, not permission to discard
payloads or change behavior. Payload pooling is optional and must prove ownership
and lifetime. Fields read in flight remain available to control logic; only opaque
body fields can travel as compact indices into retained storage.

## Optimization policy

The default runtime options are one worker and flags zero. Offline compiler
passes and runtime code-generation flags are separate controls.

| Policy | Default | Control |
|---|---|---|
| Fixed scheduling, scratch reuse, batching and dependency reordering | Enabled | `RDS_NO_REUSE`, `RDS_NO_BATCH`, `RDS_NO_REORDER` disable individual policies |
| Static partitioning for requested parallel workers | Enabled | `RDS_NO_PARTITION` selects the component scheduler |
| Double-buffered flip-flops | Enabled in compiled execution | `RDS_COPY_STATE` selects copying |
| Permissive results for partial operators | Enabled | `rds_set_strict` enables diagnostics; release compilation can remove diagnostic roots |
| Reference operation execution | Disabled | `RDS_REFERENCE` |
| Spin waiting and parallel publication/state staging | Disabled | `RDS_SPIN`, `RDS_PARALLEL_PUBLISH`, `RDS_PARALLEL_STATE` |
| Wider/semantic/demand-gated regions | Disabled | `RDS_WIDE_REGIONS`, `RDS_SEMANTIC_REGIONS`, `RDS_SELECT_REGIONS`, `RDS_UNION_DEMAND`, `RDS_GUARDED_ONEHOT` |
| Snapshot, field and flow caches | Disabled | `RDS_STABLE_PAYLOADS`, `RDS_STABLE_FIELDS`, `RDS_FLOW_CACHE` |
| Word semantic transitions and batched FIFO preparation | Disabled | `RDS_LIFT_PRIMITIVES`, `RDS_LIFT_TRANSITIONS`, `RDS_FLOW_PREPARE` |
| Alternative storage, placement and inlining | Disabled | Remaining explicit flags in the C header |

Eligibility checks can retain the ordinary implementation even when an option is
selected. Harnesses may choose an explicit combination of flags; their presets
are not runtime defaults. See the [benchmark policy](../../sims/native/BENCHMARKS.md).

Strict execution checks partial operations even in an unselected mux arm.
Release compilation assumes the design's established index/selector bounds;
use debug images for diagnostics. Neither mode implements Verilog four-state
values, arbitrary event scheduling, procedural delays or general SystemVerilog
behavior. Models must satisfy the supported synchronous clocking contract.

## Inspection and validation

JSON snapshots preserve source origins and semantic bindings. `rds_emit_plan`
adds concrete worker, state and temporary locations; generated C includes source
markers for correlation. Static operation counts are not dynamic instruction
counts. Compare generated code and measured throughput when selecting policies.

Run `make sim-test` for the owning suite and `make sim-differential-test` for
Verilator-backed comparisons. [Implementation rules](DEVELOPING.md) describe
publication, ownership and focused validation.
