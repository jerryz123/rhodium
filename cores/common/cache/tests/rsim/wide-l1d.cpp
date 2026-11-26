// SPDX-License-Identifier: Apache-2.0
#include "wide-l1d-contract.hpp"

static void check_serial_misses(const Line& a, const Line& b) {
  reset = 1; tick_model(); reset = 0;
  request(base, load, {}, 0xffff, true, 1);
  const auto first = accept_request(base, 2);
  chi_in.prequester_uresponses.pready = 0;
  request(base+64, store, b[0], 0xffff, true, 2);
  // B is accepted by the service FIFO, but cannot acquire its line while
  // A is missing a packet. A's response can remain backpressured afterward.
  for (unsigned row : {3u, 1u, 0u}) {
    refill_packet(first, row, 1, a[row]);
    for (unsigned i = 0; i < 4; ++i) {
      eval(); CHECK(!chi_out.prequests.pvalid && !core_out.presponse.pvalid);
      tick_model();
    }
  }
  refill_packet(first, 2, 1, a[2]);
  until([] { return chi_out.prequester_uresponses.pvalid; });
  for (unsigned i = 0; i < 4; ++i) {
    eval();
    CHECK(chi_out.prequester_uresponses.pvalid && !chi_out.prequests.pvalid);
    tick_model();
  }
  chi_in.prequester_uresponses.pready = 1;
  refill(base+64, 7, b);
  response(a[0], 1); response({}, 2);
  hit(base+64, load, b[0]);
}

int main() {
  return run_wide_l1d(check_serial_misses);
}
