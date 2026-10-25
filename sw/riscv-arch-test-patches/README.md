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
patches, and run `make -C sims arch-test-tests` before updating this document.
The current queue contains independent fixes:

1. `0005-avoid-reserved-vsetvl-when-forcing-vill.patch` makes the
   whole-register `vill` coverpoint use an ordinary AVL form instead of the
   reserved keep-`vl` form.
2. `0013-limit-medeleg-walk-to-base-causes.patch` excludes the optional CFI
   software-check cause from the generic Sm delegation walk.
3. `0014-program-su-wrs-timer-directly.patch` lets platforms without lower-mode
   timer MMIO protection arm the WRS wakeup timer without consuming its delay
   inside multiple T-SBI memory calls.
4. `0016-normalize-wfi-interrupt-retirement.patch` measures and removes the
   timing-dependent machine-timer handler body from the Sm/U Zicntr WFI delta,
   alongside the existing wait-loop tally. Only these counter suites opt into
   handler instrumentation. Fixed handler entry/exit work and WFI retirement
   remain checked, as do the ordinary interrupt signatures. MTIE stays disabled
   until the counter baseline is captured, including for U-mode. Accounting is
   XLEN-modular and needs no optional `mcountinhibit` CSR; the standard M-mode
   handler does not support nested traps.

Retired patch numbers are not reused. Canonical vector suite expansion and
counter-inhibit handling are now supplied upstream.
For changes to the WFI accounting patch, rebuild both counter suites with their
own reference signatures and run all Simple core/ISA configurations. Include
Sail cosim on both RTL cores and XLENs, and a DUT-only extra-instruction negative
control to verify that normalization still catches incorrect retirement outside
the measured handler body. Keep platform interrupt latency and profiles intact.

To inspect the patched source without generating tests, run:

```sh
make -C sims arch-test-source
```

The materialized source defaults to
`/tmp/rhodium-arch-test/source/riscv-arch-test`. A patch can also be inspected
directly against the clean submodule with `git apply --check`; the build never
applies it to the submodule working tree itself.
