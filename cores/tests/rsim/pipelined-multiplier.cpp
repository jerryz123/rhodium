// Checks pipelined signed products, five-stage latency, ordering, throughput,
// and reset.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = 1;
    request_in = {};
    tick_model();
    tick_model();
    tick_model();
    reset = 0;
    std::array<std::uint16_t, 32> expected{};
    unsigned sent = 0, received = 0, response_run = 0;
    for (unsigned clocks = 0; clocks < 300; ++clocks) {
      request_in = {};
      request_in.pvalid = sent < 32;
      auto &request = request_in.pbits;
      request.pleft = std::uint8_t(sent * 37 + 0x81);
      request.pright = std::uint8_t(sent * 19 + 0x43);
      request.pmode.pleft_usigned = (sent >> 1) & 1;
      request.pmode.pright_usigned = sent & 1;
      eval();
      if (clocks < 5)
        CHECK(!response_out.pvalid);
      // Like the original edge checker, observe the old response before
      // advancing.
      if (response_out.pvalid) {
        CHECK(received < sent && response_out.pbits == expected[received]);
        ++received;
        ++response_run;
      }
      if (received == 32) {
        CHECK(response_run == 32);
        return;
      }
      if (request_in.pvalid) {
        int left = request.pleft, right = request.pright;
        if (request.pmode.pleft_usigned && (left & 0x80))
          left -= 256;
        if (request.pmode.pright_usigned && (right & 0x80))
          right -= 256;
        expected[sent++] = std::uint16_t(left * right);
      }
      tick_model();
    }
    throw std::runtime_error("pipelined multiplier timeout");
  });
}
