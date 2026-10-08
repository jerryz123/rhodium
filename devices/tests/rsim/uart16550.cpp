// Verifies the CHI UART register contract, FIFOs, interrupts, and serial pins.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "wide.hpp"
constexpr std::uint8_t READ_NO_SNP = UINT64_C(4);
constexpr std::uint8_t WRITE_NO_SNP_PTL = UINT64_C(28);
constexpr std::uint8_t COMP = UINT64_C(4);
constexpr std::uint8_t DBID_RESP = UINT64_C(6);
constexpr std::uint8_t NON_COPY_BACK_WRITE_DATA = UINT64_C(3);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t REQUESTER_ID = UINT64_C(3);
constexpr std::uint8_t UART_ID = UINT64_C(11);
constexpr std::uint64_t UART_BASE = UINT64_C(268435456);

// The serial observer runs on every model edge, including edges spent on CHI
// IO.
int tx_phase = -1, tx_elapsed = 0, tx_period = 32;
unsigned tx_expected = 0;
bool tx_watching = false;
void observe_tx() {
  if (!tx_watching)
    return;
  if (tx_phase == -1) {
    if (!tx) {
      tx_phase = 0;
      tx_elapsed = 0;
    }
    return;
  }
  ++tx_elapsed;
  if (tx_elapsed == tx_period / 2 + tx_phase * tx_period) {
    CHECK(tx == (tx_phase == 0   ? 0u
                 : tx_phase == 9 ? 1u
                                 : ((tx_expected >> (tx_phase - 1)) & 1)));
    ++tx_phase;
  }
  if (tx_phase == 10 && tx_elapsed >= 10 * tx_period)
    tx_watching = false;
}
void watch_tx(unsigned value) {
  CHECK(!tx_watching);
  tx_watching = true;
  tx_phase = -1;
  tx_elapsed = 0;
  tx_expected = value;
}

void cycle() {
  {
    tick_model();
    observe_tx();
  }
}

void issue_request(std::uint8_t opcode, std::uint16_t txn_id,
                   std::uint8_t offset, std::uint16_t return_txn_id) {
  {
    port_in.preq.pbits = {};
    port_in.preq.pbits.popcode = opcode;
    port_in.preq.pbits.psrc_uid = REQUESTER_ID;
    port_in.preq.pbits.ptgt_uid = UART_ID;
    port_in.preq.pbits.ptxn_uid = txn_id;
    port_in.preq.pbits.paddress = UART_BASE + offset;
    port_in.preq.pbits.psize_uor_unum_ureq = 0;
    port_in.preq.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
        REQUESTER_ID;
    port_in.preq.pbits.preturn_utxn_uid_uor_ustash_ulpid = return_txn_id;
    port_in.preq.pvalid = UINT64_C(1);
    while (!port_out.preq.pready)
      cycle();
    cycle();
    port_in.preq = {};
  }
}

void issue_write_data(std::uint8_t offset, std::uint8_t value) {
  unsigned __int128 payload;
  std::uint16_t byte_enable;
  {
    payload = {};
    payload = uint128(value) << (offset * 8);
    byte_enable = UINT64_C(1) << offset;
    port_in.pdat.prequest.pbits = {};
    port_in.pdat.prequest.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
    port_in.pdat.prequest.pbits.psrc_uid = REQUESTER_ID;
    port_in.pdat.prequest.pbits.ptgt_uid = UART_ID;
    port_in.pdat.prequest.pbits.ptxn_uid = 0;
    port_in.pdat.prequest.pbits.pbyte_uenable = byte_enable;
    port_in.pdat.prequest.pbits.pdata = wide(payload);
    port_in.pdat.prequest.pvalid = UINT64_C(1);
    while (!port_out.pdat.prequest.pready)
      cycle();
    cycle();
    port_in.pdat.prequest = {};
  }
}

void accept_dbid(std::uint16_t request_txn_id) {
  {
    port_in.prsp.pready = UINT64_C(1);
    while (!port_out.prsp.pvalid)
      cycle();
    CHECK(port_out.prsp.pbits.popcode == DBID_RESP &&
          port_out.prsp.pbits.psrc_uid == UART_ID &&
          port_out.prsp.pbits.ptgt_uid == REQUESTER_ID &&
          port_out.prsp.pbits.ptxn_uid == request_txn_id &&
          port_out.prsp.pbits.pdbid_uor_ugroup_uid == 0);
    cycle();
    port_in.prsp.pready = UINT64_C(0);
  }
}

void accept_comp(std::uint16_t request_txn_id) {
  {
    port_in.prsp.pready = UINT64_C(1);
    while (!port_out.prsp.pvalid)
      cycle();
    CHECK(port_out.prsp.pbits.popcode == COMP &&
          port_out.prsp.pbits.ptxn_uid == request_txn_id);
    cycle();
    port_in.prsp.pready = UINT64_C(0);
  }
}

void write_register(std::uint16_t txn_id, std::uint8_t offset,
                    std::uint8_t value) {
  {
    issue_request(WRITE_NO_SNP_PTL, txn_id, offset, 0);
    accept_dbid(txn_id);
    issue_write_data(offset, value);
    accept_comp(txn_id);
  }
}

