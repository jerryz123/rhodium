// SPDX-License-Identifier: Apache-2.0
#include "../../../../sims/tests/rsim/fesvr-mmio-behavior.hpp"
extern "C" void event_fesvr_bind();
extern "C" void event_fesvr_sample(unsigned reset, unsigned command,
                                   unsigned request, unsigned write_data,
                                   unsigned completion, unsigned status,
                                   unsigned blocked);
extern "C" void event_fesvr_check();
extern "C" void event_fesvr_finish();
int main() {
  return run_test([] {
    event_fesvr_bind();
    fesvr_before_tick = [] {
      event_fesvr_sample(
          unsigned(reset), unsigned(requests_in.pvalid && requests_out.pready),
          unsigned(port_out.prequests.pvalid && port_in.prequests.pready),
          unsigned(port_out.prequest_udata.pvalid &&
                   port_in.prequest_udata.pready),
          unsigned(responses_out.pvalid && responses_in.pready),
          unsigned(responses_out.pbits.pstatus),
          unsigned((port_out.prequests.pvalid && !port_in.prequests.pready) ||
                   (port_out.prequest_udata.pvalid &&
                    !port_in.prequest_udata.pready) ||
                   (responses_out.pvalid && !responses_in.pready)));
    };
    fesvr_after_tick = event_fesvr_check;
    run_case();
    event_fesvr_finish();
  });
}
