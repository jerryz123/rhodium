// Simulates coherent reads, interventions, and one-request full-line copyback
// through CHIHNF.
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
int COPYBACK_ERROR = 0;
constexpr std::uint8_t READ_CLEAN = UINT64_C(2);
constexpr std::uint8_t READ_NO_SNP = UINT64_C(4);
constexpr std::uint8_t WRITE_UNIQUE_PTL = UINT64_C(24);
constexpr std::uint8_t WRITE_NO_SNP_PTL = UINT64_C(28);
constexpr std::uint8_t WRITE_NO_SNP_FULL = UINT64_C(29);
constexpr std::uint8_t SNP_RESP = UINT64_C(1);
constexpr std::uint8_t COMP_ACK = UINT64_C(2);
constexpr std::uint8_t COMP = UINT64_C(4);
constexpr std::uint8_t DBID_RESP = UINT64_C(6);
constexpr std::uint8_t SNP_CLEAN_INVALID = UINT64_C(9);
constexpr std::uint8_t SNP_CLEAN = UINT64_C(2);
constexpr std::uint8_t SNP_RESP_DATA = UINT64_C(1);
constexpr std::uint8_t NON_COPY_BACK_WRITE_DATA = UINT64_C(3);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t HTIF_ID = UINT64_C(1);
constexpr std::uint8_t INSTRUCTION_ID = UINT64_C(2);
constexpr std::uint8_t DATA_ID = UINT64_C(3);
constexpr std::uint8_t HOME_ID = UINT64_C(5);
constexpr std::uint8_t MEMORY_ID = UINT64_C(9);
constexpr std::uint16_t MEMORY_DBID = UINT64_C(85);
constexpr std::uint64_t LINE_ADDRESS = UINT64_C(2147483648);

void send_request(std::uint8_t source, std::uint8_t opcode, std::uint8_t size,
                  bool expect_comp_ack) {
  {
    requester_requests_in.pbits = {};
    requester_requests_in.pbits.psrc_uid = source;
    requester_requests_in.pbits.ptgt_uid = HOME_ID;
    requester_requests_in.pbits.popcode = opcode;
    requester_requests_in.pbits.paddress = LINE_ADDRESS;
    requester_requests_in.pbits.psize_uor_unum_ureq = size;
    requester_requests_in.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
        source;
    requester_requests_in.pbits.preturn_utxn_uid_uor_ustash_ulpid = UINT64_C(0);
    requester_requests_in.pbits.ptxn_uid = UINT64_C(0);
    requester_requests_in.pbits.pexp_ucomp_uack = expect_comp_ack;
    requester_requests_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.prequests.pready);
    tick_model();
    requester_requests_in = {};
  }
}

void accept_subordinate_request(std::uint8_t opcode) {
  {
    eval();
    CHECK(port_out.psubordinate.preq.pvalid);
    CHECK(port_out.psubordinate.preq.pbits.popcode == opcode &&
          port_out.psubordinate.preq.pbits.psrc_uid == HOME_ID &&
          port_out.psubordinate.preq.pbits.ptgt_uid == MEMORY_ID &&
          port_out.psubordinate.preq.pbits.ptxn_uid == UINT64_C(0));
    subordinate_requests_ready_in.pready = UINT64_C(1);
    tick_model();
    subordinate_requests_ready_in = {};
  }
}

void accept_snoop(std::uint8_t target, std::uint8_t opcode) {
  {
    snoops_ready_in.pready = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.psnoops.pvalid &&
          port_out.prequester.psnoops.pbits.ptarget_uid == target &&
          port_out.prequester.psnoops.pbits.pflit.popcode == opcode &&
          port_out.prequester.psnoops.pbits.pflit.psrc_uid == HOME_ID);
    tick_model();
    snoops_ready_in = {};
  }
}

