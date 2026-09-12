<!-- Defines reproducible full-SoC benchmarks and their optimization policy. -->

# Full-SoC benchmarking

Use the banked RV5Stage SoC for end-to-end comparisons. Both engines must execute
the same target binary and coherent host protocol on corresponding IR/RTL exports.
Isolated NoC traces and handwritten software kernels answer different questions.

## Benchmark platform

The reference measurements below ran on this host. CPU topology, compiler versions
and placement were recorded with the experiment; the remaining host inventory was
captured during PR preparation on September 12, 2026.

| Component | Configuration |
|---|---|
| Processor | AMD Ryzen 7 7800X3D, one socket, eight physical cores, 16 logical CPUs |
| Caches | Per core: 32 KiB L1D, 32 KiB L1I, 1 MiB L2; shared: 96 MiB L3 |
| Memory | 61.9 GiB OS-visible RAM; DIMM speed and timings were not recorded |
| OS | Arch Linux, x86-64, kernel `7.2.3-arch1-2` |
| Host toolchain | Clang/Clang++ 22.1.8; Verilator 5.052 |
| CPU policy | `amd-pstate-epp`, `powersave` governor, `balance_performance` preference; boost enabled, frequency not fixed |
| Placement | CPU 0 for one worker/thread; CPUs 0–1, 0–3 and 0–7 for two, four and eight |
| SMT / NUMA | SMT enabled, but sibling CPUs 8–15 excluded from simulation affinity; one NUMA node |

Both engines use the same host compiler and `-O3 -march=native -DNDEBUG`, without
PGO or LTO. `-march=native` targets this host's instruction set, including AVX2,
AVX-512 and BMI extensions; availability does not imply every kernel uses them.
Timing runs execute sequentially under an exclusive performance lock. Background
OS activity is not eliminated by CPU affinity.

## Reference result: eight-core memory stress

The September 11–12 experiment ran the complete eight-core RV5Stage SoC: cores,
private caches, an 8×3 tile network, eight LLC/memory banks, devices and coherent
host loading. This measurement predates the upstream integration in this branch.
The current configuration uses a shared external memory channel and a revised
HTIF/boot protocol; the table is evidence for the earlier paired design and
simulator snapshot, not a fresh performance measurement of that updated topology.

Every hart executes the same [pointer-chase program](../tests/programs/chase.S)
over a private ring of 1,024 mutable 64-bit nodes, striped across all eight banks.
The 64 KiB aggregate footprint exceeds the combined 16 KiB private L1D capacity
and 32 KiB LLC capacity. Two full traversals per hart exercise dependent loads,
data-dependent branches, integer mixing and dirty stores. This is a custom
memory-stress workload, not a general application suite.

| Workers / threads | Native workload (s) | Verilator workload (s) | Matched speedup | Native full run (s) | Verilator full run (s) |
|---:|---:|---:|---:|---:|---:|
| 1 | 165.160 | 2010.621 | 12.174× | 187.775 | 2284.231 |
| 2 | 90.792 | 1015.771 | 11.188× | 103.181 | 1154.233 |
| 4 | 51.130 | 545.231 | 10.664× | 58.139 | 619.377 |
| 8 | 32.787 | 261.379 | 7.972× | 37.266 | 297.024 |

Each cell is one complete run; these are not repeated-sample confidence estimates.
All eight cells match 9,030,764 workload cycles, 10,248,383 total cycles, 23,585
host polls, transaction digest `74111e35c6520941` and every hart's results.
All harts make progress and their execution intervals overlap. The independent
host oracle checks final accumulators and 64 final nodes per hart. Source and
binary hashes remained unchanged across measurement.

Native scales 5.037× from one to eight workers, versus 7.692× for Verilator.
Its lower elapsed time coexists with weaker parallel scaling; these measurements
do not distinguish synchronization, load imbalance and generated-code effects.

The measured native images include decoder specialization, bit-relation
canonicalization, idle-FIFO folding, contract lifting, matcher-grant reuse and
shared handshake rows, in addition to the ordinary compiler passes. The saved
optimizer pass trace establishes these settings: per-build switches were zero
because these transformations had already been applied to the input image.
Parallel images add bounded snapshot-prefix replication; the single-worker image
is unduplicated and uses partial empty-region specialization. Runtime flags were
`4290056208` for one worker and `4088747088` for two/four/eight; see the
[policy table](README.md#optimization-controls) for their meaning. The specialized
offer backend is ineligible for this whole-SoC workload with memories and a host
callback. Optional transformations are not all generic defaults.

The following build instructions target the current upstream-integrated design.
New results require fresh paired exports and a recorded optimization policy;
they should not be expected to reproduce the earlier topology's cycle counts.

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
