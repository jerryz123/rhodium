// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

extern "C" void bank_bind();
extern "C" void bank_sample(unsigned, unsigned, unsigned, unsigned, unsigned,
                            unsigned, unsigned, unsigned, unsigned);
extern "C" void bank_check();
extern "C" void bank_finish();

int main() {
  return run_test([] {
    bank_bind();
    auto sinks = std::array{&sinks_0_out, &sinks_1_out};
    unsigned live = 0;
    std::array<unsigned, 3> payload{};
    for (unsigned step = 0; step < 180; ++step) {
      reset = step == 0 || step == 71;
      allocation = step % 3;
      unsigned release_mask = step % 8;
      for (unsigned i = 0; i < 3; ++i)
        ports::release[i] = (release_mask >> i) & 1;
      source_in.pvalid = step % 7 != 4;
      source_in.pbits = 0x2a;
      selection[0] = (step / 2) % 3;
      selection[1] = (step / 5) % 3;
      for (unsigned i = 0; i < 2; ++i)
        emit[i] = (step >> i) & 1;
      eval();
      if (!reset) {
        CHECK(source_out.pready == (!(live & (1u << allocation)) ||
                                    (release_mask & (1u << allocation))));
        for (unsigned i = 0; i < sinks.size(); ++i) {
          CHECK(sinks[i]->pvalid == (emit[i] && (live & (1u << selection[i]))));
          if (sinks[i]->pvalid)
            CHECK(sinks[i]->pbits == payload[selection[i]]);
        }
      }
      bool capture = source_in.pvalid && source_out.pready;
      bank_sample(reset, capture, allocation, release_mask, source_in.pbits,
                  sinks[0]->pvalid, selection[0], sinks[1]->pvalid,
                  selection[1]);
      if (reset)
        live = 0;
      else {
        live &= ~release_mask;
        if (capture) {
          live |= 1u << allocation;
          payload[allocation] = source_in.pbits;
        }
      }
      tick_model();
      bank_check();
    }
    bank_finish();
  });
}
