// Simulates initial-profile HN-I translation through CHIHNIChannels.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
inline const auto &downstream_response_data_out =
    port_out.psubordinate.pdat.presponse;
inline const auto &downstream_request_data_out =
    port_out.psubordinate.pdat.prequest;
inline const auto &downstream_requests_out = port_out.psubordinate.preq;
inline const auto &downstream_responses_out = port_out.psubordinate.prsp;
inline const auto &upstream_response_data_out =
    port_out.prequester.pdat.presponse;
inline const auto &upstream_request_data_out =
    port_out.prequester.pdat.prequest;
inline const auto &upstream_responses_out = port_out.prequester.prsp.presponse;
inline const auto &upstream_requester_responses_out =
    port_out.prequester.prsp.prequester;
inline const auto &upstream_requests_out = port_out.prequester.preq;
using CHIReqFlit = std::remove_cvref_t<decltype(port_in.prequester.preq.pbits)>;
using CHIRspFlit =
    std::remove_cvref_t<decltype(port_in.prequester.prsp.prequester.pbits)>;
using CHIDatFlit =
    std::remove_cvref_t<decltype(port_in.prequester.pdat.prequest.pbits)>;
using dat_t = std::remove_cvref_t<decltype(port_in.prequester.pdat.prequest)>;
using rsp_t = std::remove_cvref_t<decltype(port_in.prequester.prsp.prequester)>;
using req_t = std::remove_cvref_t<decltype(port_in.prequester.preq)>;
inline auto &upstream_requests_in = port_in.prequester.preq;
inline auto &upstream_requester_responses_in =
    port_in.prequester.prsp.prequester;
inline auto &upstream_responses_in = port_in.prequester.prsp.presponse;
inline auto &upstream_request_data_in = port_in.prequester.pdat.prequest;
inline auto &upstream_response_data_in = port_in.prequester.pdat.presponse;
inline auto &downstream_responses_in = port_in.psubordinate.prsp;
inline auto &downstream_requests_in = port_in.psubordinate.preq;
inline auto &downstream_request_data_in = port_in.psubordinate.pdat.prequest;
inline auto &downstream_response_data_in = port_in.psubordinate.pdat.presponse;
constexpr std::uint8_t READ_NO_SNP = UINT64_C(4);
constexpr std::uint8_t WRITE_NO_SNP_FULL = UINT64_C(29);
constexpr std::uint8_t COMP = UINT64_C(4);
constexpr std::uint8_t DBID_RESP = UINT64_C(6);
constexpr std::uint8_t NON_COPY_BACK_WRITE_DATA = UINT64_C(3);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t REQUESTER_ID = UINT64_C(3);
constexpr std::uint8_t HOME_ID = UINT64_C(5);
constexpr std::uint8_t SUBORDINATE_ID = UINT64_C(9);
constexpr std::uint8_t SECOND_SUBORDINATE_ID = UINT64_C(10);
constexpr std::uint16_t SUBORDINATE_DBID = UINT64_C(85);
constexpr std::uint16_t SECOND_SUBORDINATE_DBID = UINT64_C(102);
constexpr unsigned __int128 READ_DATA =
    ((uint128(UINT64_C(4822678189205111)) << 64) |
     UINT64_C(9843086184167632639));
constexpr unsigned __int128 WRITE_DATA =
    ((uint128(UINT64_C(18441921395520346504)) << 64) |
     UINT64_C(8603657889541918976));

