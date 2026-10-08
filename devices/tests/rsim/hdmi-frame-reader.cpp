// Verifies nonallocating CHI frame reads, retry routing, ordering, row markers,
// and failures.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "wide.hpp"
int acknowledgements = 0;
void frame_tick() {
  eval();
  if (!reset && chi_out.prsp.prequester.pvalid && chi_in.prsp.prequester.pready)
    ++acknowledgements;
  tick_model();
}
constexpr std::uint8_t READ_ONCE = UINT64_C(3);
constexpr std::uint8_t COMP_ACK = UINT64_C(2);
constexpr std::uint8_t RETRY_ACK = UINT64_C(7);
constexpr std::uint8_t PCRD_GRANT = UINT64_C(3);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t NODE_ID = UINT64_C(2);
constexpr std::uint8_t HOME_ID = UINT64_C(5);
rhodium_rsim::WideBits<512> line_payload(std::uint8_t base) {
  rhodium_rsim::WideBits<512> result;
  for (unsigned i = 0; i < 4; ++i)
    result.words[4 * i] = std::uint8_t(base + i);
  return result;
}

void start_frame(std::uint64_t address) {
  {
    command_in = {};
    command_in.pvalid = UINT64_C(1);
    command_in.pbits.pbase_uaddress = address;
    command_in.pbits.ppas = UINT64_C(3);
    command_in.pbits.pqos = UINT64_C(10);
    eval();
    CHECK(command_out.pready);
    frame_tick();
    command_in = {};
  }
}

void accept_request(std::uint64_t address, std::uint16_t txn_id,
                    bool retried = 0) {
  {
    while (!chi_out.preq.pvalid)
      frame_tick();
    eval();
    CHECK(chi_out.preq.pbits.paddress == address &&
          chi_out.preq.pbits.ptxn_uid == txn_id &&
          chi_out.preq.pbits.preturn_utxn_uid_uor_ustash_ulpid == txn_id &&
          chi_out.preq.pbits.psrc_uid == NODE_ID &&
          chi_out.preq.pbits.ptgt_uid == HOME_ID &&
          chi_out.preq.pbits.popcode == READ_ONCE &&
          chi_out.preq.pbits.psize_uor_unum_ureq == UINT64_C(6) &&
          chi_out.preq.pbits.pmem_uattr.pallocate == UINT64_C(0) &&
          chi_out.preq.pbits.ppas == UINT64_C(3) &&
          chi_out.preq.pbits.pqos == UINT64_C(10) &&
          chi_out.preq.pbits.pallow_uretry == !retried &&
          chi_out.preq.pbits.ppcrd_utype ==
              (retried ? UINT64_C(11) : UINT64_C(0)));
    chi_in.preq.pready = UINT64_C(1);
    frame_tick();
    chi_in.preq = {};
  }
}

void send_response(std::uint16_t txn_id, std::uint8_t opcode,
                   std::uint8_t credit_type) {
  {
    chi_in.prsp.presponse = {};
    chi_in.prsp.presponse.pvalid = UINT64_C(1);
    chi_in.prsp.presponse.pbits.popcode = opcode;
    chi_in.prsp.presponse.pbits.ppcrd_utype = credit_type;
    chi_in.prsp.presponse.pbits.ptxn_uid = txn_id;
    chi_in.prsp.presponse.pbits.psrc_uid = HOME_ID;
    chi_in.prsp.presponse.pbits.ptgt_uid = NODE_ID;
    eval();
    CHECK(chi_out.prsp.presponse.pready);
    frame_tick();
    chi_in.prsp.presponse = {};
  }
}

