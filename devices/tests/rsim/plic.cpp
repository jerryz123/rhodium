// Verifies PLIC priorities, contexts, gateways, claim/completion, and CHI
// backpressure.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "wide.hpp"
constexpr std::uint8_t READ_NO_SNP = UINT64_C(4);
constexpr std::uint8_t WRITE_NO_SNP_FULL = UINT64_C(29);
constexpr std::uint8_t WRITE_NO_SNP_PTL = UINT64_C(28);
constexpr std::uint8_t COMP = UINT64_C(4);
constexpr std::uint8_t DBID_RESP = UINT64_C(6);
constexpr std::uint8_t NON_COPY_BACK_WRITE_DATA = UINT64_C(3);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t REQUESTER_ID = UINT64_C(3);
constexpr std::uint8_t PLIC_ID = UINT64_C(13);
constexpr std::uint64_t PLIC_BASE = UINT64_C(201326592);
constexpr std::uint64_t PRIORITY1 = PLIC_BASE + UINT64_C(4);
constexpr std::uint64_t PRIORITY2 = PLIC_BASE + UINT64_C(8);
constexpr std::uint64_t PRIORITY3 = PLIC_BASE + UINT64_C(12);
constexpr std::uint64_t PENDING = PLIC_BASE + UINT64_C(4096);
constexpr std::uint64_t ENABLE0 = PLIC_BASE + UINT64_C(8192);
constexpr std::uint64_t ENABLE1 = PLIC_BASE + UINT64_C(8320);
constexpr std::uint64_t THRESHOLD0 = PLIC_BASE + UINT64_C(2097152);
constexpr std::uint64_t CLAIM0 = PLIC_BASE + UINT64_C(2097156);
constexpr std::uint64_t THRESHOLD1 = PLIC_BASE + UINT64_C(2101248);
constexpr std::uint64_t CLAIM1 = PLIC_BASE + UINT64_C(2101252);

void cycle() {
  {
    tick_model();
  }
}

