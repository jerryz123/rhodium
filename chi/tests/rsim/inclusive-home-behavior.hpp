// Checks inclusive Home fills, eviction, ownership, copyback, and masked data
// against independent packets.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
uint128 repeated_byte(unsigned value) {
  uint128 result = 0;
  for (unsigned b = 0; b < 16; ++b)
    result |= uint128(value & 255) << (8 * b);
  return result;
}
void set_byte(uint128 &value, unsigned index, unsigned byte) {
  value = (value & ~(uint128(255) << (8 * index))) |
          (uint128(byte & 255) << (8 * index));
}
uint128 expected_line[4]{};
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
std::uint16_t latest_memory_txn{};
std::uint16_t latest_requester_dbid{};
std::uint16_t first_comp_ack_dbid{};
std::uint16_t second_comp_ack_dbid{};
std::uint16_t first_memory_txn{};
std::uint16_t second_memory_txn{};
std::uint16_t victim_memory_txn{};
std::uint16_t second_victim_memory_txn{};
CHIRspFlit stalled_response{};
int victim_request_wait_cycles{};
bool second_victim_request_seen{};
CHIReqFlit active_request{};
int line0_packets{};
int line1_packets{};
int INVALID_CASE = 0;
constexpr std::uint8_t READ_NO_SNP = UINT64_C(4);
constexpr std::uint8_t READ_ONCE = UINT64_C(3);
constexpr std::uint8_t WRITE_NO_SNP_FULL = UINT64_C(29);
constexpr std::uint8_t WRITE_NO_SNP_PTL = UINT64_C(28);
constexpr std::uint8_t WRITE_UNIQUE_PTL = UINT64_C(24);
constexpr std::uint8_t SNP_RESP = UINT64_C(1);
constexpr std::uint8_t COMP_ACK = UINT64_C(2);
constexpr std::uint8_t COMP = UINT64_C(4);
constexpr std::uint8_t DBID_RESP = UINT64_C(6);
constexpr std::uint8_t SNP_CLEAN_INVALID = UINT64_C(9);
constexpr std::uint8_t SNP_RESP_DATA_PTL = UINT64_C(5);
constexpr std::uint8_t NON_COPY_BACK_WRITE_DATA = UINT64_C(3);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t HTIF_ID = UINT64_C(1);
constexpr std::uint8_t INSTRUCTION_ID = UINT64_C(2);
constexpr std::uint8_t DATA_ID = UINT64_C(3);
constexpr std::uint8_t HOME_ID = UINT64_C(5);
constexpr std::uint8_t MEMORY_ID = UINT64_C(9);
constexpr std::uint16_t MEMORY_DBID = UINT64_C(85);
constexpr std::uint64_t LINE0 = UINT64_C(2147483648);
constexpr std::uint64_t LINE1 = UINT64_C(2147483904);
constexpr std::uint64_t LINE2 = UINT64_C(2147484160);
constexpr std::uint64_t LINE3 = UINT64_C(2147484672);
constexpr std::uint64_t SECOND_SET_PEER = UINT64_C(2147484416);
constexpr std::uint64_t SECOND_SET_REPLACEMENT = UINT64_C(2147484928);
constexpr unsigned __int128 PARTIAL_DATA =
    ((uint128(UINT64_C(17357386176853808775)) << 64) |
     UINT64_C(8676565436284608015));
constexpr std::uint16_t PARTIAL_MASK = UINT64_C(42330);
constexpr std::uint64_t SNOOP_MASKS = UINT64_C(18446603342036926465);

// Optional trace observers sample the same public edge as the protocol oracle.
inline void (*home_before_tick)() = [] {};
inline void (*home_after_tick)() = [] {};
inline void (*home_expect_backing)(unsigned) = [](unsigned) {};

// Capture public transaction identities before advancing the model.
void home_tick() {
  eval();
  if (!reset) {
    if (requester_requests_in.pvalid && port_out.prequester.prequests.pready)
      active_request = requester_requests_in.pbits;
    if (port_out.psubordinate.preq.pvalid &&
        subordinate_requests_ready_in.pready)
      latest_memory_txn = port_out.psubordinate.preq.pbits.ptxn_uid;
    if (port_out.prequester.presponses.pvalid &&
        requester_responses_ready_in.pready &&
        port_out.prequester.presponses.pbits.popcode == DBID_RESP)
      latest_requester_dbid =
          port_out.prequester.presponses.pbits.pdbid_uor_ugroup_uid;
  }
  home_before_tick();
  tick_model();
  home_after_tick();
}
void finish_directory_sweep() {

  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
    eval();
    CHECK(!port_out.prequester.prequests.pready &&
          !port_out.prequester.prequest_udata.pready &&
          !port_out.prequester.prequester_uresponses.pready &&
          !port_out.psubordinate.prsp.pready &&
          !port_out.psubordinate.pdat.presponse.pready &&
          !port_out.prequester.presponses.pvalid &&
          !port_out.prequester.presponse_udata.pvalid &&
          !port_out.prequester.psnoops.pvalid &&
          !port_out.psubordinate.preq.pvalid &&
          !port_out.psubordinate.pdat.prequest.pvalid);
    home_tick();
  }
}

void send_request(std::uint64_t address, std::uint8_t opcode,
                  std::uint8_t request_size = UINT64_C(6),
                  std::uint8_t source = HTIF_ID, bool exp_comp_ack = 0,
                  bool allocate = 0, std::uint16_t request_txn = UINT64_C(0),
                  std::uint16_t return_txn = UINT64_C(1620)) {
  {
    requester_requests_in.pbits = {};
    requester_requests_in.pbits.psrc_uid = source;
    requester_requests_in.pbits.ptgt_uid = HOME_ID;
    requester_requests_in.pbits.popcode = opcode;
    requester_requests_in.pbits.paddress = address;
    requester_requests_in.pbits.psize_uor_unum_ureq = request_size;
    requester_requests_in.pbits.ptxn_uid = request_txn;
    requester_requests_in.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
        source;
    requester_requests_in.pbits.preturn_utxn_uid_uor_ustash_ulpid = return_txn;
    requester_requests_in.pbits.ptrace_utag = 1;
    requester_requests_in.pbits.pexp_ucomp_uack = exp_comp_ack;
    requester_requests_in.pbits.pmem_uattr.pallocate = allocate;
    requester_requests_in.pbits.pqos = UINT64_C(10);
    requester_requests_in.pvalid = UINT64_C(1);
    eval();
    {
      int waited;
      waited = 0;
      while (!port_out.prequester.prequests.pready && waited < 16) {
        home_tick();
        waited++;
      }
      CHECK(port_out.prequester.prequests.pready);
    }
    home_tick();
    requester_requests_in = {};
  }
}

void accept_memory_request(std::uint64_t address, std::uint8_t opcode,
                           std::uint32_t owner_age = 0) {
  {
    while (!port_out.psubordinate.preq.pvalid)
      home_tick();
    eval();
    CHECK(port_out.psubordinate.preq.pvalid &&
          port_out.psubordinate.preq.pbits.paddress == address &&
          port_out.psubordinate.preq.pbits.psize_uor_unum_ureq == UINT64_C(6) &&
          port_out.psubordinate.preq.pbits.popcode == opcode);

    home_expect_backing(owner_age);
    subordinate_requests_ready_in.pready = UINT64_C(1);
    home_tick();
    subordinate_requests_ready_in = {};
  }
}

void accept_memory_request_transaction(std::uint64_t address,
                                       std::uint8_t opcode,
                                       std::uint16_t &transaction,
                                       std::uint32_t owner_age = 0) {
  {
    while (!port_out.psubordinate.preq.pvalid)
      home_tick();
    eval();
    CHECK(port_out.psubordinate.preq.pbits.paddress == address &&
          port_out.psubordinate.preq.pbits.psize_uor_unum_ureq == UINT64_C(6) &&
          port_out.psubordinate.preq.pbits.popcode == opcode);

    transaction = port_out.psubordinate.preq.pbits.ptxn_uid;
    home_expect_backing(owner_age);
    subordinate_requests_ready_in.pready = UINT64_C(1);
    home_tick();
    subordinate_requests_ready_in = {};
  }
}

void accept_memory_request_slot(std::uint64_t address,
                                std::uint16_t &transaction,
                                std::uint32_t owner_age = 0) {
  {
    while (!port_out.psubordinate.preq.pvalid)
      home_tick();
    eval();
    CHECK(
        port_out.psubordinate.preq.pbits.paddress == address &&
        port_out.psubordinate.preq.pbits.popcode == READ_NO_SNP &&
        port_out.psubordinate.preq.pbits.ptxn_uid ==
            port_out.psubordinate.preq.pbits.preturn_utxn_uid_uor_ustash_ulpid);

    transaction = port_out.psubordinate.preq.pbits.ptxn_uid;
    home_expect_backing(owner_age);
    subordinate_requests_ready_in.pready = UINT64_C(1);
    home_tick();
    subordinate_requests_ready_in = {};
  }
}

