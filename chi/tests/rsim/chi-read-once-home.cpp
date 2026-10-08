// Verifies ReadOnce allocation and coherent-observation policy at an inclusive
// Home.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
using CHIReqFlit =
    std::remove_cvref_t<decltype(port_in.prequester.prequests.pbits)>;
using CHIRspFlit = std::remove_cvref_t<
    decltype(port_in.prequester.prequester_uresponses.pbits)>;
using CHIDatFlit =
    std::remove_cvref_t<decltype(port_in.prequester.prequest_udata.pbits)>;
using dat_t = std::remove_cvref_t<decltype(port_in.prequester.prequest_udata)>;
using rsp_t =
    std::remove_cvref_t<decltype(port_in.prequester.prequester_uresponses)>;
using req_t = std::remove_cvref_t<decltype(port_in.prequester.prequests)>;
inline auto &requester_requests_in = port_in.prequester.prequests;
inline auto &requester_responses_in = port_in.prequester.prequester_uresponses;
inline auto &request_data_in = port_in.prequester.prequest_udata;
inline auto &requester_responses_ready_in = port_in.prequester.presponses;
inline auto &response_data_ready_in = port_in.prequester.presponse_udata;
inline auto &snoops_ready_in = port_in.prequester.psnoops;
inline auto &subordinate_responses_in = port_in.psubordinate.prsp;
inline auto &subordinate_requests_ready_in = port_in.psubordinate.preq;
inline auto &subordinate_data_ready_in = port_in.psubordinate.pdat.prequest;
inline auto &subordinate_data_in = port_in.psubordinate.pdat.presponse;
std::uint16_t response_dbid{};
std::uint8_t active_qos{};
constexpr std::uint8_t READ_CLEAN = UINT64_C(2);
constexpr std::uint8_t READ_ONCE = UINT64_C(3);
constexpr std::uint8_t READ_NO_SNP = UINT64_C(4);
constexpr std::uint8_t READ_UNIQUE = UINT64_C(7);
constexpr std::uint8_t SNP_RESP = UINT64_C(1);
constexpr std::uint8_t COMP_ACK = UINT64_C(2);
constexpr std::uint8_t SNP_ONCE = UINT64_C(3);
constexpr std::uint8_t SNP_RESP_DATA_PTL = UINT64_C(5);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t READER_ID = UINT64_C(1);
constexpr std::uint8_t OWNER_ID = UINT64_C(3);
constexpr std::uint8_t HOME_ID = UINT64_C(5);
constexpr std::uint8_t MEMORY_ID = UINT64_C(9);
constexpr std::uint64_t LINE0 = UINT64_C(2147483648);
constexpr std::uint64_t LINE2 = UINT64_C(2147484160);
constexpr std::uint64_t STREAM0 = UINT64_C(2147484672);
constexpr std::uint64_t STREAM1 = UINT64_C(2147485184);
constexpr std::uint64_t STREAM2 = UINT64_C(2147485696);

void send_read(std::uint64_t address, std::uint8_t opcode, std::uint8_t source,
               bool allocate = 0) {
  {
    requester_requests_in.pbits = {};
    requester_requests_in.pbits.psrc_uid = source;
    requester_requests_in.pbits.ptgt_uid = HOME_ID;
    requester_requests_in.pbits.popcode = opcode;
    requester_requests_in.pbits.paddress = address;
    requester_requests_in.pbits.psize_uor_unum_ureq = UINT64_C(6);
    requester_requests_in.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
        source;
    requester_requests_in.pbits.preturn_utxn_uid_uor_ustash_ulpid =
        UINT64_C(1620);
    requester_requests_in.pbits.ptrace_utag = UINT64_C(1);
    requester_requests_in.pbits.pexp_ucomp_uack = UINT64_C(1);
    requester_requests_in.pbits.pmem_uattr.pallocate = allocate;
    requester_requests_in.pbits.pqos = UINT64_C(10);
    active_qos = requester_requests_in.pbits.pqos;
    requester_requests_in.pvalid = UINT64_C(1);
    eval();
    {
      int waited;
      waited = 0;
      while (!port_out.prequester.prequests.pready && waited < 16) {
        tick_model();
        waited++;
      }
      CHECK(port_out.prequester.prequests.pready);
    }
    tick_model();
    requester_requests_in = {};
  }
}

