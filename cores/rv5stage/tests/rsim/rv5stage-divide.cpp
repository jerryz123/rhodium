// Runs the independent division program with a clocked instruction responder and store oracle.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
constexpr std::array<std::uint32_t, 23> program{
    0x06400293, 0x00700313, 0x0262c3b3, 0x00900913, 0x05203023, 0x0262e433,
    0xf9c00493, 0x0264c533, 0x0264e5b3, 0xfff00613, 0x00200693, 0x02d6573b,
    0x02d677bb, 0x0264c83b, 0x0264e8bb, 0x00703023, 0x00803423, 0x00a03823,
    0x00b03c23, 0x02e03023, 0x02f03423, 0x03003823, 0x03103c23};
std::uint32_t instruction_at(std::uint64_t address) {
  const auto offset = address - UINT64_C(0x100000000);
  return offset % 4 == 0 && offset / 4 < program.size() ? program[offset / 4]
                                                        : 0x13;
}
int main() {
  return run_test([] {
    reset = 1;
    time_counter = 0;
    hart_id = 0;
    interrupts = {};
    pipeline_access_in = {};
    instruction_access_in = {};
    data_access_in = {};
    bool response_valid = false;
    std::uint32_t response_word = 0;
    unsigned cycles = 0, stores = 0;
    const std::array<std::uint64_t, 9> addresses{64, 0,  8,  16, 24,
                                                 32, 40, 48, 56};
    const std::array<std::uint64_t, 9> values{9,
                                              14,
                                              2,
                                              std::uint64_t(-14),
                                              std::uint64_t(-2),
                                              0x7fffffff,
                                              1,
                                              std::uint64_t(-14),
                                              std::uint64_t(-2)};
    for (unsigned step = 0; step < 1602; ++step) {
      reset = step < 2;
      instruction_access_in.prequest.pready = !response_valid;
      instruction_access_in.presponse = {std::uint8_t(response_valid),
                                         {response_word, 0, 0}};
      data_access_in.prequest.pready = 1;
      data_access_in.pdrained = 1;
      eval();
      bool next_valid = response_valid;
      auto next_word = response_word;
      if (reset) {
        next_valid = false;
        next_word = 0;
        cycles = stores = 0;
      } else {
        if (response_valid && instruction_access_out.presponse.pready)
          next_valid = false;
        if (instruction_access_out.prequest.pvalid &&
            instruction_access_in.prequest.pready) {
          next_valid = true;
          next_word =
              instruction_at(instruction_access_out.prequest.pbits.paddress);
        }
        if (data_access_out.prequest.pvalid && data_access_in.prequest.pready) {
          const auto &request = data_access_out.prequest.pbits;
          CHECK(stores < values.size() && request.paccess == 2);
          CHECK(request.paddress == addresses[stores] &&
                request.pdata == values[stores]);
          if (stores == 0)
            CHECK(cycles < 50);
          if (++stores == values.size()) {
            CHECK(cycles < 400);
            return;
          }
        }
        ++cycles;
      }
      tick_model();
      response_valid = next_valid;
      response_word = next_word;
    }
    throw std::runtime_error("division program did not complete");
  });
}
