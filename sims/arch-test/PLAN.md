<!-- Plans the GEILEN=0 ACT reference mismatch without weakening architectural coverage. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Resolve the GEILEN=0 `mie.SGEIE` mismatch

## Current status

The pinned Sail 0.14.1 source is now a pristine submodule with an ordered patch
that masks `mie.SGEIE` and its `hie.SGEIE` alias when GEILEN is zero. The ACT
setup builds this model by gitlink-and-patch identity; CI builds it once and
supplies the exact artifact to all ELF-generation jobs. A focused CSR probe is
wired to check GEILEN zero and nonzero before ACT generation. The remaining
acceptance gate is a complete patched-model ACT generation and pass across all
RVA23 execution shards; do not treat the source correction alone as that result.

## Evidence and boundary

In [CI run 36347782476](https://github.com/jerryz123/rhodium/actions/runs/36347782476),
`simple-rv5stage-rva23` ACT tests `Sm_mcsr_access-00`, `Sm_mcsr_walk-01`, and
`Sm_shadow-00` expected `mie[12]` to read as one after a write; RV5Stage read
zero. The generated UDB and Sail configurations both set GEILEN to zero.
The same run also exceeded the ACT job's wall-clock budget; sharding addresses
that independent problem, not these deterministic mismatches.

The hypervisor specification aliases `mie.SGEIE` with `hie.SGEIE`, and makes
`hie` bits read-only zero when the corresponding `mideleg` bits are zero.
GEILEN=0 does not force `mideleg.SGEIP` to one. The expected ACT value is
therefore suspect, but the pinned reference model and generated ELF must be
checked directly before changing either RTL or the model.

## Investigation and fix sequence

1. Reproduce the three failures with their exact generated ELFs and pinned
   Sail configuration. In a small standalone CSR probe, write and read
   `mideleg`, `mie`, `hie`, `hgeip`, and `hgeie` with GEILEN=0. Run the same
   probe on RV5Stage, Sail, and the pinned Spike model; record all readbacks.
2. Trace the expected `mie[12]` through Sail's configured CSR semantics and
   ACT's signature generation. Determine whether the discrepancy belongs to
   the Sail model, its configuration projection, the ACT generator, or RV5Stage.
   Check the privileged specification's alias and delegation rules together,
   rather than treating a single CSR readback as the contract.
3. Fix the lowest owning layer that is wrong. If the reference expectation is
   wrong for GEILEN=0, use a version-pinned model or generator correction and
   review Spike's independent `mie.SGEIE` write mask. If RV5Stage is wrong,
   update its interrupt-enable mask and `hie` alias consistently. Do not
   exclude the tests or special-case their expected signatures in the runner.
4. Add focused GEILEN=0 CSR alias/delegation coverage, rerun all three ACT
   ELFs, then require every RVA23 shard to complete and pass. Keep the full
   generated ACT inventory and compare the final shard inventories against it.

Retire this plan after the behavior and validation procedure are recorded in
the owning RISC-V/core and simulator guides.
