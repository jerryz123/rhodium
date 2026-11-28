// Checks root-tagged address matching, final physical bounds, and guest fault provenance on both ports.
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
      r.pguest_uphysical_uaddress = lookup.pcontext.pvs_umode ? (gpa | offset) : lookup.pvirtual_uaddress;
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

    auto check_ports = [&](uint64_t demand, uint64_t hint, bool demand_hit, bool hint_hit) {
      lookup.pvirtual_uaddress = demand;
      probe.pvirtual_uaddress = hint;
      eval();
      CHECK(hit == demand_hit && probe_raw_hit == hint_hit && probe_hit == hint_hit);
      if (demand_hit) CHECK(!translation.pfault && translation.pvirtual_uaddress == demand);
      if (hint_hit) CHECK(!probe_fault && !probe_translation.pfault && probe_translation.pvirtual_uaddress == hint);
    };
    // Keep each port independent: a noncanonical alias misses even when its
    // low 41 bits match an upper-half Sv39 page hit on the other port.
    constexpr auto invalid_va = va ^ (UINT64_C(1) << 48);
    for (bool guest : {false, true}) {
      invalidate_all = 1; tick_model(); invalidate_all = 0;
      lookup.pvirtualized = guest;
      lookup.pcontext.pvs_umode = 1;
      lookup.pcontext.pg_umode = guest;
      lookup.pvirtual_uaddress = va;
      probe = lookup;
      install(pa, 0);
      check_ports(va + 0x10, invalid_va + 0xa8, true, false);
      CHECK(translation.pphysical_uaddress == pa + 0x10);
      check_ports(invalid_va + 0x10, va + 0xa8, false, true);
      CHECK(probe_physical_address == pa + 0xa8);
    }

    // VS Bare + Sv39x4: bit 40 distinguishes two resident pages; bit 41
    // makes the request invalid, not an alias for either one.
    invalidate_all = 1; tick_model(); invalidate_all = 0;
    constexpr uint64_t low = 0x3456000, high = low | (UINT64_C(1) << 40);
    constexpr uint64_t outside = UINT64_C(1) << 41, high_pa = 0xcd000;
    lookup.pcontext.pvs_umode = 0;
    lookup.pvirtual_uaddress = low;
    probe = lookup;
    install(pa, 0);
    lookup.pvirtual_uaddress = high;
    install(high_pa, 0);
    check_ports(low + 0x10, high + 0xa8, true, true);
    CHECK(translation.pphysical_uaddress == pa + 0x10 && probe_physical_address == high_pa + 0xa8);
    check_ports(high + 0x10, low + 0xa8, true, true);
    CHECK(translation.pphysical_uaddress == high_pa + 0x10 && probe_physical_address == pa + 0xa8);
    check_ports(low | outside, high, false, true);
    // Even a synthetic successful fill for the invalid alias cannot overwrite low.
    install(high_pa, 0);
    check_ports(low, high | outside, true, false);
    CHECK(translation.pphysical_uaddress == pa);
  });
}
