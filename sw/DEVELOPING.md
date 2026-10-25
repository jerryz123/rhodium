<!-- Defines ownership, provenance, and validation for target software and ELF builders. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing target software

Read the [software README](README.md) for supported suites and public commands.
`sw/` owns target-executed sources and software compilation. It consumes the
canonical, generated SoC target descriptor; it does not own the SoC memory map,
UDB projection, simulator harness, FESVR transport, or result runner. Those
remain in [`socs/`](../socs/DEVELOPING.md),
[`riscv/`](../riscv/DEVELOPING.md), and
[`sims/`](../sims/DEVELOPING.md), respectively.

## Source and build ownership

The named upstream submodules are pinned at repository gitlinks. Keep their
checkouts pristine. Apply the ordered architectural-test, Bringup-Bench, and SKL patch
series only to build-local copies through
[`riscv/patched_submodule.py`](../riscv/patched_submodule.py). Put Rhodium-owned
bare-metal startup, linkers, output checks, and ports beside their corresponding
upstream source. Preserve upstream licensing and the repository's
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

[`build/`](build/) owns ELF construction, target validation, compiler and ISA
checks, content-addressed build reuse, and manifests. `build.py` and `isa.mk`
compile the ISA tests and upstream benchmarks; the named benchmark builders
compile CoreMark, Embench-IoT, Bringup-Bench, and SKL. ISA selection derives upstream
groups, native-width smoke operations, and Make XLEN from the target descriptor.
F/D targets also select representative arithmetic, fused operations, division,
conversion, load/store, and move smoke tests for each advertised precision.
Select physical and virtual `ma_data` only when that descriptor advertises
Zicclsm; include the physical variant in ISA smoke for such targets.
Validate ELF32 and ELF64 load segments including BSS and entry location on
both fresh builds and cache reuse. ISA tests require an executable entry segment;
upstream benchmarks require the entry in a RAM load segment but do not require
`PF_X`, matching the FESVR loader, which does not enforce segment permissions.
Apply this same distinction during shared-artifact binding.
Record upstream inventory gaps (currently RV32 CBO-zero) separately from target
capability exclusions; never select by
observed pass status. `opensbi.py` derives firmware layout from the selected
target descriptor and builds OpenSBI `FW_JUMP`.
`build-litmus.py` requires an explicit `litmus7` executable and selects cases by
source thread table and pinned model-state inventory, asks litmus7 to emit
static pre-silicon RISC-V C, and links its test body and generated IO/random
helpers with `litmus7-port.c` and `litmus7-main.c` against the existing RISC-V
benchmark startup/HTIF support. The port implements only the
fixed-hart worker and option interfaces exercised by that build mode; generated
files stay in the content-addressed build directory. The simulator result
runner checks the complete histogram against the pinned Herd states and requires
at least the requested number of samples. Source-identical duplicate names
may share one case; the builder explicitly selects the namespaced MP, SB, and LB
variants, and other distinct ambiguous sources remain excluded. Keep the tool
binary, support directory, suite build specification, and port in the cache identity.
The tiled smoke and full profiles both require this litmus7 path. Smoke names
live in `litmus-riscv-baremetal/smoke-cases.txt`; the builder validates every
name against the current target-filtered inventory instead of silently
dropping unsupported cases. Full selects that entire inventory and records all
build failures with `--keep-going`. The smoke file is a coverage and runtime
selection, not a pass list: preserve known failure reproducers when changing it.
CI checks out herdtools7 at its workflow-pinned commit, installs its separate
`aslref` library into the pinned OCaml switch, and then builds litmus7. The Make
targets retain an explicit executable dependency for local use.
Compile litmus7 support code for the selected target's scalar base extensions,
plus Zalasr when advertised, so compiler-generated vector or other optional
instructions do not confound the memory-model workload. This changes neither
litmus7's generated instruction rows nor the selected SoC attestation.
Filter source instructions requiring Zalasr when the selected target does not
advertise `zalasr`; `inventory_cases` counts the raw model-backed candidates,
while `discovered_cases` counts the profile-eligible set. Do not treat toolchain
rejections as passing simulations.
For a batch build, `build-litmus.py --keep-going` records per-case
generation, adaptation, compilation, and link failures in the manifest's
`build_failures` list while retaining successfully built tests. It exits
nonzero if any case failed; `discovered_cases` is only a candidate count, not
the number of runnable ELFs. Use the successful manifest entries for a separate
simulation run and report build failures distinctly from simulator outcomes.
The builder also replaces one recognized generated sense-barrier template with
calls into the port's per-hart-arrival barrier, rejecting a changed template.
This avoids an atomic decrement on the shared counter in tiled simulation and
removes the generated `fflush` after its directly emitted HTIF output. It does
not rewrite generated test instructions or outcome code. The port forwards only
the test identity and complete histogram with one HTIF write per line;
litmus7's witness and provenance footer is not used by the model checker.
Compile the generated test body with the port's `litmus7-streams.h` shim so
its `stdout` and `stderr` identities do not evaluate Newlib's stream globals.
The port ignores these stream arguments; do not let generated output pull in
Newlib stdio's medlow-only objects at the high RAM address.
Keep histogram-header parsing local and bounded: formatted input from Newlib
pulls stdio and allocator objects whose medlow relocations cannot link into
the high-address bare-metal memory image.
OpenSBI's DTB emitter remains in `sims/opensbi/` because it projects the
simulator's selected SoC and transport endpoint. ACT's UDB/Sail platform
configuration and its DUT runner likewise remain in `sims/arch-test/`.