void translate_request(std::uint8_t opcode, std::uint8_t size,
                       std::uint16_t txn_id, std::uint16_t return_txn_id,
                       std::uint64_t address, std::uint8_t subordinate_id,
                       std::uint16_t &slot) {
  {
    upstream_requests_in.pbits = {};
    upstream_requests_in.pbits.popcode = opcode;
    upstream_requests_in.pbits.psrc_uid = REQUESTER_ID;
    upstream_requests_in.pbits.ptgt_uid = HOME_ID;
    upstream_requests_in.pbits.ptxn_uid = txn_id;
    upstream_requests_in.pbits.paddress = address;
    upstream_requests_in.pbits.psize_uor_unum_ureq = size;
    upstream_requests_in.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
        REQUESTER_ID;
    upstream_requests_in.pbits.preturn_utxn_uid_uor_ustash_ulpid =
        return_txn_id;
    upstream_requests_in.pvalid = UINT64_C(1);
    downstream_requests_in.pready = UINT64_C(0);
    eval();
    CHECK(downstream_requests_out.pvalid && !upstream_requests_out.pready);
    downstream_requests_in.pready = UINT64_C(1);
    eval();
    CHECK(upstream_requests_out.pready);
    CHECK(downstream_requests_out.pbits.popcode == opcode &&
          downstream_requests_out.pbits.psrc_uid == HOME_ID &&
          downstream_requests_out.pbits.ptgt_uid == subordinate_id);
    CHECK(downstream_requests_out.pbits
                  .preturn_unid_uor_ustash_unid_uor_udata_utarget == HOME_ID &&
          downstream_requests_out.pbits.preturn_utxn_uid_uor_ustash_ulpid ==
              downstream_requests_out.pbits.ptxn_uid);
    slot = downstream_requests_out.pbits.ptxn_uid;
    tick_model();
    upstream_requests_in = {};
    downstream_requests_in.pready = UINT64_C(0);
  }
}

void translate_subordinate_data(std::uint16_t slot, std::uint16_t return_txn_id,
                                std::uint8_t data_id,
                                std::uint8_t subordinate_id,
                                unsigned __int128 data) {
  {
    downstream_response_data_in.pbits = {};
    downstream_response_data_in.pbits.popcode = COMP_DATA;
    downstream_response_data_in.pbits.psrc_uid = subordinate_id;
    downstream_response_data_in.pbits.ptgt_uid = HOME_ID;
    downstream_response_data_in.pbits.ptxn_uid = slot;
    downstream_response_data_in.pbits.pdata_uid = data_id;
    downstream_response_data_in.pbits.pbyte_uenable = UINT64_C(65535);
    downstream_response_data_in.pbits.pdata = wide(data);
    downstream_response_data_in.pvalid = UINT64_C(1);
    upstream_response_data_in.pready = UINT64_C(1);
    eval();
    CHECK(downstream_response_data_out.pready &&
          upstream_response_data_out.pvalid);
    CHECK(upstream_response_data_out.pbits.popcode == COMP_DATA &&
          upstream_response_data_out.pbits.psrc_uid == HOME_ID &&
          upstream_response_data_out.pbits.ptgt_uid == REQUESTER_ID &&
          upstream_response_data_out.pbits.ptxn_uid == return_txn_id &&
          upstream_response_data_out.pbits.pdata_uid == data_id);
    CHECK(upstream_response_data_out.pbits.pdata == wide(data));
    tick_model();
    downstream_response_data_in = {};
    upstream_response_data_in.pready = UINT64_C(0);
  }
}

void translate_subordinate_response(std::uint8_t opcode, std::uint16_t slot,
                                    std::uint16_t dbid,
                                    std::uint16_t requester_txn_id,
                                    std::uint8_t subordinate_id,
                                    std::uint16_t &home_dbid) {
  {
    downstream_responses_in.pbits = {};
    downstream_responses_in.pbits.popcode = opcode;
    downstream_responses_in.pbits.psrc_uid = subordinate_id;
    downstream_responses_in.pbits.ptgt_uid = HOME_ID;
    downstream_responses_in.pbits.ptxn_uid = slot;
    downstream_responses_in.pbits.pdbid_uor_ugroup_uid = dbid;
    downstream_responses_in.pvalid = UINT64_C(1);
    upstream_responses_in.pready = UINT64_C(1);
    eval();
    CHECK(downstream_responses_out.pready && upstream_responses_out.pvalid);
    CHECK(upstream_responses_out.pbits.popcode == opcode &&
          upstream_responses_out.pbits.psrc_uid == HOME_ID &&
          upstream_responses_out.pbits.ptgt_uid == REQUESTER_ID &&
          upstream_responses_out.pbits.ptxn_uid == requester_txn_id);
    home_dbid = upstream_responses_out.pbits.pdbid_uor_ugroup_uid;
    tick_model();
    downstream_responses_in = {};
    upstream_responses_in.pready = UINT64_C(0);
  }
}

