<!-- Documents the Rhodium-owned patch series layered over the pinned ACT submodule. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RISC-V architectural-test patches

The adjacent [`riscv-arch-test`](../riscv-arch-test/) submodule remains an
unmodified upstream checkout.
The ordered [`series`](series) file lists the remaining generator and test
corrections layered over the pinned ACT revision. The simulator flow uses the shared
[`patched_submodule.py`](../../riscv/patched_submodule.py) tool to copy the pinned
checkout into its build root and apply this series there; generated and patched
trees are never committed.

Each patch must apply cleanly to the pinned submodule revision. When advancing
the submodule, remove changes that have landed upstream, rebase the remaining
patches, and run `make -C sims arch-test-tests` before updating this document.
The current queue contains these independently reviewable corrections:

- `0005-avoid-reserved-vsetvl-when-forcing-vill.patch` makes the
   whole-register `vill` coverpoint use an ordinary AVL form instead of the
   reserved keep-`vl` form.
- `0013-limit-medeleg-walk-to-base-causes.patch` excludes the optional CFI
  software-check cause from the generic Sm delegation walk.
- `0014-program-su-wrs-timer-directly.patch` lets platforms without lower-mode
  timer MMIO protection arm the WRS wakeup timer without consuming its delay
  inside multiple T-SBI memory calls.
- `0015-inhibit-split-counter-preload.patch` inhibits retirement counting while
  preloading split RV32 counters, so intervening instructions cannot advance
  the low half before the high-half write.
- `0016-normalize-wfi-handler-retirement.patch` measures the standard M-mode
  handler body only in the M/U counter suites. Their interrupt-taking WFI
  tests subtract this completed interval and full outer-loop trips before
  comparing exact retirement deltas. Timer-clear polling may take any number
  of instructions; WFI and fixed entry/exit retirement remain checked. The
  accounting uses XLEN-modular subtraction and does not require optional
  `mcountinhibit` support or alter the counter itself. The standard handler
  does not support nested M-mode traps.

To inspect the patched source without generating tests, run:

```sh
make -C sims arch-test-source
```

The materialized source defaults to
`/tmp/rhodium-arch-test/source/riscv-arch-test`. A patch can also be inspected
directly against the clean submodule with `git apply --check`; the build never
applies it to the submodule working tree itself.