Simulator-owned build targets pass a generated descriptor and explicit source,
port, patch, compiler, and output paths to these builders. Keep both CoreMark
variants and all bounded workload profiles unchanged during path-only moves;
do not infer suite selection from prior pass status.

## Shared software builds

`build-skl.py` uses the adjacent SKL bare-metal CMake project to consume
upstream's test-registration hook and extension detector. Pass the generated
target's compiler flags to that detector as well as the compilation commands.
Exclude X390 source implementations and executable suites and standalone
scalar-reference suites. Keep upstream test cases enabled as execution workloads.
Do not copy upstream's catalog into a first-party allowlist. The generated
inventory must be nonempty, sorted, unique, and contained in its build root.
The patched upstream driver consumes its callback interface and counter/status/allocation
helpers, requiring both test-input workloads and benchmark cases before emitting
a success marker. Clear each case's verification callback before initialization
so reference buffers are neither allocated nor populated. Do not call numerical
verification or padding/clobber checks. Upstream tables still reference scalar
symbols, so linking the reference library does not imply executing its routines.
Preserve the remaining callback ordering, status gates, cleanup,
and execute-step counter boundaries. All callbacks run at error-only log level;
still invoke report callbacks so their failures propagate. Emit one integer-only
`SKL-BENCH` line per successful benchmark case instead of upstream floating-point
throughput reports. Newlib owns allocation and formatting;
the port owns only heap bounds, startup, and the single HTIF syscall boundary.
Keep upstream sources pristine. The manifest declares execution-only mode;
Sail cosim checks architectural execution in the RTL CI jobs, not SKL numerical
accuracy. Plain Spike and non-cosim RTL runs provide execution smoke coverage.
[`skl-patches/series`](skl-patches/series) owns the fixed upstream adaptations:
the first patch bounds the single functional profile, the second changes the
existing driver to execution-only sequencing and integer reporting, and the
third optimizes input initialization. Apply them only through the shared
patched-submodule materializer, never Python source substitutions or edits to
the submodule. The bare-metal port owns only CMake, startup, linker, and syscalls;
there is no second suite driver or symbol-renaming override.
Preserve every case, including zero sizes, strides, dilation, depth multipliers,
and generic/specialized kernel coverage. Keep the descriptive dimensions in the
shared build specification consistent with the bounded-workload patch; they
are not a second source transformation or a benchmark score. When advancing the
upstream pin, review patch contexts, case catalogs, and kernel contracts.
The initialization patch adds `test/buffers.h` and `test/fp-buffers.h` to the
build-local source. Fill full source allocations, including padding, but only
allocate audited write-only data-movement and depthwise destinations. Preserve
allocation sizes, alignment, geometry, and cleanup. Retain GEMM C initialization
because it is a read/write operand. Re-audit independence from initial output
contents and the no-alias contract on upstream updates. Specialized FP16/FP32
depthwise kernels read partial sums only after their first filter-row pass has
initialized the output; those internal reads do not require a prefill.
Replace only floating-point random and sequential generators, not global
`rand()`, integer generators, or static input copying. Preserve every case's
typed domain and initialization mode.
Case/stream/index mixing makes random fills vectorizable; FP32/FP64 interpolation
avoids intermediate FP16/BF16 arithmetic, so generated samples are not bitwise
upstream inputs. BF16 uses alias-permitted integer storage and explicit
round-to-nearest/ties-to-even encoding, without a native BF16 ISA requirement.
Check finite-domain bounds, sequential ordering, zero/tail lengths, distinct
random streams, and BF16 rounding against the compiler's conversion helpers.
Inspect generated code to confirm vectorization without per-element RNG or
BF16 helper calls. The ordered patch contents and recorded initialization/reporting
policies must invalidate cached builds. Qualify runtime or buffer-policy changes with the
full suite on Spike and both RTL cores with Sail cosim.
SKL uses the existing GCC/Newlib toolchain; GCC ignores Clang unroll pragmas
without treating them as errors. CMake and compiler identity, port bytes, and
the upstream revision, patch series, and shared materializer participate in the
content key. A complete checksum-verified cache hit skips source materialization
and CMake, but still checks linked ISA attributes and RAM bounds. On a cache miss,
apply the patches and remove corrupt owned ELFs before asking CMake to rebuild them.
Publish the upstream MIT and Rhodium Apache license texts as checksum-bearing
manifest files so the generic ELF archive carries the required notices too.