void return_fill_packet(std::uint8_t packet_id, std::uint8_t payload,
                        std::uint8_t error = 0,
                        std::uint16_t transaction = latest_memory_txn) {
  {
    subordinate_data_in.pbits = {};
    subordinate_data_in.pbits.popcode = COMP_DATA;
    subordinate_data_in.pbits.presp_uerr = error;
    subordinate_data_in.pbits.psrc_uid = MEMORY_ID;
    subordinate_data_in.pbits.ptgt_uid = HOME_ID;
    subordinate_data_in.pbits.ptxn_uid = transaction;
    subordinate_data_in.pbits.pdata_uid = packet_id;
    subordinate_data_in.pbits.pbyte_uenable = UINT64_C(65535);
    subordinate_data_in.pbits.pdata = wide(payload);
    subordinate_data_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.psubordinate.pdat.presponse.pready);
    home_tick();
    subordinate_data_in = {};
  }
}

void accept_routed_packet(std::uint16_t transaction, std::uint8_t packet_id,
                          std::uint8_t payload, std::uint8_t target = HTIF_ID) {
  {
    while (!port_out.prequester.presponse_udata.pvalid)
      home_tick();
    eval();
    CHECK(port_out.prequester.presponse_udata.pbits.popcode == COMP_DATA &&
          port_out.prequester.presponse_udata.pbits.ptxn_uid == transaction &&
          port_out.prequester.presponse_udata.pbits.ptgt_uid == target &&
          port_out.prequester.presponse_udata.pbits.pdata_uid == packet_id &&
          port_out.prequester.presponse_udata.pbits.pdata == wide(payload));
    response_data_ready_in.pready = UINT64_C(1);
    home_tick();
    response_data_ready_in = {};
  }
}

void accept_cached_packet(std::uint8_t packet_id, unsigned __int128 payload,
                          std::uint8_t error = 0) {
  CHIDatFlit expected_packet;
  {
    while (!port_out.prequester.presponse_udata.pvalid)
      home_tick();
    expected_packet = {};
    expected_packet.pdata = wide(payload);
    expected_packet.presp_uerr = error;
    for (int b = 0; b < 16; b++)
      expected_packet.pbyte_uenable |=
          unsigned(active_request.psize_uor_unum_ureq >= 4 ||
                   (b >= int(slice(active_request.paddress, 3, 0)) &&
                    b < int(slice(active_request.paddress, 3, 0)) +
                            (1 << active_request.psize_uor_unum_ureq)))
          << b;
    expected_packet.pdata_uid = packet_id;
    expected_packet.ptrace_utag = active_request.ptrace_utag;
    expected_packet.pqos = active_request.pqos;
    expected_packet.popcode = COMP_DATA;
    expected_packet.phome_unid_uor_upbha_uor_umismatched_umecid = HOME_ID;
    expected_packet.psrc_uid = HOME_ID;
    expected_packet.ptgt_uid =
        active_request.preturn_unid_uor_ustash_unid_uor_udata_utarget;
    expected_packet.ptxn_uid = active_request.preturn_utxn_uid_uor_ustash_ulpid;
    if (active_request.pexp_ucomp_uack) {
      if (packet_id == 0)
        response_dbid = slice(
            port_out.prequester.presponse_udata.pbits.pdbid_uor_umecid, 11, 0);
      expected_packet.pdbid_uor_umecid =
          packet_id == 0
              ? slice(
                    port_out.prequester.presponse_udata.pbits.pdbid_uor_umecid,
                    11, 0)
              : response_dbid;
    }
    if (error == 0 && (active_request.popcode == UINT64_C(2) ||
                       active_request.popcode == UINT64_C(7)))
      expected_packet.presp =
          active_request.popcode == UINT64_C(7) ? UINT64_C(2) : UINT64_C(1);
    response_data_ready_in.pready = UINT64_C(1);
    eval();
    CHECK(
        same_data(port_out.prequester.presponse_udata.pbits, expected_packet));
    CHECK(port_out.prequester.presponse_udata.pvalid &&
          port_out.prequester.presponse_udata.pbits.pdata_uid == packet_id &&
          port_out.prequester.presponse_udata.pbits.pdata == wide(payload) &&
          port_out.prequester.presponse_udata.pbits.presp_uerr == error);
    home_tick();
    response_data_ready_in = {};
  }
}

void send_comp_ack(std::uint8_t source, std::uint16_t dbid) {
  {
    requester_responses_in = {};
    requester_responses_in.pbits.popcode = COMP_ACK;
    requester_responses_in.pbits.ptxn_uid = dbid;
    requester_responses_in.pbits.psrc_uid = source;
    requester_responses_in.pbits.ptgt_uid = HOME_ID;
    requester_responses_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.prequester_uresponses.pready);
    home_tick();
    requester_responses_in = {};
  }
}

void fill_and_return(std::uint64_t address, std::uint8_t payload_base) {
  {
    accept_memory_request(address, READ_NO_SNP);
    return_fill_packet(UINT64_C(0), payload_base + 0);
    return_fill_packet(UINT64_C(1), payload_base + 1);
    return_fill_packet(UINT64_C(2), payload_base + 2);
    return_fill_packet(UINT64_C(3), payload_base + 3);
    accept_cached_packet(UINT64_C(0),
                         ((payload_base)&low_mask(128)) + UINT64_C(0));
    accept_cached_packet(UINT64_C(1),
                         ((payload_base)&low_mask(128)) + UINT64_C(1));
    accept_cached_packet(UINT64_C(2),
                         ((payload_base)&low_mask(128)) + UINT64_C(2));
    accept_cached_packet(UINT64_C(3),
                         ((payload_base)&low_mask(128)) + UINT64_C(3));
  }
}

void send_write_packet(std::uint8_t packet_id, unsigned __int128 payload,
                       std::uint16_t byte_mask = UINT64_C(65535)) {
  {
    request_data_in.pbits = {};
    request_data_in.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
    request_data_in.pbits.ptxn_uid = latest_requester_dbid;
    request_data_in.pbits.psrc_uid = HTIF_ID;
    request_data_in.pbits.ptgt_uid = HOME_ID;
    request_data_in.pbits.pdata_uid = packet_id;
    request_data_in.pbits.pbyte_uenable = byte_mask;
    request_data_in.pbits.pdata = wide(payload);
    request_data_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.prequest_udata.pready);
    home_tick();
    request_data_in = {};
  }
}

void clean_snoop(std::uint8_t target, std::uint8_t opcode = SNP_CLEAN_INVALID,
                 std::uint8_t state = 0, std::uint8_t error = 0) {
  std::uint16_t snoop_txn_id;
  {
    while (!port_out.prequester.psnoops.pvalid)
      home_tick();
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      CHECK(port_out.prequester.psnoops.pvalid &&
            port_out.prequester.psnoops.pbits.ptarget_uid == target);
      home_tick();
    }
    snoops_ready_in.pready = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.psnoops.pvalid &&
          port_out.prequester.psnoops.pbits.ptarget_uid == target &&
          port_out.prequester.psnoops.pbits.pflit.popcode == opcode);
    snoop_txn_id = port_out.prequester.psnoops.pbits.pflit.ptxn_uid;
    home_tick();
    snoops_ready_in = {};
    requester_responses_in.pbits = {};
    requester_responses_in.pbits.popcode = SNP_RESP;
    requester_responses_in.pbits.psrc_uid = target;
    requester_responses_in.pbits.ptgt_uid = HOME_ID;
    requester_responses_in.pbits.ptxn_uid = snoop_txn_id;
    requester_responses_in.pbits.presp = state;
    requester_responses_in.pbits.presp_uerr = error;
    requester_responses_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.prequester_uresponses.pready);
    home_tick();
    requester_responses_in = {};
  }
}

