// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
extern "C" void copyback_bind();
extern "C" void copyback_sample(unsigned, unsigned, unsigned, unsigned,
                                unsigned);
extern "C" void copyback_check();
extern "C" void copyback_finish();
void tick() {
  eval();
  unsigned starts = 0, finishes = 0, requests = 0, packets = 0;
  for (unsigned i = 0; i < 3; ++i) {
    starts |=
        unsigned(enabled[i] && command_valid && observed[i].pcommand_uready)
        << i;
    finishes |= unsigned(completion_ready && observed[i].pdone) << i;
    requests |= unsigned(request_ready && observed[i].prequest_uvalid) << i;
    packets |= unsigned(data_ready && observed[i].pdata_uvalid) << i;
  }
  copyback_sample(reset, starts, finishes, requests, packets);
  tick_model();
  copyback_check();
}
void response(unsigned lane, unsigned opcode, unsigned credit = 0) {
  response_opcode = opcode;
  response_pcrd = credit;
  response_valid = 1;
  eval();
  CHECK(observed[lane].presponse_uready);
  tick();
  response_valid = 0;
}
int main() {
  return run_test([] {
    copyback_bind();
    reset = 1;
    state = 4;
    for (unsigned b = 0; b < 64; ++b)
      line.words[b / 4] |= std::uint32_t(b + 1) << ((b % 4) * 8);
    tick();
    reset = 0;
    for (unsigned lane = 0; lane < 3; ++lane)
      for (unsigned state_case = 0; state_case < 5; ++state_case) {
        unsigned width_bits = 128 << lane, packets = 512 / width_bits;
        state = 4 - state_case;
        unsigned expected_resp = state_case == 0   ? 6
                                 : state_case == 1 ? 7
                                                   : state;
        for (unsigned i = 0; i < 3; ++i)
          enabled[i] = i == lane;
        command_valid = 1;
        tick();
        command_valid = 0;
        const auto &o = observed[lane];
        for (unsigned i = 0; i < 3; ++i) {
          CHECK(o.prequest_uvalid && o.popcode == 0x1b && o.psize == 6 &&
                o.paddress == 0x80001240 && o.ptxn == 1 && o.pallow_uretry &&
                !o.pdata_uvalid);
          tick();
        }
        request_ready = 1;
        tick();
        request_ready = 0;
        if (state_case & 1) {
          response(lane, 7, 0xb);
          response(lane, 3, 0xb);
        } else {
          response(lane, 3, 0xb);
          response(lane, 7, 0xb);
        }
        eval();
        CHECK(o.prequest_uvalid && !o.pallow_uretry && o.ppcrd == 0xb);
        request_ready = 1;
        tick();
        request_ready = 0;
        snoop_pending = 1;
        response_opcode = 5;
        response_valid = 1;
        for (unsigned i = 0; i < 3; ++i) {
          eval();
          CHECK(!o.presponse_uready && !o.pdata_uvalid);
          tick();
        }
        snoop_pending = 0;
        tick();
        response_valid = 0;
        state = (~state) & 7;
        for (unsigned packet = 0; packet < packets; ++packet) {
          auto expected_data = line;
          expected_data.words.fill(0);
          if (expected_resp)
            for (unsigned w = 0; w < width_bits / 32; ++w)
              expected_data.words[w] =
                  line.words[packet * (width_bits / 32) + w];
          std::uint64_t expected_mask =
              expected_resp ? low_mask(width_bits / 8) : 0;
          for (unsigned i = 0; i < 3; ++i) {
            eval();
            CHECK(o.pdata_uvalid && o.pdata_uopcode == 2 &&
                  o.pdata_uid == packet * (width_bits / 128) &&
                  o.pdata_utxn == 0x55 && o.pdata_uresp == expected_resp &&
                  o.pdata.words == expected_data.words &&
                  o.pmask == expected_mask && !o.pdone);
            tick();
          }
          data_ready = 1;
          tick();
          data_ready = 0;
        }
        for (unsigned i = 0; i < 3; ++i) {
          CHECK(o.pdone && o.pcookie == 0xa5 && !o.pcommand_uready &&
                !o.pdata_uvalid);
          tick();
        }
        completion_ready = 1;
        tick();
        completion_ready = 0;
        CHECK(o.pcommand_uready && !o.pdone);
      }
    copyback_finish();
  });
}
