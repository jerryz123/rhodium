// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

extern "C" void event_instance_bind();
extern "C" void event_instance_sample(unsigned, unsigned, unsigned, unsigned,
                                      unsigned, unsigned, std::uint64_t);
extern "C" void event_instance_check();
extern "C" void event_instance_finish();

int main() {
  return run_test([] {
    event_instance_bind();
    auto sources = std::array{&sources_0_in, &sources_1_in, &sources_2_in};
    auto sinks = std::array{&sinks_0_out, &sinks_1_out, &sinks_2_out};
    using Forward = std::remove_reference_t<decltype(sources_0_in)>;
    std::array<Forward, 3> previous{};
    auto drive = [&](int step, bool bad) {
      reset = step < 2 || (step >= 13 && step < 15);
      int cycle = step < 13 ? step - 2 : step - 15;
      // IDs may change before first use; siblings initially share an ID.
      chip_id = std::uint8_t(cycle < 2 ? step : step < 13 ? 3 : 4);
      hart0 = std::uint8_t(cycle < 2 ? step : step < 13 ? 7 : 9);
      hart1 = step < 13 || cycle < 2 ? hart0 : 10;
      bank_id = cycle < 5   ? std::uint64_t(step)
                : step < 13 ? UINT64_MAX
                            : UINT64_C(0x8000000000000000);
      if (bad && step == 8)
        hart0 = 8;
      for (unsigned i = 0; i < sources.size(); ++i) {
        sources[i]->pvalid =
            !reset && (i == 2 ? cycle == 5 : cycle == 2 || cycle == 3);
        sources[i]->pbits = std::uint8_t(42 + cycle);
      }
    };
    for (int step = 0; step < 26; ++step) {
      drive(step, false);
      eval();
      unsigned inputs = 0, outputs = 0;
      for (unsigned i = 0; i < sources.size(); ++i) {
        if (!reset) {
          CHECK(sinks[i]->pvalid == previous[i].pvalid);
          if (sinks[i]->pvalid)
            CHECK(sinks[i]->pbits == previous[i].pbits);
        }
        inputs |= unsigned(sources[i]->pvalid) << i;
        outputs |= unsigned(sinks[i]->pvalid) << i;
        previous[i] = reset ? Forward{} : *sources[i];
      }
      event_instance_sample(reset, inputs, outputs, chip_id, hart0, hart1,
                            bank_id);
      tick_model();
      event_instance_check();
    }
    event_instance_finish();

    // Replay the first epoch, changing an identity after it has been bound.
    dut = Model{};
    expect_failure("__event_instance_1_stable", [&] {
      for (int step = 0; step <= 8; ++step) {
        drive(step, true);
        tick_model();
      }
    });
  });
}