void dirty_snoop(std::uint8_t target, std::uint8_t payload_base,
                 std::uint8_t opcode = SNP_CLEAN_INVALID,
                 std::uint8_t state = UINT64_C(4), bool early_error = 0) {
  std::uint16_t snoop_txn_id;
  {
    while (!port_out.prequester.psnoops.pvalid)
      home_tick();
    snoops_ready_in.pready = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.psnoops.pvalid &&
          port_out.prequester.psnoops.pbits.ptarget_uid == target &&
          port_out.prequester.psnoops.pbits.pflit.popcode == opcode);
    snoop_txn_id = port_out.prequester.psnoops.pbits.pflit.ptxn_uid;
    home_tick();
    snoops_ready_in = {};
    for (int packet = 0; packet < 4; packet++) {
      request_data_in.pbits = {};
      request_data_in.pbits.popcode = SNP_RESP_DATA_PTL;
      request_data_in.pbits.psrc_uid = target;
      request_data_in.pbits.ptgt_uid = HOME_ID;
      request_data_in.pbits.ptxn_uid = snoop_txn_id;
      request_data_in.pbits.pdata_uid = slice(packet, 1, 0);
      request_data_in.pbits.pbyte_uenable =
          ((SNOOP_MASKS >> (packet * 16)) & 65535);
      request_data_in.pbits.presp = state;
      request_data_in.pbits.presp_uerr =
          early_error && packet == 0 ? UINT64_C(2) : 0;
      request_data_in.pbits.pdata =
          wide(repeated_byte(payload_base + slice(packet, 7, 0)));
      request_data_in.pvalid = UINT64_C(1);
      eval();
      CHECK(port_out.prequester.prequest_udata.pready);
      home_tick();
      request_data_in = {};
      if (packet != 3)
        for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
          CHECK(!port_out.prequester.psnoops.pvalid &&
                !port_out.prequester.presponse_udata.pvalid &&
                !port_out.prequester.prequests.pready);
          home_tick();
        }
    }
  }
}

void clean_snoops_back_to_back(std::uint8_t first_target,
                               std::uint8_t second_target, std::uint8_t opcode,
                               std::uint8_t state) {
  std::uint16_t first_txn_id;
  std::uint16_t second_txn_id;
  {
    while (!port_out.prequester.psnoops.pvalid)
      home_tick();
    snoops_ready_in.pready = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.psnoops.pbits.ptarget_uid == first_target &&
          port_out.prequester.psnoops.pbits.pflit.popcode == opcode);
    first_txn_id = port_out.prequester.psnoops.pbits.pflit.ptxn_uid;
    home_tick();
    eval();
    CHECK(port_out.prequester.psnoops.pvalid &&
          port_out.prequester.psnoops.pbits.ptarget_uid == second_target &&
          port_out.prequester.psnoops.pbits.pflit.popcode == opcode);
    second_txn_id = port_out.prequester.psnoops.pbits.pflit.ptxn_uid;
    CHECK(first_txn_id != second_txn_id);
    home_tick();
    snoops_ready_in = {};

    requester_responses_in.pbits = {};
    requester_responses_in.pbits.popcode = SNP_RESP;
    requester_responses_in.pbits.psrc_uid = second_target;
    requester_responses_in.pbits.ptgt_uid = HOME_ID;
    requester_responses_in.pbits.ptxn_uid = second_txn_id;
    requester_responses_in.pbits.presp = state;
    requester_responses_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.prequester.prequester_uresponses.pready);
    home_tick();

    requester_responses_in.pbits.psrc_uid = first_target;
    requester_responses_in.pbits.ptxn_uid = first_txn_id;
    eval();
    CHECK(port_out.prequester.prequester_uresponses.pready);
    home_tick();
    requester_responses_in = {};
  }
}

void finish_cached(std::uint8_t error = 0) {
  while (!port_out.prequester.presponse_udata.pvalid) {
    CHECK(!port_out.prequester.psnoops.pvalid &&
          !port_out.psubordinate.preq.pvalid);
    home_tick();
  }
  for (int packet = 0; packet < 4; packet++)
    accept_cached_packet(((packet)&low_mask(2)), expected_line[packet], error);
}

void finish_maintenance() {
  while (!port_out.prequester.presponses.pvalid)
    home_tick();
  CHECK(port_out.prequester.presponses.pbits.popcode == COMP &&
        port_out.prequester.presponses.pbits.presp_uerr == 0);
  requester_responses_ready_in.pready = UINT64_C(1);
  home_tick();
  requester_responses_ready_in = {};
}

void accept_victim_packet(std::uint8_t packet_id, unsigned __int128 payload) {
  CHIDatFlit expected_packet;
  {
    while (!port_out.psubordinate.pdat.prequest.pvalid)
      home_tick();
    expected_packet = {};
    expected_packet.pdata = wide(payload);
    expected_packet.pbyte_uenable = UINT64_MAX;
    expected_packet.pdata_uid = packet_id;
    expected_packet.ptrace_utag = active_request.ptrace_utag;
    expected_packet.pqos = active_request.pqos;
    expected_packet.popcode = NON_COPY_BACK_WRITE_DATA;
    expected_packet.pdbid_uor_umecid = MEMORY_DBID;
    expected_packet.phome_unid_uor_upbha_uor_umismatched_umecid = HOME_ID;
    expected_packet.psrc_uid = HOME_ID;
    expected_packet.ptgt_uid = MEMORY_ID;
    expected_packet.ptxn_uid = MEMORY_DBID;
    subordinate_data_ready_in.pready = UINT64_C(1);
    eval();
    CHECK(
        same_data(port_out.psubordinate.pdat.prequest.pbits, expected_packet));
    CHECK(port_out.psubordinate.pdat.prequest.pvalid &&
          port_out.psubordinate.pdat.prequest.pbits.popcode ==
              NON_COPY_BACK_WRITE_DATA &&
          port_out.psubordinate.pdat.prequest.pbits.ptxn_uid == MEMORY_DBID &&
          port_out.psubordinate.pdat.prequest.pbits.pdata_uid == packet_id &&
          port_out.psubordinate.pdat.prequest.pbits.pdata == wide(payload));
    home_tick();
    subordinate_data_ready_in = {};
  }
}