void read_register(std::uint16_t txn_id, std::uint8_t offset,
                   std::uint8_t expected) {
  unsigned __int128 held_data;
  std::uint16_t expected_enable;
  {
    issue_request(READ_NO_SNP, txn_id, offset, txn_id + UINT64_C(1024));
    while (!port_out.pdat.presponse.pvalid)
      cycle();
    held_data = wide_value(port_out.pdat.presponse.pbits.pdata);
    cycle();
    CHECK(port_out.pdat.presponse.pvalid &&
          wide_value(port_out.pdat.presponse.pbits.pdata) == held_data);
    expected_enable = UINT64_C(1) << offset;
    CHECK(port_out.pdat.presponse.pbits.popcode == COMP_DATA &&
          port_out.pdat.presponse.pbits.psrc_uid == UART_ID &&
          port_out.pdat.presponse.pbits.ptgt_uid == REQUESTER_ID &&
          port_out.pdat.presponse.pbits.ptxn_uid == txn_id + UINT64_C(1024) &&
          port_out.pdat.presponse.pbits.pbyte_uenable == expected_enable &&
          slice(wide_value(port_out.pdat.presponse.pbits.pdata),
                (offset * 8) + 8 - 1, offset * 8) == expected);
    port_in.pdat.presponse.pready = UINT64_C(1);
    cycle();
    port_in.pdat.presponse.pready = UINT64_C(0);
  }
}

void send_rx_byte(std::uint8_t value, int clocks_per_bit, bool valid_stop) {
  int index;
  {
    rx = UINT64_C(0);
    for (unsigned repeat_index = 0; repeat_index < (clocks_per_bit);
         ++repeat_index)
      cycle();
    for (index = 0; index < 8; index = index + 1) {
      rx = ((value >> index) & 1);
      for (unsigned repeat_index = 0; repeat_index < (clocks_per_bit);
           ++repeat_index)
        cycle();
    }
    rx = valid_stop;
    for (unsigned repeat_index = 0; repeat_index < (clocks_per_bit);
         ++repeat_index)
      cycle();
    rx = UINT64_C(1);
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      cycle();
  }
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    reset = UINT64_C(1);
    identity = {.pnode_uid = UART_ID, .pbase_uaddress = UART_BASE};
    rx = UINT64_C(1);
    port_in = {};
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      cycle();
    reset = UINT64_C(0);
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      cycle();
    CHECK(tx == UINT64_C(1) && !interrupt);

    read_register(UINT64_C(257), UINT64_C(5), UINT64_C(96));
    write_register(UINT64_C(258), UINT64_C(7), UINT64_C(165));
    read_register(UINT64_C(259), UINT64_C(7), UINT64_C(165));

    write_register(UINT64_C(260), UINT64_C(3), UINT64_C(128));
    write_register(UINT64_C(261), UINT64_C(0), UINT64_C(2));
    write_register(UINT64_C(262), UINT64_C(1), UINT64_C(0));
    read_register(UINT64_C(263), UINT64_C(0), UINT64_C(2));
    read_register(UINT64_C(264), UINT64_C(1), UINT64_C(0));
    write_register(UINT64_C(265), UINT64_C(3), UINT64_C(3));
    write_register(UINT64_C(266), UINT64_C(2), UINT64_C(7));
    read_register(UINT64_C(267), UINT64_C(2), UINT64_C(193));

    watch_tx(166);
    write_register(UINT64_C(268), UINT64_C(0), UINT64_C(166));
    while (tx_watching)
      cycle();
    read_register(UINT64_C(269), UINT64_C(5), UINT64_C(96));

    watch_tx(150);
    {
      write_register(UINT64_C(288), UINT64_C(0), UINT64_C(150));
      write_register(UINT64_C(289), UINT64_C(0), UINT64_C(105));
      write_register(UINT64_C(290), UINT64_C(2), UINT64_C(5));
    }
    while (tx_watching)
      cycle();
    for (unsigned repeat_index = 0; repeat_index < (352); ++repeat_index) {
      cycle();
      CHECK(tx);
    }
    read_register(UINT64_C(291), UINT64_C(5), UINT64_C(96));

    write_register(UINT64_C(270), UINT64_C(1), UINT64_C(1));
    send_rx_byte(UINT64_C(60), 32, UINT64_C(1));
    CHECK(interrupt);
    read_register(UINT64_C(271), UINT64_C(5), UINT64_C(97));
    read_register(UINT64_C(272), UINT64_C(2), UINT64_C(196));
    read_register(UINT64_C(273), UINT64_C(0), UINT64_C(60));
    CHECK(!interrupt);
    read_register(UINT64_C(274), UINT64_C(5), UINT64_C(96));

    send_rx_byte(UINT64_C(18), 32, UINT64_C(1));
    send_rx_byte(UINT64_C(52), 32, UINT64_C(1));
    read_register(UINT64_C(275), UINT64_C(0), UINT64_C(18));
    read_register(UINT64_C(276), UINT64_C(0), UINT64_C(52));

    send_rx_byte(UINT64_C(90), 32, UINT64_C(1));
    write_register(UINT64_C(277), UINT64_C(2), UINT64_C(3));
    read_register(UINT64_C(278), UINT64_C(5), UINT64_C(96));

    send_rx_byte(UINT64_C(129), 32, UINT64_C(0));
    read_register(UINT64_C(279), UINT64_C(5), UINT64_C(105));
    read_register(UINT64_C(280), UINT64_C(0), UINT64_C(129));
    read_register(UINT64_C(281), UINT64_C(5), UINT64_C(96));
    for (unsigned repeat_index = 0; repeat_index < (32); ++repeat_index)
      cycle();

    write_register(UINT64_C(282), UINT64_C(2), UINT64_C(0));
    send_rx_byte(UINT64_C(170), 32, UINT64_C(1));
    read_register(UINT64_C(283), UINT64_C(5), UINT64_C(97));
    send_rx_byte(UINT64_C(85), 32, UINT64_C(1));
    read_register(UINT64_C(284), UINT64_C(5), UINT64_C(99));
    read_register(UINT64_C(285), UINT64_C(0), UINT64_C(170));
    read_register(UINT64_C(286), UINT64_C(5), UINT64_C(96));
  });
}
