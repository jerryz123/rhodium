// Validates actual RHDL-to-SV-to-DPI hook payloads with the production collector.
// SPDX-License-Identifier: Apache-2.0
#include "../../../events/dpi.h"
#include <cstdlib>
#include <cstdio>
#include <memory>
using namespace rhodium::cosim::observation;
static Collector collector;
static std::unique_ptr<DpiBinding> binding;
static void require(bool ok) { if (!ok) std::abort(); }
extern "C" void cosim_test_begin(std::int64_t sample) {
  if (!binding) {
    collector.reset(0, 0, {0, 0x80000000, {3, false}, 64, 256});
    binding = std::make_unique<DpiBinding>(collector);
  }
  collector.begin_sample(sample);
}
extern "C" void cosim_test_end(std::int64_t sample) {
  try {
    binding->check();
    const auto records = collector.end_sample();
    require(records.size() == (sample < 2 ? 0 : sample == 2 ? 2 : 1));
    if (sample == 2) {
      const auto& r = records[0];
      require(r.id.order == 0 && records[1].id.order == 1 && r.effects.size() == 3);
      const auto& w = std::get<RegisterWrite>(r.effects.at({0,0}));
      require(w.bank == Bank::Vector && w.index == 7 && w.bit_offset == 128 && w.mask == ~Word{0} && w.value == 0x8000000000000042ULL);
      const auto& c = std::get<CsrUpdate>(r.effects.at({0,1}));
      require(c.address == 1 && c.operation == CsrOperation::SetBits && c.mask == 31 && c.value == 16);
      const auto& m = std::get<MemoryEffect>(r.effects.at({0,2}));
      require(m.access_id == 7 && m.fragment_offset == 3 && m.kind == AccessKind::Load && m.virtual_address == 0x2000);
      require(m.physical_valid && m.physical_address == 0x80001000 && m.byte_mask == 255 && m.read_valid && !m.write_valid);
      require(m.read_data == w.value && m.result == AccessResult::Success);
    }
    if (sample >= 3) {
      const auto& t = std::get<Trap>(records[0].outcome);
      require(t.tval == 0x2000 && t.htval == 0x800 && t.htinst == 0x3000 && t.guest_valid && t.privilege.virtualized);
      require(std::holds_alternative<Interrupt>(records[0].event) == (sample == 4));
    }
    if (sample == 4) collector.finish();
  } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); std::abort(); }
}
