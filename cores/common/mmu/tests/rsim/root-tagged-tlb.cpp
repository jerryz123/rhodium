// Checks final physical bounds independently of guest fault provenance on both TLB ports.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    constexpr uint64_t va = 0xffffffc000210000, gpa = 0x123456000, pa = 0xab000;
    constexpr unsigned load = 1, store = 2, guest_fault = 2, access_fault = 3;
    reset = 1; tick_model(); reset = 0;
    lookup.pvirtualized = 1;
    lookup.pcontext.pvs_umode = lookup.pcontext.pg_umode = 1;
    lookup.pcontext.pprivilege = 1;
    lookup.paccess = load;
    lookup.pvirtual_uaddress = va + 0x10;
    probe = lookup;
    probe.pvirtual_uaddress = va + 0xa8;
    auto install = [&](uint64_t address, unsigned size) {
      fill_in.pbits.prequest = lookup;
      auto &r = fill_in.pbits.presult;
      const auto offset = lookup.pvirtual_uaddress & 0xfff;
      r.pphysical_uaddress = address | offset;
      r.pguest_uphysical_uaddress = gpa | offset;
      r.ppage_usize = size;
      auto &vs = r.pvs_uleaf;
      vs.pvalid = vs.pread = vs.pwrite = vs.paccessed = vs.pdirty = 1;
      r.pg_uleaf = vs;
      r.pg_uleaf.puser = 1;
      r.pg_uleaf.pwrite = 0;
      fill_in.pvalid = 1; tick_model(); fill_in.pvalid = 0; eval();
    };
    install(pa, 0);
    CHECK(hit && probe_hit && !translation.pfault && !probe_fault);
    CHECK(translation.pphysical_uaddress == pa + 0x10 && probe_physical_address == pa + 0xa8);
    CHECK(translation.pguest_uphysical_uaddress == gpa + 0x10);
    install((UINT64_C(1) << 55) | pa, 0);
    CHECK(hit && probe_hit && translation.pfault == access_fault && probe_translation.pfault == access_fault);
    CHECK(probe_demand_fault && prefetch_probe.pfault);
    auto provenance = [&](const auto &mapping, uint64_t offset) {
      CHECK(mapping.pvirtual_uaddress == va + offset && mapping.pguest_uphysical_uaddress == gpa + offset);
      CHECK(mapping.pfault_udetails.pguest_uvirtual_uaddress &&
            mapping.pfault_udetails.pguest_uphysical_uaddress == gpa + offset && !mapping.pfault_udetails.paccess);
    };
    provenance(translation, 0x10);
    provenance(probe_translation, 0xa8);
    lookup.paccess = probe.paccess = store;
    eval();
    CHECK(translation.pfault == guest_fault && probe_translation.pfault == guest_fault);
    provenance(translation, 0x10);
    provenance(probe_translation, 0xa8);

    // A host superpage in the same root-tagged bank straddles the 20-bit bound.
    // Filling from its first out-of-range byte must not poison the last valid byte.
    lookup.pvirtualized = lookup.pcontext.pg_umode = 0;
    lookup.paccess = load;
    lookup.pvirtual_uaddress = 0x40100000;
    probe = lookup;
    probe.pvirtual_uaddress--;
    install(0x100000, 3);
    CHECK(hit && translation.pfault == access_fault);
    CHECK(probe_hit && !probe_fault && !probe_translation.pfault && probe_physical_address == 0xfffff);
    std::swap(lookup.pvirtual_uaddress, probe.pvirtual_uaddress);
    eval();
    CHECK(hit && !translation.pfault && translation.pphysical_uaddress == 0xfffff);
    CHECK(probe_hit && probe_demand_fault && probe_fault && probe_translation.pfault == access_fault);
  });
}
