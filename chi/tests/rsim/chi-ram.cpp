// Simulates CHIRam masks, concurrent allocation/DAT, reset recovery, stalls,
// and address-space-end access.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
inline const auto &response_data_out = port_out.pdat.presponse;
inline const auto &request_data_out = port_out.pdat.prequest;
inline const auto &requests_out = port_out.preq;
inline const auto &responses_out = port_out.prsp;
using CHIReqFlit = std::remove_cvref_t<decltype(port_in.preq.pbits)>;
using CHIRspFlit = std::remove_cvref_t<decltype(port_out.prsp.pbits)>;
using CHIDatFlit = std::remove_cvref_t<decltype(port_in.pdat.prequest.pbits)>;
using dat_t = std::remove_cvref_t<decltype(port_in.pdat.prequest)>;
using rsp_t = std::remove_cvref_t<decltype(port_out.prsp)>;
using req_t = std::remove_cvref_t<decltype(port_in.preq)>;
inline auto &responses_in = port_in.prsp;
inline auto &requests_in = port_in.preq;
inline auto &request_data_in = port_in.pdat.prequest;
inline auto &response_data_in = port_in.pdat.presponse;
constexpr std::uint8_t READ_NO_SNP = UINT64_C(4);
constexpr std::uint8_t WRITE_NO_SNP_PTL = UINT64_C(28);
constexpr std::uint8_t WRITE_NO_SNP_FULL = UINT64_C(29);
constexpr std::uint8_t COMP = UINT64_C(4);
constexpr std::uint8_t DBID_RESP = UINT64_C(6);
constexpr std::uint8_t NON_COPY_BACK_WRITE_DATA = UINT64_C(3);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t REQUESTER_ID = UINT64_C(3);
constexpr std::uint8_t RAM_ID = UINT64_C(9);

void issue_request(std::uint8_t opcode, std::uint16_t txn_id,
                   std::uint64_t address, std::uint8_t size,
                   std::uint16_t return_txn_id) {
  {
    requests_in.pbits = {};
    requests_in.pbits.popcode = opcode;
    requests_in.pbits.psrc_uid = REQUESTER_ID;
    requests_in.pbits.ptgt_uid = RAM_ID;
    requests_in.pbits.ptxn_uid = txn_id;
    requests_in.pbits.paddress = address;
    requests_in.pbits.psize_uor_unum_ureq = size;
    requests_in.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
        REQUESTER_ID;
    requests_in.pbits.preturn_utxn_uid_uor_ustash_ulpid = return_txn_id;
    requests_in.pvalid = UINT64_C(1);
    while (!requests_out.pready)
      tick_model();
    tick_model();
    requests_in.pvalid = UINT64_C(0);
    requests_in.pbits = {};
  }
}

void issue_write_data(std::uint16_t dbid, std::uint8_t data_id,
                      std::uint16_t byte_enable, unsigned __int128 data) {
  {
    request_data_in.pbits = {};
    request_data_in.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
    request_data_in.pbits.psrc_uid = REQUESTER_ID;
    request_data_in.pbits.ptgt_uid = RAM_ID;
    request_data_in.pbits.ptxn_uid = dbid;
    request_data_in.pbits.pdata_uid = data_id;
    request_data_in.pbits.pbyte_uenable = byte_enable;
    request_data_in.pbits.pdata = wide(data);
    request_data_in.pvalid = UINT64_C(1);
    while (!request_data_out.pready)
      tick_model();
    tick_model();
    request_data_in.pvalid = UINT64_C(0);
    request_data_in.pbits = {};
  }
}

void wait_rsp() {
  int cycles;
  {
    cycles = 0;
    while (!responses_out.pvalid && cycles < 100) {
      tick_model();
      cycles = cycles + 1;
    }
    CHECK(responses_out.pvalid);
  }
}

void wait_dat() {
  int cycles;
  {
    cycles = 0;
    while (!response_data_out.pvalid && cycles < 100) {
      tick_model();
      cycles = cycles + 1;
    }
    CHECK(response_data_out.pvalid);
  }
}

void accept_dbid(std::uint16_t request_txn_id, std::uint16_t &dbid) {
  {
    responses_in.pready = UINT64_C(1);
    wait_rsp();
    CHECK(responses_out.pbits.popcode == DBID_RESP);
    CHECK(responses_out.pbits.psrc_uid == RAM_ID &&
          responses_out.pbits.ptgt_uid == REQUESTER_ID &&
          responses_out.pbits.ptxn_uid == request_txn_id);
    dbid = responses_out.pbits.pdbid_uor_ugroup_uid;
    tick_model();
    responses_in.pready = UINT64_C(0);
  }
}

