// Checks complete maintenance REQs, retained identity/PAS, retries, errors, and
// backpressure.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
bool WRONG_SOURCE = false, BAD_ADDRESS = false;
using CHIReqFlit = std::remove_cvref_t<decltype(requests_out.pbits)>;
CHIReqFlit held_request;
std::remove_cvref_t<decltype(completion_out.pbits)> held_result;
int operation;

void response(std::uint8_t opcode, std::uint8_t error_code) {
  responses_in = {};
  responses_in.pvalid = 1;
  responses_in.pbits.popcode = opcode;
  responses_in.pbits.psrc_uid = WRONG_SOURCE ? 6 : 5;
  responses_in.pbits.ptgt_uid = 3;
  responses_in.pbits.ptxn_uid = opcode == 7 ? UINT64_C(2457) : UINT64_C(66);
  responses_in.pbits.ppcrd_utype = UINT64_C(11);
  responses_in.pbits.presp_uerr = error_code;
  eval();
  CHECK(responses_out.pready);
  tick_model();
  responses_in = {};
}
void attempt(bool retry_attempt) {
  CHIReqFlit expected_request;
  expected_request = {};
  expected_request.popcode = ((8 + operation) & low_mask(7));
  expected_request.paddress =
      BAD_ADDRESS ? UINT64_C(2147483713) : UINT64_C(2147483712);
  expected_request.pexcl_usnoop_ume_ucah = 1;
  expected_request.psnp_uattr_uor_udo_udwt = 1;
  expected_request.pmem_uattr.pcacheable = 1;
  expected_request.psize_uor_unum_ureq = 6;
  expected_request.psrc_uid = 3;
  expected_request.ptgt_uid = 5;
  expected_request.preturn_unid_uor_ustash_unid_uor_udata_utarget = 3;
  expected_request.ptxn_uid = UINT64_C(66);
  expected_request.pallow_uretry = !retry_attempt;
  expected_request.ppcrd_utype = retry_attempt ? UINT64_C(11) : UINT64_C(0);
  expected_request.ppas = ((operation)&low_mask(3));
  eval();
  CHECK(same_request(requests_out.pbits, expected_request));
  CHECK(requests_out.pvalid);
  CHECK(requests_out.pbits.popcode == ((8 + operation) & low_mask(7)) &&
        requests_out.pbits.paddress == UINT64_C(2147483712) &&
        requests_out.pbits.pexcl_usnoop_ume_ucah &&
        !requests_out.pbits.pexp_ucomp_uack &&
        requests_out.pbits.psize_uor_unum_ureq == 6 &&
        requests_out.pbits.psrc_uid == 3 && requests_out.pbits.ptgt_uid == 5 &&
        requests_out.pbits.ptxn_uid == UINT64_C(66) &&
        requests_out.pbits.pallow_uretry == !retry_attempt &&
        requests_out.pbits.ppcrd_utype == (retry_attempt ? 11 : 0));
  held_request = requests_out.pbits;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    tick_model();
    CHECK(requests_out.pvalid &&
          same_request(requests_out.pbits, held_request));
  }
  requests_in.pready = 1;
  tick_model();
  requests_in.pready = 0;
}

void run_case() {
  reset = 1;
  command_in = {};
  completion_in = {};
  requests_in = {};
  responses_in = {};
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick_model();
  reset = 0;
  tick_model();
  for (operation = 0; operation < 3; operation++) {
    node_id = 3;
    txn_id = UINT64_C(66);
    command_in.pvalid = 1;
    command_in.pbits.paddress =
        BAD_ADDRESS ? UINT64_C(2147483713) : UINT64_C(2147483712);
    command_in.pbits.popcode = ((8 + operation) & low_mask(7));
    command_in.pbits.psnoop_ume = 1;
    command_in.pbits.ppas = ((operation)&low_mask(3));
    command_in.pbits.phome_uid = 5;
    command_in.pbits.pcontext = ((operation + 17) & low_mask(8));
    eval();
    CHECK(command_out.pready);
    tick_model();
    command_in = {};
    node_id = 7;
    txn_id = UINT64_C(2457);
    attempt(0);
    if (operation == 0) {
      response(7, 0);
      response(3, 0);
    } else if (operation == 1) {
      response(3, 0);
      for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
        tick_model();
        CHECK(!requests_out.pvalid);
      }
      response(7, 0);
    }
    if (operation < 2)
      attempt(1);
    response(4, operation == 2 ? 2 : 0);
    held_result = completion_out.pbits;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index) {
      CHECK(completion_out.pvalid &&
            completion_out.pbits.pcontext == held_result.pcontext &&
            completion_out.pbits.perror == held_result.perror &&
            !command_out.pready && !requests_out.pvalid);
      tick_model();
    }
    CHECK(held_result.pcontext == ((operation + 17) & low_mask(8)) &&
          held_result.perror == (operation == 2 ? 2 : 0));
    completion_in.pready = 1;
    tick_model();
    completion_in.pready = 0;
    CHECK(command_out.pready && !completion_out.pvalid);
  }
}

int main() {
  return run_test([] {
    run_case();
    dut = Model{};
    WRONG_SOURCE = true;
    expect_failure("chi_maintenance_response_source", run_case);
    dut = Model{};
    WRONG_SOURCE = false;
    BAD_ADDRESS = true;
    expect_failure("chi_maintenance_aligned", run_case);
  });
}