void accept_memory_read(std::uint64_t address, int stall_cycles = 0) {
  CHIReqFlit held_request;
  {
    while (!port_out.psubordinate.preq.pvalid) {
      CHECK(!port_out.prequester.psnoops.pvalid);
      tick_model();
    }
    eval();
    CHECK(port_out.psubordinate.preq.pbits.paddress == address &&
          port_out.psubordinate.preq.pbits.psize_uor_unum_ureq == UINT64_C(6) &&
          port_out.psubordinate.preq.pbits.popcode == READ_NO_SNP);
    CHECK(!port_out.prequester.psnoops.pvalid);
    held_request = port_out.psubordinate.preq.pbits;
    for (unsigned repeat_index = 0; repeat_index < (stall_cycles);
         ++repeat_index) {
      tick_model();
      CHECK(port_out.psubordinate.preq.pvalid &&
            same_request(port_out.psubordinate.preq.pbits, held_request));
    }
    subordinate_requests_ready_in.pready = UINT64_C(1);
    tick_model();
    subordinate_requests_ready_in = {};
  }
}

void return_fill(std::uint8_t payload_base, std::uint8_t first_error = 0) {
  {
    for (int packet = 0; packet < 4; packet++) {
      subordinate_data_in.pbits = {};
      subordinate_data_in.pbits.popcode = COMP_DATA;
      subordinate_data_in.pbits.presp_uerr = packet == 0 ? first_error : 0;
      subordinate_data_in.pbits.psrc_uid = MEMORY_ID;
      subordinate_data_in.pbits.ptgt_uid = HOME_ID;
      subordinate_data_in.pbits.pdata_uid = slice(packet, 1, 0);
      subordinate_data_in.pbits.pbyte_uenable = UINT64_C(65535);
      subordinate_data_in.pbits.pdata =
          wide(std::uint64_t(payload_base + slice(packet, 7, 0)));
      subordinate_data_in.pvalid = UINT64_C(1);
      eval();
      CHECK(port_out.psubordinate.pdat.presponse.pready);
      tick_model();
      subordinate_data_in = {};
    }
  }
}

void accept_line(std::uint8_t payload_base, std::uint8_t state,
                 std::uint8_t error = 0, int initial_stall = 0,
                 bool require_quiet = 0) {
  CHIDatFlit held_packet;
  {
    while (!port_out.prequester.presponse_udata.pvalid) {
      if (require_quiet)
        CHECK(!port_out.prequester.psnoops.pvalid &&
              !port_out.psubordinate.preq.pvalid);
      tick_model();
    }
    held_packet = port_out.prequester.presponse_udata.pbits;
    if (require_quiet)
      CHECK(!port_out.prequester.psnoops.pvalid &&
            !port_out.psubordinate.preq.pvalid);
    for (unsigned repeat_index = 0; repeat_index < (initial_stall);
         ++repeat_index) {
      tick_model();
      CHECK(port_out.prequester.presponse_udata.pvalid &&
            same_data(port_out.prequester.presponse_udata.pbits, held_packet));
      if (require_quiet)
        CHECK(!port_out.prequester.psnoops.pvalid &&
              !port_out.psubordinate.preq.pvalid);
    }
    for (int packet = 0; packet < 4; packet++) {
      eval();
      if (require_quiet)
        CHECK(!port_out.prequester.psnoops.pvalid &&
              !port_out.psubordinate.preq.pvalid);
      CHECK(port_out.prequester.presponse_udata.pvalid &&
            port_out.prequester.presponse_udata.pbits.popcode == COMP_DATA &&
            port_out.prequester.presponse_udata.pbits.pdata_uid ==
                slice(packet, 1, 0) &&
            port_out.prequester.presponse_udata.pbits.pdata ==
                wide(std::uint64_t(payload_base + slice(packet, 7, 0))) &&
            port_out.prequester.presponse_udata.pbits.presp_uerr == error);
      if (error == 0)
        CHECK(port_out.prequester.presponse_udata.pbits.presp == state);
      if (packet == 0)
        response_dbid = slice(
            port_out.prequester.presponse_udata.pbits.pdbid_uor_umecid, 11, 0);
      response_data_ready_in.pready = UINT64_C(1);
      tick_model();
      response_data_ready_in = {};
    }
  }
}