void accept_comp(std::uint16_t request_txn_id, std::uint16_t dbid) {
  {
    responses_in.pready = UINT64_C(1);
    wait_rsp();
    CHECK(responses_out.pbits.popcode == COMP);
    CHECK(responses_out.pbits.psrc_uid == RAM_ID &&
          responses_out.pbits.ptgt_uid == REQUESTER_ID &&
          responses_out.pbits.ptxn_uid == request_txn_id &&
          responses_out.pbits.pdbid_uor_ugroup_uid == dbid);
    tick_model();
    responses_in.pready = UINT64_C(0);
  }
}

void accept_read(std::uint16_t return_txn_id, std::uint8_t data_id,
                 std::uint16_t byte_enable, unsigned __int128 data) {
  {
    response_data_in.pready = UINT64_C(1);
    wait_dat();
    CHECK(response_data_out.pbits.popcode == COMP_DATA);
    CHECK(response_data_out.pbits.psrc_uid == RAM_ID &&
          response_data_out.pbits.ptgt_uid == REQUESTER_ID &&
          response_data_out.pbits.ptxn_uid == return_txn_id);
    CHECK(response_data_out.pbits.pbyte_uenable == byte_enable);
    CHECK(response_data_out.pbits.pdata_uid == data_id);
    CHECK(response_data_out.pbits.pdata == wide(data));
    tick_model();
    response_data_in.pready = UINT64_C(0);
  }
}

