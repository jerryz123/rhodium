// Verifies boot-address reset, word/doubleword access, byte masks, and CHI
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
constexpr std::uint8_t BOOT_ID = UINT64_C(13);
constexpr std::uint64_t BOOT_BASE = UINT64_C(4096);

void cycle() {
  {
    tick_model();
  }
}

void issue_request(std::uint8_t opcode, std::uint16_t txn_id,
                   std::uint64_t address, std::uint16_t return_txn_id,
                   std::uint8_t size) {
  {
    port_in.preq.pbits = {};
    port_in.preq.pbits.popcode = opcode;
    port_in.preq.pbits.psrc_uid = REQUESTER_ID;
    port_in.preq.pbits.ptgt_uid = BOOT_ID;
    port_in.preq.pbits.ptxn_uid = txn_id;
    port_in.preq.pbits.paddress = address;
    port_in.preq.pbits.psize_uor_unum_ureq = size;
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
    port_in.pdat.prequest.pbits.ptgt_uid = BOOT_ID;
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

    port_in.preq = {};
    port_in.preq.pvalid = 1;
    port_in.pdat.prequest = {};
    port_in.pdat.prequest.pvalid = 1;
    eval();
    CHECK(port_out.preq.pready && port_out.pdat.prequest.pready);
    cycle();
    port_in.preq = {};
    port_in.pdat.prequest = {};
    CHECK(port_out.prsp.pvalid && port_out.prsp.pbits.ptxn_uid == held_txn_id);
    CHECK(port_out.prsp.pbits.popcode == DBID_RESP &&
          port_out.prsp.pbits.psrc_uid == BOOT_ID &&
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
          port_out.pdat.presponse.pbits.psrc_uid == BOOT_ID &&
          port_out.pdat.presponse.pbits.ptgt_uid == REQUESTER_ID &&
          port_out.pdat.presponse.pbits.ptxn_uid == return_txn_id &&
          port_out.pdat.presponse.pbits.pbyte_uenable == byte_enable &&
          wide_value(port_out.pdat.presponse.pbits.pdata) == expected);
    port_in.pdat.presponse.pready = UINT64_C(1);
    cycle();
    port_in.pdat.presponse.pready = UINT64_C(0);
  }
}

void write_address(std::uint64_t address, std::uint8_t size, std::uint16_t mask,
                   unsigned __int128 value,
                   std::uint8_t opcode = WRITE_NO_SNP_PTL) {
  issue_request(opcode, UINT64_C(2748), address, 0, size);
  accept_dbid(UINT64_C(2748));
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    cycle();
  issue_write_data(mask, value);

  port_in.preq.pbits = {};
  port_in.preq.pbits.popcode = READ_NO_SNP;
  port_in.preq.pbits.ptgt_uid = BOOT_ID;
  port_in.preq.pbits.paddress = BOOT_BASE;
  port_in.preq.pbits.psize_uor_unum_ureq = 3;
  port_in.preq.pvalid = 1;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    cycle();
    CHECK(!port_out.preq.pready);
  }
  accept_comp(UINT64_C(2748));
  port_in.preq = {};
}

void read_address(std::uint64_t address, std::uint8_t size, std::uint16_t mask,
                  unsigned __int128 value) {
  issue_request(READ_NO_SNP, UINT64_C(291), address, UINT64_C(1110), size);
  accept_read(UINT64_C(1110), mask, value);
}

