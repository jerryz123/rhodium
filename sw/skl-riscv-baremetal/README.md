<!-- Describes execution-only SiFive SKL vector workloads on RV64 bare-metal SoCs. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# SKL on Rhodium

This port runs the pinned [SiFive Kernel Library](../skl/) on Simple RVA23
SoCs. Contributor ownership and validation rules are in
[`../DEVELOPING.md`](../DEVELOPING.md).

The ordered [`../skl-patches/series`](../skl-patches/series) supplies the bounded
workloads, execution-only driver, and efficient input generation in a build-local
copy. The upstream submodule stays pristine. This port supplies only the
bare-metal CMake integration, startup, linker, and syscall boundary.

```sh
make -C sims skl-setup
make -C sims skl-test SOC=simple-rv5stage-rva23 COSIM=1
make -C sims skl-test SOC=simple-rv2wide-rva23 COSIM=1
make -C sims skl-test SOC=simple-spike-rva23
```

Use the repository's GCC/Newlib toolchain and CMake 3.23 or newer. `skl-elfs`
builds without simulation; `skl-run` executes an existing manifest through the
normal simulator flow. `PROGRAM_JOBS`, `SKL_TIMEOUT` (900 seconds per suite), and `SKL_MAX_CYCLES`
bound execution; `BUILD_JOBS` controls compilation.

Upstream's CMake catalog and compiler-detected target extensions select the
suites. X390 implementations and standalone scalar-reference suites are
excluded. At the pinned revision this selects 28 suites: three depthwise
convolutions, five GEMMs, nine packing/unpacking/transposes, three exponentials,
one GELU, three sigmoids, and four softmax suites.

Each executable retains upstream's test-input cases and benchmark cases as
execution workloads, without numerical verification, scalar reference
computation, or padding/clobber comparisons. Reference-only buffers are not
allocated or populated. A pass requires successful HTIF completion and a
`SKL-RESULT` marker with workload/benchmark counts after both nonempty categories
finish successfully. Setup, execution, reporting, and cleanup failures still fail
the suite. Sail cosim checks architectural execution on the RTL cores; it does
not establish SKL's mathematical accuracy. Runs without cosim, including Spike,
are execution smoke tests. CI enables cosim on both RTL cores.
Benchmark cases emit one integer-only `SKL-BENCH` line with suite name, case ID,
cycles, and instructions. Verbose upstream informational reports and target-side
floating-point throughput formatting are suppressed; errors remain visible.
Reported measurements cover the kernel's execute step, not the full program; these functional runs are not
published comparative benchmark scores.

The nine pack, unpack, and transpose suites use deterministic, case-ID/index-based
integer patterns instead of per-element `rand()` and modulo operations. Every
source element in the allocated span is initialized, including stride and tile
padding. Their distinct write-only destinations retain the original allocation
size and alignment but are not pre-filled. Depthwise convolution outputs are
likewise allocated without filling; GEMM C remains initialized because kernels
can read it through `beta*C`.

Floating-point random fills use deterministic case/stream/index-based mixing
instead of per-element `rand()`. Random and sequential interpolation use FP32
for FP16/BF16/FP32 buffers and FP64 for FP64 buffers, clamped to each case's
original typed bounds. BF16 storage is rounded with integer operations rather
than per-element software conversions, requiring no BF16 ISA extension. Random
samples and reduced-precision sequential rounding differ from upstream, but
initialization modes, domains, static input bytes, and case parameters remain
unchanged. Full source allocations, including padding, are initialized.
These initialization and reporting policies belong to the single functional
profile and participate in build-cache identity.

One functional profile bounds data volume for RTL simulation: large depthwise cases use
17 input channels (34 outputs for depth multiplier two) and 7×7 or 8×8 inputs;
unary operations use 257 elements, and GEMM benchmark cases use 7×17×19
matrices. GEMM test-input cases bound M/K to at most 11, retaining their
modulo-four tails and unchanged N dimensions to cover vector boundaries.
For FP64, large N dimensions instead become
31, 32, 33, or 37, preserving modulo-eight tails and repeated vector tiles.
Case selection, strides, and dilation are unchanged; upstream's
15-channel tail cases are retained too. These edits
are made only in build-local source copies and recorded in the build specification.
Row-wise softmax uses 1, 2, 3, and 5 rows (five in benchmark cases). FP32
softmax test-input columns are 1, 27, 67, 60, 28, and 64 instead of the
larger upstream sizes, retaining short, multi-vector, aligned, and tail lengths.
All alignment/stride, beta, domain, and zero-size cases remain present.
Packing's 300×200 cases become 172×40, retaining two full 64×16 blocks plus
the original tails; 64/128-row cases use 16 rows. Transposes bound repeated
rows to 16–31 while retaining their original column sizes and row counts modulo
16. Natural row/column-major strides follow the bounded dimensions; block layouts
and explicit padding/non-natural strides are unchanged.

The port supplies startup, a target-derived linker map, a bounded Newlib heap,
and one shared HTIF console/completion endpoint. It does not provide an OS,
threads, files, or additional ISA support. Unsupported extensions remain
excluded by upstream's capability checks, not by observed pass status.
