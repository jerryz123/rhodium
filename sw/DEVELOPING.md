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
checkouts pristine. Apply the ordered ISA-benchmark, architectural-test,
Bringup-Bench, Embench, OpenSBI, SKL, and generated-litmus runtime patch series
only to build-local copies through
[`riscv/patched_submodule.py`](../riscv/patched_submodule.py). Put Rhodium-owned
bare-metal startup, linkers, output checks, and ports beside their corresponding
upstream source. Preserve upstream licensing and the repository's
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

[`build/`](build/) owns ELF construction, target validation, compiler and ISA
checks, content-addressed build reuse, and manifests. `build.py` and `isa.mk`
compile the ISA tests and upstream benchmarks; the named benchmark builders
compile CoreMark, Embench-IoT, Bringup-Bench, SKL, and Highway. ISA selection derives upstream
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

Multihart benchmarks materialize [`riscv-isa-tests-patches/series`](riscv-isa-tests-patches/series)
through the shared materializer. The series owns startup and collective exit;
`RHODIUM_BENCHMARK_HARTS` is a compiler definition, not a source substitution.
Single-hart benchmarks and ISA tests use the unmodified upstream tree. Hash the
series, ordered patches, and materializer for every multihart build.

First-party bare-metal ports align each HTIF mailbox symbol to the platform's
64-byte cache line and pad the `.tohost` output section through its final line.
Put alignment on the output section itself, not only a preceding location-counter
assignment: `> ram` allocation must honor the actual section address. Keep normal
data, BSS, and executable sections outside those lines so coherent FESVR polling
cannot false-share with workload state. Linker assertions enforce symbol alignment;
check the section bounds and neighboring allocations in real linked ELFs after
changing a port or linker. Litmus force-includes its port's `htif.h` when compiling
upstream syscalls so those pristine definitions inherit the same symbol alignment.

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
runner checks the complete histogram against independently enumerated Herd states
and requires at least the requested number of samples. Source-identical duplicate names
may share one case; the builder explicitly selects the namespaced MP, SB, and LB
variants, and other distinct ambiguous sources remain excluded. Keep the tool
binary, support directory, suite build specification, and port in the cache identity.
The upstream `herd.logs` selects the model-backed inventory, but is not the
execution oracle: it assumes exact-address LR/SC pairing. Enumerate each selected
source with the pinned `herd7`, `riscv.cat`, and `lrscdiffok`, retaining all states
with `-speedcheck false`. RISC-V allows SC to a different address within the LR
reservation set. This model intentionally abstracts physical reservation layout;
it is not a check of granule bounds. Do not whitelist observed
states or weaken RVWMO ordering axioms. Retain `herd.log` for diagnosis and record
the model, variant, and tool provenance in the manifest. Hash both tool binaries,
their support/model directories, and the enumerated states into the build identity;
do not hash the nondeterministic Herd timing lines.
The tiled smoke and full profiles both require this litmus7 path. Smoke names
live in `litmus-riscv-baremetal/smoke-cases.txt`; the builder validates every
name against the current target-filtered inventory instead of silently
dropping unsupported cases. Full selects that entire inventory and records all
build failures with `--keep-going`. The smoke file is a coverage and runtime
selection, not a pass list: preserve known failure reproducers when changing it.
Its 64 cases cover basic ordering and dependency/fence variants, same-address
coherence, three-/four-hart propagation, and AMO/LR-SC acquire/release behavior.
Keep one shared selection for all tiled cores; expanding this list does not
change the separate sample-count or hart-placement policy.
CI checks out herdtools7 at its workflow-pinned commit, installs its separate
`aslref` library into the pinned OCaml switch, and then builds litmus7 and herd7.
The Make targets require explicit executables for local use.
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
The builder applies the ordered [runtime patch series](litmus-riscv-baremetal/patches/series)
with the shared `riscv/patched_submodule.py` materializer. Each case stages the
pristine generated test and upstream startup in `upstream-source/`; the
materializer produces the patched `source/` tree used for compilation. The
series, ordered patches, and materializer participate in the build identity.
The builder does not rewrite source with regular expressions or string
substitutions. The patch selects the startup hart count through `LITMUS_HARTS`
and replaces the generated sense barrier with
calls into the port's per-hart-arrival barrier, rejecting changed contexts.
This avoids an atomic decrement on the shared counter in tiled simulation and
removes the generated `fflush`. It does not rewrite generated test instructions
or outcome collection. Arrivals are release stores; unsuccessful polls use
relaxed loads. An acquire fence after observing all arrivals orders
the completing hart before it publishes the release generation store. A peer
that observes that generation with a relaxed load likewise executes an acquire
fence before returning. Preserve both successful-exit ordering paths when
changing polling; do not remove or coalesce the generated barrier calls.
Measure layout changes over the complete selected workload before padding
arrival counters: extra cold cache lines can outweigh reduced false sharing.
The second patch brackets the test identity and complete
histogram with the port's output begin/end calls, then returns from the postlude
before computing or formatting litmus7's unused witness and provenance footer.
The generated postlude supplies static storage of `(HASHSZ + 2) * 1024` bytes:
one bounded line per possible state plus the two header lines. The port appends
directly to that buffer and sends the completed batch in one HTIF write. It
needs neither allocation nor histogram-header parsing. Reject buffer overflow,
overlong lines, and incomplete final lines before transmitting any histogram;
keep HTIF failure reporting explicit. The host checker still validates the
histogram structure, samples, and allowed outcomes.
The patch caps generated `HASHSZ` at twice the maximum observation count plus
one, never increasing the upstream table size. The maximum observation count is
`SIZE_OF_TEST * NUMBER_OF_RUN * NEXE`: the fixed-argv port preserves those
generated defaults, and each instance records at most one outcome per internal
iteration. Size by the sampling budget, not the model's allowed states, so an
unexpected or forbidden outcome remains recordable. C preprocessor expressions
consume the generated constants directly, without parsing their values. Check
the sticky `hash_ok` flag after worker joins and histogram merging, before reporting any
histogram; insertion or merge overflow must terminate as a harness error rather
than silently omit observations. Keep barriers, generated test instructions,
sample counts, and host histogram validation unchanged.
Compile the generated test body with the port's `litmus7-streams.h` shim so
its `stdout` and `stderr` identities do not evaluate Newlib's stream globals.
The port ignores these stream arguments; do not let generated output pull in
Newlib stdio's medlow-only objects at the high RAM address.
Do not introduce Newlib formatted-input or allocator dependencies for output
batching: their medlow relocations cannot link into the high-address bare-metal
memory image. The generated storage and bounded character appends suffice.
OpenSBI consumes the SoC's BootROM DTB at runtime; its build-local patch series
initializes firmware-owned HTIF reset without a synthetic device-tree node, while
console selection discovers the DT-described UART. ACT's UDB/Sail platform
configuration and its DUT runner likewise remain in `sims/arch-test/`.

