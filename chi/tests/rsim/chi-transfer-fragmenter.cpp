// Simulates fragmented CHI RAM writes and reads with stalled read responses.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
inline auto &requests_in = port_in.preq;
inline auto &responses_in = port_in.prsp;
inline auto &request_data_in = port_in.pdat.prequest;
inline auto &response_data_in = port_in.pdat.presponse;
constexpr std::uint8_t READ_NO_SNP = UINT64_C(4);
constexpr std::uint8_t WRITE_NO_SNP_FULL = UINT64_C(29);
constexpr std::uint8_t COMP = UINT64_C(4);
constexpr std::uint8_t DBID_RESP = UINT64_C(6);
constexpr std::uint8_t NON_COPY_BACK_WRITE_DATA = UINT64_C(3);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t HOME_ID = UINT64_C(5);
constexpr std::uint8_t RAM_ID = UINT64_C(9);

void issue_request(std::uint8_t opcode, std::uint16_t txn_id,
                   std::uint64_t address, std::uint8_t size,
                   std::uint16_t return_txn_id) {
  {
    requests_in.pbits = {};
    requests_in.pbits.popcode = opcode;
    requests_in.pbits.psrc_uid = HOME_ID;
    requests_in.pbits.ptgt_uid = RAM_ID;
    requests_in.pbits.ptxn_uid = txn_id;
    requests_in.pbits.paddress = address;
    requests_in.pbits.psize_uor_unum_ureq = size;
    requests_in.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget = HOME_ID;
    requests_in.pbits.preturn_utxn_uid_uor_ustash_ulpid = return_txn_id;
    requests_in.pvalid = UINT64_C(1);
    while (!port_out.preq.pready)
      tick_model();
    tick_model();
    requests_in = {};
  }
}

void wait_rsp() {
  int cycles;
  {
    for (cycles = 0; cycles < 100 && !port_out.prsp.pvalid; cycles = cycles + 1)
      tick_model();
    CHECK(port_out.prsp.pvalid);
  }
}

void write_word(std::uint64_t address, std::uint16_t txn_id,
                unsigned __int128 data) {
  std::uint16_t dbid;
  {
    issue_request(WRITE_NO_SNP_FULL, txn_id, address, UINT64_C(4), UINT64_C(0));
    responses_in.pready = UINT64_C(1);
    wait_rsp();
    CHECK(port_out.prsp.pbits.popcode == DBID_RESP);
    dbid = port_out.prsp.pbits.pdbid_uor_ugroup_uid;
    tick_model();
    responses_in.pready = UINT64_C(0);

    request_data_in.pbits = {};
    request_data_in.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
    request_data_in.pbits.psrc_uid = HOME_ID;
    request_data_in.pbits.ptgt_uid = RAM_ID;
    request_data_in.pbits.ptxn_uid = dbid;
    request_data_in.pbits.pdata_uid = slice(address, 5, 4);
    request_data_in.pbits.pbyte_uenable = UINT64_C(65535);
    request_data_in.pbits.pdata = wide(data);
    request_data_in.pvalid = UINT64_C(1);
    while (!port_out.pdat.prequest.pready)
      tick_model();
    tick_model();
    request_data_in = {};

    responses_in.pready = UINT64_C(1);
    wait_rsp();
    CHECK(port_out.prsp.pbits.popcode == COMP &&
          port_out.prsp.pbits.ptxn_uid == txn_id);
    tick_model();
    responses_in.pready = UINT64_C(0);
  }
}

void send_write_beat(std::uint16_t dbid, std::uint8_t data_id,
                     unsigned __int128 data) {
  {
    request_data_in.pbits = {};
    request_data_in.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
    request_data_in.pbits.psrc_uid = HOME_ID;
    request_data_in.pbits.ptgt_uid = RAM_ID;
    request_data_in.pbits.ptxn_uid = dbid;
    request_data_in.pbits.pdata_uid = data_id;
    request_data_in.pbits.pbyte_uenable = UINT64_C(65535);
    request_data_in.pbits.pdata = wide(data);
    request_data_in.pvalid = UINT64_C(1);
    while (!port_out.pdat.prequest.pready)
      tick_model();
    tick_model();
    request_data_in = {};
  }
}

