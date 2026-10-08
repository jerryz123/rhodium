// Checks complete CHI adapter payloads, route keys, backpressure, and expected
// assertion failures.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
int FAILURE = 0;

void clear() {
  req_in_0_in = {};
  req_routed_0_in = {};
  req_eject_0_in = {};
  req_out_0_in = {};
  req_in_1_in = {};
  req_routed_1_in = {};
  req_eject_1_in = {};
  req_out_1_in = {};
  rsp_in_0_in = {};
  rsp_routed_0_in = {};
  rsp_eject_0_in = {};
  rsp_out_0_in = {};
  rsp_in_1_in = {};
  rsp_routed_1_in = {};
  rsp_eject_1_in = {};
  rsp_out_1_in = {};
  snp_in_0_in = {};
  snp_routed_0_in = {};
  snp_eject_0_in = {};
  snp_out_0_in = {};
  snp_in_1_in = {};
  snp_routed_1_in = {};
  snp_eject_1_in = {};
  snp_out_1_in = {};
  dat_in_0_in = {};
  dat_routed_0_in = {};
  dat_eject_0_in = {};
  dat_out_0_in = {};
  dat_in_1_in = {};
  dat_routed_1_in = {};
  dat_eject_1_in = {};
  dat_out_1_in = {};
}