void send_line(std::uint16_t txn_id, std::uint16_t dbid, std::uint8_t base,
               std::uint8_t error = 0, bool poisoned = 0) {
  {
    for (int packet = 0; packet < 4; packet++) {
      chi_in.pdat.presponse = {};
      chi_in.pdat.presponse.pvalid = UINT64_C(1);
      chi_in.pdat.presponse.pbits.popcode = COMP_DATA;
      chi_in.pdat.presponse.pbits.presp_uerr = packet == 0 ? error : 0;
      chi_in.pdat.presponse.pbits.ppoison =
          packet == 0 && poisoned ? UINT64_C(1) : 0;
      chi_in.pdat.presponse.pbits.psrc_uid = HOME_ID;
      chi_in.pdat.presponse.pbits.ptgt_uid = NODE_ID;
      chi_in.pdat.presponse.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
          HOME_ID;
      chi_in.pdat.presponse.pbits.ptxn_uid = txn_id;
      chi_in.pdat.presponse.pbits.pdbid_uor_umecid = dbid;
      chi_in.pdat.presponse.pbits.pdata_uid = slice(packet, 1, 0);
      chi_in.pdat.presponse.pbits.pbyte_uenable = UINT64_C(65535);
      chi_in.pdat.presponse.pbits.pdata = wide(std::uint8_t(base + packet));
      eval();
      CHECK(chi_out.pdat.presponse.pready);
      frame_tick();
      chi_in.pdat.presponse = {};
    }
  }
}

void accept_line(std::uint8_t base, bool row_end, bool frame_end,
                 std::uint8_t error = 0, bool poisoned = 0) {
  std::remove_cvref_t<decltype(line_out.pbits)> held;
  {
    while (!line_out.pvalid)
      frame_tick();
    held = line_out.pbits;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
      frame_tick();
      CHECK(line_out.pvalid &&
            (line_out.pbits.pdata == held.pdata &&
             line_out.pbits.prow_uend == held.prow_uend &&
             line_out.pbits.pframe_uend == held.pframe_uend &&
             line_out.pbits.perror == held.perror &&
             line_out.pbits.ppoisoned == held.ppoisoned));
    }
    CHECK(line_out.pbits.pdata == line_payload(base) &&
          line_out.pbits.prow_uend == row_end &&
          line_out.pbits.pframe_uend == frame_end &&
          line_out.pbits.perror == error &&
          line_out.pbits.ppoisoned == poisoned);
    line_in.pready = UINT64_C(1);
    eval();
    CHECK(done == frame_end);
    frame_tick();
    line_in = {};
    CHECK(!done);
  }
}

void run_case() {
  reset = 1;
  identity = {.pnode_uid = NODE_ID, .phome_uid = HOME_ID};
  identity = {.pnode_uid = NODE_ID, .phome_uid = HOME_ID};
  command_in = {};
  line_in = {};
  chi_in = {};
  chi_in.prsp.prequester.pready = UINT64_C(1);
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    frame_tick();
  reset = UINT64_C(0);
  frame_tick();

  start_frame(UINT64_C(4096));
  CHECK(active && !fetch_failed);
  accept_request(UINT64_C(4096), UINT64_C(16));
  accept_request(UINT64_C(4160), UINT64_C(17));
  accept_request(UINT64_C(4224), UINT64_C(18));

  send_response(UINT64_C(17), RETRY_ACK, UINT64_C(11));
  send_response(UINT64_C(17), PCRD_GRANT, UINT64_C(11));
  accept_request(UINT64_C(4160), UINT64_C(17), UINT64_C(1));

  line_in.pready = UINT64_C(1);
  frame_tick();
  CHECK(underflow && starved);
  line_in = {};

  send_line(UINT64_C(18), UINT64_C(280), UINT64_C(48));
  send_line(UINT64_C(16), UINT64_C(278), UINT64_C(16));
  send_line(UINT64_C(17), UINT64_C(279), UINT64_C(32), UINT64_C(2),
            UINT64_C(1));
  while (acknowledgements != 3)
    frame_tick();

  accept_line(UINT64_C(16), UINT64_C(0), UINT64_C(0));
  accept_request(UINT64_C(4288), UINT64_C(16));
  accept_line(UINT64_C(32), UINT64_C(1), UINT64_C(0), UINT64_C(2), UINT64_C(1));
  CHECK(fetch_failed);
  accept_line(UINT64_C(48), UINT64_C(0), UINT64_C(0));
  send_line(UINT64_C(16), UINT64_C(294), UINT64_C(64));
  while (acknowledgements != 4)
    frame_tick();
  accept_line(UINT64_C(64), UINT64_C(1), UINT64_C(1));
  CHECK(!active && starved && fetch_failed);
  CHECK(!chi_out.pdat.prequest.pvalid);

  start_frame(UINT64_C(8192));
  CHECK(active && !fetch_failed);
  accept_request(UINT64_C(8192), UINT64_C(17));
}

int main() { return run_test(run_case); }
