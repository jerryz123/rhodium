// Checks generic ReadOnce retries, packet assembly, CompAck, errors, and
// retained results.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
bool BAD_ADDRESS = false, DUPLICATE_DATA = false;
using CHIReqFlit = std::remove_cvref_t<decltype(requests_out.pbits)>;
using CHIRspFlit = std::remove_cvref_t<decltype(acknowledgements_out.pbits)>;
using CHIReadOnceResult = std::remove_cvref_t<decltype(completion_out.pbits)>;
unsigned expected_node, expected_txn, expected_home, expected_pas, expected_qos,
    expected_context;
std::uint64_t expected_address;
bool expected_allocate;
uint128 expected_packets[4]{};

constexpr std::uint16_t RESPONSE_DBID = UINT64_C(341);

void begin_read(std::uint8_t requester, std::uint16_t transaction,
                std::uint8_t home, std::uint64_t address, std::uint8_t pas,
                bool allocate, std::uint8_t qos, std::uint8_t command_context) {
  expected_node = requester;
  expected_txn = transaction;
  expected_home = home;
  expected_address = address;
  expected_pas = pas;
  expected_allocate = allocate;
  expected_qos = qos;
  expected_context = command_context;
  node_id = requester;
  txn_id = transaction;
  command_in = {};
  command_in.pvalid = 1;
  command_in.pbits.paddress = BAD_ADDRESS ? (address | 1) : address;
  command_in.pbits.phome_uid = home;
  command_in.pbits.ppas = pas;
  command_in.pbits.pallocate = allocate;
  command_in.pbits.pqos = qos;
  command_in.pbits.pcontext = command_context;
  eval();
  CHECK(command_out.pready);
  tick_model();
  command_in = {};
  node_id = requester + 1;
  txn_id = transaction + 1;
}

void accept_request(bool retried, int stall_cycles) {
  CHIReqFlit expected_request, held_request;
  expected_request = {};
  expected_request.pexp_ucomp_uack = 1;
  expected_request.psnp_uattr_uor_udo_udwt = 1;
  expected_request.pmem_uattr.pallocate = expected_allocate;
  expected_request.pmem_uattr.pcacheable = 1;
  expected_request.ppcrd_utype = retried ? UINT64_C(11) : UINT64_C(0);
  expected_request.pallow_uretry = !retried;
  expected_request.ppas = expected_pas;
  expected_request.paddress = expected_address;
  expected_request.psize_uor_unum_ureq = 6;
  expected_request.popcode = UINT64_C(3);
  expected_request.preturn_utxn_uid_uor_ustash_ulpid = expected_txn;
  expected_request.preturn_unid_uor_ustash_unid_uor_udata_utarget =
      expected_node;
  expected_request.ptxn_uid = expected_txn;
  expected_request.psrc_uid = expected_node;
  expected_request.ptgt_uid = expected_home;
  expected_request.pqos = expected_qos;
  eval();
  CHECK(requests_out.pvalid &&
        same_request(requests_out.pbits, expected_request));
  held_request = requests_out.pbits;
  for (unsigned repeat_index = 0; repeat_index < (stall_cycles);
       ++repeat_index) {
    tick_model();
    CHECK(requests_out.pvalid &&
          same_request(requests_out.pbits, held_request));
  }
  requests_in.pready = 1;
  tick_model();
  requests_in.pready = 0;
}

void send_response(std::uint8_t opcode, std::uint8_t credit_type) {
  responses_in = {};
  responses_in.pvalid = 1;
  responses_in.pbits.popcode = opcode;
  responses_in.pbits.ppcrd_utype = credit_type;
  responses_in.pbits.ptxn_uid = opcode == UINT64_C(3) ? expected_txn : 0;
  responses_in.pbits.psrc_uid = expected_home;
  responses_in.pbits.ptgt_uid = expected_node;
  eval();
  CHECK(responses_out.pready);
  tick_model();
  responses_in = {};
}

void send_data(std::uint8_t data_id, unsigned __int128 data, std::uint8_t error,
               std::uint8_t poison) {
  response_data_in = {};
  response_data_in.pvalid = 1;
  response_data_in.pbits.ppoison = poison;
  response_data_in.pbits.pdata = wide(data);
  response_data_in.pbits.pbyte_uenable = UINT16_MAX;
  response_data_in.pbits.pdata_uid = data_id;
  response_data_in.pbits.pdbid_uor_umecid = RESPONSE_DBID;
  response_data_in.pbits.presp_uerr = error;
  response_data_in.pbits.popcode = UINT64_C(4);
  response_data_in.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
      expected_home;
  response_data_in.pbits.ptxn_uid = expected_txn;
  response_data_in.pbits.psrc_uid = expected_home;
  response_data_in.pbits.ptgt_uid = expected_node;
  response_data_in.pbits.pqos = expected_qos;
  eval();
  CHECK(response_data_out.pready);
  tick_model();
  response_data_in = {};
}