void drive(int seed, std::uint8_t target) {
  fill_request(req_in_0_in.pbits, [=] { return (seed & 1) ? UINT32_MAX : 0u; });
  req_in_0_in.pvalid = 1;
  req_in_0_in.pbits.ptgt_uid = target;
  req_in_0_in.pbits.ptxn_uid = ((seed + 0) & low_mask(12));
  fill_request(req_eject_0_in.pbits.ppayload,
               [=] { return (seed & 1) ? 0u : UINT32_MAX; });
  req_eject_0_in.pvalid = 1;
  req_eject_0_in.pbits.proute_ukey = 1;
  req_eject_0_in.pbits.ppayload.ptgt_uid = UINT64_C(5);
  req_eject_0_in.pbits.ppayload.ptxn_uid = ((seed + 0) & low_mask(12));
  req_routed_0_in.pready = (seed & 1) ^ ((0) & low_mask(1));
  req_out_0_in.pready = (seed & 1) ^ ((0) & low_mask(1));
  fill_request(req_in_1_in.pbits, [=] { return (seed & 1) ? UINT32_MAX : 0u; });
  req_in_1_in.pvalid = 1;
  req_in_1_in.pbits.ptgt_uid = target;
  req_in_1_in.pbits.ptxn_uid = ((seed + 17) & low_mask(12));
  fill_request(req_eject_1_in.pbits.ppayload,
               [=] { return (seed & 1) ? 0u : UINT32_MAX; });
  req_eject_1_in.pvalid = 1;
  req_eject_1_in.pbits.proute_ukey = 1;
  req_eject_1_in.pbits.ppayload.ptgt_uid = UINT64_C(5);
  req_eject_1_in.pbits.ppayload.ptxn_uid = ((seed + 31) & low_mask(12));
  req_routed_1_in.pready = (seed & 1) ^ ((1) & low_mask(1));
  req_out_1_in.pready = (seed & 1) ^ ((1) & low_mask(1));
  fill_response(rsp_in_0_in.pbits,
                [=] { return (seed & 1) ? UINT32_MAX : 0u; });
  rsp_in_0_in.pvalid = 1;
  rsp_in_0_in.pbits.ptgt_uid = target;
  rsp_in_0_in.pbits.ptxn_uid = ((seed + 0) & low_mask(12));
  fill_response(rsp_eject_0_in.pbits.ppayload,
                [=] { return (seed & 1) ? 0u : UINT32_MAX; });
  rsp_eject_0_in.pvalid = 1;
  rsp_eject_0_in.pbits.proute_ukey = 1;
  rsp_eject_0_in.pbits.ppayload.ptgt_uid = UINT64_C(5);
  rsp_eject_0_in.pbits.ppayload.ptxn_uid = ((seed + 0) & low_mask(12));
  rsp_routed_0_in.pready = (seed & 1) ^ ((0) & low_mask(1));
  rsp_out_0_in.pready = (seed & 1) ^ ((0) & low_mask(1));
  fill_response(rsp_in_1_in.pbits,
                [=] { return (seed & 1) ? UINT32_MAX : 0u; });
  rsp_in_1_in.pvalid = 1;
  rsp_in_1_in.pbits.ptgt_uid = target;
  rsp_in_1_in.pbits.ptxn_uid = ((seed + 17) & low_mask(12));
  fill_response(rsp_eject_1_in.pbits.ppayload,
                [=] { return (seed & 1) ? 0u : UINT32_MAX; });
  rsp_eject_1_in.pvalid = 1;
  rsp_eject_1_in.pbits.proute_ukey = 1;
  rsp_eject_1_in.pbits.ppayload.ptgt_uid = UINT64_C(5);
  rsp_eject_1_in.pbits.ppayload.ptxn_uid = ((seed + 31) & low_mask(12));
  rsp_routed_1_in.pready = (seed & 1) ^ ((1) & low_mask(1));
  rsp_out_1_in.pready = (seed & 1) ^ ((1) & low_mask(1));
  fill_snoop(snp_in_0_in.pbits.pflit,
             [=] { return (seed & 1) ? UINT32_MAX : 0u; });
  snp_in_0_in.pvalid = 1;
  snp_in_0_in.pbits.ptarget_uid = target;
  snp_in_0_in.pbits.pflit.ptxn_uid = ((seed + 0) & low_mask(12));
  fill_snoop(snp_eject_0_in.pbits.ppayload,
             [=] { return (seed & 1) ? 0u : UINT32_MAX; });
  snp_eject_0_in.pvalid = 1;
  snp_eject_0_in.pbits.proute_ukey = 1;
  snp_eject_0_in.pbits.ppayload.psrc_uid = UINT64_C(11);
  snp_eject_0_in.pbits.ppayload.ptxn_uid = ((seed + 0) & low_mask(12));
  snp_routed_0_in.pready = (seed & 1) ^ ((0) & low_mask(1));
  snp_out_0_in.pready = (seed & 1) ^ ((0) & low_mask(1));
  fill_snoop(snp_in_1_in.pbits.pflit,
             [=] { return (seed & 1) ? UINT32_MAX : 0u; });
  snp_in_1_in.pvalid = 1;
  snp_in_1_in.pbits.ptarget_uid = target;
  snp_in_1_in.pbits.pflit.ptxn_uid = ((seed + 17) & low_mask(12));
  fill_snoop(snp_eject_1_in.pbits.ppayload,
             [=] { return (seed & 1) ? 0u : UINT32_MAX; });
  snp_eject_1_in.pvalid = 1;
  snp_eject_1_in.pbits.proute_ukey = 1;
  snp_eject_1_in.pbits.ppayload.psrc_uid = UINT64_C(11);
  snp_eject_1_in.pbits.ppayload.ptxn_uid = ((seed + 31) & low_mask(12));
  snp_routed_1_in.pready = (seed & 1) ^ ((1) & low_mask(1));
  snp_out_1_in.pready = (seed & 1) ^ ((1) & low_mask(1));
  fill_data<128>(dat_in_0_in.pbits,
                 [=] { return (seed & 1) ? UINT32_MAX : 0u; });
  dat_in_0_in.pvalid = 1;
  dat_in_0_in.pbits.ptgt_uid = target;
  dat_in_0_in.pbits.ptxn_uid = ((seed + 0) & low_mask(12));
  fill_data<128>(dat_eject_0_in.pbits.ppayload,
                 [=] { return (seed & 1) ? 0u : UINT32_MAX; });
  dat_eject_0_in.pvalid = 1;
  dat_eject_0_in.pbits.proute_ukey = 1;
  dat_eject_0_in.pbits.ppayload.ptgt_uid = UINT64_C(5);
  dat_eject_0_in.pbits.ppayload.ptxn_uid = ((seed + 0) & low_mask(12));
  dat_routed_0_in.pready = (seed & 1) ^ ((0) & low_mask(1));
  dat_out_0_in.pready = (seed & 1) ^ ((0) & low_mask(1));
  fill_data<128>(dat_in_1_in.pbits,
                 [=] { return (seed & 1) ? UINT32_MAX : 0u; });
  dat_in_1_in.pvalid = 1;
  dat_in_1_in.pbits.ptgt_uid = target;
  dat_in_1_in.pbits.ptxn_uid = ((seed + 17) & low_mask(12));
  fill_data<128>(dat_eject_1_in.pbits.ppayload,
                 [=] { return (seed & 1) ? 0u : UINT32_MAX; });
  dat_eject_1_in.pvalid = 1;
  dat_eject_1_in.pbits.proute_ukey = 1;
  dat_eject_1_in.pbits.ppayload.ptgt_uid = UINT64_C(5);
  dat_eject_1_in.pbits.ppayload.ptxn_uid = ((seed + 31) & low_mask(12));
  dat_routed_1_in.pready = (seed & 1) ^ ((1) & low_mask(1));
  dat_out_1_in.pready = (seed & 1) ^ ((1) & low_mask(1));
}