void issue_request(std::uint8_t opcode, std::uint16_t txn_id,
                   std::uint64_t address, std::uint16_t return_txn_id) {
  {
    port_in.preq.pbits = {};
    port_in.preq.pbits.popcode = opcode;
    port_in.preq.pbits.psrc_uid = REQUESTER_ID;
    port_in.preq.pbits.ptgt_uid = PLIC_ID;
    port_in.preq.pbits.ptxn_uid = txn_id;
    port_in.preq.pbits.paddress = address;
    port_in.preq.pbits.psize_uor_unum_ureq = UINT64_C(2);
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

void issue_write_data(std::uint16_t byte_enable, unsigned __int128 data) {
  {
    port_in.pdat.prequest.pbits = {};
    port_in.pdat.prequest.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
    port_in.pdat.prequest.pbits.psrc_uid = REQUESTER_ID;
    port_in.pdat.prequest.pbits.ptgt_uid = PLIC_ID;
    port_in.pdat.prequest.pbits.ptxn_uid = UINT64_C(0);
    port_in.pdat.prequest.pbits.pbyte_uenable = byte_enable;
    port_in.pdat.prequest.pbits.pdata = wide(data);
    port_in.pdat.prequest.pvalid = UINT64_C(1);
    while (!port_out.pdat.prequest.pready)
      cycle();
    cycle();
    port_in.pdat.prequest = {};
  }
}

void accept_dbid(std::uint16_t request_txn_id) {
  std::uint16_t held_txn_id;
  {
    while (!port_out.prsp.pvalid)
      cycle();
    held_txn_id = port_out.prsp.pbits.ptxn_uid;
    cycle();
    CHECK(port_out.prsp.pvalid && port_out.prsp.pbits.ptxn_uid == held_txn_id);
    CHECK(port_out.prsp.pbits.popcode == DBID_RESP &&
          port_out.prsp.pbits.psrc_uid == PLIC_ID &&
          port_out.prsp.pbits.ptgt_uid == REQUESTER_ID &&
          port_out.prsp.pbits.ptxn_uid == request_txn_id &&
          port_out.prsp.pbits.pdbid_uor_ugroup_uid == 0);
    port_in.prsp.pready = UINT64_C(1);
    cycle();
    port_in.prsp.pready = UINT64_C(0);
  }
}

void accept_comp(std::uint16_t request_txn_id) {
  {
    while (!port_out.prsp.pvalid)
      cycle();
    CHECK(port_out.prsp.pbits.popcode == COMP &&
          port_out.prsp.pbits.ptxn_uid == request_txn_id);
    port_in.prsp.pready = UINT64_C(1);
    cycle();
    port_in.prsp.pready = UINT64_C(0);
  }
}

void accept_read(std::uint16_t return_txn_id, std::uint16_t byte_enable,
                 unsigned __int128 expected) {
  unsigned __int128 held_data;
  {
    while (!port_out.pdat.presponse.pvalid)
      cycle();
    held_data = wide_value(port_out.pdat.presponse.pbits.pdata);
    cycle();
    CHECK(port_out.pdat.presponse.pvalid &&
          wide_value(port_out.pdat.presponse.pbits.pdata) == held_data);
    CHECK(port_out.pdat.presponse.pbits.popcode == COMP_DATA &&
          port_out.pdat.presponse.pbits.psrc_uid == PLIC_ID &&
          port_out.pdat.presponse.pbits.ptgt_uid == REQUESTER_ID &&
          port_out.pdat.presponse.pbits.ptxn_uid == return_txn_id &&
          port_out.pdat.presponse.pbits.pbyte_uenable == byte_enable &&
          wide_value(port_out.pdat.presponse.pbits.pdata) == expected);
    port_in.pdat.presponse.pready = UINT64_C(1);
    cycle();
    port_in.pdat.presponse.pready = UINT64_C(0);
  }
}

void write32_opcode(std::uint8_t opcode, std::uint16_t txn_id,
                    std::uint64_t address, std::uint32_t value) {
  std::uint8_t lane;
  std::uint16_t byte_enable;
  unsigned __int128 data;
  {
    lane = slice(address, 3, 0);
    byte_enable = UINT64_C(15) << lane;
    data = uint128(value) << (lane * 8);
    issue_request(opcode, txn_id, address, UINT64_C(0));
    accept_dbid(txn_id);
    issue_write_data(byte_enable, data);
    accept_comp(txn_id);
  }
}

void write32(std::uint16_t txn_id, std::uint64_t address, std::uint32_t value) {
  write32_opcode(WRITE_NO_SNP_PTL, txn_id, address, value);
}

void read32(std::uint16_t txn_id, std::uint64_t address,
            std::uint32_t expected_value) {
  std::uint8_t lane;
  std::uint16_t byte_enable;
  unsigned __int128 expected_data;
  {
    lane = slice(address, 3, 0);
    byte_enable = UINT64_C(15) << lane;
    expected_data = uint128(expected_value) << (lane * 8);
    issue_request(READ_NO_SNP, txn_id, address, txn_id + UINT64_C(1024));
    accept_read(txn_id + UINT64_C(1024), byte_enable, expected_data);
  }
}

void run_case() {
  reset = UINT64_C(1);
  reset = UINT64_C(1);
  identity = {.pnode_uid = PLIC_ID, .pbase_uaddress = PLIC_BASE};
  sources = {};
  port_in = {};
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
    cycle();
  reset = UINT64_C(0);
  CHECK(pack_bits(context_interrupts) == 0);
  read32(UINT64_C(256), PLIC_BASE, UINT64_C(0));
  read32(UINT64_C(288), CLAIM0, UINT64_C(0));
  read32(UINT64_C(289), CLAIM1, UINT64_C(0));

  write32_opcode(WRITE_NO_SNP_FULL, UINT64_C(257), PRIORITY1, UINT64_C(3));
  write32(UINT64_C(258), PRIORITY2, UINT64_C(3));
  write32(UINT64_C(259), PRIORITY3, UINT64_C(5));
  sources = {1, 1, 0};
  cycle();
  sources = {};
  cycle();
  read32(UINT64_C(260), PENDING, UINT64_C(6));
  CHECK(pack_bits(context_interrupts) == 0);

  write32(UINT64_C(261), ENABLE0, UINT64_C(6));
  read32(UINT64_C(262), ENABLE0, UINT64_C(6));
  CHECK(pack_bits(context_interrupts) == UINT64_C(1));
  write32(UINT64_C(263), THRESHOLD0, UINT64_C(3));
  CHECK(pack_bits(context_interrupts) == 0);
  write32(UINT64_C(264), THRESHOLD0, UINT64_C(2));
  CHECK(pack_bits(context_interrupts) == UINT64_C(1));

  read32(UINT64_C(265), CLAIM0, UINT64_C(1));
  read32(UINT64_C(266), PENDING, UINT64_C(4));
  CHECK(pack_bits(context_interrupts) == UINT64_C(1));
  read32(UINT64_C(267), CLAIM0, UINT64_C(2));
  CHECK(pack_bits(context_interrupts) == 0);
  write32(UINT64_C(268), CLAIM0, UINT64_C(1));
  write32(UINT64_C(269), CLAIM0, UINT64_C(2));

  write32(UINT64_C(270), ENABLE1, UINT64_C(8));
  write32(UINT64_C(271), THRESHOLD1, UINT64_C(5));
  sources = {0, 0, 1};
  cycle();
  sources = {};
  cycle();
  CHECK(pack_bits(context_interrupts) == 0);
  read32(UINT64_C(272), CLAIM1, UINT64_C(3));
  write32(UINT64_C(273), CLAIM0, UINT64_C(3));
  sources = {0, 0, 1};
  cycle();
  sources = {};
  cycle();
  read32(UINT64_C(274), PENDING, UINT64_C(0));

  sources = {0, 0, 1};
  write32(UINT64_C(275), CLAIM1, UINT64_C(3));
  sources = {};
  cycle();
  read32(UINT64_C(276), PENDING, UINT64_C(8));
  CHECK(pack_bits(context_interrupts) == 0);
  write32(UINT64_C(277), THRESHOLD1, UINT64_C(4));
  CHECK(pack_bits(context_interrupts) == UINT64_C(2));
  read32(UINT64_C(278), CLAIM1, UINT64_C(3));
  write32(UINT64_C(279), CLAIM1, UINT64_C(3));

  write32(UINT64_C(280), THRESHOLD0, UINT64_C(0));
  write32(UINT64_C(281), THRESHOLD1, UINT64_C(0));
  write32(UINT64_C(282), ENABLE0, UINT64_C(2));
  write32(UINT64_C(283), ENABLE1, UINT64_C(2));
  sources = {1, 0, 0};
  cycle();
  CHECK(pack_bits(context_interrupts) == UINT64_C(3));
  read32(UINT64_C(284), CLAIM1, UINT64_C(1));
  CHECK(pack_bits(context_interrupts) == 0);

  read32(UINT64_C(290), CLAIM0, UINT64_C(0));
  read32(UINT64_C(291), CLAIM1, UINT64_C(0));
  sources = {};
  cycle();
  write32(UINT64_C(285), CLAIM1, UINT64_C(1));
  read32(UINT64_C(292), CLAIM0, UINT64_C(0));
  read32(UINT64_C(293), CLAIM1, UINT64_C(0));
}

int main() {
  return run_test([] {
    run_case();
    dut = Model{};
    expect_failure("plic_request_supported", [] {
      reset = 1;
      sources = {};
      port_in = {};
      identity = {.pnode_uid = PLIC_ID, .pbase_uaddress = PLIC_BASE};
      tick_model();
      tick_model();
      reset = 0;
      port_in.preq.pbits.popcode = READ_NO_SNP;
      port_in.preq.pbits.psrc_uid = REQUESTER_ID;
      port_in.preq.pbits.ptgt_uid = PLIC_ID;
      port_in.preq.pbits.paddress = PLIC_BASE;
      port_in.preq.pbits.psize_uor_unum_ureq = 3;
      port_in.preq.pvalid = 1;
      tick_model();
    });
  });
}