void finish_read(std::uint8_t error, bool poisoned) {
  CHIRspFlit expected_ack, held_ack;
  CHIReadOnceResult held_result;
  expected_ack = {};
  expected_ack.popcode = UINT64_C(2);
  expected_ack.ptxn_uid = RESPONSE_DBID;
  expected_ack.psrc_uid = expected_node;
  expected_ack.ptgt_uid = expected_home;
  expected_ack.pqos = expected_qos;
  eval();
  CHECK(acknowledgements_out.pvalid &&
        same_response(acknowledgements_out.pbits, expected_ack));
  CHECK(!completion_out.pvalid);
  held_ack = acknowledgements_out.pbits;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    tick_model();
    CHECK(acknowledgements_out.pvalid &&
          same_response(acknowledgements_out.pbits, held_ack));
  }
  acknowledgements_in.pready = 1;
  tick_model();
  acknowledgements_in.pready = 0;
  eval();
  CHECK(completion_out.pvalid);
  CHECK(completion_out.pbits.paddress == expected_address &&
        completion_out.pbits.pcontext == expected_context &&
        completion_out.pbits.perror == error &&
        completion_out.pbits.ppoisoned == poisoned);
  for (int packet = 0; packet < 4; packet++) {
    for (unsigned word = 0; word < 4; ++word)
      CHECK(completion_out.pbits.pline.words[packet * 4 + word] ==
            std::uint32_t(expected_packets[packet] >> (32 * word)));
  }
  held_result = completion_out.pbits;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    tick_model();
    CHECK(completion_out.pvalid &&
          completion_out.pbits.pline == held_result.pline &&
          completion_out.pbits.paddress == held_result.paddress &&
          completion_out.pbits.pcontext == held_result.pcontext &&
          completion_out.pbits.perror == held_result.perror &&
          completion_out.pbits.ppoisoned == held_result.ppoisoned &&
          !command_out.pready);
  }
  completion_in.pready = 1;
  tick_model();
  completion_in.pready = 0;
  CHECK(command_out.pready && !completion_out.pvalid);
}

void run_case() {
  reset = 1;
  command_in = {};
  completion_in = {};
  requests_in = {};
  acknowledgements_in = {};
  responses_in = {};
  response_data_in = {};
  node_id = {};
  txn_id = {};
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick_model();
  reset = 0;
  tick_model();

  begin_read(UINT64_C(3), UINT64_C(66), UINT64_C(5), UINT64_C(2147483712),
             UINT64_C(3), UINT64_C(0), UINT64_C(10), UINT64_C(17));
  if (BAD_ADDRESS) {
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick_model();
    fail(1, "misaligned ReadOnce command was not rejected");
  }
  accept_request(UINT64_C(0), 3);
  send_response(UINT64_C(7), UINT64_C(11));
  send_response(UINT64_C(3), UINT64_C(11));
  accept_request(UINT64_C(1), 2);
  expected_packets[0] = UINT64_C(256);
  expected_packets[1] = UINT64_C(257);
  expected_packets[2] = UINT64_C(258);
  expected_packets[3] = UINT64_C(259);
  send_data(2, expected_packets[2], 0, 0);
  if (DUPLICATE_DATA) {
    send_data(2, expected_packets[2], 0, 0);
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick_model();
    fail(1, "duplicate ReadOnce DataID was not rejected");
  }
  send_data(0, expected_packets[0], 0, 0);
  send_data(3, expected_packets[3], 2, UINT64_C(1));
  send_data(1, expected_packets[1], 0, 0);
  finish_read(2, UINT64_C(1));

  begin_read(UINT64_C(4), UINT64_C(67), UINT64_C(6), UINT64_C(2147483776),
             UINT64_C(1), UINT64_C(1), UINT64_C(3), UINT64_C(34));
  accept_request(UINT64_C(0), 0);
  for (int packet = 0; packet < 4; packet++) {
    expected_packets[packet] = UINT64_C(512) + ((packet)&low_mask(128));
    send_data(slice(packet, 1, 0), expected_packets[packet], 0, 0);
  }
  finish_read(0, UINT64_C(0));
}

int main() {
  return run_test([] {
    run_case();
    dut = Model{};
    BAD_ADDRESS = true;
    expect_failure("chi_read_once_address_aligned", run_case);
    dut = Model{};
    BAD_ADDRESS = false;
    DUPLICATE_DATA = true;
    expect_failure("chi_read_once_data_id_unique", run_case);
  });
}