void check_paths(bool expected_route) {
  CHECK(req_routed_0_out.pvalid &&
        req_routed_0_out.pbits.proute_ukey == expected_route &&
        same_request(req_routed_0_out.pbits.ppayload, req_in_0_in.pbits) &&
        req_in_0_out.pready == req_routed_0_in.pready);
  CHECK(req_out_0_out.pvalid &&
        same_request(req_out_0_out.pbits, req_eject_0_in.pbits.ppayload) &&
        req_eject_0_out.pready == req_out_0_in.pready);
  CHECK(req_routed_1_out.pvalid &&
        req_routed_1_out.pbits.proute_ukey == expected_route &&
        same_request(req_routed_1_out.pbits.ppayload, req_in_1_in.pbits) &&
        req_in_1_out.pready == req_routed_1_in.pready);
  CHECK(req_out_1_out.pvalid &&
        same_request(req_out_1_out.pbits, req_eject_1_in.pbits.ppayload) &&
        req_eject_1_out.pready == req_out_1_in.pready);
  CHECK(rsp_routed_0_out.pvalid &&
        rsp_routed_0_out.pbits.proute_ukey == expected_route &&
        same_response(rsp_routed_0_out.pbits.ppayload, rsp_in_0_in.pbits) &&
        rsp_in_0_out.pready == rsp_routed_0_in.pready);
  CHECK(rsp_out_0_out.pvalid &&
        same_response(rsp_out_0_out.pbits, rsp_eject_0_in.pbits.ppayload) &&
        rsp_eject_0_out.pready == rsp_out_0_in.pready);
  CHECK(rsp_routed_1_out.pvalid &&
        rsp_routed_1_out.pbits.proute_ukey == expected_route &&
        same_response(rsp_routed_1_out.pbits.ppayload, rsp_in_1_in.pbits) &&
        rsp_in_1_out.pready == rsp_routed_1_in.pready);
  CHECK(rsp_out_1_out.pvalid &&
        same_response(rsp_out_1_out.pbits, rsp_eject_1_in.pbits.ppayload) &&
        rsp_eject_1_out.pready == rsp_out_1_in.pready);
  CHECK(snp_routed_0_out.pvalid &&
        snp_routed_0_out.pbits.proute_ukey == expected_route &&
        same_snoop(snp_routed_0_out.pbits.ppayload, snp_in_0_in.pbits.pflit) &&
        snp_in_0_out.pready == snp_routed_0_in.pready);
  CHECK(snp_out_0_out.pvalid &&
        same_snoop(snp_out_0_out.pbits, snp_eject_0_in.pbits.ppayload) &&
        snp_eject_0_out.pready == snp_out_0_in.pready);
  CHECK(snp_routed_1_out.pvalid &&
        snp_routed_1_out.pbits.proute_ukey == expected_route &&
        same_snoop(snp_routed_1_out.pbits.ppayload, snp_in_1_in.pbits.pflit) &&
        snp_in_1_out.pready == snp_routed_1_in.pready);
  CHECK(snp_out_1_out.pvalid &&
        same_snoop(snp_out_1_out.pbits, snp_eject_1_in.pbits.ppayload) &&
        snp_eject_1_out.pready == snp_out_1_in.pready);
  CHECK(dat_routed_0_out.pvalid &&
        dat_routed_0_out.pbits.proute_ukey == expected_route &&
        same_data(dat_routed_0_out.pbits.ppayload, dat_in_0_in.pbits) &&
        dat_in_0_out.pready == dat_routed_0_in.pready);
  CHECK(dat_out_0_out.pvalid &&
        same_data(dat_out_0_out.pbits, dat_eject_0_in.pbits.ppayload) &&
        dat_eject_0_out.pready == dat_out_0_in.pready);
  CHECK(dat_routed_1_out.pvalid &&
        dat_routed_1_out.pbits.proute_ukey == expected_route &&
        same_data(dat_routed_1_out.pbits.ppayload, dat_in_1_in.pbits) &&
        dat_in_1_out.pready == dat_routed_1_in.pready);
  CHECK(dat_out_1_out.pvalid &&
        same_data(dat_out_1_out.pbits, dat_eject_1_in.pbits.ppayload) &&
        dat_eject_1_out.pready == dat_out_1_in.pready);
}

