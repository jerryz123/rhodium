// Verifies CHI BootROM reads, byte lanes, backpressure, and
// top-of-address-space containment.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "wide.hpp"
constexpr std::uint8_t READ_NO_SNP = UINT64_C(4);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t REQUESTER_ID = UINT64_C(3);
constexpr std::uint8_t BOOTROM_ID = UINT64_C(12);
constexpr std::uint64_t BOOTROM_BASE = UINT64_C(65536);
constexpr unsigned __int128 BEAT_0 =
    (uint128(UINT64_C(18412269522275730839)) << 64 |
     UINT64_C(1436391435150707));
constexpr unsigned __int128 BEAT_1 =
    (uint128(UINT64_C(18437719759283814515)) << 64 |
     UINT64_C(18375390611642450583));

void cycle() {
  {
    tick_model();
  }
}

void issue_read(std::uint16_t txn_id, std::uint64_t address, std::uint8_t size,
                std::uint16_t return_txn_id) {
  {
    port_in.preq.pbits = {};
    port_in.preq.pbits.popcode = READ_NO_SNP;
    port_in.preq.pbits.psrc_uid = REQUESTER_ID;
    port_in.preq.pbits.ptgt_uid = BOOTROM_ID;
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

void check_response(std::uint8_t data_id, std::uint16_t return_txn_id,
                    std::uint16_t byte_enable, unsigned __int128 expected) {
  {
    while (!port_out.pdat.presponse.pvalid)
      cycle();
    CHECK(port_out.pdat.presponse.pbits.popcode == COMP_DATA &&
          port_out.pdat.presponse.pbits.psrc_uid == BOOTROM_ID &&
          port_out.pdat.presponse.pbits.ptgt_uid == REQUESTER_ID &&
          port_out.pdat.presponse.pbits.ptxn_uid == return_txn_id &&
          port_out.pdat.presponse.pbits.pdata_uid == data_id &&
          port_out.pdat.presponse.pbits.pbyte_uenable == byte_enable &&
          wide_value(port_out.pdat.presponse.pbits.pdata) == expected);
    cycle();
  }
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    reset = UINT64_C(1);
    identity = {.pnode_uid = BOOTROM_ID, .pbase_uaddress = BOOTROM_BASE};
    port_in = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      cycle();
    reset = UINT64_C(0);

    issue_read(UINT64_C(257), BOOTROM_BASE, UINT64_C(6), UINT64_C(1281));
    while (!port_out.pdat.presponse.pvalid)
      cycle();
    CHECK(wide_value(port_out.pdat.presponse.pbits.pdata) == BEAT_0);
    cycle();
    CHECK(port_out.pdat.presponse.pvalid &&
          port_out.pdat.presponse.pbits.pdata_uid == 0 &&
          wide_value(port_out.pdat.presponse.pbits.pdata) == BEAT_0);

    port_in.pdat.presponse.pready = UINT64_C(1);
    check_response(UINT64_C(0), UINT64_C(1281), UINT64_C(65535), BEAT_0);
    check_response(UINT64_C(1), UINT64_C(1281), UINT64_C(65535), BEAT_1);
    check_response(UINT64_C(2), UINT64_C(1281), UINT64_C(65535), UINT64_C(0));
    check_response(UINT64_C(3), UINT64_C(1281), UINT64_C(65535), UINT64_C(0));
    port_in.pdat.presponse.pready = UINT64_C(0);

    issue_read(UINT64_C(258), BOOTROM_BASE + UINT64_C(4), UINT64_C(2),
               UINT64_C(1282));
    port_in.pdat.presponse.pready = UINT64_C(1);
    check_response(UINT64_C(0), UINT64_C(1282), UINT64_C(240), BEAT_0);
    port_in.pdat.presponse.pready = UINT64_C(0);

    issue_read(UINT64_C(259), BOOTROM_BASE + UINT64_C(16), UINT64_C(2),
               UINT64_C(1283));
    port_in.pdat.presponse.pready = UINT64_C(1);
    check_response(UINT64_C(1), UINT64_C(1283), UINT64_C(15), BEAT_1);
    port_in.pdat.presponse.pready = UINT64_C(0);

    CHECK(!port_out.prsp.pvalid);

    identity.pbase_uaddress = UINT64_C(17592186044352);
    issue_read(UINT64_C(260), UINT64_C(17592186044400), UINT64_C(4),
               UINT64_C(1284));
    port_in.pdat.presponse.pready = UINT64_C(1);
    check_response(UINT64_C(3), UINT64_C(1284), UINT64_C(65535), UINT64_C(0));
    port_in.pdat.presponse.pready = UINT64_C(0);
    port_in.preq.pbits.popcode = READ_NO_SNP;
    port_in.preq.pbits.ptgt_uid = BOOTROM_ID;
    port_in.preq.pbits.psize_uor_unum_ureq = UINT64_C(4);
    port_in.preq.pbits.paddress = UINT64_C(17592186044336);
    port_in.preq.pvalid = UINT64_C(1);

    eval();
    CHECK(!port_out.preq.pready);
    port_in.preq = {};
  });
}
