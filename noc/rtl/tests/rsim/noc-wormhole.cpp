// Checks two concurrent packet producers against ordered, noninterleaved
// ejection.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <random>
int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    tick_model();
    reset = 0;
    std::mt19937 random(0x519bc);
    int sent[2]{}, received = 0, active = -1, index = 0, cycles = 0;
    unsigned seen = 0;
    while (received < 5 && cycles < 400) {
      // Each producer retains its current beat until the pre-edge ready/valid
      // transfer.
      injection_0_in = {
          std::uint8_t(sent[0] < 3),
          {std::uint8_t(sent[0] == 0),
           std::uint8_t(sent[0] == 2),
           {std::uint8_t(sent[0] != 0), std::uint8_t(0xa0 + sent[0])}}};
      injection_1_in = {
          std::uint8_t(sent[1] < 2),
          {std::uint8_t(sent[1] == 0),
           std::uint8_t(sent[1] == 1),
           {std::uint8_t(sent[1] == 0), std::uint8_t(0xb0 + sent[1])}}};
      ejection_in.pready = random() % 3 != 0;
      eval();
      if (ejection_out.pvalid && ejection_in.pready) {
        const auto &beat = ejection_out.pbits;
        unsigned payload = beat.ppayload.ppayload;
        CHECK((payload >= 0xa0 && payload <= 0xa2) ||
              (payload >= 0xb0 && payload <= 0xb1));
        if (beat.phead) {
          CHECK(active == -1);
          active = (payload >> 4) & 1;
          index = 0;
        } else
          CHECK(active != -1);
        CHECK(payload == unsigned((active ? 0xb0 : 0xa0) + index));
        unsigned bit = (active ? 3 : 0) + index;
        CHECK(!(seen & (1u << bit)));
        seen |= 1u << bit;
        ++index;
        if (beat.ptail) {
          CHECK(index == (active ? 2 : 3));
          active = -1;
        }
        ++received;
      }
      sent[0] += injection_0_in.pvalid && injection_0_out.pready;
      sent[1] += injection_1_in.pvalid && injection_1_out.pready;
      tick_model();
      ++cycles;
    }
    CHECK(received == 5 && seen == 31 && active == -1 && cycles < 400);
    CHECK(sent[0] == 3 && sent[1] == 2);
  });
}