void translate_write_data(std::uint16_t home_dbid,
                          std::uint16_t subordinate_dbid, std::uint8_t data_id,
                          std::uint8_t subordinate_id, unsigned __int128 data) {
  {
    upstream_request_data_in.pbits = {};
    upstream_request_data_in.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
    upstream_request_data_in.pbits.psrc_uid = REQUESTER_ID;
    upstream_request_data_in.pbits.ptgt_uid = HOME_ID;
    upstream_request_data_in.pbits.ptxn_uid = home_dbid;
    upstream_request_data_in.pbits.pdata_uid = data_id;
    upstream_request_data_in.pbits.pbyte_uenable = UINT64_C(65535);
    upstream_request_data_in.pbits.pdata = wide(data);
    upstream_request_data_in.pvalid = UINT64_C(1);
    downstream_request_data_in.pready = UINT64_C(1);
    eval();
    CHECK(upstream_request_data_out.pready &&
          downstream_request_data_out.pvalid);
    CHECK(downstream_request_data_out.pbits.popcode ==
              NON_COPY_BACK_WRITE_DATA &&
          downstream_request_data_out.pbits.psrc_uid == HOME_ID &&
          downstream_request_data_out.pbits.ptgt_uid == subordinate_id &&
          downstream_request_data_out.pbits.ptxn_uid == subordinate_dbid &&
          downstream_request_data_out.pbits.pdata_uid == data_id);
    CHECK(downstream_request_data_out.pbits.pdata == wide(data));
    tick_model();
    upstream_request_data_in = {};
    downstream_request_data_in.pready = UINT64_C(0);
  }
}

std::uint16_t slot;
std::uint16_t first_slot;
std::uint16_t second_slot;
std::uint16_t first_home_dbid;
std::uint16_t second_home_dbid;
std::uint16_t completion_dbid;

void run_case() {
  reset = UINT64_C(1);
  upstream_requests_in = {};
  upstream_requester_responses_in = {};
  upstream_request_data_in = {};
  upstream_responses_in = {};
  upstream_response_data_in = {};
  downstream_requests_in = {};
  downstream_request_data_in = {};
  downstream_responses_in = {};
  downstream_response_data_in = {};
  tick_model();
  reset = UINT64_C(0);

  translate_request(READ_NO_SNP, UINT64_C(6), UINT64_C(257), UINT64_C(1281),
                    UINT64_C(2147483648), SUBORDINATE_ID, slot);
  translate_subordinate_data(slot, UINT64_C(1281), UINT64_C(2), SUBORDINATE_ID,
                             READ_DATA + 2);
  translate_subordinate_data(slot, UINT64_C(1281), UINT64_C(0), SUBORDINATE_ID,
                             READ_DATA + 0);
  translate_subordinate_data(slot, UINT64_C(1281), UINT64_C(3), SUBORDINATE_ID,
                             READ_DATA + 3);
  translate_subordinate_data(slot, UINT64_C(1281), UINT64_C(1), SUBORDINATE_ID,
                             READ_DATA + 1);

  translate_request(READ_NO_SNP, UINT64_C(4), UINT64_C(259), UINT64_C(1283),
                    UINT64_C(2415919104), SECOND_SUBORDINATE_ID, slot);
  translate_subordinate_data(slot, UINT64_C(1283), UINT64_C(0),
                             SECOND_SUBORDINATE_ID, READ_DATA);

  translate_request(WRITE_NO_SNP_FULL, UINT64_C(4), UINT64_C(258), UINT64_C(0),
                    UINT64_C(2147483648), SUBORDINATE_ID, first_slot);
  translate_request(WRITE_NO_SNP_FULL, UINT64_C(4), UINT64_C(260), UINT64_C(0),
                    UINT64_C(2415919104), SECOND_SUBORDINATE_ID, second_slot);

  translate_subordinate_response(DBID_RESP, second_slot,
                                 SECOND_SUBORDINATE_DBID, UINT64_C(260),
                                 SECOND_SUBORDINATE_ID, second_home_dbid);
  translate_subordinate_response(DBID_RESP, first_slot, SUBORDINATE_DBID,
                                 UINT64_C(258), SUBORDINATE_ID,
                                 first_home_dbid);
  translate_write_data(first_home_dbid, SUBORDINATE_DBID, UINT64_C(0),
                       SUBORDINATE_ID, WRITE_DATA);
  translate_write_data(second_home_dbid, SECOND_SUBORDINATE_DBID, UINT64_C(0),
                       SECOND_SUBORDINATE_ID, WRITE_DATA + 1);
  translate_subordinate_response(COMP, first_slot, SUBORDINATE_DBID,
                                 UINT64_C(258), SUBORDINATE_ID,
                                 completion_dbid);
  CHECK(completion_dbid == first_home_dbid);
  translate_subordinate_response(COMP, second_slot, SECOND_SUBORDINATE_DBID,
                                 UINT64_C(260), SECOND_SUBORDINATE_ID,
                                 completion_dbid);
  CHECK(completion_dbid == second_home_dbid);
}

