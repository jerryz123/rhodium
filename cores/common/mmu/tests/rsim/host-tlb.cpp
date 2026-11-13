// Checks map-bounded host TLB fills, page offsets, and fault priority on independent ports.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

constexpr uint64_t va = 0x40000000;
constexpr unsigned page_fault = 1, access_fault = 3;

void install(uint64_t physical, unsigned size) {
  fill_in.pbits.prequest = lookup;
  auto &r = fill_in.pbits.presult;
  r.pphysical_uaddress = physical;
  r.ppage_usize = size;
  // Readable but not writable; probes ignore A/D, not privilege or map bounds.
  r.pvs_uleaf.pread = r.pvs_uleaf.paccessed = r.pvs_uleaf.pvalid = 1;
  fill_in.pvalid = 1;
  tick_model();
  fill_in.pvalid = 0;
}

int main() {
  return run_test([] {
    reset = 1; tick_model(); reset = 0;
    lookup.pvirtual_uaddress = va;
    lookup.paccess = 1; // Load
    lookup.pcontext.pvs_umode = 1; // Sv39
    lookup.pcontext.pprivilege = 1; // Supervisor
    probe = lookup;
    install(0xab000, 0);
    lookup.pvirtual_uaddress = probe.pvirtual_uaddress = va + 0x48;
    eval();
    CHECK(hit && probe_hit && !translation.pfault && !probe_fault);
    CHECK(translation.pphysical_uaddress == 0xab048 && probe_physical_address == 0xab048);

    // A 4 KiB page with identical low PPN must hit and fault, not replay or authorize the alias.
    install((UINT64_C(1) << 55) | 0xab000, 0);
    eval();
    CHECK(hit && probe_hit && translation.pfault == access_fault &&
          probe_translation.pfault == access_fault && probe_demand_fault && prefetch_probe.pfault);
    lookup.paccess = probe.paccess = 2; // Store
    eval();
    CHECK(hit && translation.pfault == page_fault && probe_translation.pfault == page_fault);

    // Bare ignores resident fault metadata even when the selected slot was poisoned.
    lookup.pcontext.pvs_umode = probe.pcontext.pvs_umode = 0;
    eval();
    CHECK(hit && !translation.pfault && translation.pphysical_uaddress == va + 0x48 && !probe_fault);
    invalidate_all = 1; tick_model(); invalidate_all = 0;
    lookup.pcontext.pvs_umode = probe.pcontext.pvs_umode = 1;
    lookup.paccess = probe.paccess = 1;
    // The initiating offset is outside the map. It is not part of the normalized
    // 1 GiB base: another offset in this mapping must subsequently succeed.
    lookup.pvirtual_uaddress = va + 0x180000;
    install(0x180000, 3);
    probe.pvirtual_uaddress = va + 0x80000;
    eval();
    CHECK(hit && translation.pfault == access_fault);
    CHECK(probe_hit && !probe_fault && !probe_translation.pfault && probe_physical_address == 0x80000);
    std::swap(lookup.pvirtual_uaddress, probe.pvirtual_uaddress);
    eval();
    CHECK(hit && !translation.pfault && translation.pphysical_uaddress == 0x80000);
    CHECK(probe_hit && prefetch_probe.pfault && probe_translation.pfault == access_fault);
  });
}
