<!-- Documents the Rhodium-owned patch series layered over the pinned ACT submodule. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RISC-V architectural-test patches

The adjacent [`riscv-arch-test`](../riscv-arch-test/) submodule remains an
unmodified upstream checkout.
The ordered [`series`](series) file lists the Rhodium-owned changes needed to
generate the complete vector inventory through ACT's canonical `testgen`
command. The simulator flow uses the shared
[`patched_submodule.py`](../../riscv/patched_submodule.py) tool to copy the pinned
checkout into its build root and apply this series there; generated and patched
trees are never committed.

Each patch must apply cleanly to the pinned submodule revision. When advancing
the submodule, remove changes that have landed upstream, rebase the remaining
patches, and run `make -C sims arch-test-tests` before updating this document.
The queue is deliberately ordered so each prefix can be proposed and reviewed
upstream independently:

1. `0001-generalize-canonical-vector-test-generation.patch` generalizes shared
   vector generation, data, formatting, and test-plan infrastructure without
   enabling additional suites.
2. `0002-add-canonical-vector-floating-point-generation.patch` adds vector FP
   formatters, helpers, and coverpoints on top of the generalized machinery.
3. `0003-add-canonical-vector-crypto-generation.patch` adds vector crypto
   formatting and canonical GCM test data.
4. `0004-enable-canonical-vector-suite-expansion.patch` removes the legacy suite
   restriction only after all required generators exist.
5. `0005-avoid-reserved-vsetvl-when-forcing-vill.patch` makes the
   whole-register `vill` coverpoint use an ordinary AVL form instead of the
   reserved keep-`vl` form.
6. `0006-align-ordered-index-overlap-data.patch` aligns the ordered indexed
   segment overlap fixture to the encoded index EEW used by its setup load.
7. `0007-align-vector-fp-wide-source-data.patch` records custom vector-FP
   source fixtures at the effective source EEW used by their setup loads.
8. `0008-reserve-vfmv-broadcast-destination-group.patch` reserves the complete
   LMUL-sized destination of `vfmv.v.f` so its checker cannot overwrite it.
9. `0009-gate-fault-first-access-fault-generation.patch` gates the generated
   fault-dependent vector-load chunk with the same optional DUT macro as its
   coverage model.
10. `0010-size-exceptions-sm-trap-signatures.patch` sizes each delegation-walk
    trap region for the complete expanded exception sequence.
11. `0011-guard-mcountinhibit-initialization.patch` skips common setup and
    generated CSR coverpoints when UDB reports that the optional CSR is absent.
12. `0012-ignore-unconstrained-sc-results.patch` keeps the virtual-memory LR/SC
    exception tests focused on translation and trap behavior instead of requiring
    unconstrained SC operations to succeed.
13. `0013-limit-medeleg-walk-to-base-causes.patch` excludes the optional CFI
    software-check cause from the generic Sm delegation walk.

The first three patches expand capability while retaining ACT's existing active
suite inventory, the fourth makes the complete vector inventory visible to
canonical `testgen` invocations, and the remaining patches are independent
semantic fixes for that generated inventory.

To inspect the patched source without generating tests, run:

```sh
make -C sims arch-test-source
```

The materialized source defaults to
`/tmp/rhodium-arch-test/source/riscv-arch-test`. A patch can also be inspected
directly against the clean submodule with `git apply --check`; the build never
applies it to the submodule working tree itself.