void service_dirty_snoop_packet(std::uint8_t packet_id) {
  {
    request_data_in.pbits = {};
    request_data_in.pbits.popcode = SNP_RESP_DATA;
    request_data_in.pbits.psrc_uid = DATA_ID;
    request_data_in.pbits.ptgt_uid = HOME_ID;
    request_data_in.pbits.ptxn_uid = UINT64_C(0);
    request_data_in.pbits.pdata_uid = packet_id;
    request_data_in.pbits.pbyte_uenable = UINT64_C(65535);
    request_data_in.pbits.presp = UINT64_C(4);
    request_data_in.pbits.pdata = wide(std::uint64_t(packet_id));
    request_data_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.prequest_udata.pready);
    tick_model();
    request_data_in = {};

    accept_subordinate_request(WRITE_NO_SNP_FULL);
    subordinate_responses_in.pbits = {};
    subordinate_responses_in.pbits.popcode = DBID_RESP;
    subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
    subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
    subordinate_responses_in.pbits.ptxn_uid = UINT64_C(0);
    subordinate_responses_in.pbits.pdbid_uor_ugroup_uid = MEMORY_DBID;
    subordinate_responses_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.psubordinate.prsp.pready);
    tick_model();
    subordinate_responses_in = {};

    subordinate_data_ready_in.pready = UINT64_C(1);
    eval();
    CHECK(port_out.psubordinate.pdat.prequest.pvalid &&
          port_out.psubordinate.pdat.prequest.pbits.popcode ==
              NON_COPY_BACK_WRITE_DATA &&
          port_out.psubordinate.pdat.prequest.pbits.pdata_uid == packet_id &&
          port_out.psubordinate.pdat.prequest.pbits.pdata ==
              wide(std::uint64_t(packet_id)));
    tick_model();
    subordinate_data_ready_in = {};

    subordinate_responses_in.pbits = {};
    subordinate_responses_in.pbits.popcode = COMP;
    subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
    subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
    subordinate_responses_in.pbits.ptxn_uid = UINT64_C(0);
    subordinate_responses_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.psubordinate.prsp.pready);
    tick_model();
    subordinate_responses_in = {};
  }
}

void return_read_packet(std::uint8_t packet_id, std::uint8_t target_id,
                        std::uint8_t response) {
  {
    subordinate_data_in.pbits = {};
    subordinate_data_in.pbits.popcode = COMP_DATA;
    subordinate_data_in.pbits.psrc_uid = MEMORY_ID;
    subordinate_data_in.pbits.ptgt_uid = HOME_ID;
    subordinate_data_in.pbits.ptxn_uid = UINT64_C(0);
    subordinate_data_in.pbits.pdata_uid = packet_id;
    subordinate_data_in.pbits.pbyte_uenable = UINT64_C(65535);
    subordinate_data_in.pbits.pdata = wide(std::uint64_t(packet_id));
    subordinate_data_in.pvalid = UINT64_C(1);
    response_data_ready_in.pready = UINT64_C(1);
    eval();
    CHECK(port_out.psubordinate.pdat.presponse.pready &&
          port_out.prequester.presponse_udata.pvalid);
    CHECK(port_out.prequester.presponse_udata.pbits.psrc_uid == HOME_ID &&
          port_out.prequester.presponse_udata.pbits.ptgt_uid == target_id &&
          port_out.prequester.presponse_udata.pbits.ptxn_uid == UINT64_C(0) &&
          port_out.prequester.presponse_udata.pbits.pdata_uid == packet_id &&
          port_out.prequester.presponse_udata.pbits.presp == response);
    tick_model();
    subordinate_data_in = {};
    response_data_ready_in = {};
  }
}

void send_requester_write_packet(std::uint8_t packet_id) {
  {
    request_data_in.pbits = {};
    request_data_in.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
    request_data_in.pbits.psrc_uid = HTIF_ID;
    request_data_in.pbits.ptgt_uid = HOME_ID;
    request_data_in.pbits.ptxn_uid = UINT64_C(0);
    request_data_in.pbits.pdata_uid = packet_id;
    request_data_in.pbits.pbyte_uenable = UINT64_C(65535);
    request_data_in.pbits.pdata = wide(std::uint64_t(packet_id));
    request_data_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.prequest_udata.pready);
    tick_model();
    request_data_in = {};
  }
}

void accept_subordinate_write_packet(std::uint8_t packet_id) {
  {
    subordinate_data_ready_in.pready = UINT64_C(1);
    eval();
    CHECK(port_out.psubordinate.pdat.prequest.pvalid &&
          port_out.psubordinate.pdat.prequest.pbits.popcode ==
              NON_COPY_BACK_WRITE_DATA &&
          port_out.psubordinate.pdat.prequest.pbits.ptxn_uid == MEMORY_DBID &&
          port_out.psubordinate.pdat.prequest.pbits.pdata_uid == packet_id &&
          port_out.psubordinate.pdat.prequest.pbits.pdata ==
              wide(std::uint64_t(packet_id)));
    tick_model();
    subordinate_data_ready_in = {};
  }
}