void run_case() {
  reset = UINT64_C(1);
  reset = UINT64_C(1);
  identity = {.pnode_uid = BOOT_ID, .pbase_uaddress = BOOT_BASE};
  port_in = {};
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    cycle();
  reset = 0;
  cycle();
  read_address(BOOT_BASE, 3, UINT64_C(255), UINT64_C(1311768467015204864));
  read_address(BOOT_BASE, 2, UINT64_C(15), UINT64_C(1311768467015204864));
  read_address(BOOT_BASE + 4, 2, UINT64_C(240), UINT64_C(1311768467015204864));
  write_address(BOOT_BASE, 2, UINT64_C(15), UINT64_C(3735928559),
                WRITE_NO_SNP_FULL);
  read_address(BOOT_BASE, 3, UINT64_C(255), UINT64_C(1311768468603649775));
  write_address(BOOT_BASE + 4, 2, UINT64_C(240), UINT64_C(18364758542507835392),
                WRITE_NO_SNP_FULL);
  read_address(BOOT_BASE, 3, UINT64_C(255), UINT64_C(18364758546243763951));
  write_address(BOOT_BASE, 3, UINT64_C(129), UINT64_C(1224979098644774946));
  read_address(BOOT_BASE, 3, UINT64_C(255), UINT64_C(1287108759254842914));
  write_address(BOOT_BASE, 3, UINT64_C(0), UINT64_MAX);
  read_address(BOOT_BASE, 3, UINT64_C(255), UINT64_C(1287108759254842914));
  write_address(BOOT_BASE, 3, UINT64_C(255), UINT64_C(6442459136),
                WRITE_NO_SNP_FULL);
  read_address(BOOT_BASE, 3, UINT64_C(255), UINT64_C(6442459136));

  port_in.preq.pvalid = 1;
  port_in.preq.pbits = {};
  port_in.pdat.prequest.pvalid = 1;
  port_in.pdat.prequest.pbits = {};
  cycle();
  port_in = {};
  read_address(BOOT_BASE, 3, UINT64_C(255), UINT64_C(6442459136));
  reset = 1;
  cycle();
  reset = 0;
  cycle();
  read_address(BOOT_BASE, 3, UINT64_C(255), UINT64_C(1311768467015204864));

  issue_request(WRITE_NO_SNP_FULL, UINT64_C(3567), BOOT_BASE, 0, 3);
  reset = 1;
  cycle();
  reset = 0;
  cycle();
  CHECK(!port_out.prsp.pvalid && !port_out.pdat.presponse.pvalid);
  read_address(BOOT_BASE, 3, UINT64_C(255), UINT64_C(1311768467015204864));
}

void invalid_access(int mode) {
  dut = Model{};
  reset = 1;
  identity = {.pnode_uid = BOOT_ID, .pbase_uaddress = BOOT_BASE};
  port_in = {};
  tick_model();
  tick_model();
  reset = 0;
  tick_model();
  auto &req = port_in.preq;
  req.pvalid = 1;
  req.pbits.popcode = mode < 3 ? READ_NO_SNP : WRITE_NO_SNP_FULL;
  req.pbits.psrc_uid = REQUESTER_ID;
  req.pbits.ptgt_uid = BOOT_ID;
  req.pbits.paddress = BOOT_BASE + (mode == 0 ? 8 : mode == 1 ? 2 : 0);
  req.pbits.psize_uor_unum_ureq = mode == 2 ? 1 : 2;
  if (mode >= 3) {
    eval();
    while (!port_out.preq.pready)
      tick_model();
    tick_model();
    req.pvalid = 0;
    if (mode != 7) {
      port_in.prsp.pready = 1;
      eval();
      while (!port_out.prsp.pvalid)
        tick_model();
      tick_model();
    }
    port_in.prsp.pready = 0;
    auto &dat = port_in.pdat.prequest;
    dat.pvalid = 1;
    dat.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
    dat.pbits.psrc_uid = mode == 3 ? REQUESTER_ID + 1 : REQUESTER_ID;
    dat.pbits.ptgt_uid = mode == 6 ? BOOT_ID + 1 : BOOT_ID;
    dat.pbits.ptxn_uid = mode == 5 ? 1 : 0;
    dat.pbits.pbyte_uenable = mode == 4 ? 0x1f : 0xf;
  }
  tick_model();
}
int main() {
  return run_test([] {
    run_case();
    for (int mode = 0; mode < 8; ++mode)
      expect_failure(mode < 3    ? "boot_address_request_supported"
                     : mode == 7 ? "boot_address_write_data_expected"
                                 : "boot_address_write_data_supported",
                     [=] { invalid_access(mode); });
  });
}