void wrong_response() {
  reset = 1;
  std::uint16_t slot = 0;

  port_in = {};
  tick_model();
  reset = UINT64_C(0);
  port_in.psubordinate.preq.pready = UINT64_C(1);
  port_in.prequester.preq.pbits.popcode = UINT64_C(29);
  port_in.prequester.preq.pbits.psrc_uid = UINT64_C(3);
  port_in.prequester.preq.pbits.ptgt_uid = UINT64_C(5);
  port_in.prequester.preq.pbits.ptxn_uid = UINT64_C(257);
  port_in.prequester.preq.pbits.paddress = UINT64_C(2147483648);
  port_in.prequester.preq.pbits.psize_uor_unum_ureq = UINT64_C(4);
  port_in.prequester.preq.pvalid = UINT64_C(1);
  eval();
  while (!port_out.prequester.preq.pready)
    tick_model();
  slot = port_out.psubordinate.preq.pbits.ptxn_uid;
  tick_model();
  port_in.prequester.preq = {};
  port_in.prequester.prsp.presponse.pready = UINT64_C(1);
  port_in.psubordinate.prsp.pbits.popcode = UINT64_C(6);
  port_in.psubordinate.prsp.pbits.psrc_uid = UINT64_C(10);
  port_in.psubordinate.prsp.pbits.ptgt_uid = UINT64_C(5);
  port_in.psubordinate.prsp.pbits.ptxn_uid = slot;
  port_in.psubordinate.prsp.pvalid = UINT64_C(1);
  tick_model();
}

void wrong_data() {
  reset = 1;
  std::uint16_t slot = 0;

  port_in = {};
  tick_model();
  reset = UINT64_C(0);
  port_in.psubordinate.preq.pready = UINT64_C(1);
  port_in.prequester.preq.pbits.popcode = UINT64_C(4);
  port_in.prequester.preq.pbits.psrc_uid = UINT64_C(3);
  port_in.prequester.preq.pbits.ptgt_uid = UINT64_C(5);
  port_in.prequester.preq.pbits.ptxn_uid = UINT64_C(258);
  port_in.prequester.preq.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
      UINT64_C(3);
  port_in.prequester.preq.pbits.preturn_utxn_uid_uor_ustash_ulpid =
      UINT64_C(1282);
  port_in.prequester.preq.pbits.paddress = UINT64_C(2147483648);
  port_in.prequester.preq.pbits.psize_uor_unum_ureq = UINT64_C(4);
  port_in.prequester.preq.pvalid = UINT64_C(1);
  eval();
  while (!port_out.prequester.preq.pready)
    tick_model();
  slot = port_out.psubordinate.preq.pbits.ptxn_uid;
  tick_model();
  port_in.prequester.preq = {};
  port_in.prequester.pdat.presponse.pready = UINT64_C(1);
  port_in.psubordinate.pdat.presponse.pbits.popcode = UINT64_C(4);
  port_in.psubordinate.pdat.presponse.pbits.psrc_uid = UINT64_C(10);
  port_in.psubordinate.pdat.presponse.pbits.ptgt_uid = UINT64_C(5);
  port_in.psubordinate.pdat.presponse.pbits.ptxn_uid = slot;
  port_in.psubordinate.pdat.presponse.pbits.pdata_uid = UINT64_C(0);
  port_in.psubordinate.pdat.presponse.pvalid = UINT64_C(1);
  tick_model();
}
int main() {
  return run_test([] {
    run_case();
    dut = Model{};
    expect_failure("chi_hni_response_source_subordinate", wrong_response);
    dut = Model{};
    expect_failure("chi_hni_read_data_source_subordinate", wrong_data);
  });
}
