// SPDX-License-Identifier: Apache-2.0
#include "inclusive-home-behavior.hpp"

int main() {
  return run_test([] {
    run_case();
    const char *labels[] = {
        "chi_inclusive_hnf_copyback_byte_enable",
        "chi_inclusive_hnf_requester_write_data_id_unique",
        "chi_inclusive_hnf_copyback_state_consistent",
        "chi_inclusive_victim_writeback_dbid_error_zero",
        "chi_inclusive_victim_writeback_completion_matches_dbid"};
    for (INVALID_CASE = 1; INVALID_CASE <= 5; ++INVALID_CASE) {
      dut = Model{};
      expect_failure(labels[INVALID_CASE - 1], run_case);
    }
  });
}