std::uint16_t dbid_a;
std::uint16_t dbid_b;
bool saw_concurrent_allocation_data = 0;
void run_case() {
  reset = UINT64_C(1);
  identity = {.pnode_uid = RAM_ID, .pbase_uaddress = UINT64_C(2147483648)};
  requests_in = {};
  request_data_in = {};
  responses_in = {};
  response_data_in = {};
  tick_model();
  CHECK(!responses_out.pvalid && !response_data_out.pvalid);
  reset = UINT64_C(0);

  issue_request(READ_NO_SNP, UINT64_C(257), UINT64_C(2147483648), UINT64_C(4),
                UINT64_C(1281));
  accept_read(UINT64_C(1281), UINT64_C(0), UINT64_C(65535), UINT64_C(0));

  issue_request(WRITE_NO_SNP_FULL, UINT64_C(258), UINT64_C(2147483648),
                UINT64_C(4), UINT64_C(0));
  accept_dbid(UINT64_C(258), dbid_a);
  issue_write_data(dbid_a, UINT64_C(0), UINT64_C(65535),
                   ((uint128(UINT64_C(4822678189205111)) << 64) |
                    UINT64_C(9843086184167632639)));
  accept_comp(UINT64_C(258), dbid_a);

  issue_request(WRITE_NO_SNP_PTL, UINT64_C(259), UINT64_C(2147483648),
                UINT64_C(4), UINT64_C(0));
  accept_dbid(UINT64_C(259), dbid_a);
  issue_write_data(dbid_a, UINT64_C(0), UINT64_C(255),
                   ((uint128(UINT64_C(18441921395520346504)) << 64) |
                    UINT64_C(8603657889541918976)));
  accept_comp(UINT64_C(259), dbid_a);

  issue_request(READ_NO_SNP, UINT64_C(260), UINT64_C(2147483648), UINT64_C(4),
                UINT64_C(1284));
  accept_read(UINT64_C(1284), UINT64_C(0), UINT64_C(65535),
              ((uint128(UINT64_C(4822678189205111)) << 64) |
               UINT64_C(8603657889541918976)));

  issue_request(WRITE_NO_SNP_FULL, UINT64_C(261), UINT64_C(2147483648),
                UINT64_C(6), UINT64_C(0));
  accept_dbid(UINT64_C(261), dbid_a);
  issue_write_data(dbid_a, UINT64_C(2), UINT64_C(65535),
                   ((uint128(UINT64_C(2459565876494606882)) << 64) |
                    UINT64_C(2459565876494606882)));
  issue_write_data(dbid_a, UINT64_C(0), UINT64_C(65535), UINT64_C(0));
  issue_write_data(dbid_a, UINT64_C(3), UINT64_C(65535),
                   ((uint128(UINT64_C(3689348814741910323)) << 64) |
                    UINT64_C(3689348814741910323)));
  issue_write_data(dbid_a, UINT64_C(1), UINT64_C(65535),
                   ((uint128(UINT64_C(1229782938247303441)) << 64) |
                    UINT64_C(1229782938247303441)));
  accept_comp(UINT64_C(261), dbid_a);

  issue_request(READ_NO_SNP, UINT64_C(262), UINT64_C(2147483648), UINT64_C(6),
                UINT64_C(1286));
  accept_read(UINT64_C(1286), UINT64_C(0), UINT64_C(65535), UINT64_C(0));
  accept_read(UINT64_C(1286), UINT64_C(1), UINT64_C(65535),
              ((uint128(UINT64_C(1229782938247303441)) << 64) |
               UINT64_C(1229782938247303441)));
  accept_read(UINT64_C(1286), UINT64_C(2), UINT64_C(65535),
              ((uint128(UINT64_C(2459565876494606882)) << 64) |
               UINT64_C(2459565876494606882)));
  accept_read(UINT64_C(1286), UINT64_C(3), UINT64_C(65535),
              ((uint128(UINT64_C(3689348814741910323)) << 64) |
               UINT64_C(3689348814741910323)));

  issue_request(WRITE_NO_SNP_FULL, UINT64_C(513), UINT64_C(2147483664),
                UINT64_C(4), UINT64_C(0));
  issue_request(WRITE_NO_SNP_FULL, UINT64_C(514), UINT64_C(2147483680),
                UINT64_C(4), UINT64_C(0));
  accept_dbid(UINT64_C(513), dbid_a);
  accept_dbid(UINT64_C(514), dbid_b);
  CHECK(dbid_a != dbid_b);

  issue_write_data(dbid_a, UINT64_C(1), UINT64_C(65535),
                   ((uint128(UINT64_C(1229782938533634594)) << 64) |
                    UINT64_C(3689348815028241476)));
  issue_write_data(dbid_b, UINT64_C(2), UINT64_C(65535),
                   ((uint128(UINT64_C(12297829382759365563)) << 64) |
                    UINT64_C(14757395259253972445)));
  accept_comp(UINT64_C(513), dbid_a);
  accept_comp(UINT64_C(514), dbid_b);

  issue_request(READ_NO_SNP, UINT64_C(515), UINT64_C(2147483664), UINT64_C(4),
                UINT64_C(1283));
  accept_read(UINT64_C(1283), UINT64_C(1), UINT64_C(65535),
              ((uint128(UINT64_C(1229782938533634594)) << 64) |
               UINT64_C(3689348815028241476)));
  issue_request(READ_NO_SNP, UINT64_C(516), UINT64_C(2147483680), UINT64_C(5),
                UINT64_C(1284));
  accept_read(UINT64_C(1284), UINT64_C(2), UINT64_C(65535),
              ((uint128(UINT64_C(12297829382759365563)) << 64) |
               UINT64_C(14757395259253972445)));
  accept_read(UINT64_C(1284), UINT64_C(3), UINT64_C(65535),
              ((uint128(UINT64_C(3689348814741910323)) << 64) |
               UINT64_C(3689348814741910323)));

  issue_request(WRITE_NO_SNP_FULL, UINT64_C(769), UINT64_C(2147483648),
                UINT64_C(5), UINT64_C(0));
  accept_dbid(UINT64_C(769), dbid_a);
  // Offer allocation and write data together, sample both transfers before the
  // edge.
  requests_in = {};
  requests_in.pvalid = 1;
  requests_in.pbits.popcode = WRITE_NO_SNP_FULL;
  requests_in.pbits.psrc_uid = REQUESTER_ID;
  requests_in.pbits.ptgt_uid = RAM_ID;
  requests_in.pbits.ptxn_uid = 770;
  requests_in.pbits.paddress = 0x80000020;
  requests_in.pbits.psize_uor_unum_ureq = 4;
  request_data_in = {};
  request_data_in.pvalid = 1;
  request_data_in.pbits.popcode = NON_COPY_BACK_WRITE_DATA;
  request_data_in.pbits.psrc_uid = REQUESTER_ID;
  request_data_in.pbits.ptgt_uid = RAM_ID;
  request_data_in.pbits.ptxn_uid = dbid_a;
  request_data_in.pbits.pdata_uid = 1;
  request_data_in.pbits.pbyte_uenable = 65535;
  request_data_in.pbits.pdata =
      wide((uint128(0x5555555555555555ULL) << 64) | 0x5555555555555555ULL);
  while (requests_in.pvalid || request_data_in.pvalid) {
    eval();
    bool req_fire = requests_in.pvalid && requests_out.pready;
    bool dat_fire = request_data_in.pvalid && request_data_out.pready;
    saw_concurrent_allocation_data |= req_fire && dat_fire;
    tick_model();
    if (req_fire)
      requests_in = {};
    if (dat_fire)
      request_data_in = {};
  }
  CHECK(saw_concurrent_allocation_data);
  accept_dbid(UINT64_C(770), dbid_b);
  CHECK(dbid_a != dbid_b);
  issue_write_data(dbid_a, UINT64_C(0), UINT64_C(65535),
                   ((uint128(UINT64_C(4919131752989213764)) << 64) |
                    UINT64_C(4919131752989213764)));
  accept_comp(UINT64_C(769), dbid_a);
  issue_write_data(dbid_b, UINT64_C(2), UINT64_C(65535),
                   ((uint128(UINT64_C(7378697629483820646)) << 64) |
                    UINT64_C(7378697629483820646)));
  accept_comp(UINT64_C(770), dbid_b);
  issue_request(READ_NO_SNP, UINT64_C(771), UINT64_C(2147483648), UINT64_C(5),
                UINT64_C(1539));
  accept_read(UINT64_C(1539), UINT64_C(0), UINT64_C(65535),
              ((uint128(UINT64_C(4919131752989213764)) << 64) |
               UINT64_C(4919131752989213764)));
  accept_read(UINT64_C(1539), UINT64_C(1), UINT64_C(65535),
              ((uint128(UINT64_C(6148914691236517205)) << 64) |
               UINT64_C(6148914691236517205)));
  issue_request(READ_NO_SNP, UINT64_C(772), UINT64_C(2147483680), UINT64_C(4),
                UINT64_C(1540));
  accept_read(UINT64_C(1540), UINT64_C(2), UINT64_C(65535),
              ((uint128(UINT64_C(7378697629483820646)) << 64) |
               UINT64_C(7378697629483820646)));

  issue_request(WRITE_NO_SNP_FULL, UINT64_C(773), UINT64_C(2147483648),
                UINT64_C(5), UINT64_C(0));
  accept_dbid(UINT64_C(773), dbid_a);
  issue_write_data(dbid_a, UINT64_C(1), UINT64_C(65535), UINT64_C(30583));
  reset = UINT64_C(1);
  tick_model();
  reset = UINT64_C(0);
  tick_model();
  CHECK(!responses_out.pvalid && !response_data_out.pvalid);
  issue_request(WRITE_NO_SNP_FULL, UINT64_C(774), UINT64_C(2147483664),
                UINT64_C(4), UINT64_C(0));
  accept_dbid(UINT64_C(774), dbid_b);
  issue_write_data(dbid_b, UINT64_C(1), UINT64_C(65535), UINT64_C(34952));
  accept_comp(UINT64_C(774), dbid_b);
  issue_request(READ_NO_SNP, UINT64_C(775), UINT64_C(2147483664), UINT64_C(4),
                UINT64_C(1543));
  accept_read(UINT64_C(1543), UINT64_C(1), UINT64_C(65535), UINT64_C(34952));

  issue_request(READ_NO_SNP, UINT64_C(776), UINT64_C(2147483648), UINT64_C(4),
                UINT64_C(1544));
  wait_dat();
  issue_request(WRITE_NO_SNP_FULL, UINT64_C(777), UINT64_C(2147483680),
                UINT64_C(4), UINT64_C(0));
  accept_dbid(UINT64_C(777), dbid_a);
  issue_write_data(dbid_a, UINT64_C(2), UINT64_C(65535), UINT64_C(39321));
  accept_comp(UINT64_C(777), dbid_a);
  CHECK(response_data_out.pvalid);
  accept_read(UINT64_C(1544), UINT64_C(0), UINT64_C(65535),
              ((uint128(UINT64_C(4919131752989213764)) << 64) |
               UINT64_C(4919131752989213764)));

  identity.pbase_uaddress = UINT64_C(17592186044352);
  issue_request(READ_NO_SNP, UINT64_C(517), UINT64_C(17592186044400),
                UINT64_C(4), UINT64_C(1285));
  accept_read(UINT64_C(1285), UINT64_C(3), UINT64_C(65535),
              ((uint128(UINT64_C(3689348814741910323)) << 64) |
               UINT64_C(3689348814741910323)));
}

void invalid_address() {
  reset = 1;

  identity = {.pnode_uid = UINT64_C(9), .pbase_uaddress = UINT64_C(2147483648)};
  requests_in = {};
  request_data_in = {};
  responses_in = {};
  response_data_in = {};
  tick_model();
  reset = UINT64_C(0);

  requests_in.pbits = {};
  requests_in.pbits.popcode = UINT64_C(4);
  requests_in.pbits.psrc_uid = UINT64_C(3);
  requests_in.pbits.ptgt_uid = UINT64_C(9);
  requests_in.pbits.ptxn_uid = UINT64_C(769);
  requests_in.pbits.paddress = UINT64_C(2147483712);
  requests_in.pbits.psize_uor_unum_ureq = UINT64_C(4);
  requests_in.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
      UINT64_C(3);
  requests_in.pbits.preturn_utxn_uid_uor_ustash_ulpid = UINT64_C(1793);
  requests_in.pvalid = UINT64_C(1);
  while (!requests_out.pready)
    tick_model();
  tick_model();
}
int main() {
  return run_test([] {
    run_case();
    dut = Model{};
    expect_failure("chi_ram_request_address_supported", invalid_address);
  });
}