void write_line() {
  std::uint16_t dbid;
  {
    issue_request(WRITE_NO_SNP_FULL, UINT64_C(256), UINT64_C(2147483648),
                  UINT64_C(6), UINT64_C(0));
    responses_in.pready = UINT64_C(1);
    wait_rsp();
    CHECK(port_out.prsp.pbits.popcode == DBID_RESP);
    dbid = port_out.prsp.pbits.pdbid_uor_ugroup_uid;
    tick_model();
    responses_in.pready = UINT64_C(0);

    send_write_beat(dbid, UINT64_C(2),
                    ((uint128(UINT64_C(2459565876494606882)) << 64) |
                     UINT64_C(2459565876494606882)));
    send_write_beat(dbid, UINT64_C(0), UINT64_C(0));
    send_write_beat(dbid, UINT64_C(3),
                    ((uint128(UINT64_C(3689348814741910323)) << 64) |
                     UINT64_C(3689348814741910323)));
    send_write_beat(dbid, UINT64_C(1),
                    ((uint128(UINT64_C(1229782938247303441)) << 64) |
                     UINT64_C(1229782938247303441)));

    responses_in.pready = UINT64_C(1);
    wait_rsp();
    CHECK(port_out.prsp.pbits.popcode == COMP &&
          port_out.prsp.pbits.ptxn_uid == UINT64_C(256) &&
          port_out.prsp.pbits.pdbid_uor_ugroup_uid == dbid);
    tick_model();
    responses_in.pready = UINT64_C(0);
  }
}

void accept_read_beat(std::uint8_t data_id, unsigned __int128 data) {
  int cycles;
  std::remove_cvref_t<decltype(port_out.pdat.presponse)> held;
  {
    response_data_in.pready = UINT64_C(0);
    for (cycles = 0; cycles < 100 && !port_out.pdat.presponse.pvalid;
         cycles = cycles + 1)
      tick_model();
    CHECK(port_out.pdat.presponse.pvalid);
    held = port_out.pdat.presponse;
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick_model();
      CHECK(port_out.pdat.presponse.pvalid == held.pvalid &&
            same_data(port_out.pdat.presponse.pbits, held.pbits));
    }
    CHECK(port_out.pdat.presponse.pbits.popcode == COMP_DATA &&
          port_out.pdat.presponse.pbits.psrc_uid == RAM_ID &&
          port_out.pdat.presponse.pbits.ptgt_uid == HOME_ID &&
          port_out.pdat.presponse.pbits.ptxn_uid == UINT64_C(1792));
    CHECK(port_out.pdat.presponse.pbits.pdata_uid == data_id &&
          port_out.pdat.presponse.pbits.pbyte_uenable == UINT64_C(65535) &&
          port_out.pdat.presponse.pbits.pdata == wide(data));
    response_data_in.pready = UINT64_C(1);
    tick_model();
    response_data_in.pready = UINT64_C(0);
  }
}

void run_case() {
  reset = 1;
  requests_in = {};
  responses_in = {};
  request_data_in = {};
  response_data_in = {};
  tick_model();
  reset = UINT64_C(0);

  write_line();

  issue_request(READ_NO_SNP, UINT64_C(512), UINT64_C(2147483648), UINT64_C(6),
                UINT64_C(1792));
  accept_read_beat(UINT64_C(0), UINT64_C(0));
  accept_read_beat(UINT64_C(1),
                   ((uint128(UINT64_C(1229782938247303441)) << 64) |
                    UINT64_C(1229782938247303441)));
  accept_read_beat(UINT64_C(2),
                   ((uint128(UINT64_C(2459565876494606882)) << 64) |
                    UINT64_C(2459565876494606882)));
  accept_read_beat(UINT64_C(3),
                   ((uint128(UINT64_C(3689348814741910323)) << 64) |
                    UINT64_C(3689348814741910323)));

  issue_request(READ_NO_SNP, UINT64_C(513), UINT64_C(2147483680), UINT64_C(5),
                UINT64_C(1792));
  accept_read_beat(UINT64_C(2),
                   ((uint128(UINT64_C(2459565876494606882)) << 64) |
                    UINT64_C(2459565876494606882)));
  accept_read_beat(UINT64_C(3),
                   ((uint128(UINT64_C(3689348814741910323)) << 64) |
                    UINT64_C(3689348814741910323)));

  write_word(UINT64_C(2147483664), UINT64_C(514),
             ((uint128(UINT64_C(18364758544493064720)) << 64) |
              UINT64_C(81985529216486895)));
  issue_request(READ_NO_SNP, UINT64_C(515), UINT64_C(2147483664), UINT64_C(4),
                UINT64_C(1792));
  accept_read_beat(UINT64_C(1),
                   ((uint128(UINT64_C(18364758544493064720)) << 64) |
                    UINT64_C(81985529216486895)));
}

int main() { return run_test(run_case); }
