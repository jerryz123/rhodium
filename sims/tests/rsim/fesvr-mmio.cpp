// Checks the FESVR RTL requester against independent MMIO packets, fragmentation, stalls, and errors.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "wide.hpp"
#include "../../../chi/tests/rsim/request.hpp"
using CHIReqFlit = std::remove_cvref_t<decltype(port_out.prequests.pbits)>;

void issue(bool wr, std::uint64_t address, std::uint64_t data, int bytes) {
  CHECK(requests_out.pready);
  requests_in = {.pvalid = 1,
                 .pbits = {.pwrite = wr,
                           .paddress = address,
                           .pdata = data,
                           .plength = std::uint8_t(bytes)}};
  tick_model();
  requests_in.pvalid = 0;
}
void expect_request(bool wr, bool coherent, bool device, std::uint64_t address,
                    int size) {
  CHIReqFlit saved, expected_request;
  expected_request = {};
  expected_request.paddress = slice(address, 43, 0);
  expected_request.psize_uor_unum_ureq = ((size)&low_mask(6));
  expected_request.ptgt_uid = coherent ? UINT64_C(5) : UINT64_C(6);
  expected_request.psrc_uid = UINT64_C(1);
  expected_request.preturn_unid_uor_ustash_unid_uor_udata_utarget = UINT64_C(1);
  expected_request.popcode = coherent ? (wr ? UINT64_C(24) : UINT64_C(2))
                                      : (wr ? UINT64_C(28) : UINT64_C(4));
  expected_request.pmem_uattr.pcacheable = coherent;
  expected_request.pmem_uattr.pdevice = device;
  expected_request.psnp_uattr_uor_udo_udwt = coherent;
  saved = port_out.prequests.pbits;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    CHECK(port_out.prequests.pvalid &&
          same_request(port_out.prequests.pbits, saved));
    CHECK(same_request(saved, expected_request));
    CHECK(saved.paddress == slice(address, 43, 0) &&
          saved.psize_uor_unum_ureq == ((size)&low_mask(6)));
    CHECK(saved.ptgt_uid == (coherent ? 5 : 6) && saved.psrc_uid == 1);
    CHECK(saved.popcode == (coherent ? (wr ? UINT64_C(24) : UINT64_C(2))
                                     : (wr ? UINT64_C(28) : UINT64_C(4))));
    CHECK(saved.pmem_uattr.pcacheable == coherent &&
          saved.pmem_uattr.pdevice == device);
    CHECK(saved.psnp_uattr_uor_udo_udwt == coherent &&
          !saved.pmem_uattr.pearly_uwrite_uacknowledge);
    tick_model();
  }
  port_in.prequests.pready = 1;
  tick_model();
  port_in.prequests.pready = 0;
}
void return_read(bool coherent, std::uint64_t address, std::uint64_t data,
                 int error_kind = 0) {
  port_in.presponse_udata = {};
  port_in.presponse_udata.pvalid = 1;
  port_in.presponse_udata.pbits.popcode = UINT64_C(4);
  port_in.presponse_udata.pbits.psrc_uid = coherent ? 9 : 6;
  port_in.presponse_udata.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
      coherent ? 5 : 6;
  port_in.presponse_udata.pbits.ptgt_uid = 1;
  port_in.presponse_udata.pbits.pdata_uid = slice(address, 5, 4);
  port_in.presponse_udata.pbits.pdata =
      wide(uint128(data) << (slice(address, 3, 0) * 8));
  switch (error_kind) {
  case 1:
    port_in.presponse_udata.pbits.presp_uerr = UINT64_C(2);

    break;
  case 2:
    port_in.presponse_udata.pbits.ptxn_uid = 1;

    break;
  case 3:
    port_in.presponse_udata.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
        7;

    break;
  case 4:
    port_in.presponse_udata.pbits.pdata_uid = 3;

    break;
  case 5:
    port_in.presponse_udata.pbits.popcode = UINT64_C(3);

    break;
  case 6:
    port_in.presponse_udata.pbits.psrc_uid = 7;

    break;
  default:;

    break;
  }
  tick_model();
  port_in.presponse_udata.pvalid = 0;
}
void return_write(bool coherent, std::uint64_t address, std::uint64_t data,
                  int bytes, int comp_error = 0) {
  port_in.presponses = {};
  port_in.presponses.pvalid = 1;
  port_in.presponses.pbits.popcode = UINT64_C(6);
  port_in.presponses.pbits.psrc_uid = coherent ? 5 : 6;
  port_in.presponses.pbits.ptgt_uid = 1;
  port_in.presponses.pbits.pdbid_uor_ugroup_uid = UINT64_C(855);
  tick_model();
  port_in.presponses.pvalid = 0;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    CHECK(port_out.prequest_udata.pvalid &&
          port_out.prequest_udata.pbits.ptxn_uid == UINT64_C(855));
    CHECK(port_out.prequest_udata.pbits.ptgt_uid == (coherent ? 5 : 6));
    CHECK(port_out.prequest_udata.pbits.pbyte_uenable ==
          ((((1 << bytes) - 1) << slice(address, 3, 0)) & low_mask(16)));
    CHECK(port_out.prequest_udata.pbits.pdata ==
          (wide(uint128(data) << (slice(address, 3, 0) * 8))));
    CHECK(port_out.prequest_udata.pbits.pdata_uid == slice(address, 5, 4));
    CHECK(!responses_out.pvalid);
    tick_model();
  }
  port_in.prequest_udata.pready = 1;
  tick_model();
  port_in.prequest_udata.pready = 0;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    CHECK(!responses_out.pvalid);
    tick_model();
  }
  port_in.presponses.pvalid = 1;
  port_in.presponses.pbits.popcode = UINT64_C(4);
  if (comp_error == 1)
    port_in.presponses.pbits.presp_uerr = UINT64_C(2);
  if (comp_error == 2)
    port_in.presponses.pbits.ptxn_uid = 1;
  tick_model();
  port_in.presponses.pvalid = 0;
}
void finish_command(std::uint64_t data, int status = 0) {
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    CHECK(responses_out.pvalid &&
          responses_out.pbits.pstatus == ((status)&low_mask(8)));
    if (status == 0)
      CHECK(responses_out.pbits.pdata == data);
    CHECK(!requests_out.pready && !port_out.prequests.pvalid);
    tick_model();
  }
  responses_in.pready = 1;
  tick_model();
  responses_in.pready = 0;
  CHECK(requests_out.pready);
}
void run_case() {
  reset = 1;
  requests_in = {};
  responses_in = {};
  port_in = {};
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick_model();
  reset = 0;
  tick_model();

  for (int phase = 0; phase < 3; ++phase) {
    issue(0, phase == 2 ? UINT64_C(8192) : UINT64_C(2147483651), 0, 8);
    if (phase == 1) {
      expect_request(0, 1, 0, UINT64_C(2147483651), 0);
      return_read(1, UINT64_C(2147483651), UINT64_C(17));
    }
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    reset = 1;
    requests_in = {};
    responses_in = {};
    port_in = {};
    tick_model();
    reset = 0;
    tick_model();
    CHECK(requests_out.pready && !responses_out.pvalid &&
          !port_out.prequests.pvalid && !port_out.prequest_udata.pvalid);
  }
  issue(1, UINT64_C(4096), UINT64_C(9833440827789222417), 8);
  expect_request(1, 0, 1, UINT64_C(4096), 3);

  port_in.psnoops.pvalid = 1;
  port_in.psnoops.pbits.popcode = UINT64_C(2);
  port_in.psnoops.pbits.psrc_uid = 5;
  port_in.psnoops.pbits.ptxn_uid = UINT64_C(582);
  CHECK(port_out.psnoops.pready);
  tick_model();
  port_in.psnoops.pvalid = 0;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    CHECK(port_out.prequester_uresponses.pvalid);
    CHECK(port_out.prequester_uresponses.pbits.popcode == UINT64_C(1));
    CHECK(port_out.prequester_uresponses.pbits.presp == 0);
    CHECK(port_out.prequester_uresponses.pbits.ptgt_uid == 5);
    CHECK(port_out.prequester_uresponses.pbits.ptxn_uid == UINT64_C(582));
    CHECK(!responses_out.pvalid);
    tick_model();
  }
  port_in.prequester_uresponses.pready = 1;
  tick_model();
  port_in.prequester_uresponses.pready = 0;
  return_write(0, UINT64_C(4096), UINT64_C(9833440827789222417), 8);
  finish_command(0);
  issue(0, UINT64_C(4100), 0, 4);
  expect_request(0, 0, 1, UINT64_C(4100), 2);
  return_read(0, UINT64_C(4100), UINT64_C(2289526357));
  finish_command(UINT64_C(2289526357));

  issue(1, UINT64_C(4112), UINT64_C(81985529216486895), 8);
  expect_request(1, 0, 1, UINT64_C(4112), 3);
  return_write(0, UINT64_C(4112), UINT64_C(81985529216486895), 8);
  finish_command(0);
  issue(0, UINT64_C(4112), 0, 8);
  expect_request(0, 0, 1, UINT64_C(4112), 3);
  return_read(0, UINT64_C(4112), UINT64_C(81985529216486895));
  finish_command(UINT64_C(81985529216486895));
  issue(1, UINT64_C(268435463), UINT64_C(165), 1);
  expect_request(1, 0, 1, UINT64_C(268435463), 0);
  return_write(0, UINT64_C(268435463), UINT64_C(165), 1);
  finish_command(0);
  issue(0, UINT64_C(268435463), 0, 1);
  expect_request(0, 0, 1, UINT64_C(268435463), 0);
  return_read(0, UINT64_C(268435463), UINT64_C(4276993701));
  finish_command(UINT64_C(165));
  issue(0, UINT64_C(65536), 0, 4);
  expect_request(0, 0, 0, UINT64_C(65536), 2);
  return_read(0, UINT64_C(65536), UINT64_C(305419896));
  finish_command(UINT64_C(305419896));

  issue(0, UINT64_C(2147483651), 0, 8);
  expect_request(0, 1, 0, UINT64_C(2147483651), 0);
  return_read(1, UINT64_C(2147483651), UINT64_C(17));
  expect_request(0, 1, 0, UINT64_C(2147483652), 2);
  return_read(1, UINT64_C(2147483652), UINT64_C(1430532898));
  expect_request(0, 1, 0, UINT64_C(2147483656), 1);
  return_read(1, UINT64_C(2147483656), UINT64_C(30566));
  expect_request(0, 1, 0, UINT64_C(2147483658), 0);
  return_read(1, UINT64_C(2147483658), UINT64_C(136));
  finish_command(UINT64_C(9833440827789222417));
  issue(1, UINT64_C(2147483688), UINT64_C(9833440827789222417), 8);
  expect_request(1, 1, 0, UINT64_C(2147483688), 3);
  return_write(1, UINT64_C(2147483688), UINT64_C(9833440827789222417), 8);
  finish_command(0);

  issue(1, UINT64_C(2147483711), UINT64_C(9833440827789222417), 8);
  expect_request(1, 1, 0, UINT64_C(2147483711), 0);
  return_write(1, UINT64_C(2147483711), UINT64_C(9833440827789222417), 1);
  expect_request(1, 1, 0, UINT64_C(2147483712), 2);
  return_write(1, UINT64_C(2147483712), UINT64_C(38411878233551650), 4);
  expect_request(1, 1, 0, UINT64_C(2147483716), 1);
  return_write(1, UINT64_C(2147483716), UINT64_C(8943462), 2);
  expect_request(1, 1, 0, UINT64_C(2147483718), 0);
  return_write(1, UINT64_C(2147483718), UINT64_C(136), 1);
  finish_command(0);
  issue(1, UINT64_C(65536), 0, 4);
  finish_command(0, 1);
  issue(0, UINT64_C(268435456), 0, 4);
  finish_command(0, 1);
  issue(0, UINT64_C(4097), 0, 4);
  finish_command(0, 1);
  issue(0, UINT64_C(4100), 0, 8);
  finish_command(0, 1);
  issue(0, UINT64_C(8192), 0, 4);
  finish_command(0, 1);
  issue(0, UINT64_C(1152921504606851072), 0, 8);
  finish_command(0, 1);
  issue(0, UINT64_C(2147487743), 0, 2);
  finish_command(0, 1);
  issue(0, UINT64_C(4096), 0, 0);
  finish_command(0, 1);
  issue(0, UINT64_C(4096), 0, 9);
  finish_command(0, 1);
  for (int error_kind = 1; error_kind <= 6; error_kind++) {
    issue(0, UINT64_C(4096), 0, 8);
    expect_request(0, 0, 1, UINT64_C(4096), 3);
    return_read(0, UINT64_C(4096), 0, error_kind);
    finish_command(0, error_kind == 1 ? 2 : 3);
  }
  for (int error_kind = 1; error_kind <= 2; error_kind++) {
    issue(1, UINT64_C(4096), 0, 8);
    expect_request(1, 0, 1, UINT64_C(4096), 3);
    return_write(0, UINT64_C(4096), 0, 8, error_kind);
    finish_command(0, error_kind == 1 ? 2 : 3);
  }
  issue(1, UINT64_C(4096), 0, 8);
  expect_request(1, 0, 1, UINT64_C(4096), 3);
  port_in.presponses = {};
  port_in.presponses.pvalid = 1;
  port_in.presponses.pbits.popcode = UINT64_C(6);
  port_in.presponses.pbits.psrc_uid = 7;
  port_in.presponses.pbits.ptgt_uid = 1;
  tick_model();
  port_in.presponses.pvalid = 0;
  CHECK(!port_out.prequest_udata.pvalid);
  finish_command(0, 3);
}

int main() { return run_test(run_case); }
