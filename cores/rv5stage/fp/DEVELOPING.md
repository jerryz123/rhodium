<!-- Guides contributors through RV5Stage floating-point ownership and validation. -->

# Developing RV5Stage floating point

Read the package [README](README.md) for supported profiles, flow contracts,
destination ownership, and scalar-pipeline integration. This guide owns the FP
implementation structure, dependency boundary, and focused validation.

## Architecture and dependency boundary

The package may depend on RV5Stage FP decode controls, the RISC-V FP model and
RTL helpers, HardFloat, and public Rhodium libraries. It must not import
`core.rhdl`, caches, the MMU, backends, examples, or tests. The decode package
may import [`types.rhdl`](types.rhdl), but not FP execution modules.

Instruction selection remains in
[`../decode/fp-ctrl.rhdl`](../decode/fp-ctrl.rhdl), while
[`../core.rhdl`](../core.rhdl) owns dispatch, scalar-pipeline integration,
memory requests, and integer-result completion. Cache, MMU, and uncached paths
import `types.rhdl` only to preserve FP precision metadata.

## Implementation map

| File | Ownership |
|---|---|
| [`types.rhdl`](types.rhdl) | Precision tags shared with decode and memory paths |
| [`bundles.rhdl`](bundles.rhdl) | Issue, execution, completion, load, and store payloads |
| [`register-file.rhdl`](register-file.rhdl) | Three-read, two-write architectural FP register bank |
| [`datapath.rhdl`](datapath.rhdl) | Fixed-latency F, D, optional half-precision, and Zfa execution |
| [`div-sqrt.rhdl`](div-sqrt.rhdl) | Buffered HardFloat division and square-root lanes |
| [`pipeline.rhdl`](pipeline.rhdl) | Register state, scoreboard, execution composition, LSU bridges, and completion arbitration |

Keep `types.rhdl` dependency-light because decode and memory paths import it.
The payload definitions may depend on decode controls; datapaths and the
register file feed the pipeline composition. Keep profile specialization at
host elaboration so disabled formats and units do not become runtime hardware.

## Change workflow

1. Put shared precision tags in `types.rhdl` and cross-boundary payloads in
   `bundles.rhdl`.
2. Keep combinational format operations in `datapath.rhdl`; put retained or
   variable-latency divide/square-root behavior in `div-sqrt.rhdl`.
3. Integrate register state, scoreboarding, execution selection, and completion
   only through `pipeline.rhdl`.
4. Update the package README when supported profiles or observable flow,
   ownership, timing, or failure contracts change.
5. Preserve the common enabled/disabled interface shape used by `core.rhdl`.

## Focused validation

From the repository root, run the host owners in one fresh compiled-root batch:

```sh
tools/run-racket-tests.sh \
  cores/rv5stage/tests/fp-ctrl-test.rhm \
  cores/rv5stage/tests/fp-pipeline-test.rhm \
  cores/rv5stage/tests/rv5stage-test.rhm
```

The host control-table test owns decoder specialization. Use the
`rv5stage-fp-register-file` and `rv5stage-fp-pipeline` CIRCT fixtures for their
cycle-visible boundaries. Include `rv5stage-core-rv32f` and
`rv5stage-core-rv64d` when imports, payloads, profile specialization, or
scalar-core integration change. Run `make check-boundaries` after moving
modules or changing dependency direction. The backend fixture
[`DEVELOPING.md`](../../../tests/backend/DEVELOPING.md) owns runner modes and
artifact policy.