void delayed_comp_ack(std::uint8_t source, int delay_cycles = 0) {
  {

    requester_requests_in.pbits.paddress = LINE0 + UINT64_C(256);
    eval();
    for (unsigned repeat_index = 0; repeat_index < (delay_cycles);
         ++repeat_index) {
      CHECK(port_out.prequester.prequests.pready);
      tick_model();
    }
    requester_requests_in = {};
    requester_responses_in.pbits = {};
    requester_responses_in.pbits.popcode = COMP_ACK;
    requester_responses_in.pbits.ptxn_uid = response_dbid;
    requester_responses_in.pbits.psrc_uid = source;
    requester_responses_in.pbits.ptgt_uid = HOME_ID;
    requester_responses_in.pbits.pqos = active_qos;
    requester_responses_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.prequester_uresponses.pready);
    tick_model();
    requester_responses_in = {};

    tick_model();
    CHECK(port_out.prequester.prequests.pready);
  }
}

void clean_snoop(int stall_cycles = 0) {
  std::remove_cvref_t<decltype(port_out.prequester.psnoops.pbits)> held_snoop;
  {
    while (!port_out.prequester.psnoops.pvalid)
      tick_model();
    eval();
    CHECK(port_out.prequester.psnoops.pbits.ptarget_uid == OWNER_ID &&
          port_out.prequester.psnoops.pbits.pflit.popcode == SNP_ONCE);
    held_snoop = port_out.prequester.psnoops.pbits;
    for (unsigned repeat_index = 0; repeat_index < (stall_cycles);
         ++repeat_index) {
      tick_model();
      CHECK(port_out.prequester.psnoops.pvalid &&
            port_out.prequester.psnoops.pbits.ptarget_uid ==
                held_snoop.ptarget_uid &&
            same_snoop(port_out.prequester.psnoops.pbits.pflit,
                       held_snoop.pflit));
    }
    snoops_ready_in.pready = UINT64_C(1);
    tick_model();
    snoops_ready_in = {};
    requester_responses_in.pbits = {};
    requester_responses_in.pbits.popcode = SNP_RESP;
    requester_responses_in.pbits.psrc_uid = OWNER_ID;
    requester_responses_in.pbits.ptgt_uid = HOME_ID;
    requester_responses_in.pbits.ptxn_uid = held_snoop.pflit.ptxn_uid;
    requester_responses_in.pbits.presp = UINT64_C(1);
    requester_responses_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.prequester_uresponses.pready);
    tick_model();
    requester_responses_in = {};
  }
}

