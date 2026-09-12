<!-- Defines reproducible full-SoC benchmarks and their optimization policy. -->

# Full-SoC benchmarking

Use the banked RV5Stage SoC for end-to-end comparisons. Both engines must execute
the same target binary and coherent host protocol on corresponding IR/RTL exports.
Isolated NoC traces and handwritten software kernels answer different questions.

## Build

Use installed Racket/Rhombus, CIRCT, Verilator, Clang and a RISC-V-capable LLD.
Choose an external directory and export both representations from the same source:

```sh
export RDS_VVADD_BUILD_DIR="$(mktemp -d)"
export CC=clang CXX=clang++
bash sims/native/vvadd-build.sh rtl 8
bash sims/native/vvadd-build.sh extract 8
bash sims/native/vvadd-build.sh optimize 8
for workers in 1 2 4 8; do
  bash sims/native/vvadd-build.sh native 8 "$workers"
  bash sims/native/vvadd-build.sh verilator 8 "$workers"
done
```

The shell wrappers isolate Racket compiled roots. Retained exports can be reused
for offline optimization, but source and artifact hashes must establish their
correspondence. A changed upstream design requires fresh paired exports.

For the memory-intensive pointer-ring program:

```sh
clang --target=riscv64-unknown-elf -march=rv64i_zicsr -mabi=lp64 \
  -nostdlib -fuse-ld=lld -Wl,--build-id=none \
  -Wl,-T,sims/tests/programs/vvadd.ld sims/tests/programs/chase.S \
  -o "$RDS_VVADD_BUILD_DIR/harts-8/chase.elf"
llvm-objcopy -O binary "$RDS_VVADD_BUILD_DIR/harts-8/chase.elf" \
  "$RDS_VVADD_BUILD_DIR/harts-8/chase.bin"
bash sims/native/chase-benchmark.sh "$RDS_VVADD_BUILD_DIR" 2 1
```

Every hart owns 1,024 mutable 64-bit nodes, with private addresses distributed
across all eight LLC address stripes on the shared memory channel. Dependent loads, conditional integer mixing and dirty
stores create capacity/conflict pressure. The host oracle checks accumulated
results and 64 final nodes per hart, plus progress and overlapping execution.
This is a memory-stress workload, not an application benchmark suite.

## Policy and reporting

Banked builds use `-O3 -march=native -DNDEBUG`, with PGO and LTO disabled, for the
runtime, generated native C, drivers and all Verilator C++ compilation classes.
Verilator also uses RTL `-O3` with assertions disabled. Use the same compiler
installation for both engines. The [harness guide](README.md#optimization-controls)
and saved flags identify its explicit native policies; optional offline passes
must be recorded separately from the host compiler flags.

Pin matched worker/thread counts to the same physical CPUs. The current chase
script uses CPUs 0 through workers minus one; verify that these are physical cores
on the benchmark host. Run one timing process at a time. Each result must report
actual workers, target harts, rounds, cycles, trace digest and per-hart outcomes.
Reject mismatched signatures and changed source/binary hashes.

`seconds` measures synchronized release through observed completion, including
all CPU/cache/NoC/memory work and host polling. `whole_seconds` additionally covers
reset, coherent loading, initialization and result inspection. Both exclude offline
compilation and model construction. Report sample counts; one long run is not a
confidence interval. Increase repetitions to assess variability or close results.

Generated native plans differ by worker count, including bounded duplication and
single-worker specialization. Scaling therefore includes code-generation and
layout changes as well as synchronization. Measure pass ablations independently;
static operation counts alone do not establish throughput.