void run_case() {
  reset = UINT64_C(1);
  identity = {.phome_unode_uid = HOME_ID,
              .psubordinate_unode_uid = MEMORY_ID,
              .pservice_ubase = LINE_ADDRESS};
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
  tick_model();
  reset = UINT64_C(0);

  send_request(INSTRUCTION_ID, READ_CLEAN, UINT64_C(6), UINT64_C(1));
  accept_snoop(DATA_ID, SNP_CLEAN);
  service_dirty_snoop_packet(UINT64_C(0));
  service_dirty_snoop_packet(UINT64_C(1));
  service_dirty_snoop_packet(UINT64_C(2));
  service_dirty_snoop_packet(UINT64_C(3));
  tick_model();
  accept_subordinate_request(READ_NO_SNP);
  return_read_packet(UINT64_C(0), INSTRUCTION_ID, UINT64_C(1));
  return_read_packet(UINT64_C(1), INSTRUCTION_ID, UINT64_C(1));
  return_read_packet(UINT64_C(2), INSTRUCTION_ID, UINT64_C(1));
  return_read_packet(UINT64_C(3), INSTRUCTION_ID, UINT64_C(1));
  CHECK(!port_out.prequester.prequests.pready);
  requester_responses_in.pbits = {};
  requester_responses_in.pbits.popcode = COMP_ACK;
  requester_responses_in.pbits.psrc_uid = INSTRUCTION_ID;
  requester_responses_in.pbits.ptgt_uid = HOME_ID;
  requester_responses_in.pbits.ptxn_uid = UINT64_C(0);
  requester_responses_in.pvalid = UINT64_C(1);
  eval();
  CHECK(port_out.prequester.prequester_uresponses.pready);
  tick_model();
  requester_responses_in = {};
  CHECK(port_out.prequester.prequests.pready);

  send_request(HTIF_ID, UINT64_C(3), UINT64_C(6), UINT64_C(0));
  accept_snoop(INSTRUCTION_ID, UINT64_C(3));
  requester_responses_in.pbits = {};
  requester_responses_in.pbits.popcode = SNP_RESP;
  requester_responses_in.pbits.psrc_uid = INSTRUCTION_ID;
  requester_responses_in.pbits.ptgt_uid = HOME_ID;
  requester_responses_in.pbits.presp = UINT64_C(2);
  requester_responses_in.pvalid = 1;
  tick_model();
  requester_responses_in = {};
  accept_snoop(DATA_ID, UINT64_C(3));
  for (int packet = 0; packet < 4; packet++)
    service_dirty_snoop_packet(((packet)&low_mask(2)));
  tick_model();
  accept_subordinate_request(READ_NO_SNP);
  for (int packet = 0; packet < 4; packet++)
    return_read_packet(((packet)&low_mask(2)), HTIF_ID, UINT64_C(0));
  CHECK(port_out.prequester.prequests.pready);

  send_request(HTIF_ID, READ_NO_SNP, UINT64_C(2), UINT64_C(0));
  accept_subordinate_request(READ_NO_SNP);
  return_read_packet(UINT64_C(0), HTIF_ID, UINT64_C(0));
  CHECK(port_out.prequester.prequests.pready);

  for (int writer = 0; writer < 2; writer++) {
    send_request(writer == 0 ? DATA_ID : HTIF_ID, WRITE_UNIQUE_PTL, UINT64_C(3),
                 UINT64_C(0));
    requester_responses_ready_in.pready = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.presponses.pvalid &&
          port_out.prequester.presponses.pbits.popcode == DBID_RESP &&
          port_out.prequester.presponses.pbits.ptgt_uid ==
              (writer == 0 ? DATA_ID : HTIF_ID));
    tick_model();
    requester_responses_ready_in = {};

    request_data_in.pbits = {};
    request_data_in.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
    request_data_in.pbits.psrc_uid = writer == 0 ? DATA_ID : HTIF_ID;
    request_data_in.pbits.ptgt_uid = HOME_ID;
    request_data_in.pbits.ptxn_uid = UINT64_C(0);
    request_data_in.pbits.pbyte_uenable = UINT64_C(255);
    request_data_in.pbits.pdata = wide(UINT64_C(1234605616436508552));
    request_data_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.prequest_udata.pready);
    tick_model();
    request_data_in = {};

    accept_snoop(INSTRUCTION_ID, SNP_CLEAN_INVALID);

    requester_responses_in.pbits = {};
    requester_responses_in.pbits.popcode = SNP_RESP;
    requester_responses_in.pbits.psrc_uid = INSTRUCTION_ID;
    requester_responses_in.pbits.ptgt_uid = HOME_ID;
    requester_responses_in.pbits.ptxn_uid = UINT64_C(0);
    requester_responses_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.prequester_uresponses.pready);
    tick_model();
    requester_responses_in = {};
    tick_model();

    if (writer == 1) {
      accept_snoop(DATA_ID, SNP_CLEAN_INVALID);
      requester_responses_in.pbits = {};
      requester_responses_in.pbits.popcode = SNP_RESP;
      requester_responses_in.pbits.psrc_uid = DATA_ID;
      requester_responses_in.pbits.ptgt_uid = HOME_ID;
      requester_responses_in.pvalid = 1;
      eval();
      CHECK(port_out.prequester.prequester_uresponses.pready);
      tick_model();
      requester_responses_in = {};
      tick_model();
    }

    accept_subordinate_request(WRITE_NO_SNP_PTL);
    subordinate_responses_in.pbits = {};
    subordinate_responses_in.pbits.popcode = DBID_RESP;
    subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
    subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
    subordinate_responses_in.pbits.ptxn_uid = UINT64_C(0);
    subordinate_responses_in.pbits.pdbid_uor_ugroup_uid = MEMORY_DBID;
    subordinate_responses_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.psubordinate.prsp.pready);
    tick_model();
    subordinate_responses_in = {};

    subordinate_data_ready_in.pready = UINT64_C(1);
    eval();
    CHECK(port_out.psubordinate.pdat.prequest.pvalid &&
          port_out.psubordinate.pdat.prequest.pbits.psrc_uid == HOME_ID &&
          port_out.psubordinate.pdat.prequest.pbits.ptgt_uid == MEMORY_ID &&
          port_out.psubordinate.pdat.prequest.pbits.ptxn_uid == MEMORY_DBID &&
          port_out.psubordinate.pdat.prequest.pbits.pdata ==
              wide(UINT64_C(1234605616436508552)));
    tick_model();
    subordinate_data_ready_in = {};

    subordinate_responses_in.pbits = {};
    subordinate_responses_in.pbits.popcode = COMP;
    subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
    subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
    subordinate_responses_in.pbits.ptxn_uid = UINT64_C(0);
    subordinate_responses_in.pbits.pdbid_uor_ugroup_uid = MEMORY_DBID;
    subordinate_responses_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.psubordinate.prsp.pready);
    tick_model();
    subordinate_responses_in = {};

    requester_responses_ready_in.pready = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.presponses.pvalid &&
          port_out.prequester.presponses.pbits.popcode == COMP &&
          port_out.prequester.presponses.pbits.psrc_uid == HOME_ID &&
          port_out.prequester.presponses.pbits.ptgt_uid ==
              (writer == 0 ? DATA_ID : HTIF_ID));
    tick_model();
    CHECK(port_out.prequester.prequests.pready);
    requester_responses_ready_in = {};
  }

  send_request(HTIF_ID, WRITE_NO_SNP_FULL, UINT64_C(6), UINT64_C(0));
  requester_responses_ready_in.pready = UINT64_C(1);
  eval();
  CHECK(port_out.prequester.presponses.pvalid &&
        port_out.prequester.presponses.pbits.popcode == DBID_RESP);
  tick_model();
  requester_responses_ready_in = {};

  send_requester_write_packet(UINT64_C(2));
  send_requester_write_packet(UINT64_C(0));
  send_requester_write_packet(UINT64_C(3));
  send_requester_write_packet(UINT64_C(1));
  tick_model();
  accept_subordinate_request(WRITE_NO_SNP_FULL);

  subordinate_responses_in.pbits = {};
  subordinate_responses_in.pbits.popcode = DBID_RESP;
  subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
  subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
  subordinate_responses_in.pbits.ptxn_uid = UINT64_C(0);
  subordinate_responses_in.pbits.pdbid_uor_ugroup_uid = MEMORY_DBID;
  subordinate_responses_in.pvalid = UINT64_C(1);
  eval();
  CHECK(port_out.psubordinate.prsp.pready);
  tick_model();
  subordinate_responses_in = {};

  accept_subordinate_write_packet(UINT64_C(0));
  accept_subordinate_write_packet(UINT64_C(1));
  accept_subordinate_write_packet(UINT64_C(2));
  accept_subordinate_write_packet(UINT64_C(3));

  subordinate_responses_in.pbits = {};
  subordinate_responses_in.pbits.popcode = COMP;
  subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
  subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
  subordinate_responses_in.pbits.ptxn_uid = UINT64_C(0);
  subordinate_responses_in.pbits.pdbid_uor_ugroup_uid = MEMORY_DBID;
  subordinate_responses_in.pvalid = UINT64_C(1);
  eval();
  CHECK(port_out.psubordinate.prsp.pready);
  tick_model();
  subordinate_responses_in = {};

  requester_responses_ready_in.pready = UINT64_C(1);
  eval();
  CHECK(port_out.prequester.presponses.pvalid &&
        port_out.prequester.presponses.pbits.popcode == COMP);
  tick_model();
  CHECK(port_out.prequester.prequests.pready);

  for (int state_index = 0; state_index < 5; state_index++) {
    std::uint8_t state;
    state = state_index == 0   ? UINT64_C(6)
            : state_index == 1 ? UINT64_C(7)
                               : ((state_index - 2) & low_mask(3));
    requester_responses_ready_in = {};
    send_request(DATA_ID, UINT64_C(27), UINT64_C(6), 0);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      CHECK(port_out.prequester.presponses.pvalid &&
            port_out.prequester.presponses.pbits.popcode == UINT64_C(5) &&
            !port_out.prequester.prequest_udata.pready);
      tick_model();
    }
    requester_responses_ready_in.pready = 1;
    tick_model();
    requester_responses_ready_in = {};
    for (int packet = 3; packet >= 0; packet--) {
      request_data_in = {};
      request_data_in.pvalid = 1;
      request_data_in.pbits.popcode = UINT64_C(2);
      request_data_in.pbits.psrc_uid = DATA_ID;
      request_data_in.pbits.ptgt_uid = HOME_ID;
      request_data_in.pbits.pdata_uid = ((packet)&low_mask(2));
      request_data_in.pbits.presp = state;
      request_data_in.pbits.pbyte_uenable = state == 0 ? 0 : UINT64_MAX;
      request_data_in.pbits.pdata = wide(state == 0 ? 0 : packet);
      eval();
      CHECK(port_out.prequester.prequest_udata.pready);
      tick_model();
      request_data_in = {};
    }
    if (((state >> (2)) & 1)) {
      for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index) {
        CHECK(!port_out.prequester.presponses.pvalid &&
              !port_out.prequester.psnoops.pvalid &&
              port_out.psubordinate.preq.pvalid &&
              port_out.psubordinate.preq.pbits.psize_uor_unum_ureq == 6);
        tick_model();
      }
      accept_subordinate_request(WRITE_NO_SNP_FULL);
      subordinate_responses_in = {};
      subordinate_responses_in.pvalid = 1;
      subordinate_responses_in.pbits.popcode = DBID_RESP;
      subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
      subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
      subordinate_responses_in.pbits.pdbid_uor_ugroup_uid = MEMORY_DBID;
      tick_model();
      subordinate_responses_in = {};
      for (int packet = 0; packet < 4; packet++) {
        CHIDatFlit held;
        held = port_out.psubordinate.pdat.prequest.pbits;
        for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
          CHECK(port_out.psubordinate.pdat.prequest.pvalid &&
                same_data(port_out.psubordinate.pdat.prequest.pbits, held) &&
                held.presp == 0 && held.pbyte_uenable == UINT16_MAX);
          tick_model();
        }
        accept_subordinate_write_packet(((packet)&low_mask(2)));
      }
      subordinate_responses_in = {};
      subordinate_responses_in.pvalid = 1;
      subordinate_responses_in.pbits.popcode = COMP;
      subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
      subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
      subordinate_responses_in.pbits.presp_uerr =
          COPYBACK_ERROR ? UINT64_C(2) : UINT64_C(0);
      tick_model();
      subordinate_responses_in = {};
    }
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      CHECK(port_out.prequester.prequests.pready &&
            !port_out.prequester.presponses.pvalid &&
            !port_out.prequester.psnoops.pvalid &&
            !port_out.psubordinate.preq.pvalid);
      tick_model();
    }
  }
}

int main() {
  return run_test([] {
    run_case();
    dut = Model{};
    COPYBACK_ERROR = 1;
    expect_failure("chi_hnf_copyback_backing_response_ok", run_case);
  });
}
