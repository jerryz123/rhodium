// Checks credited-link underflow and overgrant hardware assertions with native
// ticks.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void expect_assertion(const char *label, auto action) {
  try {
    action();
  } catch (const std::exception &error) {
    CHECK(std::string(error.what()).find(label) != std::string::npos);
    return;
  }
  throw std::runtime_error(std::string("missing hardware assertion: ") + label);
}
int main() {
  return run_test([] {
    reset = 1;
    send = 0;
    payload = 0xa5;
    link_in = {};
    tick_model();
    reset = 0;
    send = 1;
    expect_assertion("credited_transfer_has_credit", [] { tick_model(); });
    dut = Model{};
    reset = 1;
    send = 0;
    payload = 0;
    link_in = {};
    tick_model();
    reset = 0;
    link_in.pcredit = 1;
    tick_model();
    tick_model();
    expect_assertion("credited_grant_within_limit", [] { tick_model(); });
  });
}