void copyback(std::uint64_t address, std::uint8_t state) {
  CHIRspFlit grant;
  {
    send_request(address, UINT64_C(27), UINT64_C(6), DATA_ID);
    eval();
    grant = port_out.prequester.presponses.pbits;
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      CHECK(port_out.prequester.presponses.pvalid &&
            grant.popcode == UINT64_C(5) && grant.ptgt_uid == DATA_ID &&
            grant.presp == 0 &&
            same_response(port_out.prequester.presponses.pbits, grant) &&
            !port_out.prequester.prequest_udata.pready);
      home_tick();
    }
    requester_responses_ready_in.pready = 1;
    home_tick();
    requester_responses_ready_in = {};

    for (int packet = 3; packet >= 0; packet--) {
      request_data_in = {};
      request_data_in.pvalid = 1;
      request_data_in.pbits.popcode = UINT64_C(2);
      request_data_in.pbits.psrc_uid = DATA_ID;
      request_data_in.pbits.ptgt_uid = HOME_ID;
      request_data_in.pbits.ptxn_uid = grant.pdbid_uor_ugroup_uid;
      request_data_in.pbits.pdata_uid = ((packet)&low_mask(2));
      request_data_in.pbits.presp = state;
      request_data_in.pbits.pbyte_uenable = state == 0 ? 0 : UINT64_MAX;
      request_data_in.pbits.pdata = wide(
          state == 0 ? 0 : UINT64_C(3735879680) + ((packet)&low_mask(128)));
      if (INVALID_CASE == 1)
        request_data_in.pbits.pbyte_uenable = UINT64_C(255);
      if (INVALID_CASE == 2 && packet == 2)
        request_data_in.pbits.pdata_uid = 3;
      if (INVALID_CASE == 3 && packet == 2)
        request_data_in.pbits.presp = UINT64_C(7);
      eval();
      CHECK(port_out.prequester.prequest_udata.pready);
      home_tick();
      request_data_in = {};
      for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
        CHECK(!port_out.prequester.presponses.pvalid &&
              !port_out.prequester.psnoops.pvalid &&
              !port_out.psubordinate.preq.pvalid);
        home_tick();
      }
    }
    while (!port_out.prequester.prequests.pready) {
      CHECK(!port_out.prequester.presponses.pvalid &&
            !port_out.prequester.psnoops.pvalid &&
            !port_out.psubordinate.preq.pvalid);
      home_tick();
    }
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
  home_tick();
  reset = UINT64_C(0);
  finish_directory_sweep();

  requester_requests_in.pbits = {};
  requester_requests_in.pbits.psrc_uid = DATA_ID;
  requester_requests_in.pbits.ptgt_uid = HOME_ID;
  requester_requests_in.pbits.popcode = WRITE_UNIQUE_PTL;
  requester_requests_in.pbits.paddress = LINE0;
  requester_requests_in.pbits.psize_uor_unum_ureq = UINT64_C(2);
  requester_requests_in.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
      DATA_ID;
  requester_requests_in.pvalid = UINT64_C(1);
  eval();
  CHECK(port_out.prequester.prequests.pready);
  home_tick();
  requester_requests_in = {};
  requester_responses_ready_in.pready = UINT64_C(1);
  eval();
  CHECK(port_out.prequester.presponses.pvalid &&
        port_out.prequester.presponses.pbits.popcode == DBID_RESP &&
        port_out.prequester.presponses.pbits.ptgt_uid == DATA_ID);
  home_tick();
  requester_responses_ready_in = {};
  reset = UINT64_C(1);
  home_tick();
  reset = UINT64_C(0);
  finish_directory_sweep();

  send_request(LINE0, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 0, UINT64_C(1793),
               UINT64_C(1809));
  accept_memory_request_slot(LINE0, first_memory_txn);
  send_request(LINE1, WRITE_UNIQUE_PTL, UINT64_C(2), DATA_ID, 0, 0,
               UINT64_C(1794), UINT64_C(1810));
  while (!port_out.prequester.presponses.pvalid)
    home_tick();
  stalled_response = port_out.prequester.presponses.pbits;
  CHECK(stalled_response.popcode == DBID_RESP);
  home_tick();
  subordinate_data_in.pbits = {};
  subordinate_data_in.pbits.popcode = COMP_DATA;
  subordinate_data_in.pbits.psrc_uid = MEMORY_ID;
  subordinate_data_in.pbits.ptgt_uid = HOME_ID;
  subordinate_data_in.pbits.ptxn_uid = first_memory_txn;
  subordinate_data_in.pbits.pdata_uid = 0;
  subordinate_data_in.pbits.pbyte_uenable = UINT64_C(65535);
  subordinate_data_in.pbits.pdata = wide(UINT64_C(90));
  subordinate_data_in.pvalid = UINT64_C(1);
  eval();
  CHECK(port_out.psubordinate.pdat.presponse.pready);
  CHECK(port_out.prequester.presponses.pvalid &&
        same_response(port_out.prequester.presponses.pbits, stalled_response));
  home_tick();
  subordinate_data_in = {};
  requester_responses_ready_in.pready = UINT64_C(1);
  eval();
  CHECK(port_out.prequester.presponses.pvalid &&
        same_response(port_out.prequester.presponses.pbits, stalled_response));
  home_tick();
  requester_responses_ready_in = {};
  reset = UINT64_C(1);
  home_tick();
  reset = UINT64_C(0);
  finish_directory_sweep();

  send_request(LINE0, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 1);
  home_tick();
  fill_and_return(LINE0, UINT64_C(80));
  send_request(LINE1, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 0, UINT64_C(48),
               UINT64_C(816));
  accept_memory_request_slot(LINE1, first_memory_txn);
  requester_requests_in.pbits = {};
  requester_requests_in.pbits.psrc_uid = HTIF_ID;
  requester_requests_in.pbits.ptgt_uid = HOME_ID;
  requester_requests_in.pbits.popcode = READ_ONCE;
  requester_requests_in.pbits.paddress = LINE1 + UINT64_C(512);
  requester_requests_in.pbits.psize_uor_unum_ureq = UINT64_C(6);
  requester_requests_in.pvalid = UINT64_C(1);
  eval();
  CHECK(!port_out.prequester.prequests.pready);
  requester_requests_in = {};
  send_request(LINE0, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 1, UINT64_C(64),
               UINT64_C(1088));
  while (!port_out.prequester.presponse_udata.pvalid) {
    CHECK(!port_out.prequester.psnoops.pvalid &&
          !port_out.psubordinate.preq.pvalid);
    home_tick();
  }
  subordinate_data_in.pbits = {};
  subordinate_data_in.pbits.popcode = COMP_DATA;
  subordinate_data_in.pbits.psrc_uid = MEMORY_ID;
  subordinate_data_in.pbits.ptgt_uid = HOME_ID;
  subordinate_data_in.pbits.ptxn_uid = first_memory_txn;
  subordinate_data_in.pbits.pdata_uid = 0;
  subordinate_data_in.pbits.pbyte_uenable = UINT64_C(65535);
  subordinate_data_in.pbits.pdata = wide(UINT64_C(96));
  subordinate_data_in.pvalid = UINT64_C(1);
  eval();
  CHECK(port_out.psubordinate.pdat.presponse.pready &&
        port_out.prequester.presponse_udata.pvalid &&
        port_out.prequester.presponse_udata.pbits.ptxn_uid == UINT64_C(1088) &&
        port_out.prequester.presponse_udata.pbits.pdata_uid == 0 &&
        port_out.prequester.presponse_udata.pbits.pdata == wide(UINT64_C(80)));
  home_tick();
  subordinate_data_in = {};
  for (int packet = 0; packet < 4; packet++)
    accept_cached_packet(((packet)&low_mask(2)),
                         UINT64_C(80) + ((packet)&low_mask(128)));
  for (int packet = 1; packet < 4; packet++)
    return_fill_packet(((packet)&low_mask(2)),
                       UINT64_C(96) + ((packet)&low_mask(8)), 0,
                       first_memory_txn);
  for (int packet = 0; packet < 4; packet++)
    accept_routed_packet(UINT64_C(816), ((packet)&low_mask(2)),
                         UINT64_C(96) + ((packet)&low_mask(8)));
  reset = UINT64_C(1);
  home_tick();
  reset = UINT64_C(0);
  finish_directory_sweep();

  send_request(LINE0, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 1, UINT64_C(80),
               UINT64_C(336));
  accept_memory_request_slot(LINE0, first_memory_txn);
  send_request(LINE1, UINT64_C(7), UINT64_C(6), DATA_ID, 0, 0, UINT64_C(96),
               UINT64_C(352));
  accept_memory_request_slot(LINE1, second_memory_txn);
  for (int packet = 0; packet < 4; packet++)
    return_fill_packet(((packet)&low_mask(2)),
                       UINT64_C(112) + ((packet)&low_mask(8)), 0,
                       second_memory_txn);
  for (int packet = 0; packet < 4; packet++)
    accept_routed_packet(UINT64_C(352), ((packet)&low_mask(2)),
                         UINT64_C(112) + ((packet)&low_mask(8)), DATA_ID);
  for (int packet = 0; packet < 4; packet++)
    return_fill_packet(((packet)&low_mask(2)),
                       UINT64_C(96) + ((packet)&low_mask(8)), 0,
                       first_memory_txn);
  for (int packet = 0; packet < 4; packet++)
    accept_routed_packet(UINT64_C(336), ((packet)&low_mask(2)),
                         UINT64_C(96) + ((packet)&low_mask(8)));
  copyback(LINE1, UINT64_C(6));
  reset = UINT64_C(1);
  home_tick();
  reset = UINT64_C(0);
  finish_directory_sweep();

  send_request(LINE0, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 0, UINT64_C(16),
               UINT64_C(272));
  requester_requests_in.pbits = {};
  requester_requests_in.pbits.psrc_uid = HTIF_ID;
  requester_requests_in.pbits.ptgt_uid = HOME_ID;
  requester_requests_in.pbits.popcode = READ_ONCE;
  requester_requests_in.pbits.paddress = LINE2;
  requester_requests_in.pbits.psize_uor_unum_ureq = UINT64_C(6);
  requester_requests_in.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
      HTIF_ID;
  requester_requests_in.pvalid = UINT64_C(1);
  eval();
  CHECK(!port_out.prequester.prequests.pready);
  requester_requests_in = {};
  send_request(LINE1, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 0, UINT64_C(32),
               UINT64_C(544));
  accept_memory_request_slot(LINE0, first_memory_txn, 1);
  accept_memory_request_slot(LINE1, second_memory_txn);
  CHECK(first_memory_txn != second_memory_txn);
  for (int packet = 0; packet < 4; packet++)
    return_fill_packet(((packet)&low_mask(2)),
                       UINT64_C(32) + ((packet)&low_mask(8)), 0,
                       second_memory_txn);
  while (!port_out.prequester.presponse_udata.pvalid)
    home_tick();
  home_tick();
  subordinate_data_in.pbits = {};
  subordinate_data_in.pbits.popcode = COMP_DATA;
  subordinate_data_in.pbits.psrc_uid = MEMORY_ID;
  subordinate_data_in.pbits.ptgt_uid = HOME_ID;
  subordinate_data_in.pbits.ptxn_uid = first_memory_txn;
  subordinate_data_in.pbits.pdata_uid = 0;
  subordinate_data_in.pbits.pbyte_uenable = UINT64_C(65535);
  subordinate_data_in.pbits.pdata = wide(UINT64_C(16));
  subordinate_data_in.pvalid = UINT64_C(1);
  eval();
  CHECK(port_out.psubordinate.pdat.presponse.pready &&
        port_out.prequester.presponse_udata.pvalid &&
        port_out.prequester.presponse_udata.pbits.ptxn_uid == UINT64_C(544) &&
        port_out.prequester.presponse_udata.pbits.pdata_uid == 0 &&
        port_out.prequester.presponse_udata.pbits.pdata == wide(UINT64_C(32)));
  home_tick();
  subordinate_data_in = {};
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
    eval();
    CHECK(port_out.prequester.presponse_udata.pvalid &&
          port_out.prequester.presponse_udata.pbits.ptxn_uid == UINT64_C(544) &&
          port_out.prequester.presponse_udata.pbits.pdata_uid == 0 &&
          port_out.prequester.presponse_udata.pbits.pdata ==
              wide(UINT64_C(32)));
    home_tick();
  }
  for (int packet = 1; packet < 4; packet++)
    return_fill_packet(((packet)&low_mask(2)),
                       UINT64_C(16) + ((packet)&low_mask(8)), 0,
                       first_memory_txn);

  for (int packet = 0; packet < 4; packet++) {
    accept_routed_packet(UINT64_C(544), ((packet)&low_mask(2)),
                         UINT64_C(32) + ((packet)&low_mask(8)));
    accept_routed_packet(UINT64_C(272), ((packet)&low_mask(2)),
                         UINT64_C(16) + ((packet)&low_mask(8)));
  }
  reset = UINT64_C(1);
  home_tick();
  reset = UINT64_C(0);
  finish_directory_sweep();

  send_request(LINE0, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 1);
  home_tick();
  fill_and_return(LINE0, UINT64_C(145));
  send_request(LINE1, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 1);
  home_tick();
  fill_and_return(LINE1, UINT64_C(161));
  send_request(LINE0, UINT64_C(7), UINT64_C(6), DATA_ID);
  for (int packet = 0; packet < 4; packet++)
    accept_routed_packet(UINT64_C(1620), ((packet)&low_mask(2)),
                         UINT64_C(145) + ((packet)&low_mask(8)), DATA_ID);
  send_request(LINE0, UINT64_C(7), UINT64_C(6), DATA_ID, 0, 0, UINT64_C(2049),
               UINT64_C(2065));
  while (!port_out.prequester.presponse_udata.pvalid)
    home_tick();
  send_request(LINE1, UINT64_C(7), UINT64_C(6), INSTRUCTION_ID, 0, 0,
               UINT64_C(2050), UINT64_C(2082));
  line0_packets = 0;
  line1_packets = 0;
  for (int beat = 0; beat < 8; beat++) {
    while (!port_out.prequester.presponse_udata.pvalid)
      home_tick();
    if (port_out.prequester.presponse_udata.pbits.ptgt_uid == DATA_ID) {
      CHECK(port_out.prequester.presponse_udata.pbits.ptxn_uid ==
                UINT64_C(2065) &&
            port_out.prequester.presponse_udata.pbits.pdata_uid ==
                ((line0_packets)&low_mask(2)) &&
            port_out.prequester.presponse_udata.pbits.pdata ==
                wide(((UINT64_C(145)) + ((line0_packets)&low_mask(8))) &
                     low_mask(128)));
      line0_packets++;
    } else {
      CHECK(port_out.prequester.presponse_udata.pbits.ptgt_uid ==
                INSTRUCTION_ID &&
            port_out.prequester.presponse_udata.pbits.ptxn_uid ==
                UINT64_C(2082) &&
            port_out.prequester.presponse_udata.pbits.pdata_uid ==
                ((line1_packets)&low_mask(2)) &&
            port_out.prequester.presponse_udata.pbits.pdata ==
                wide(((UINT64_C(161)) + ((line1_packets)&low_mask(8))) &
                     low_mask(128)));
      line1_packets++;
    }
    response_data_ready_in.pready = UINT64_C(1);
    home_tick();
    response_data_ready_in = {};
  }
  CHECK(line0_packets == 4 && line1_packets == 4);
  send_request(LINE0, READ_ONCE);
  while (!port_out.prequester.psnoops.pvalid &&
         !port_out.prequester.presponse_udata.pvalid)
    home_tick();
  CHECK(port_out.prequester.psnoops.pvalid &&
        port_out.prequester.psnoops.pbits.ptarget_uid == DATA_ID);
  reset = UINT64_C(1);
  home_tick();
  reset = UINT64_C(0);
  finish_directory_sweep();

  send_request(LINE0, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 1);
  home_tick();
  fill_and_return(LINE0, UINT64_C(8));
  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(8) + ((packet)&low_mask(128));
  send_request(LINE0, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 1);
  finish_cached();

  send_request(LINE2, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 1);
  home_tick();
  fill_and_return(LINE2, UINT64_C(24));
  send_request(LINE0, UINT64_C(2), UINT64_C(6), DATA_ID);
  finish_cached();

  send_request(LINE0, READ_ONCE);
  finish_cached();

  send_request(LINE3, READ_ONCE);
  while (!port_out.psubordinate.preq.pvalid &&
         !port_out.prequester.psnoops.pvalid)
    home_tick();
  eval();
  CHECK(!port_out.prequester.psnoops.pvalid &&
        port_out.psubordinate.preq.pvalid);
  fill_and_return(LINE3, UINT64_C(40));
  send_request(LINE0, READ_ONCE);
  finish_cached();

  send_request(LINE3, READ_ONCE);
  home_tick();
  fill_and_return(LINE3, UINT64_C(56));

  reset = UINT64_C(1);
  home_tick();
  reset = UINT64_C(0);
  finish_directory_sweep();

  send_request(LINE0, READ_NO_SNP);
  home_tick();
  fill_and_return(LINE0, UINT64_C(16));

  send_request(LINE0, READ_NO_SNP);
  while (!port_out.prequester.presponse_udata.pvalid &&
         !port_out.prequester.psnoops.pvalid &&
         !port_out.psubordinate.preq.pvalid)
    home_tick();
  CHECK(port_out.prequester.presponse_udata.pvalid &&
        !port_out.prequester.psnoops.pvalid &&
        !port_out.psubordinate.preq.pvalid);
  accept_cached_packet(UINT64_C(0), UINT64_C(16));
  accept_cached_packet(UINT64_C(1), UINT64_C(17));
  accept_cached_packet(UINT64_C(2), UINT64_C(18));
  accept_cached_packet(UINT64_C(3), UINT64_C(19));
  CHECK(!port_out.psubordinate.preq.pvalid);

  send_request(LINE0 + UINT64_C(16), READ_NO_SNP, UINT64_C(4));
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    home_tick();
  accept_cached_packet(UINT64_C(1), UINT64_C(17));
  send_request(LINE0 + UINT64_C(32), READ_NO_SNP, UINT64_C(5));
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    home_tick();
  accept_cached_packet(UINT64_C(2), UINT64_C(18));
  accept_cached_packet(UINT64_C(3), UINT64_C(19));

  send_request(LINE0, WRITE_NO_SNP_FULL);
  requester_responses_ready_in.pready = UINT64_C(1);
  eval();
  CHECK(port_out.prequester.presponses.pvalid &&
        port_out.prequester.presponses.pbits.popcode == DBID_RESP);
  home_tick();
  requester_responses_ready_in = {};
  send_write_packet(UINT64_C(2), UINT64_C(130));
  send_write_packet(UINT64_C(0), UINT64_C(128));
  send_write_packet(UINT64_C(3), UINT64_C(131));
  send_write_packet(UINT64_C(1), UINT64_C(129));
  home_tick();
  home_tick();
  home_tick();
  home_tick();
  requester_responses_ready_in.pready = UINT64_C(1);
  eval();
  CHECK(port_out.prequester.presponses.pvalid &&
        port_out.prequester.presponses.pbits.popcode == COMP);
  home_tick();
  requester_responses_ready_in = {};

  send_request(LINE1, READ_NO_SNP);
  home_tick();
  fill_and_return(LINE1, UINT64_C(32));

  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(32) + ((packet)&low_mask(128));
  send_request(LINE1 + UINT64_C(16), WRITE_NO_SNP_PTL, UINT64_C(4));
  requester_responses_ready_in.pready = UINT64_C(1);
  eval();
  CHECK(port_out.prequester.presponses.pvalid &&
        port_out.prequester.presponses.pbits.popcode == DBID_RESP);
  home_tick();
  requester_responses_ready_in = {};
  send_write_packet(UINT64_C(1), PARTIAL_DATA, PARTIAL_MASK);
  for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
    home_tick();
  requester_responses_ready_in.pready = UINT64_C(1);
  eval();
  CHECK(port_out.prequester.presponses.pvalid &&
        port_out.prequester.presponses.pbits.popcode == COMP);
  home_tick();
  requester_responses_ready_in = {};
  for (int byte_index = 0; byte_index < 16; byte_index++)
    if (((PARTIAL_MASK >> (byte_index)) & 1))
      set_byte(expected_line[1], (byte_index * 8) / 8,
               ((PARTIAL_DATA >> (byte_index * 8)) & 255));
  send_request(LINE1, READ_NO_SNP);
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    home_tick();
  for (int packet = 0; packet < 4; packet++)
    accept_cached_packet(slice(packet, 1, 0), expected_line[packet]);

  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(128) + ((packet)&low_mask(128));

  send_request(LINE2, READ_NO_SNP);
  home_tick();
  fill_and_return(LINE2, UINT64_C(48));

  send_request(LINE0, UINT64_C(7), UINT64_C(6), DATA_ID);
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    home_tick();
  for (int packet = 0; packet < 4; packet++)
    accept_cached_packet(slice(packet, 1, 0), expected_line[packet]);

  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(48) + ((packet)&low_mask(128));
  send_request(LINE2, READ_NO_SNP);
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    home_tick();
  for (int packet = 0; packet < 4; packet++)
    accept_cached_packet(slice(packet, 1, 0), expected_line[packet]);
  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(128) + ((packet)&low_mask(128));
  send_request(LINE3, READ_NO_SNP);
  home_tick();
  dirty_snoop(DATA_ID, UINT64_C(160));

  for (int packet = 0; packet < 4; packet++)
    for (int byte_index = 0; byte_index < 16; byte_index++)
      if (((SNOOP_MASKS >> (packet * 16 + byte_index)) & 1))
        set_byte(expected_line[packet], (byte_index * 8) / 8,
                 UINT64_C(160) + ((packet)&low_mask(8)));
  home_tick();
  home_tick();
  accept_memory_request_transaction(LINE0, WRITE_NO_SNP_FULL,
                                    victim_memory_txn);

  accept_memory_request_slot(LINE3, first_memory_txn);
  CHECK(victim_memory_txn != first_memory_txn);
  subordinate_responses_in.pbits = {};
  subordinate_responses_in.pbits.popcode = DBID_RESP;
  subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
  subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
  subordinate_responses_in.pbits.ptxn_uid = victim_memory_txn;
  subordinate_responses_in.pbits.pdbid_uor_ugroup_uid = MEMORY_DBID;
  if (INVALID_CASE == 4)
    subordinate_responses_in.pbits.presp_uerr = UINT64_C(2);
  subordinate_responses_in.pvalid = UINT64_C(1);
  eval();
  CHECK(port_out.psubordinate.prsp.pready);
  home_tick();
  subordinate_responses_in = {};
  for (int packet = 0; packet < 4; packet++)
    accept_victim_packet(slice(packet, 1, 0), expected_line[packet]);
  subordinate_responses_in.pbits = {};
  subordinate_responses_in.pbits.popcode = COMP;
  subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
  subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
  subordinate_responses_in.pbits.ptxn_uid = victim_memory_txn;
  subordinate_responses_in.pbits.pdbid_uor_ugroup_uid =
      INVALID_CASE == 5 ? MEMORY_DBID + 1 : MEMORY_DBID;
  return_fill_packet(UINT64_C(0), UINT64_C(64), UINT64_C(0), first_memory_txn);
  return_fill_packet(UINT64_C(1), UINT64_C(65), UINT64_C(0), first_memory_txn);
  return_fill_packet(UINT64_C(2), UINT64_C(66), UINT64_C(0), first_memory_txn);
  return_fill_packet(UINT64_C(3), UINT64_C(67), UINT64_C(0), first_memory_txn);
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    CHECK(!port_out.prequester.presponse_udata.pvalid);
    home_tick();
  }
  subordinate_responses_in.pvalid = UINT64_C(1);
  eval();
  CHECK(port_out.psubordinate.prsp.pready);
  home_tick();
  subordinate_responses_in = {};
  while (!port_out.prequester.presponse_udata.pvalid)
    home_tick();
  accept_cached_packet(UINT64_C(0), UINT64_C(64));
  accept_cached_packet(UINT64_C(1), UINT64_C(65));
  accept_cached_packet(UINT64_C(2), UINT64_C(66));
  accept_cached_packet(UINT64_C(3), UINT64_C(67));

  if (INVALID_CASE == 0) {

    reset = 1;
    home_tick();
    reset = 0;
    finish_directory_sweep();
    send_request(LINE0, READ_NO_SNP);
    home_tick();
    fill_and_return(LINE0, UINT64_C(16));
    send_request(LINE2, READ_NO_SNP);
    home_tick();
    fill_and_return(LINE2, UINT64_C(32));
    send_request(LINE1, READ_NO_SNP);
    home_tick();
    fill_and_return(LINE1, UINT64_C(48));
    send_request(SECOND_SET_PEER, READ_NO_SNP);
    home_tick();
    fill_and_return(SECOND_SET_PEER, UINT64_C(64));

    for (int packet = 0; packet < 4; packet++)
      expected_line[packet] = UINT64_C(16) + ((packet)&low_mask(128));
    send_request(LINE0, UINT64_C(7), UINT64_C(6), DATA_ID);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      home_tick();
    finish_cached();
    for (int packet = 0; packet < 4; packet++)
      expected_line[packet] = UINT64_C(32) + ((packet)&low_mask(128));
    send_request(LINE2, READ_NO_SNP);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      home_tick();
    finish_cached();
    for (int packet = 0; packet < 4; packet++)
      expected_line[packet] = UINT64_C(48) + ((packet)&low_mask(128));
    send_request(LINE1, UINT64_C(7), UINT64_C(6), DATA_ID);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      home_tick();
    finish_cached();
    for (int packet = 0; packet < 4; packet++)
      expected_line[packet] = UINT64_C(64) + ((packet)&low_mask(128));
    send_request(SECOND_SET_PEER, READ_NO_SNP);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      home_tick();
    finish_cached();

    for (int packet = 0; packet < 4; packet++)
      expected_line[packet] = UINT64_C(16) + ((packet)&low_mask(128));
    send_request(LINE3, READ_NO_SNP);
    home_tick();
    dirty_snoop(DATA_ID, UINT64_C(160));
    for (int packet = 0; packet < 4; packet++)
      for (int byte_index = 0; byte_index < 16; byte_index++)
        if (((SNOOP_MASKS >> (packet * 16 + byte_index)) & 1))
          set_byte(expected_line[packet], (byte_index * 8) / 8,
                   UINT64_C(160) + ((packet)&low_mask(8)));
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      home_tick();
    accept_memory_request_transaction(LINE0, WRITE_NO_SNP_FULL,
                                      victim_memory_txn);
    accept_memory_request_slot(LINE3, first_memory_txn);
    subordinate_responses_in.pbits = {};
    subordinate_responses_in.pbits.popcode = DBID_RESP;
    subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
    subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
    subordinate_responses_in.pbits.ptxn_uid = victim_memory_txn;
    subordinate_responses_in.pbits.pdbid_uor_ugroup_uid = MEMORY_DBID;
    subordinate_responses_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.psubordinate.prsp.pready);
    home_tick();
    subordinate_responses_in = {};
    for (int packet = 0; packet < 4; packet++)
      accept_victim_packet(slice(packet, 1, 0), expected_line[packet]);

    send_request(SECOND_SET_REPLACEMENT, READ_NO_SNP);
    home_tick();
    dirty_snoop(DATA_ID, UINT64_C(176));
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      home_tick();
    subordinate_responses_in.pbits = {};
    subordinate_responses_in.pbits.popcode = COMP;
    subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
    subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
    subordinate_responses_in.pbits.ptxn_uid = victim_memory_txn;
    subordinate_responses_in.pbits.pdbid_uor_ugroup_uid = MEMORY_DBID;
    subordinate_responses_in.pvalid = UINT64_C(1);
    eval();
    CHECK(port_out.psubordinate.prsp.pready);
    home_tick();
    subordinate_responses_in = {};

    second_victim_request_seen = 0;
    victim_request_wait_cycles = 0;
    subordinate_requests_ready_in.pready = UINT64_C(1);
    while (!second_victim_request_seen && victim_request_wait_cycles < 12) {
      eval();
      if (port_out.psubordinate.preq.pvalid) {
        home_expect_backing(0);

        if (port_out.psubordinate.preq.pbits.paddress == LINE1 &&
            port_out.psubordinate.preq.pbits.popcode == WRITE_NO_SNP_FULL) {
          second_victim_memory_txn = port_out.psubordinate.preq.pbits.ptxn_uid;
          second_victim_request_seen = 1;
        } else {
          CHECK(port_out.psubordinate.preq.pbits.paddress ==
                    SECOND_SET_REPLACEMENT &&
                port_out.psubordinate.preq.pbits.popcode == READ_NO_SNP);
        }
      }
      home_tick();
      victim_request_wait_cycles = victim_request_wait_cycles + 1;
    }
    subordinate_requests_ready_in = {};
    CHECK(second_victim_request_seen);
    CHECK(second_victim_memory_txn == victim_memory_txn);
  }

  reset = 1;
  home_tick();
  reset = 0;
  finish_directory_sweep();
  send_request(LINE0, READ_NO_SNP);
  home_tick();
  fill_and_return(LINE0, UINT64_C(80));
  send_request(LINE2, READ_NO_SNP);
  home_tick();
  fill_and_return(LINE2, UINT64_C(112));
  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(80) + ((packet)&low_mask(128));
  send_request(LINE0, READ_NO_SNP);
  finish_cached();
  send_request(LINE3, READ_NO_SNP);
  while (!port_out.psubordinate.preq.pvalid &&
         !port_out.prequester.psnoops.pvalid)
    home_tick();
  CHECK(port_out.psubordinate.preq.pvalid &&
        !port_out.prequester.psnoops.pvalid);
  fill_and_return(LINE3, UINT64_C(64));
  send_request(LINE2, READ_NO_SNP);
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    home_tick();
  fill_and_return(LINE2, UINT64_C(112));

  reset = 1;
  home_tick();
  reset = 0;
  finish_directory_sweep();
  send_request(LINE0, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 1);
  home_tick();
  accept_memory_request(LINE0, READ_NO_SNP);
  for (int packet = 0; packet < 4; packet++)
    return_fill_packet(((packet)&low_mask(2)), ((packet)&low_mask(8)),
                       packet == 0 ? UINT64_C(2) : UINT64_C(0));
  for (int packet = 0; packet < 4; packet++)
    accept_cached_packet(((packet)&low_mask(2)), ((packet)&low_mask(128)),
                         UINT64_C(2));
  send_request(LINE0, READ_ONCE, UINT64_C(6), HTIF_ID, 0, 1);
  home_tick();
  fill_and_return(LINE0, UINT64_C(80));

  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(80) + ((packet)&low_mask(128));
  for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index) {
    send_request(LINE0, READ_ONCE);
    finish_cached();
  }

  send_request(LINE0, WRITE_UNIQUE_PTL, UINT64_C(4), DATA_ID);
  requester_responses_ready_in.pready = 1;
  home_tick();
  requester_responses_ready_in = {};
  request_data_in = {};
  request_data_in.pvalid = 1;
  request_data_in.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
  request_data_in.pbits.psrc_uid = DATA_ID;
  request_data_in.pbits.ptgt_uid = HOME_ID;
  request_data_in.pbits.pbyte_uenable = UINT64_MAX;
  request_data_in.pbits.pdata = wide(UINT64_C(96));
  home_tick();
  request_data_in = {};
  while (!port_out.prequester.presponses.pvalid) {
    CHECK(!port_out.prequester.psnoops.pvalid);
    home_tick();
  }
  requester_responses_ready_in.pready = 1;
  home_tick();
  requester_responses_ready_in = {};
  expected_line[0] = UINT64_C(96);
  send_request(LINE0, UINT64_C(2), UINT64_C(6), INSTRUCTION_ID);
  finish_cached();

  send_request(LINE0, UINT64_C(2), UINT64_C(6), DATA_ID);
  finish_cached();
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
    send_request(LINE0, READ_ONCE);
    finish_cached();
  }

  send_request(LINE0, UINT64_C(7), UINT64_C(6), DATA_ID);
  clean_snoop(INSTRUCTION_ID, UINT64_C(7), UINT64_C(0));
  finish_cached();
  send_request(LINE0, READ_ONCE);
  clean_snoop(DATA_ID, UINT64_C(3), UINT64_C(2));
  finish_cached();

  send_request(LINE0, UINT64_C(2), UINT64_C(6), INSTRUCTION_ID);
  clean_snoop(DATA_ID, UINT64_C(2), UINT64_C(1));
  finish_cached();
  send_request(LINE0, READ_ONCE);
  finish_cached();
  send_request(LINE0, UINT64_C(7), UINT64_C(6), INSTRUCTION_ID);
  clean_snoop(DATA_ID, UINT64_C(7), UINT64_C(0));
  finish_cached();
  send_request(LINE0, READ_ONCE);
  clean_snoop(INSTRUCTION_ID, UINT64_C(3), UINT64_C(2));
  finish_cached();

  send_request(LINE0, UINT64_C(7), UINT64_C(6), DATA_ID);
  clean_snoop(INSTRUCTION_ID, UINT64_C(7), UINT64_C(0), UINT64_C(2));
  finish_cached(UINT64_C(2));
  send_request(LINE0, READ_ONCE);
  clean_snoop(INSTRUCTION_ID, UINT64_C(3), UINT64_C(2));
  finish_cached();

  send_request(LINE0, UINT64_C(2), UINT64_C(6), DATA_ID);
  dirty_snoop(INSTRUCTION_ID, UINT64_C(176), UINT64_C(2), UINT64_C(4), 1);
  for (int packet = 0; packet < 4; packet++)
    for (int b = 0; b < 16; b++)
      if (((SNOOP_MASKS >> (packet * 16 + b)) & 1))
        set_byte(expected_line[packet], (b * 8) / 8,
                 UINT64_C(176) + ((packet)&low_mask(8)));
  finish_cached(UINT64_C(2));
  send_request(LINE0, READ_ONCE);
  dirty_snoop(INSTRUCTION_ID, UINT64_C(192), UINT64_C(3), UINT64_C(4));
  for (int packet = 0; packet < 4; packet++)
    for (int b = 0; b < 16; b++)
      if (((SNOOP_MASKS >> (packet * 16 + b)) & 1))
        set_byte(expected_line[packet], (b * 8) / 8,
                 UINT64_C(192) + ((packet)&low_mask(8)));
  finish_cached();
  send_request(LINE0, READ_ONCE);
  finish_cached();

  send_request(LINE0, UINT64_C(7), UINT64_C(6), INSTRUCTION_ID, 1);
  finish_cached();
  first_comp_ack_dbid = response_dbid;
  requester_requests_in.pbits = {};
  requester_requests_in.pbits.paddress = LINE0;
  requester_requests_in.pvalid = UINT64_C(1);
  eval();
  CHECK(!port_out.prequester.prequests.pready);
  requester_requests_in = {};
  requester_requests_in.pbits.paddress = LINE1;
  eval();
  for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index) {
    CHECK(port_out.prequester.prequests.pready);
    home_tick();
  }
  send_request(LINE1, READ_ONCE, UINT64_C(6), HTIF_ID, 1);
  fill_and_return(LINE1, UINT64_C(82));
  second_comp_ack_dbid = response_dbid;
  CHECK(first_comp_ack_dbid != second_comp_ack_dbid);

  requester_requests_in.pbits = {};
  requester_requests_in.pbits.psrc_uid = HTIF_ID;
  requester_requests_in.pbits.ptgt_uid = HOME_ID;
  requester_requests_in.pbits.popcode = READ_ONCE;
  requester_requests_in.pbits.paddress = LINE0;
  requester_requests_in.pbits.psize_uor_unum_ureq = UINT64_C(6);
  requester_requests_in.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
      HTIF_ID;
  requester_requests_in.pbits.preturn_utxn_uid_uor_ustash_ulpid =
      UINT64_C(1620);
  requester_requests_in.pbits.pexp_ucomp_uack = UINT64_C(1);
  requester_requests_in.pvalid = UINT64_C(1);
  eval();
  CHECK(!port_out.prequester.prequests.pready);
  requester_requests_in = {};

  send_comp_ack(INSTRUCTION_ID, first_comp_ack_dbid);

  home_tick();
  send_request(LINE0, READ_ONCE);
  while (!port_out.prequester.psnoops.pvalid)
    home_tick();
  send_comp_ack(HTIF_ID, second_comp_ack_dbid);
  CHECK(port_out.prequester.psnoops.pvalid);
  clean_snoop(INSTRUCTION_ID, UINT64_C(3), UINT64_C(2));
  finish_cached();
  CHECK(port_out.prequester.prequests.pready);
  send_request(LINE0, UINT64_C(3));
  clean_snoop(INSTRUCTION_ID, UINT64_C(3), UINT64_C(2));
  finish_cached();

  send_request(LINE2, READ_NO_SNP);
  home_tick();
  fill_and_return(LINE2, UINT64_C(112));
  send_request(LINE3, READ_NO_SNP);
  clean_snoop(INSTRUCTION_ID, SNP_CLEAN_INVALID, UINT64_C(0), UINT64_C(2));
  finish_cached(UINT64_C(2));
  send_request(LINE0, UINT64_C(3));
  clean_snoop(INSTRUCTION_ID, UINT64_C(3), UINT64_C(0));
  finish_cached();

  send_request(LINE0, UINT64_C(7), UINT64_C(6), DATA_ID);
  finish_cached();
  copyback(LINE0, UINT64_C(6));
  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(3735879680) + ((packet)&low_mask(128));
  send_request(LINE0, UINT64_C(3));
  finish_cached();

  for (int state_index = 0; state_index < 3; state_index++) {
    copyback(LINE0, state_index == 0   ? UINT64_C(0)
                    : state_index == 1 ? UINT64_C(1)
                                       : UINT64_C(2));
    send_request(LINE0, UINT64_C(3));
    finish_cached();
  }

  copyback(LINE0 + UINT64_C(65536), UINT64_C(0));
  send_request(LINE0, UINT64_C(3));
  finish_cached();

  reset = 1;
  home_tick();
  reset = 0;
  finish_directory_sweep();
  send_request(LINE0, READ_NO_SNP);
  home_tick();
  fill_and_return(LINE0, UINT64_C(128));
  send_request(LINE2, READ_NO_SNP);
  home_tick();
  fill_and_return(LINE2, UINT64_C(48));
  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(128) + ((packet)&low_mask(128));
  send_request(LINE0, UINT64_C(7), UINT64_C(6), DATA_ID);
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    home_tick();
  finish_cached();
  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(48) + ((packet)&low_mask(128));
  send_request(LINE2, READ_NO_SNP);
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    home_tick();
  finish_cached();
  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(128) + ((packet)&low_mask(128));
  send_request(LINE3, READ_NO_SNP);
  home_tick();
  dirty_snoop(DATA_ID, UINT64_C(208));
  for (int packet = 0; packet < 4; packet++)
    for (int byte_index = 0; byte_index < 16; byte_index++)
      if (((SNOOP_MASKS >> (packet * 16 + byte_index)) & 1))
        set_byte(expected_line[packet], (byte_index * 8) / 8,
                 UINT64_C(208) + ((packet)&low_mask(8)));
  accept_memory_request_transaction(LINE0, WRITE_NO_SNP_FULL,
                                    victim_memory_txn);
  accept_memory_request_slot(LINE3, first_memory_txn);

  subordinate_responses_in = {};
  subordinate_responses_in.pbits.popcode = COMP;
  subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
  subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
  subordinate_responses_in.pbits.ptxn_uid = victim_memory_txn;
  subordinate_responses_in.pbits.presp_uerr = UINT64_C(2);
  subordinate_responses_in.pbits.pdbid_uor_ugroup_uid = MEMORY_DBID;
  subordinate_responses_in.pvalid = UINT64_C(1);
  home_tick();
  subordinate_responses_in = {};
  subordinate_responses_in.pbits.popcode = DBID_RESP;
  subordinate_responses_in.pbits.psrc_uid = MEMORY_ID;
  subordinate_responses_in.pbits.ptgt_uid = HOME_ID;
  subordinate_responses_in.pbits.ptxn_uid = victim_memory_txn;
  subordinate_responses_in.pbits.pdbid_uor_ugroup_uid = MEMORY_DBID;
  subordinate_responses_in.pvalid = UINT64_C(1);
  home_tick();
  subordinate_responses_in = {};
  for (int packet = 0; packet < 4; packet++)
    accept_victim_packet(slice(packet, 1, 0), expected_line[packet]);
  for (int packet = 0; packet < 4; packet++)
    return_fill_packet(slice(packet, 1, 0), UINT64_C(144) + slice(packet, 7, 0),
                       UINT64_C(0), first_memory_txn);
  while (!port_out.prequester.presponse_udata.pvalid)
    home_tick();
  for (int packet = 0; packet < 4; packet++)
    accept_cached_packet(slice(packet, 1, 0),
                         UINT64_C(144) + ((packet)&low_mask(128)), UINT64_C(2));
  send_request(LINE0, READ_NO_SNP);
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    home_tick();
  finish_cached();

  reset = 1;
  home_tick();
  reset = 0;
  finish_directory_sweep();
  send_request(LINE0, READ_NO_SNP);
  home_tick();
  fill_and_return(LINE0, UINT64_C(160));
  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(160) + ((packet)&low_mask(128));
  send_request(LINE0, UINT64_C(2), UINT64_C(6), INSTRUCTION_ID);
  finish_cached();
  send_request(LINE0, UINT64_C(2), UINT64_C(6), DATA_ID);
  finish_cached();
  send_request(LINE0, UINT64_C(8), UINT64_C(6), HTIF_ID);
  clean_snoops_back_to_back(INSTRUCTION_ID, DATA_ID, UINT64_C(8), UINT64_C(1));
  finish_maintenance();

  send_request(LINE0, UINT64_C(7), UINT64_C(6), DATA_ID);
  clean_snoop(INSTRUCTION_ID, UINT64_C(7), UINT64_C(0));
  finish_cached();
  send_request(LINE0, UINT64_C(8), UINT64_C(6), HTIF_ID);
  clean_snoop(DATA_ID, UINT64_C(8), UINT64_C(2));
  finish_maintenance();
  send_request(LINE0, READ_ONCE);
  clean_snoop(DATA_ID, UINT64_C(3), UINT64_C(2));
  finish_cached();
  send_request(LINE0, UINT64_C(2), UINT64_C(6), INSTRUCTION_ID);
  dirty_snoop(DATA_ID, UINT64_C(224), UINT64_C(2), UINT64_C(5));
  for (int packet = 0; packet < 4; packet++)
    for (int byte_index = 0; byte_index < 16; byte_index++)
      if (((SNOOP_MASKS >> (packet * 16 + byte_index)) & 1))
        set_byte(expected_line[packet], (byte_index * 8) / 8,
                 UINT64_C(224) + ((packet)&low_mask(8)));
  finish_cached();
  send_request(LINE0, READ_ONCE);
  finish_cached();

  reset = 1;
  home_tick();
  reset = 0;
  finish_directory_sweep();
  send_request(LINE0, WRITE_NO_SNP_FULL);
  while (!port_out.prequester.presponses.pvalid)
    home_tick();
  requester_responses_ready_in.pready = 1;
  home_tick();
  requester_responses_ready_in = {};
  for (int packet = 0; packet < 4; packet++)
    send_write_packet(((packet)&low_mask(2)),
                      UINT64_C(240) + ((packet)&low_mask(128)));
  accept_memory_request_slot(LINE0, first_memory_txn);
  send_request(LINE1, UINT64_C(7), UINT64_C(6), DATA_ID, 1);
  accept_memory_request_slot(LINE1, second_memory_txn);
  for (int packet = 0; packet < 4; packet++)
    return_fill_packet(((packet)&low_mask(2)),
                       UINT64_C(16) + ((packet)&low_mask(8)), 0,
                       first_memory_txn);
  while (!port_out.prequester.presponses.pvalid)
    home_tick();
  stalled_response = port_out.prequester.presponses.pbits;
  CHECK(stalled_response.popcode == COMP);
  for (int packet = 0; packet < 4; packet++)
    return_fill_packet(((packet)&low_mask(2)),
                       UINT64_C(32) + ((packet)&low_mask(8)), 0,
                       second_memory_txn);
  for (int packet = 0; packet < 3; packet++)
    accept_cached_packet(((packet)&low_mask(2)),
                         UINT64_C(32) + ((packet)&low_mask(128)));
  CHECK(port_out.prequester.presponses.pvalid &&
        same_response(port_out.prequester.presponses.pbits, stalled_response));
  requester_responses_ready_in.pready = 1;
  requester_responses_in = {};
  requester_responses_in.pbits.popcode = COMP_ACK;
  requester_responses_in.pbits.ptxn_uid = response_dbid;
  requester_responses_in.pbits.psrc_uid = DATA_ID;
  requester_responses_in.pbits.ptgt_uid = HOME_ID;
  requester_responses_in.pvalid = 1;
  accept_cached_packet(UINT64_C(3), UINT64_C(35));
  requester_responses_ready_in = {};
  requester_responses_in = {};
  requester_requests_in.pbits.paddress = LINE1;
  eval();
  CHECK(!port_out.prequester.prequests.pready);

  home_tick();
  CHECK(!port_out.prequester.prequests.pready);
  home_tick();
  CHECK(port_out.prequester.prequests.pready &&
        !port_out.prequester.presponses.pvalid &&
        !port_out.prequester.presponse_udata.pvalid);
  send_request(LINE1, READ_ONCE);
  clean_snoop(DATA_ID, UINT64_C(3), UINT64_C(2));
  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(32) + ((packet)&low_mask(128));
  finish_cached();
  send_request(LINE0, READ_NO_SNP);
  for (int packet = 0; packet < 4; packet++)
    expected_line[packet] = UINT64_C(240) + ((packet)&low_mask(128));
  finish_cached();

  send_request(LINE2, READ_NO_SNP);
  fill_and_return(LINE2, UINT64_C(48));
  reset = 1;
  home_tick();
  reset = 0;
  home_tick();
  reset = 1;
  home_tick();
  reset = 0;
  finish_directory_sweep();
  send_request(LINE2, READ_NO_SNP);
  fill_and_return(LINE2, UINT64_C(64));
  send_request(LINE0, READ_NO_SNP);
  fill_and_return(LINE0, UINT64_C(80));
}
