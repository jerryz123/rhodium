// Verifies the CHI-native ACLINT timer, compare, software-interrupt, and
// masking behavior.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "wide.hpp"
unsigned time_update_count = 0;
std::uint64_t last_time_update = 0;
constexpr std::uint8_t READ_NO_SNP = UINT64_C(4);
constexpr std::uint8_t WRITE_NO_SNP_PTL = UINT64_C(28);
constexpr std::uint8_t COMP = UINT64_C(4);
constexpr std::uint8_t DBID_RESP = UINT64_C(6);
constexpr std::uint8_t NON_COPY_BACK_WRITE_DATA = UINT64_C(3);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t REQUESTER_ID = UINT64_C(3);
constexpr std::uint8_t ACLINT_ID = UINT64_C(9);
constexpr std::uint64_t ACLINT_BASE = UINT64_C(33554432);
constexpr std::uint64_t MSWI0 = ACLINT_BASE + UINT64_C(0);
constexpr std::uint64_t MSWI1 = ACLINT_BASE + UINT64_C(4);
constexpr std::uint64_t MTIMECMP0 = ACLINT_BASE + UINT64_C(16384);
constexpr std::uint64_t MTIME = ACLINT_BASE + UINT64_C(49144);

void cycle() {
  {
    eval();
    if (reset) {
      time_update_count = 0;
      last_time_update = 0;
    } else if (time_update_out.pvalid) {
      ++time_update_count;
      last_time_update = time_update_out.pbits;
    }
    tick_model();
  }
}

void issue_request(std::uint8_t opcode, std::uint16_t txn_id,
                   std::uint64_t address, std::uint8_t size,
                   std::uint16_t return_txn_id) {
  {
    port_in.preq.pbits = {};
    port_in.preq.pbits.popcode = opcode;
    port_in.preq.pbits.psrc_uid = REQUESTER_ID;
    port_in.preq.pbits.ptgt_uid = ACLINT_ID;
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

void issue_write_data(std::uint16_t dbid, std::uint16_t byte_enable,
                      unsigned __int128 data) {
  {
    port_in.pdat.prequest.pbits = {};
    port_in.pdat.prequest.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
    port_in.pdat.prequest.pbits.psrc_uid = REQUESTER_ID;
    port_in.pdat.prequest.pbits.ptgt_uid = ACLINT_ID;
    port_in.pdat.prequest.pbits.ptxn_uid = dbid;
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
  {
    port_in.prsp.pready = UINT64_C(1);
    while (!port_out.prsp.pvalid)
      cycle();
    CHECK(port_out.prsp.pbits.popcode == DBID_RESP &&
          port_out.prsp.pbits.psrc_uid == ACLINT_ID &&
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
          port_out.pdat.presponse.pbits.psrc_uid == ACLINT_ID &&
          port_out.pdat.presponse.pbits.ptgt_uid == REQUESTER_ID &&
          port_out.pdat.presponse.pbits.ptxn_uid == return_txn_id &&
          port_out.pdat.presponse.pbits.pbyte_uenable == byte_enable &&
          wide_value(port_out.pdat.presponse.pbits.pdata) == expected);
    port_in.pdat.presponse.pready = UINT64_C(1);
    cycle();
    port_in.pdat.presponse.pready = UINT64_C(0);
  }
}

void write_register(std::uint16_t txn_id, std::uint64_t address,
                    std::uint8_t size, std::uint16_t byte_enable,
                    unsigned __int128 data) {
  {
    issue_request(WRITE_NO_SNP_PTL, txn_id, address, size, UINT64_C(0));
    accept_dbid(txn_id);
    issue_write_data(UINT64_C(0), byte_enable, data);
    accept_comp(txn_id);
  }
}

void read_register(std::uint16_t txn_id, std::uint64_t address,
                   std::uint8_t size, std::uint16_t byte_enable,
                   unsigned __int128 expected) {
  {
    issue_request(READ_NO_SNP, txn_id, address, size, txn_id + UINT64_C(1024));
    accept_read(txn_id + UINT64_C(1024), byte_enable, expected);
  }
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    reset = UINT64_C(1);
    identity = {.pnode_uid = ACLINT_ID};
    tick = UINT64_C(0);
    port_in = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      cycle();
    reset = UINT64_C(0);
    CHECK(time_counter == 0 && pack_bits(machine_software) == 0 &&
          pack_bits(machine_timer) == 0);
    CHECK(!time_update_out.pvalid);

    tick = UINT64_C(1);
    eval();
    CHECK(time_update_out.pvalid && time_update_out.pbits == 1);
    cycle();
    tick = UINT64_C(0);
    CHECK(time_counter == 1 && time_update_count == 1 && last_time_update == 1);

    write_register(UINT64_C(257), MTIME, UINT64_C(3), UINT64_C(65280),
                   UINT64_C(0));
    CHECK(time_update_count == 2 && last_time_update == 0);
    write_register(
        UINT64_C(258), MTIME + UINT64_C(4), UINT64_C(2), UINT64_C(61440),
        (uint128(UINT64_C(1311768464867721216)) << 64 | UINT64_C(0)));
    CHECK(time_update_count == 3 &&
          last_time_update == UINT64_C(1311768464867721216));
    read_register(UINT64_C(259), MTIME, UINT64_C(3), UINT64_C(65280),
                  (uint128(UINT64_C(1311768464867721216)) << 64 | UINT64_C(0)));
    CHECK(time_update_count == 3);

    write_register(UINT64_C(260), MTIMECMP0, UINT64_C(3), UINT64_C(255),
                   UINT64_C(1311768464867721218));
    CHECK(!machine_timer[0]);
    tick = UINT64_C(1);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      cycle();
    tick = UINT64_C(0);
    CHECK(machine_timer[0] && !machine_timer[1] && time_update_count == 5 &&
          last_time_update == UINT64_C(1311768464867721218));

    write_register(UINT64_C(261), MSWI1, UINT64_C(2), UINT64_C(16),
                   UINT64_C(4294967296));
    CHECK(pack_bits(machine_software) == UINT64_C(2));
    read_register(UINT64_C(262), MSWI1, UINT64_C(2), UINT64_C(240),
                  UINT64_C(4294967296));
    write_register(UINT64_C(263), MSWI1, UINT64_C(2), UINT64_C(16),
                   UINT64_C(0));
    CHECK(pack_bits(machine_software) == 0);
    read_register(UINT64_C(264), MSWI0, UINT64_C(2), UINT64_C(15), UINT64_C(0));

    write_register(
        UINT64_C(265), MTIME, UINT64_C(3), UINT64_C(33024),
        (uint128(UINT64_C(12321848580485677261)) << 64 | UINT64_C(0)));
    CHECK(time_counter == UINT64_C(12336580352670695629) &&
          time_update_count == 6 && last_time_update == time_counter);
    write_register(UINT64_C(266), MTIME, UINT64_C(3), UINT64_C(0), UINT64_MAX);
    CHECK(time_counter == UINT64_C(12336580352670695629) &&
          time_update_count == 7 && last_time_update == time_counter);
    read_register(
        UINT64_C(267), MTIME, UINT64_C(3), UINT64_C(65280),
        (uint128(UINT64_C(12336580352670695629)) << 64 | UINT64_C(0)));
  });
}
