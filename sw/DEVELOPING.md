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
compile CoreMark, Embench-IoT, and Bringup-Bench. `opensbi.py` derives firmware
layout from the selected target descriptor and builds OpenSBI `FW_JUMP`.
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
bash tools/check-ci-changes.sh
make check-license-headers check-boundaries
```

For moved upstreams or ports, also initialize their new submodule paths and
build representative ELFs with the existing `make -C sims ...-elfs` or
`...-firmware` targets. Execute at least one workload and OpenSBI qualification
on each affected SoC to validate the load and completion boundary. The
[simulator guide](../sims/README.md) owns those commands and generated-artifact
locations. Keep source, patch, port, compiler, and target identity in each
builder's cache key; a path migration may cause a one-time cache miss.