`build/program_target.py` projects a suite's ELF build specification from the
generated SoC descriptor. Keep config names and resolved microarchitecture
metadata in the full target fingerprint, outside the compilation identity.
ISA inventory includes extensions, MMU, and privilege modes; compiled benchmarks
include compiler ISA/ABI and RAM layout. CoreMark also embeds clock frequency.
Each builder adds its actual selection and scale options to the specification,
then includes source revisions, compiler identity, adapter/port bytes, and patches
in its content-addressed cache key. Add any new target-dependent compiler input
to this projection before allowing cross-config reuse.

`build/bind.py` consumes a complete shared build manifest and validates its
specification against the destination SoC. It checks ELF hashes, XLEN, physical
load segments, entry location under the producing suite's policy, and boot harts
before writing a separate `run-manifest.json` beside the build manifest. It retains the original build
target as provenance and uses the destination's full target fingerprint for
execution. Binding needs neither a compiler nor Rhodium elaboration. Simulator
attestation and execution remain owned by `sims/program-test/`.

Litmus uses this same boundary: its compilation specification includes hart IDs,
ISA/ABI, RAM, clock frequency, case selection, and embedded sample count, but not
core or SoC identity. The pinned litmus7 binary, support files, model log, source
revisions, compiler, and runtime bytes still invalidate the build cache.
Histogram checking remains an execution contract, not a build-group selector.

OpenSBI projects its minimum firmware ISA and firmware/jump/FDT layout and adds
the checksum of the actual generated platform DTB. `opensbi.py manifest` records
the relocatable firmware, S-mode validation payload, and DTB as one archive.
Binding requires the destination's generated DTB, checks both image hashes and
their relocated RAM ranges, and retains build provenance. Do not replace that
DTB comparison with equality of ISA or config names. Image symbol ownership is
checked during compilation; execution checks the unchanged published images.

## Change an architectural-test pin

Follow the repository's [source documentation requirements](../AGENTS.md#source-documentation),
including the exemption for files under `tests/`.

Advance `riscv-arch-test` on upstream's `act4` development branch and stage
the new gitlink before `make -C sims arch-test-setup`, which initializes the
index-recorded revision. Keep its checkout pristine. Rebase only the remaining
downstream contracts in `riscv-arch-test-patches/`; prefer landed upstream
generators and fixes over retaining a second implementation.
At pin `ea22c06859691c5e2b44ef1fe4f7e06a8f1e4256`, ACT requires Sail model
0.16 and bundles UDB 0.1.18 with udb-gen 0.1.17. Refresh its isolated Python and Ruby dependencies
with the ordinary setup target, then generate all canonical suites in a fresh
ACT build root. Preserve each core's own UDB configuration and reference
signatures; never weaken profiles or exclude failed tests during validation.
Native vector-crypto generation and index-overlap alignment replace the former
downstream generators. Generated DUT headers now live under `work/<config>/env`;
do not assume that an older cached build directory has the current layout.
Native Sscofpmf generation now inhibits every privilege while preloading
counters, replacing the former split-counter preload patch. Privileged suite
merges and renames are upstream-owned; regenerate the canonical inventory rather
than translating old suite names or preserving old test counts. Spike remains
a DUT here even though ACT no longer supports it as a reference model.
The [simulator guide](../sims/DEVELOPING.md#other-simulation-contracts) owns
platform hooks, schema overlays, and full generation/execution checks.

## Validation

Run the focused software-target, ELF-layout, and architectural-projection checks:

```sh
python3 -m unittest discover -s sw/tests
make -C sims program-test-adapter-test arch-test-adapter-test opensbi-adapter-test
make check-license-headers check-boundaries
```

Build recipes, caches, artifact reuse, and runner orchestration have no separate
test suites. Validate those changes through the actual affected build and
execution path. For moved upstreams or ports, initialize their new submodule
paths and build representative ELFs with the existing `make -C sims ...-elfs` or
`...-firmware` targets. Execute at least one workload and OpenSBI validation
on each affected SoC to validate the load and completion boundary. The
[simulator guide](../sims/README.md) owns those commands and generated-artifact
locations. Keep source, patch, port, compiler, and target identity in each
builder's cache key; a path migration may cause a one-time cache miss.
