<!-- Documents the bounded native-RVV Highway workloads and their bare-metal execution contract. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Highway vector workloads

This directory compiles repository-owned bounded benchmark drivers against the
pristine [`Highway submodule`](../highway/). It does not modify or compile
upstream's hosted benchmark sources. Contributor ownership and validation live
in [`../DEVELOPING.md`](../DEVELOPING.md).

## Run

Use CMake 3.23+ and the repository's GCC 15+ Newlib toolchain, including its
matching `riscv64-unknown-elf-g++` and libstdc++:

```sh
make -C sims highway-setup
make -C sims highway-test SOC=simple-spike-rva23
make -C sims highway-test SOC=simple-rv5stage-rva23 COSIM=1
make -C sims highway-test SOC=simple-rv2wide-rva23 COSIM=1
```

`highway-elfs` builds without execution; `highway-run` consumes a prebuilt shared
manifest. Build grouping, archives, target binding, attestation, JSON/JUnit
results, and CI use the same paths as the other software suites. All Simple
RVA23 core bindings select the same complete profile. No benchmark-source patch
series, GoogleTest, hosted filesystem, or OS thread runtime is required.

## Workloads

| Executable | Highway operations |
|---|---|
| `highway_algo` | Unique/all-unique for four integer widths; conditional copy across five densities |
| `highway_dot` | FP16/FP32/FP64 dot products, including short and tail inputs |
| `highway_delta` | Integer/FP adjacent differences using vector subtraction and partial loads/stores |
| `highway_math` | All 28 upstream benchmark math functions, FP32/FP64 with capped, full, and double-width tags |
| `highway_sort` | Signed/unsigned integer and FP VQSort, ascending/descending, random/sorted/reverse/duplicate inputs |
| `highway_hash` | Every upstream 32/64-bit hash family, dependent latency and vector-array throughput |
| `highway_lemac` | Keyed LeMac and fingerprints, one/two stripes, empty through multi-block inputs |
| `highway_phast` | PHAST, ShardMul, Cuckoo2x2, and u16/u32 cuckoo queries |
| `highway_iguana` | Compression and vector decompression of deterministic mixed literal/repeated bytes |
| `highway_btree` | Set/map build, point/bound/batch lookup, iteration, insertion, and erasure |

There is one bounded execution profile, not a full-versus-smoke switch. Common
element counts are 1, 7, 16, 31, 64, and 257; sorting uses 1, 31, and 129 elements
for singleton, vector-tail, and the existing largest-case paths. Every sort
datatype, pattern, and direction remains enabled, but the size grid is reduced.
Math runs eight input values per function/tag, selected from the original
16-point grid at indices 0, 2, 4, 6, 9, 11, 13, and 15 to preserve both endpoints;
hash tables use 64 keys (rounded to whole vector batches); LeMac uses at most
1024 bytes; Iguana uses 128 and 1024
bytes. B-tree set/map construction retains 16- and 257-key trees, full traversal,
and mutations, but uses 16 scalar/batched queries per tree, including endpoint
hits and interior/outside misses.

Each measurement executes exactly once, without a warmup. Its counters describe
that invocation, not warmed steady-state performance. Sort and unique copy
canonical inputs before mutation. Math precomputes its typed values before
timing and reuses one output buffer per vector tag; function and tag coverage
are unchanged, with fewer samples per input domain.

Packed u16 cuckoo queries preserve Highway's minimum of 2^18 buckets while
using its supported one-slot bucket geometry to bound the table footprint.
Cuckoo2x2 and packed cuckoo inputs use sparse public data/slot constructors,
with both primary and secondary placements, instead of scalar matching searches.
The upstream queries and packed-slot conversion are unchanged. PHAST, ShardMul,
and ordinary cuckoo builders still run on the target.
The hash-table driver disables loop if-conversion so GCC retains the sparse
empty-slot branch instead of hashing every empty slot. Unconditional fills
and copies can still auto-vectorize, and Highway's explicit RVV query kernels
remain enabled. Every builder and the full packed-slot conversion still runs
on the target, including the minimum 2^18-bucket scans.

Every ELF must execute at least one measurement and emit `HIGHWAY-RESULT: PASS`
in addition to successful HTIF completion. `HIGHWAY-BENCH` lines report guest
cycle and retired-instruction deltas. They exclude setup and console output
outside the measurement, but include setup intrinsic to an operation, such as
the B-tree build. These bounded runs are not official Highway performance scores.
The executable inventory is discovered from this directory's benchmark sources.

All measurement records retain their order and decimal counters. The driver
uses bounded whole-line integer conversion and a 4 KiB HTIF output buffer,
rather than per-measurement Newlib formatting. A partial buffer is flushed
before completion, and write/formatting errors fail execution.

Nonzero `HIGHWAY-PHASE` records account for setup, input generation/restoration,
kernels, and reporting, plus individual hash-builder/initialization and
packed-conversion phases. Packed conversion includes its upstream allocation
and clearing. Phase counts include their counter-probe overhead. `startup`
is the counter snapshot at main entry, including boot/startup execution since
the counter origin. `summary-report` accounts for the phase/completion report
itself. Their sum equals `HIGHWAY-TOTAL: before-final-report`, which excludes
the final two accounting records and exit/HTIF completion tail. These are
measured phases, not estimates from per-case counters.

## Execution boundary and limits

Compilation uses the generated SoC ISA/ABI and RAM description, with native RVV
static dispatch and no scalar target or reference comparisons. The port enables
Highway's explicit FP16 RVV override for the supported GCC toolchain. Scalar
tails and scalar work internal to a vector algorithm remain legitimate.
Sail cosim on the RTL cores checks architectural execution, not numerical or
algorithmic correctness. Spike is an execution smoke check.

`serial-pool.h` supplies a synchronous one-worker executor for the concrete
`hwy::ThreadPool` API consumed by hash builders and Iguana. It replaces only the
hosted executor at compile time; library kernels and sources remain unchanged.
It does not create threads, emulate pthreads, or claim parallel coverage.
Parallel sorting is deliberately outside this single-hart profile.

Startup owns one HTIF mailbox pair, Newlib syscalls, constructors, the bounded
heap/stack, and single-hart TLS. VQSort's non-security PRNG is seeded
deterministically. Unsupported OS clock syscalls return `ENOSYS`; kernel
measurements use architectural counters directly. Native RVV `memset`/`memcpy`
use aligned 64-bit bulk accesses, byte heads/tails, and byte vectors for operations
shorter than a cache line or copies whose operands cannot both be aligned. They bound the setup cost of large
library tables and Newlib allocations without requiring misaligned wide accesses.
All outputs stay under `PROGRAM_BUILD_ROOT`; the submodule remains pristine.
