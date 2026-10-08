// Checks ordered payload delivery and queued backpressure across
// family-remapped CHI attachments.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
using CHIReqFlit = std::remove_cvref_t<decltype(request_in_in.pbits)>;

CHIReqFlit request_payload(int index) {
  CHIReqFlit return_value{};

  CHIReqFlit result;
  fill_request(result, [] { return UINT64_MAX; });
  result.ptgt_uid = UINT64_C(5);
  result.psrc_uid = UINT64_C(3);
  result.ptxn_uid = ((index)&low_mask(12));
  result.popcode = UINT64_C(1);
  result.paddress = UINT64_C(305419776) + ((index * 64) & low_mask(44));
  return result;

  return return_value;
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    int cycles, sent, received;
    bool accepted, delivered, stalled;
    CHIReqFlit held;
    request_in_in = {};
    request_out_in.pready = UINT64_C(0);
    tick_model();
    tick_model();
    reset = UINT64_C(0);
    sent = 0;
    received = 0;
    stalled = UINT64_C(0);
    held = {};
    for (cycles = 0; cycles < 400 && received < 24; cycles++) {
      eval();
      request_in_in.pvalid = sent < 24;
      request_in_in.pbits = request_payload(sent);

      request_out_in.pready = cycles >= 12 && (cycles % 5 < 2);
      eval();
      if (stalled)
        CHECK(request_out_out.pvalid &&
              same_request(request_out_out.pbits, held));
      if (request_out_out.pvalid)
        CHECK(same_request(request_out_out.pbits, request_payload(received)));
      if (cycles == 11)
        CHECK(request_out_out.pvalid && !request_in_out.pready);
      accepted = request_in_in.pvalid && request_in_out.pready;
      delivered = request_out_out.pvalid && request_out_in.pready;
      stalled = request_out_out.pvalid && !request_out_in.pready;
      held = request_out_out.pbits;
      tick_model();
      if (accepted)
        sent++;
      if (delivered)
        received++;
    }
    CHECK(sent == 24 && received == 24);
    request_in_in.pvalid = UINT64_C(0);
    request_out_in.pready = UINT64_C(1);
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index) {
      tick_model();
      CHECK(!request_out_out.pvalid);
    }
  });
}