Simulator-owned build targets pass a generated descriptor and explicit source,
port, patch, compiler, and output paths to these builders. Keep both CoreMark
variants and all bounded workload profiles unchanged during path-only moves;
do not infer suite selection from prior pass status.

## Buildroot Linux smoke

[`build/linux.py`](build/linux.py) consumes the same target descriptor and
OpenSBI layout as other software builders. The adjacent
[`buildroot-riscv/`](buildroot-riscv/README.md) BR2_EXTERNAL tree owns the kernel
configuration, static PID 1, device table, and pinned kernel hash. Keep the
Buildroot and OpenSBI submodules pristine; Buildroot owns its Linux, musl, and
Linux-targeting toolchain builds. Do not substitute the bare-metal Newlib runtime.
Buildroot applies its version-specific [GCC 15.3.0 backport](buildroot-riscv/patches/gcc/15.3.0/0005-riscv-add-h-to-the-canonical-order.patch)
to the initial and final compiler sources. It lets configure accept the target's
`h` extension. Retire it when the selected GCC release contains PR target/121312.
Project the target compiler ISA into Buildroot's native RISC-V fields instead
of maintaining a core-name allowlist. Linux and OpenSBI retain their upstream
privileged-code ISA/ABI policies; userspace uses the projected ISA and LP64D.

The builder wraps the raw Linux Image in a physical-address ELF for FESVR,
without executable startup glue. Validate its boot-header offset, image/BSS
extent, RAM bounds, and non-overlap with firmware and the relocated DTB.
OpenSBI remains the only HTIF mailbox owner. The generic runner requires both
the PID 1 marker and kernel poweroff message, rejects panic/oops/failure output,
and requires successful simulator termination. An HTIF reset alone is not proof
of a successful Linux boot.

Build reuse hashes source pins, patches, the port, builder dependencies, target build inputs,
host compiler identity, and build paths. Revalidate every exported artifact's
checksum before reuse. The manifest carries configs, init, and license notices
alongside the firmware and kernel ELFs. The simulator's BootROM supplies its
own hardware DTB; firmware copies it to RAM and applies normal fixups. The
kernel configuration owns the smoke command line, including its 128 MiB limit.

