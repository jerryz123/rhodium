// Checks transaction monitoring under stalls, concurrent progress, multibeat
// retirement, reuse, and invalid associations.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
int MODE = 0;
using CHIReqFlit = std::remove_cvref_t<decltype(req_bits)>;
using CHIRspFlit = std::remove_cvref_t<decltype(rsp_bits)>;
using CHIDatFlit = std::remove_cvref_t<decltype(wdat_bits)>;

void request(bool write_request, std::uint16_t txn = UINT64_C(257),
             std::uint8_t size = 4) {
  req_bits = {};
  req_bits.popcode = write_request ? UINT64_C(29) : UINT64_C(4);
  req_bits.psrc_uid = 3;
  req_bits.ptgt_uid = 9;
  req_bits.preturn_unid_uor_ustash_unid_uor_udata_utarget = 3;
  req_bits.preturn_utxn_uid_uor_ustash_ulpid = txn;
  req_bits.ptxn_uid = txn;
  req_bits.psize_uor_unum_ureq = size;
  req_valid = 1;
  req_ready = 0;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick_model();
  req_ready = 1;
  tick_model();
  req_valid = 0;
}

void read_response() {
  rdat_bits = {};
  rdat_bits.popcode = 4;
  rdat_bits.psrc_uid = 9;
  rdat_bits.ptgt_uid = 3;
  rdat_bits.ptxn_uid = UINT64_C(257);
  rdat_valid = 1;
  rdat_ready = 0;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick_model();
  rdat_ready = 1;
  tick_model();
  rdat_valid = 0;
}

void run_case() {
  reset = 1;
  req_valid = 0;
  req_ready = 0;
  rsp_valid = 0;
  rsp_ready = 0;
  wdat_valid = 0;
  wdat_ready = 0;
  rdat_valid = 0;
  rdat_ready = 0;
  req_bits = {};
  rsp_bits = {};
  wdat_bits = {};
  rdat_bits = {};
  tick_model();
  reset = 0;
  request(0);
  if (MODE == 1) {
    req_valid = 1;
    tick_model();
  }
  if (MODE == 2) {
    rdat_bits.popcode = 4;
    rdat_bits.psrc_uid = 9;
    rdat_bits.ptgt_uid = 4;
    rdat_bits.ptxn_uid = UINT64_C(257);
    rdat_valid = 1;
    rdat_ready = 1;
    tick_model();
  }
  read_response();
  request(1);
  wdat_bits.popcode = 3;
  wdat_bits.psrc_uid = 3;
  wdat_bits.ptgt_uid = 9;
  wdat_bits.ptxn_uid = UINT64_C(85);
  wdat_bits.pbyte_uenable = UINT64_MAX;
  wdat_valid = 1;
  wdat_ready = 0;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick_model();
  if (MODE == 3) {
    wdat_ready = 1;
    tick_model();
  }
  rsp_bits.popcode = 6;
  rsp_bits.psrc_uid = 9;
  rsp_bits.ptgt_uid = 3;
  rsp_bits.ptxn_uid = UINT64_C(257);
  rsp_bits.pdbid_uor_ugroup_uid = UINT64_C(85);
  rsp_valid = 1;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick_model();
  rsp_ready = 1;
  tick_model();
  rsp_valid = 0;
  wdat_ready = 1;
  tick_model();
  wdat_valid = 0;
  rsp_bits.popcode = 4;
  rsp_valid = 1;
  rsp_ready = 0;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick_model();
  rsp_ready = 1;
  tick_model();
  rsp_valid = 0;
  request(0);
  reset = 1;
  tick_model();
  reset = 0;
  request(0);
  read_response();

  request(0);
  request(1, UINT64_C(514), 5);
  rsp_bits.popcode = 6;
  rsp_bits.ptxn_uid = UINT64_C(514);
  rsp_bits.pdbid_uor_ugroup_uid = UINT64_C(102);
  rsp_valid = 1;
  rdat_valid = 1;
  tick_model();
  rsp_valid = 0;
  rdat_valid = 0;
  wdat_bits.ptxn_uid = UINT64_C(102);
  wdat_bits.pdata_uid = 1;
  wdat_valid = 1;
  tick_model();
  wdat_valid = 0;

  req_bits.popcode = UINT64_C(4);
  req_bits.ptxn_uid = UINT64_C(257);
  req_bits.preturn_utxn_uid_uor_ustash_ulpid = UINT64_C(257);
  req_bits.psize_uor_unum_ureq = 4;
  req_valid = 1;
  wdat_bits.pdata_uid = 0;
  wdat_valid = 1;
  tick_model();
  req_valid = 0;
  wdat_valid = 0;
  rsp_bits.popcode = 4;
  rsp_valid = 1;
  rdat_valid = 1;
  tick_model();
  rsp_valid = 0;
  rdat_valid = 0;
  request(0);
  read_response();
  if (MODE != 0)
    fail(1, "missing expected assertion");
}

int main() {
  return run_test([] {
    run_case();
    const char *labels[] = {"rni_transaction_txn_id_unique",
                            "rni_rx_dat_tgt_id",
                            "rni_transaction_write_data_has_dbid"};
    for (MODE = 1; MODE <= 3; ++MODE) {
      dut = Model{};
      expect_failure(labels[MODE - 1], run_case);
    }
  });
}