void run_case() {
  reset = 1;
  injection_site = 2;
  ejection_site = 2;
  clear();
  tick_model();
  tick_model();
  eval();
  reset = 0;
  if (FAILURE != 0) {
    switch (FAILURE) {
    case 1: {
      req_in_0_in.pvalid = 1;
      req_in_0_in.pbits.ptgt_uid = UINT64_C(99);
    }

    break;
    case 2: {
      rsp_eject_0_in.pvalid = 1;
      rsp_eject_0_in.pbits.ppayload.ptgt_uid = UINT64_C(6);
    }

    break;
    case 3: {
      injection_site = 0;
      dat_in_1_in.pvalid = 1;
      dat_in_1_in.pbits.ptgt_uid = UINT64_C(5);
    }

    break;
    case 4: {
      req_eject_1_in.pvalid = 1;
      req_eject_1_in.pbits.ppayload.ptgt_uid = UINT64_C(6);
    }

    break;
    case 5: {
      ejection_site = 0;
      snp_eject_1_in.pvalid = 1;
    }

    break;
    }
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick_model();
    fail(1, "expected CHI adapter assertion did not fire");
  } else {
    for (int seed = 0; seed < 4; seed++) {
      eval();
      drive(seed, ((seed >> 1) & 1) ? UINT64_C(6) : UINT64_C(5));
      eval();

      check_paths(!((seed >> 1) & 1));
      tick_model();
      check_paths(!((seed >> 1) & 1));
    }
    eval();
    clear();

    injection_site = 0;
    ejection_site = 0;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
  }
}

int main() {
  return run_test([] {
    run_case();
    const char *labels[] = {"chi_req_noc_target_has_route",
                            "chi_rsp_noc_ejection_target",
                            "chi_dat_noc_family_target_has_route",
                            "chi_req_noc_family_ejection_target",
                            "chi_snp_noc_family_ejection_site"};
    for (FAILURE = 1; FAILURE <= 5; ++FAILURE) {
      dut = Model{};
      expect_failure(labels[FAILURE - 1], run_case);
    }
  });
}
