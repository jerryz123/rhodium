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
checkouts pristine. Apply the ordered architectural-test and Bringup-Bench patch
series only to build-local copies through
[`riscv/patched_submodule.py`](../riscv/patched_submodule.py). Put Rhodium-owned
bare-metal startup, linkers, output checks, and ports beside their corresponding
upstream source. Preserve upstream licensing and the repository's
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

[`build/`](build/) owns ELF construction, target validation, compiler and ISA
checks, content-addressed build reuse, and manifests. `build.py` and `isa.mk`
compile the ISA tests and upstream benchmarks; the named benchmark builders
compile CoreMark, Embench-IoT, and Bringup-Bench. ISA selection derives upstream
groups, native-width smoke operations, and Make XLEN from the target descriptor.
Validate ELF32 and ELF64 load segments including BSS and executable entry on
both fresh builds and cache reuse. Record upstream inventory gaps (currently
RV32 CBO-zero) separately from target capability exclusions; never select by
observed pass status. `build-litmus.py` discovers
the branch-free RV64I cases in upstream's model-run `@all` inventory, combines
their exact instruction rows with the pinned Herd state log, and links a
Rhodium-owned multihart runtime from `litmus-riscv-baremetal/` against the
existing RISC-V benchmark startup/HTIF support. Model-log names are not
source-path unique, so the builder
explicitly chooses only the qualified MP, SB, and LB variants and skips other
ambiguous names. It rejects syntax outside its explicit subset instead of
silently dropping instructions or inventing allowed outcomes. `opensbi.py`
derives firmware layout from the selected target descriptor and builds OpenSBI `FW_JUMP`.
With an explicit `litmus7` executable, the builder instead selects cases by
source thread table and pinned model-state inventory, asks litmus7 to emit
static pre-silicon RISC-V C, and links its test body and generated IO/random
helpers with `litmus7-port.c` and `litmus7-main.c`. The port implements only the
fixed-hart worker and option interfaces exercised by that build mode; generated
files stay in the content-addressed build directory. The simulator result
runner checks the complete histogram against the pinned Herd states and requires
at least the requested number of samples. Source-identical duplicate names
may share one case; distinct ambiguous sources remain excluded. Keep the tool
binary, support directory, target, and port in the cache identity.
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
For a bulk qualification, `build-litmus.py --keep-going` records per-case
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

## Validation

Run the focused builder tests and simulator adapters first:

```sh
python3 -m unittest discover -s sw/tests
make -C sims program-test-adapter-test arch-test-adapter-test opensbi-adapter-test
make ci-plan-test
make check-license-headers check-boundaries
```

For moved upstreams or ports, also initialize their new submodule paths and
build representative ELFs with the existing `make -C sims ...-elfs` or
`...-firmware` targets. Execute at least one workload and OpenSBI qualification
on each affected SoC to validate the load and completion boundary. The
[simulator guide](../sims/README.md) owns those commands and generated-artifact
locations. Keep source, patch, port, compiler, and target identity in each
builder's cache key; a path migration may cause a one-time cache miss.