Run `python3 -m unittest discover -s sw/tests` for target projection changes,
and `make -C sims program-target SOC=simple-spike-rva23` for descriptor changes.
CI's build-only Linux producer compiles on x86 Linux, packages the ordinary
manifest archive, extracts it, and binds it to each compatible Simple RVA23
descriptor. Buildroot owns the toolchain; the producer installs no simulator or
bare-metal compiler. Only upstream downloads are cached, not toolchains or
compiled output. The [CI guide](../tools/testing/DEVELOPING.md#ci-ownership)
owns selection, gating, and artifact retention policy.
Kernel, init, toolchain, or handoff changes require a real Linux-host build.
Boot qualification remains deferred and has no CI execution job. Before claiming
a qualified Linux platform, boot first on Simple Spike, then RV5Stage and
RV2Wide. Neither a successful image build nor static archive binding is boot
coverage.

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
The toolchain setup rebuilds the pinned GCC release's Newlib, libgcc,
libstdc++, and libsupc++ for RV64GC/LP64D with `-mcmodel=medany`, keeping matching
headers and libraries together. `tools/install-riscv-runtime.sh` owns those checksum-pinned sources
and library-only recipes; it uses GCC's installed target headers without
rebuilding the compiler. GCC and G++ must match the pinned runtime source version.
The existing toolchain cache owns the installed result.
Do not change the SoC RAM map or add libc symbol overrides to accommodate
medlow archives.
All toolchain-dependent CI caches include both installer scripts. SKL also
records and hashes the compiler-selected `libc.a`, `libm.a`, and `libgcc.a`, so
replacing a runtime without changing the GCC version invalidates its ELF cache.
Keep upstream sources pristine. The manifest declares execution-only mode;
Sail cosim checks architectural execution in the RTL CI jobs, not SKL numerical
accuracy. Plain Spike and non-cosim RTL runs provide execution smoke coverage.
[`skl-patches/series`](skl-patches/series) owns the fixed upstream adaptations:
the first patch bounds the single functional profile, the second changes the
existing driver to execution-only sequencing and integer reporting, and the
third optimizes floating-point initialization and audited output allocations;
the fourth supplies constant movement and integer input tiles. Apply them only
through the shared patched-submodule materializer, never Python source substitutions or edits to
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
The movement sources and integer GEMM/convolution random inputs copy rotated
1024-element constant tiles from `test/input-values.inc`. This bounds ELF size
without target-side pattern generation. Keep full readable padding initialized,
all original allocations/frees, and GEMM C's signed input domain. The integer
adapter rejects changed random domains; explicit static and sequential inputs
retain upstream initialization. These samples deliberately differ from upstream
and the former per-case movement pattern. Do not replace global `rand()`.
Case/stream/index mixing makes random fills vectorizable; FP32/FP64 interpolation
avoids intermediate FP16/BF16 arithmetic, so generated samples are not bitwise
upstream inputs. BF16 uses alias-permitted integer storage and explicit
round-to-nearest/ties-to-even encoding, without a native BF16 ISA requirement.
Check finite-domain bounds, sequential ordering, zero/tail lengths, distinct
random streams, and BF16 rounding against the compiler's conversion helpers.
Inspect generated code to confirm vectorization without per-element RNG or
BF16 helper calls. The ordered patch contents and recorded initialization/reporting
policies must invalidate cached builds. Validate runtime or buffer-policy changes with the
full suite on Spike and both RTL cores with Sail cosim. For input fixture checks,
run `python3 -m unittest discover -s sw/tests -p test_benchmark_inputs.py` with
initialized upstream submodules; `RHODIUM_BRINGUP_SOURCE` and `RHODIUM_SKL_SOURCE`
may name pristine temporary checkouts instead.
SKL uses the existing GCC/Newlib toolchain; GCC ignores Clang unroll pragmas
without treating them as errors. CMake and compiler identity, port bytes, and
the upstream revision, patch series, and shared materializer participate in the
content key. A complete checksum-verified cache hit skips source materialization
and CMake, but still checks linked ISA attributes and RAM bounds. On a cache miss,
apply the patches and remove corrupt owned ELFs before asking CMake to rebuild them.
Publish the upstream MIT and Rhodium Apache license texts as checksum-bearing
manifest files so the generic ELF archive carries the required notices too.

`build-highway.py` builds the repository-owned CMake project in
[`highway-riscv-baremetal/`](highway-riscv-baremetal/), linking unmodified
[`highway/`](highway/) headers and library sources directly. Do not patch,
copy, or compile upstream's hosted benchmark drivers. The adjacent benchmark
sources own one bounded native-RVV profile; CMake discovers all of them and
emits the ELF inventory. Keep input sizes, repetitions, and covered API families
in the port, with descriptive selection metadata in the build specification.
The whole executable port inventory, GCC/G++ and CMake identities, source pin,
and generated target requirements invalidate the content-addressed build.
The selected `libc.a`, `libm.a`, `libgcc.a`, `libstdc++.a`, and `libsupc++.a`
paths and hashes also participate in that identity and are recorded in the manifest.
Rebuilding a runtime with the same compiler version must not reuse older ELFs.
Use Highway's native static target and explicit GCC FP16-vector override; reject
unsupported target capabilities rather than silently falling back to scalar.
The port's synchronous executor supplies only the single-worker ThreadPool API
needed by library builders and Iguana. It is not a threading implementation.
Startup must initialize constructors and TLS as well as BSS, FP/vector state,
the Newlib heap, and the single HTIF boundary. Keep deterministic inputs and
sort PRNG state, anti-elision barriers, canonical input preparation before mutation,
nonempty completion accounting, and status checks. Do not add scalar reference
passes. All families run on Spike and both RTL cores with Sail cosim; this is
execution coverage, not algorithmic accuracy or an upstream performance score.
Keep the lossless per-measurement records buffered through the existing HTIF
boundary, with bounded integer formatting and explicit write/overflow failures.
Execute each case once without a warmup; these counters are not warmed
performance scores. Preserve all sort datatypes/patterns/orders at the reduced
size grid, B-tree construction/traversal/mutation with bounded hit/miss/boundary
queries, and math functions/tags with endpoint-preserving input subsampling.
Attribute input preparation, kernels, reporting, and hash builders to
measured phases; document probe and end-of-run accounting boundaries. Retain
all constructors and packed conversions on the target. Prevent dense compiler
if-conversion of sparse packed-slot scans without disabling unconditional-loop
vectorization. Dot and delta use aligned, constexpr typed inputs; obscure their
addresses at the kernel boundary to prevent compile-time evaluation. Keep the
same 257-element readable extent and exact prior values. Reuse canonical inputs
only when their typed contents and required
mutation restoration are preserved, and keep B-tree construction inside its
workload. Qualify word-wide memory helpers for independently varied source and
destination alignment, tails, values, returns, and guards. Full-profile changes
require all ten suites on all three cores, case-inventory comparisons, and
before/after full-run instruction and runtime evidence.
Validate the normal shared archive/binder path and checksum-verified cache reuse,
in addition to building and running every ELF. Preserve upstream license notices
in the published archive and keep the submodule pristine.

Embench's [`embench-iot-patches/series`](embench-iot-patches/series) supplies the
exact seed-zero CRC32 input bytes, guards local-scale defaults for compiler
overrides, and owns the bounded XGBoost sample selection and exact correctness
check. Compile directly from that patched tree with `-DLOCAL_SCALE_FACTOR`;
the builder reads pristine scale defaults only for manifest metadata. Preserve
the ten XGBoost sample indices and nine-correct expectation recorded in its
functional-profile metadata when maintaining the patch. There is no second
rewritten source tree or Python source adaptation.
Bringup-Bench's series also recognizes `TARGET_RHODIUM` in `target/libtarg.h`;
compile against that patched header rather than generating a shadow header.
Bringup-Bench's packet fixture likewise preserves every seed-42 packet field and
the existing output hash. Both kernels use opaque input pointers to retain
target execution. Keep patch bytes and the shared materializer in builder cache
identities, and refresh whole-ELF performance expectations from qualified CI
measurements after intentional setup-cost changes.

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
core or SoC identity. The pinned litmus7/herd7 binaries, support/model files,
enumerated oracle, source revisions, compiler, and runtime bytes still invalidate
the build cache.
Histogram checking remains an execution contract, not a build-group selector.

OpenSBI projects its minimum firmware ISA and firmware/jump/FDT layout.
`opensbi.py manifest` records the relocatable firmware and S-mode validation
payload as one archive. Binding checks that projection, both image hashes and
their relocated RAM ranges, and retains build provenance. Hardware DTB contents
are runtime inputs from BootROM, not software compilation inputs. Image symbol ownership is
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
