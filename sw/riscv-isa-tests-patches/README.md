<!-- Explains the ordered multihart runtime adaptation for pinned riscv-tests benchmarks. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RISC-V benchmark runtime patches

The ordered [series](series) adapts only multihart benchmark builds from
[`riscv-isa-tests/`](../riscv-isa-tests/). The shared
[`materializer`](../../riscv/patched_submodule.py) produces a build-local tree;
the upstream checkout and its BSD-3-Clause notices remain unchanged.

The runtime boots `RHODIUM_BENCHMARK_HARTS` workers and waits for every worker
to finish before signaling HTIF exit, preserving any worker's failure code.
The builder passes the hart count with `-D`, including for assembly startup.
Single-hart benchmarks and ISA tests do not apply this series.

Use the normal `SOC=tiled8core CORE=rv2wide ISA=rva23 moon run rhodium:tiled-mt-benchmark-elfs`
workflow. The [software contributor guide](../DEVELOPING.md) owns validation
and cache-input requirements. Keep runtime adaptations in patches, not source
rewriting code in the builder.