void dirty_snoop(std::uint8_t payload_base) {
  std::uint16_t snoop_txn_id;
  {
    while (!port_out.prequester.psnoops.pvalid)
      tick_model();
    eval();
    CHECK(port_out.prequester.psnoops.pbits.ptarget_uid == OWNER_ID &&
          port_out.prequester.psnoops.pbits.pflit.popcode == SNP_ONCE);
    snoop_txn_id = port_out.prequester.psnoops.pbits.pflit.ptxn_uid;
    snoops_ready_in.pready = UINT64_C(1);
    tick_model();
    snoops_ready_in = {};
    for (int packet = 0; packet < 4; packet++) {
      request_data_in.pbits = {};
      request_data_in.pbits.popcode = SNP_RESP_DATA_PTL;
      request_data_in.pbits.psrc_uid = OWNER_ID;
      request_data_in.pbits.ptgt_uid = HOME_ID;
      request_data_in.pbits.ptxn_uid = snoop_txn_id;
      request_data_in.pbits.pdata_uid = slice(packet, 1, 0);
      request_data_in.pbits.pbyte_uenable = UINT64_C(65535);
      request_data_in.pbits.presp = UINT64_C(4);
      request_data_in.pbits.pdata =
          wide(std::uint64_t(payload_base + slice(packet, 7, 0)));
      request_data_in.pvalid = UINT64_C(1);
      eval();
      CHECK(port_out.prequester.prequest_udata.pready);
      tick_model();
      request_data_in = {};
    }
  }
}

void fill_read(std::uint64_t address, std::uint8_t payload_base, bool allocate,
               std::uint8_t error = 0, int request_stall = 0,
               int response_stall = 0) {
  {
    send_read(address, READ_ONCE, READER_ID, allocate);
    accept_memory_read(address, request_stall);
    return_fill(payload_base, error);
    accept_line(payload_base, UINT64_C(0), error, response_stall);
    delayed_comp_ack(READER_ID, 2);
  }
}

void run_case() {
  reset = UINT64_C(1);
  identity = {.phome_unode_uid = HOME_ID,
              .psubordinate_unode_uid = MEMORY_ID,
              .pservice_ubase = LINE0};
  requester_requests_in = {};
  requester_responses_in = {};
  request_data_in = {};
  requester_responses_ready_in = {};
  response_data_ready_in = {};
  snoops_ready_in = {};
  subordinate_responses_in = {};
  subordinate_requests_ready_in = {};
  subordinate_data_ready_in = {};
  subordinate_data_in = {};
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick_model();
  reset = UINT64_C(0);
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
    eval();
    CHECK(!port_out.prequester.prequests.pready);
    tick_model();
  }
  tick_model();

  fill_read(LINE0, UINT64_C(16), UINT64_C(1), 0, 3, 3);

  send_read(LINE0, READ_CLEAN, OWNER_ID);
  accept_line(UINT64_C(16), UINT64_C(1), 0, 0, 1);
  delayed_comp_ack(OWNER_ID);
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
    send_read(LINE0, READ_ONCE, READER_ID);
    accept_line(UINT64_C(16), UINT64_C(0), 0, 2, 1);
    delayed_comp_ack(READER_ID);
  }

  send_read(LINE0, READ_UNIQUE, OWNER_ID);
  accept_line(UINT64_C(16), UINT64_C(2), 0, 0, 1);
  delayed_comp_ack(OWNER_ID);
  send_read(LINE0, READ_ONCE, READER_ID);
  dirty_snoop(UINT64_C(64));
  accept_line(UINT64_C(64), UINT64_C(0));
  delayed_comp_ack(READER_ID);
  send_read(LINE0, READ_ONCE, READER_ID);
  accept_line(UINT64_C(64), UINT64_C(0), 0, 2, 1);
  delayed_comp_ack(READER_ID);

  fill_read(LINE2, UINT64_C(96), UINT64_C(1));
  fill_read(STREAM0, UINT64_C(128), UINT64_C(0), UINT64_C(2));
  fill_read(STREAM0, UINT64_C(144), UINT64_C(0));
  fill_read(STREAM1, UINT64_C(160), UINT64_C(0));
  fill_read(STREAM2, UINT64_C(176), UINT64_C(0));

  send_read(LINE0, READ_ONCE, READER_ID);
  accept_line(UINT64_C(64), UINT64_C(0), 0, 0, 1);
  delayed_comp_ack(READER_ID);
  send_read(LINE2, READ_ONCE, READER_ID);
  accept_line(UINT64_C(96), UINT64_C(0), 0, 0, 1);
  delayed_comp_ack(READER_ID);
}

int main() { return run_test(run_case); }
