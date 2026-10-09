// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

extern "C" void retained_bind();
extern "C" void retained_sample(unsigned, unsigned, unsigned, unsigned,
                                unsigned, unsigned, unsigned);
extern "C" void retained_check();
extern "C" void retained_finish();

int main() {
  return run_test([] {
    retained_bind();
    bool model_active = false;
    unsigned model_bits = 0;
    for (unsigned step = 0; step < 120; ++step) {
      reset = step == 0 || step == 29;
      source_in.pvalid = step % 11 != 7;
      source_in.pbits = 0x2a;
      unrelated_in.pvalid = step % 3 != 0;
      unrelated_in.pbits = 0x2a;
      emit = step % 5 != 2;
      finish_owner = step % 9 == 7;
      sink_in.pready = step % 4 != 1;
      eval();
      if (!reset) {
        CHECK(source_out.pready == (!model_active || finish_owner));
        CHECK(sink_out.pvalid ==
              ((model_active && emit) || unrelated_in.pvalid));
        if (sink_out.pvalid)
          CHECK(sink_out.pbits == ((model_active && emit)
                                       ? ((model_bits + 1) & 255u)
                                       : unrelated_in.pbits));
        if (unrelated_in.pvalid)
          CHECK(unrelated_out.pready ==
                (sink_in.pready && !(model_active && emit)));
      }
      retained_sample(
          reset, source_in.pvalid && source_out.pready, finish_owner,
          source_in.pbits, sink_out.pvalid && sink_in.pready,
          unrelated_in.pvalid && unrelated_out.pready, sink_out.pbits);
      if (reset)
        model_active = false;
      else if (source_in.pvalid && source_out.pready) {
        model_active = true;
        model_bits = source_in.pbits;
      } else if (finish_owner)
        model_active = false;
      tick_model();
      retained_check();
    }
    retained_finish();
  });
}
