<!-- Documents the Rhodium-owned patch series layered over the pinned ACT submodule. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RISC-V architectural-test patches

The adjacent [`riscv-arch-test`](../riscv-arch-test/) submodule remains an
unmodified upstream checkout.
The ordered [`series`](series) file lists the remaining generator and platform
corrections layered over ACT's canonical `testgen` command. The simulator flow
uses the shared
[`patched_submodule.py`](../../riscv/patched_submodule.py) tool to copy the pinned
checkout into its build root and apply this series there; generated and patched
trees are never committed.

Each patch must apply cleanly to the pinned submodule revision. When advancing
the submodule, remove changes that have landed upstream, rebase the remaining
patches, and run `moon run rhodium:act-generate` before updating this document.
The current queue contains independent fixes:

1. `0005-avoid-reserved-vsetvl-when-forcing-vill.patch` makes the
   whole-register `vill` coverpoint use an ordinary AVL form instead of the
   reserved keep-`vl` form.
2. `0013-limit-medeleg-walk-to-base-causes.patch` excludes the optional CFI
   software-check cause from the generic Sm delegation walk.
3. `0014-program-su-wrs-timer-directly.patch` lets platforms without lower-mode
   timer MMIO protection arm the WRS wakeup timer without consuming its delay
   inside multiple T-SBI memory calls.
4. `0017-configurable-compiler-flags.patch` lets the target supply its native
   compiler flags without changing the canonical suite inventory.

Retired patch numbers are not reused. Canonical vector suite expansion and
counter-inhibit handling are now supplied upstream. At pin
`2a1ab1f8947b6aa68419d48de3787f9e1be3e9da`, native counter tests avoid measuring
WFI with a taken interrupt: retirement at that boundary and handler instruction
counts are implementation-dependent. Patch 0016 and its handler instrumentation
are retired. Native pending/wakeup WFI cases still exercise counter behavior;
regenerate both counter suites and their reference signatures for both XLENs
instead of preserving the old taken-interrupt inventory.

To inspect the patched source without generating tests, run:

```sh
moon run rhodium:act-source
```

The materialized source defaults to
`.rhodium-cache/moon/act/source`. A patch can also be inspected
directly against the clean submodule with `git apply --check`; the build never
applies it to the submodule working tree itself.
