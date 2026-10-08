// Verifies coherent and ROM instruction snapshots, VIPT hits, refill errors,
// and FENCE.I cancellation.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../chi/tests/rsim/request.hpp"
#include "test.hpp"
#include "wide.hpp"
using CHIReqFlit = std::remove_cvref_t<decltype(chi_out.preq.pbits)>;
using CHIRspFlit = std::remove_cvref_t<decltype(chi_out.prsp.prequester.pbits)>;
constexpr unsigned FETCH_BITS = sizeof(core_out.presponse.pbits.pdata) * 8;
using Line = std::array<std::uint64_t, 8>;
Line filled_line(std::uint32_t value) {
  Line line;
  line.fill((std::uint64_t(value) << 32) | value);
  return line;
}
bool rom_phase = false, forbid_core_response = false, probe_only = false,
     lookup_override = false;
std::remove_cvref_t<decltype(virtual_lookup_in)> staged_lookup{};
std::uint64_t virtual_page_xor = 0x40000000;
void settle() {
  virtual_lookup_in =
      lookup_override
          ? staged_lookup
          : decltype(staged_lookup){
                .pvalid = std::uint8_t(core_in.prequest.pvalid || probe_only),
                .pbits = core_in.prequest.pbits.paddress ^ virtual_page_xor};
  eval();
}
std::uint64_t line_slice(const Line &line, unsigned bit, unsigned width) {
  return (line[bit / 64] >> (bit % 64)) & low_mask(width);
}
constexpr std::uint8_t READ_ONCE = UINT64_C(3);
constexpr std::uint8_t COMP_ACK = UINT64_C(2);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t HOME_ID = UINT64_C(1);
constexpr std::uint8_t CACHE_ID = UINT64_C(2);
void cache_tick() {
  settle();
  if (rom_phase)
    CHECK(!chi_out.prsp.prequester.pvalid);
  if (forbid_core_response)
    CHECK(!core_out.presponse.pvalid);
  tick_model();
  settle();
}
void send_prefetch(std::uint64_t address) {
  {
    prefetch_in.pbits.paddress = address;
    prefetch_in.pbits.poperation = UINT64_C(1);
    prefetch_in.pvalid = UINT64_C(1);
    cache_tick();
    prefetch_in = {};
  }
}

void grant_req_credit() {
  {
    chi_in.preq.pready = UINT64_C(1);
  }
}

void grant_rsp_credit() {
  {
    chi_in.prsp.prequester.pready = UINT64_C(1);
  }
}

void wait_dat_credit() {
  int cycles;
  {
    cycles = 0;
    while (!chi_out.pdat.presponse.pready && cycles < 50) {
      cache_tick();
      cycles = cycles + 1;
    }
    CHECK(chi_out.pdat.presponse.pready);
  }
}

void send_core_request(std::uint64_t address) {
  int cycles;
  {
    cycles = 0;
    core_in.prequest.pbits.paddress = address;
    probe_only = UINT64_C(1);

    settle();
    while (!virtual_lookup_out.pready && cycles < 200) {
      cache_tick();
      cycles++;
    }
    CHECK(virtual_lookup_out.pready);
    cache_tick();
    probe_only = UINT64_C(0);
    core_in.prequest.pvalid = UINT64_C(1);
    cache_tick();
    core_in.prequest.pvalid = UINT64_C(0);
  }
}

void accept_read_request(std::uint64_t address) {
  int cycles;
  {
    cycles = 0;
    while (!chi_out.preq.pvalid && cycles < 100) {
      cache_tick();
      cycles = cycles + 1;
    }
    CHECK(chi_out.preq.pvalid);
    CHECK(chi_out.preq.pbits.popcode == READ_ONCE &&
          chi_out.preq.pbits.psrc_uid == CACHE_ID &&
          chi_out.preq.pbits.ptgt_uid == HOME_ID &&
          chi_out.preq.pbits.ptxn_uid == UINT64_C(0) &&
          chi_out.preq.pbits.preturn_utxn_uid_uor_ustash_ulpid == UINT64_C(0) &&
          chi_out.preq.pbits.paddress == slice(address, 43, 0) &&
          chi_out.preq.pbits.psize_uor_unum_ureq == UINT64_C(6) &&
          chi_out.preq.pbits.pexp_ucomp_uack &&
          chi_out.preq.pbits.psnp_uattr_uor_udo_udwt &&
          chi_out.preq.pbits.pmem_uattr.pallocate == 1 &&
          chi_out.preq.pbits.pmem_uattr.pcacheable == 1 &&
          chi_out.preq.pbits.pmem_uattr.pdevice == 0 &&
          chi_out.preq.pbits.pmem_uattr.pearly_uwrite_uacknowledge == 1 &&
          chi_out.preq.pbits.pallow_uretry &&
          chi_out.preq.pbits.ppcrd_utype == UINT64_C(0));
    cache_tick();
  }
}

void return_line(std::uint64_t address, const Line &line, int gap = 0,
                 std::uint8_t error = 0) {
  int packet;
  {
    for (packet = 3; packet >= 0; packet = packet - 1) {
      for (unsigned repeat_index = 0; repeat_index < (gap); ++repeat_index)
        cache_tick();
      wait_dat_credit();
      chi_in.pdat.presponse.pbits = {};
      chi_in.pdat.presponse.pbits.pdata =
          wide((uint128(line[2 * packet + 1]) << 64) | line[2 * packet]);
      chi_in.pdat.presponse.pbits.pbyte_uenable = UINT64_C(65535);
      chi_in.pdat.presponse.pbits.pdata_uid =
          slice(address, 5, 4) + slice(packet, 1, 0);
      chi_in.pdat.presponse.pbits.presp = UINT64_C(0);
      chi_in.pdat.presponse.pbits.presp_uerr = error;
      chi_in.pdat.presponse.pbits.popcode = COMP_DATA;
      chi_in.pdat.presponse.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
          rom_phase ? UINT64_C(4) : HOME_ID;
      chi_in.pdat.presponse.pbits.pdbid_uor_umecid = UINT64_C(85);
      chi_in.pdat.presponse.pbits.ptxn_uid = UINT64_C(0);
      chi_in.pdat.presponse.pbits.psrc_uid = rom_phase ? UINT64_C(4) : HOME_ID;
      chi_in.pdat.presponse.pbits.ptgt_uid = CACHE_ID;
      chi_in.pdat.presponse.pvalid = UINT64_C(1);
      cache_tick();
      chi_in.pdat.presponse.pvalid = UINT64_C(0);
      chi_in.pdat.presponse.pbits = {};
    }
  }
}

void accept_comp_ack() {
  int cycles;
  {
    cycles = 0;
    while (!chi_out.prsp.prequester.pvalid && cycles < 100) {
      cache_tick();
      cycles = cycles + 1;
    }
    CHECK(chi_out.prsp.prequester.pvalid);
    CHECK(chi_out.prsp.prequester.pbits.popcode == COMP_ACK &&
          chi_out.prsp.prequester.pbits.psrc_uid == CACHE_ID &&
          chi_out.prsp.prequester.pbits.ptgt_uid == HOME_ID &&
          chi_out.prsp.prequester.pbits.ptxn_uid == UINT64_C(85));
    cache_tick();
  }
}

void accept_rom_request(std::uint64_t address) {
  for (int c = 0; !chi_out.preq.pvalid && c < 100; c++)
    cache_tick();
  CHECK(chi_out.preq.pvalid && chi_out.preq.pbits.popcode == UINT64_C(4) &&
        chi_out.preq.pbits.paddress == slice(address, 43, 0) &&
        chi_out.preq.pbits.psize_uor_unum_ureq == 6 &&
        chi_out.preq.pbits.psrc_uid == CACHE_ID &&
        chi_out.preq.pbits.ptgt_uid == 4 &&
        !chi_out.preq.pbits.pexp_ucomp_uack &&
        !chi_out.preq.pbits.pallow_uretry &&
        !chi_out.preq.pbits.pmem_uattr.pallocate &&
        !chi_out.preq.pbits.pmem_uattr.pcacheable &&
        !chi_out.preq.pbits.pmem_uattr.pdevice &&
        !chi_out.preq.pbits.pmem_uattr.pearly_uwrite_uacknowledge);
  cache_tick();
}

void wait_result() {
  int attempts;
  attempts = 0;
  while ((!core_out.presponse.pvalid || core_out.presponse.pbits.preplay) &&
         attempts < 100) {
    send_core_request(core_in.prequest.pbits.paddress);
    attempts++;
  }
  CHECK(core_out.presponse.pvalid && !core_out.presponse.pbits.preplay);
}

void expect_instruction(std::uint32_t instruction) {
  int cycles;
  {
    wait_result();
    CHECK(core_out.presponse.pvalid &&
          !core_out.presponse.pbits.paccess_ufault &&
          slice(core_out.presponse.pbits.pdata, 31, 0) == instruction);
    cache_tick();
  }
}

void invalidate_cache(std::uint64_t address) {
  core_in.pinvalidate_uall = 1;
  cache_tick();
  core_in.pinvalidate_uall = 0;
}

constexpr std::uint64_t ADDRESS = UINT64_C(4294967296);
constexpr std::uint64_t SECOND_ADDRESS = UINT64_C(4294967360);
constexpr std::uint64_t THIRD_ADDRESS = UINT64_C(4294967424);
constexpr std::uint64_t PREFETCH_ADDRESS = UINT64_C(4294967488);
constexpr Line LINE = {
    UINT64_C(2459565876208275729),  UINT64_C(4919131752702882611),
    UINT64_C(7378697629197489493),  UINT64_C(9838263505692096375),
    UINT64_C(11068046441648750592), UINT64_C(13527612320434006698),
    UINT64_C(15987178196928613580), UINT64_C(18446744073423220462)};

constexpr std::uint64_t COLLIDE_B_ADDRESS = ADDRESS + UINT64_C(256);
constexpr std::uint64_t COLLIDE_C_ADDRESS = ADDRESS + UINT64_C(512);
constexpr Line LINE_B = [] {
  auto value = LINE;
  value[0] = (value[0] & UINT64_C(0xffffffff00000000)) | UINT64_C(2981212593);
  return value;
}();
constexpr Line LINE_C = [] {
  auto value = LINE;
  value[0] = (value[0] & UINT64_C(0xffffffff00000000)) | UINT64_C(3250700737);
  return value;
}();

void expect_block(std::uint64_t expected) {
  wait_result();
  CHECK(!core_out.presponse.pbits.paccess_ufault &&
        ((core_out.presponse.pbits.pdata) & low_mask(64)) == expected);
  cache_tick();
}

void wide_fetch() {

  send_core_request(ADDRESS + UINT64_C(4032));
  accept_read_request(ADDRESS + UINT64_C(4032));
  return_line(ADDRESS + UINT64_C(4032), LINE, 1);
  accept_comp_ack();
  expect_block(line_slice(LINE, 0, 64));
  for (int block_index = 7; block_index >= 0; block_index--) {
    send_core_request(ADDRESS + UINT64_C(4032) +
                      ((block_index * 8) & low_mask(64)));
    expect_block(line_slice(LINE, block_index * 64, 64));
    CHECK(!chi_out.preq.pvalid);
  }

  lookup_override = 1;
  staged_lookup = {.pvalid = 1,
                   .pbits = (ADDRESS + UINT64_C(4032)) ^ virtual_page_xor};
  cache_tick();
  core_in.prequest = {.pvalid = 1,
                      .pbits = {.paddress = ADDRESS + UINT64_C(4032)}};
  staged_lookup.pbits = (ADDRESS + UINT64_C(4040)) ^ virtual_page_xor;
  cache_tick();
  CHECK(core_out.presponse.pvalid && !core_out.presponse.pbits.preplay &&
        ((core_out.presponse.pbits.pdata) & low_mask(64)) ==
            line_slice(LINE, 0, 64));
  core_in.prequest.pbits.paddress = ADDRESS + UINT64_C(4040);
  staged_lookup.pvalid = 0;
  cache_tick();
  core_in.prequest.pvalid = 0;
  CHECK(core_out.presponse.pvalid && !core_out.presponse.pbits.preplay &&
        ((core_out.presponse.pbits.pdata) & low_mask(64)) ==
            line_slice(LINE, 64, 64));
  cache_tick();
  lookup_override = 0;

  send_core_request(ADDRESS + UINT64_C(4096));
  accept_read_request(ADDRESS + UINT64_C(4096));

  core_in.pflush = 1;
  cache_tick();
  core_in.pflush = 0;
  return_line(ADDRESS + UINT64_C(4096), LINE_B);
  accept_comp_ack();
  for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index) {
    cache_tick();
    CHECK(!core_out.presponse.pvalid);
  }
  send_core_request(ADDRESS + UINT64_C(4096));
  expect_block(line_slice(LINE_B, 0, 64));

  invalidate_cache(ADDRESS);
  send_core_request(ADDRESS);
  accept_read_request(ADDRESS);
  core_in.pinvalidate_uall = 1;
  cache_tick();
  core_in.pinvalidate_uall = 0;
  return_line(ADDRESS, LINE_B);
  accept_comp_ack();
  for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
    cache_tick();
  send_core_request(ADDRESS);
  accept_read_request(ADDRESS);
  return_line(ADDRESS, LINE_C);
  accept_comp_ack();
  expect_block(line_slice(LINE_C, 0, 64));

  invalidate_cache(ADDRESS);
  send_core_request(ADDRESS);
  accept_read_request(ADDRESS);
  return_line(ADDRESS, LINE, 1, UINT64_C(2));
  accept_comp_ack();
  wait_result();
  CHECK(core_out.presponse.pbits.paccess_ufault);
  cache_tick();
  send_core_request(ADDRESS);
  accept_read_request(ADDRESS);
  return_line(ADDRESS, LINE);
  accept_comp_ack();
  expect_block(line_slice(LINE, 0, 64));

  rom_phase = 1;
  chi_in.prsp.prequester.pready = 0;
  send_core_request(UINT64_C(65536));
  accept_rom_request(UINT64_C(65536));
  return_line(UINT64_C(65536), LINE, 1);
  expect_block(line_slice(LINE, 0, 64));
  send_core_request(UINT64_C(65592));
  expect_block(line_slice(LINE, 448, 64));
}

void run_case() {
  reset = 1;
  node_id = CACHE_ID;
  core_in = {};
  prefetch_in = {};
  chi_in = {};
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
    cache_tick();
  reset = UINT64_C(0);
  grant_req_credit();
  grant_rsp_credit();

  if (FETCH_BITS == 64) {
    wide_fetch();
    return;
  }

  probe_only = UINT64_C(1);
  core_in.prequest.pbits.paddress = ADDRESS;
  for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index) {
    cache_tick();
    CHECK(!core_out.presponse.pvalid && !chi_out.preq.pvalid);
  }
  probe_only = UINT64_C(0);

  forbid_core_response = UINT64_C(1);
  send_prefetch(PREFETCH_ADDRESS);
  accept_read_request(PREFETCH_ADDRESS);
  return_line(PREFETCH_ADDRESS, LINE);
  accept_comp_ack();
  for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
    cache_tick();
  forbid_core_response = UINT64_C(0);
  send_core_request(PREFETCH_ADDRESS);
  expect_instruction(UINT64_C(286331153));

  virtual_page_xor = UINT64_C(2147483648);
  send_core_request(PREFETCH_ADDRESS);
  expect_instruction(UINT64_C(286331153));

  send_core_request(ADDRESS);
  accept_read_request(ADDRESS);
  return_line(ADDRESS, LINE);
  accept_comp_ack();
  expect_instruction(UINT64_C(286331153));

  send_core_request(ADDRESS + UINT64_C(4));
  expect_instruction(UINT64_C(572662306));
  send_core_request(ADDRESS + UINT64_C(8));
  expect_instruction(UINT64_C(858993459));
  send_core_request(ADDRESS + UINT64_C(12));
  expect_instruction(UINT64_C(1145324612));

  lookup_override = UINT64_C(1);
  staged_lookup = {.pvalid = UINT64_C(1), .pbits = ADDRESS ^ virtual_page_xor};
  settle();
  CHECK(virtual_lookup_out.pready);
  cache_tick();
  core_in.prequest = {.pvalid = UINT64_C(1), .pbits = {.paddress = ADDRESS}};
  staged_lookup.pbits = (ADDRESS + UINT64_C(4)) ^ virtual_page_xor;
  settle();
  CHECK(virtual_lookup_out.pready);
  cache_tick();
  CHECK(core_out.presponse.pvalid &&
        slice(core_out.presponse.pbits.pdata, 31, 0) == UINT64_C(286331153));
  core_in.prequest.pbits.paddress = ADDRESS + UINT64_C(4);
  staged_lookup.pvalid = UINT64_C(0);
  settle();
  cache_tick();
  core_in.prequest.pvalid = UINT64_C(0);
  CHECK(core_out.presponse.pvalid &&
        slice(core_out.presponse.pbits.pdata, 31, 0) == UINT64_C(572662306));
  cache_tick();
  lookup_override = UINT64_C(0);

  probe_only = 1;
  core_in.prequest.pbits.paddress = ADDRESS;
  cache_tick();
  probe_only = 0;
  core_in.prequest.pvalid = 1;
  core_in.ps1_ukill = 1;
  cache_tick();
  core_in.prequest.pvalid = 0;
  core_in.ps1_ukill = 0;
  CHECK(!core_out.presponse.pvalid);

  grant_rsp_credit();
  invalidate_cache(ADDRESS);
  grant_req_credit();
  grant_rsp_credit();
  send_core_request(ADDRESS);
  accept_read_request(ADDRESS);
  return_line(ADDRESS, LINE);
  accept_comp_ack();
  expect_instruction(UINT64_C(286331153));

  probe_only = UINT64_C(1);
  core_in.prequest.pbits.paddress = ADDRESS + UINT64_C(4);
  core_in.pflush = UINT64_C(1);
  settle();
  CHECK(virtual_lookup_out.pready);
  cache_tick();
  core_in.pflush = UINT64_C(0);
  probe_only = UINT64_C(0);
  core_in.prequest.pvalid = UINT64_C(1);
  cache_tick();
  core_in.prequest.pvalid = UINT64_C(0);
  expect_instruction(UINT64_C(572662306));
  probe_only = UINT64_C(1);
  core_in.prequest.pbits.paddress = ADDRESS;
  core_in.pinvalidate_uall = UINT64_C(1);
  settle();
  CHECK(virtual_lookup_out.pready);
  cache_tick();
  core_in.pinvalidate_uall = UINT64_C(0);
  probe_only = UINT64_C(0);
  core_in.prequest.pvalid = UINT64_C(1);
  cache_tick();
  core_in.prequest.pvalid = UINT64_C(0);
  grant_req_credit();
  grant_rsp_credit();
  accept_read_request(ADDRESS);
  return_line(ADDRESS, LINE);
  accept_comp_ack();
  expect_instruction(UINT64_C(286331153));

  grant_req_credit();
  grant_rsp_credit();
  send_core_request(SECOND_ADDRESS);
  accept_read_request(SECOND_ADDRESS);
  core_in.pinvalidate_uall = UINT64_C(1);
  cache_tick();
  core_in.pinvalidate_uall = UINT64_C(0);
  return_line(SECOND_ADDRESS, LINE);
  accept_comp_ack();
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
    cache_tick();
  CHECK(!core_out.presponse.pvalid);
  grant_req_credit();
  grant_rsp_credit();
  send_core_request(SECOND_ADDRESS);
  accept_read_request(SECOND_ADDRESS);
  return_line(SECOND_ADDRESS, LINE);
  accept_comp_ack();
  expect_instruction(UINT64_C(286331153));

  grant_req_credit();
  grant_rsp_credit();
  send_core_request(THIRD_ADDRESS);
  accept_read_request(THIRD_ADDRESS);
  core_in.pflush = UINT64_C(1);
  cache_tick();
  core_in.pflush = UINT64_C(0);
  return_line(THIRD_ADDRESS, LINE);
  accept_comp_ack();
  for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index) {
    cache_tick();
    CHECK(!core_out.presponse.pvalid);
  }
  send_core_request(THIRD_ADDRESS);
  expect_instruction(UINT64_C(286331153));

  core_in.pinvalidate_uall = UINT64_C(1);
  cache_tick();
  core_in.pinvalidate_uall = UINT64_C(0);
  grant_req_credit();
  grant_rsp_credit();
  send_core_request(ADDRESS);
  accept_read_request(ADDRESS);
  return_line(ADDRESS, LINE);
  accept_comp_ack();
  expect_instruction(UINT64_C(286331153));
  send_core_request(COLLIDE_B_ADDRESS);
  accept_read_request(COLLIDE_B_ADDRESS);
  return_line(COLLIDE_B_ADDRESS, LINE_B);
  accept_comp_ack();
  expect_instruction(UINT64_C(2981212593));
  send_core_request(ADDRESS);
  expect_instruction(UINT64_C(286331153));
  send_core_request(COLLIDE_B_ADDRESS);
  expect_instruction(UINT64_C(2981212593));

  send_core_request(ADDRESS);
  expect_instruction(UINT64_C(286331153));
  send_core_request(COLLIDE_C_ADDRESS);
  accept_read_request(COLLIDE_C_ADDRESS);
  return_line(COLLIDE_C_ADDRESS, LINE_C);
  accept_comp_ack();
  expect_instruction(UINT64_C(3250700737));
  send_core_request(ADDRESS);
  expect_instruction(UINT64_C(286331153));
  send_core_request(COLLIDE_C_ADDRESS);
  expect_instruction(UINT64_C(3250700737));
  grant_req_credit();
  grant_rsp_credit();
  send_core_request(COLLIDE_B_ADDRESS);
  accept_read_request(COLLIDE_B_ADDRESS);
  return_line(COLLIDE_B_ADDRESS, LINE_B);
  accept_comp_ack();
  expect_instruction(UINT64_C(2981212593));

  virtual_page_xor = UINT64_C(1073745920);
  send_core_request(ADDRESS + UINT64_C(4288));
  accept_read_request(ADDRESS + UINT64_C(4288));
  return_line(ADDRESS + UINT64_C(4288), LINE_B);
  accept_comp_ack();
  expect_instruction(UINT64_C(2981212593));
  virtual_page_xor = UINT64_C(1073750016);
  send_core_request(ADDRESS + UINT64_C(8384));
  accept_read_request(ADDRESS + UINT64_C(8384));
  return_line(ADDRESS + UINT64_C(8384), LINE_C);
  accept_comp_ack();
  expect_instruction(UINT64_C(3250700737));
  virtual_page_xor = UINT64_C(1073745920);
  send_core_request(ADDRESS + UINT64_C(4288));
  expect_instruction(UINT64_C(2981212593));

  for (int offset = 0; offset < 64; offset += 4) {
    core_in.pinvalidate_uall = 1;
    cache_tick();
    core_in.pinvalidate_uall = 0;
    grant_req_credit();
    grant_rsp_credit();
    send_core_request(ADDRESS + ((offset)&low_mask(64)));
    accept_read_request(ADDRESS);
    forbid_core_response = 1;
    return_line(ADDRESS, filled_line(UINT64_C(1437248085)), offset % 3);
    forbid_core_response = 0;
    accept_comp_ack();
    wait_result();
    CHECK(core_out.presponse.pvalid &&
          slice(core_out.presponse.pbits.pdata, 31, 0) == UINT64_C(1437248085));
    grant_rsp_credit();
    invalidate_cache(ADDRESS);
    grant_req_credit();
    grant_rsp_credit();
    send_core_request(ADDRESS + ((offset)&low_mask(64)));
    accept_read_request(ADDRESS);
    return_line(ADDRESS, filled_line(UINT64_C(2857719210)), offset % 3);
    accept_comp_ack();
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      cache_tick();
    expect_instruction(UINT64_C(2857719210));
  }

  invalidate_cache(ADDRESS);
  chi_in.preq.pready = 0;
  send_core_request(ADDRESS);
  for (int c = 0; !chi_out.preq.pvalid && c < 100; c++)
    cache_tick();
  {
    CHIReqFlit saved;
    saved = chi_out.preq.pbits;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index) {
      cache_tick();
      CHECK(chi_out.preq.pvalid && same_request(chi_out.preq.pbits, saved));
    }
  }
  chi_in.preq.pready = 1;
  accept_read_request(ADDRESS);
  for (int event_index = 0; event_index < 2; event_index++) {
    chi_in.prsp.presponse.pbits = {};
    chi_in.prsp.presponse.pbits.popcode =
        event_index == 0 ? UINT64_C(3) : UINT64_C(7);
    chi_in.prsp.presponse.pbits.psrc_uid = HOME_ID;
    chi_in.prsp.presponse.pbits.ptgt_uid = CACHE_ID;
    chi_in.prsp.presponse.pbits.ppcrd_utype = 2;
    chi_in.prsp.presponse.pvalid = 1;
    settle();
    for (int c = 0; !chi_out.prsp.presponse.pready && c < 100; c++)
      cache_tick();
    CHECK(chi_out.prsp.presponse.pready);
    cache_tick();
    chi_in.prsp.presponse.pvalid = 0;
    chi_in.preq.pready = 0;
  }
  for (int c = 0; !chi_out.preq.pvalid && c < 100; c++)
    cache_tick();
  CHECK(chi_out.preq.pvalid && chi_out.preq.pbits.popcode == READ_ONCE &&
        chi_out.preq.pbits.paddress == slice(ADDRESS, 43, 0) &&
        !chi_out.preq.pbits.pallow_uretry &&
        chi_out.preq.pbits.ppcrd_utype == 2);
  chi_in.preq.pready = 1;
  cache_tick();
  chi_in.prsp.prequester.pready = 0;
  return_line(ADDRESS, LINE);
  for (int c = 0; !chi_out.prsp.prequester.pvalid && c < 100; c++)
    cache_tick();
  {
    CHIRspFlit saved;
    saved = chi_out.prsp.prequester.pbits;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index) {
      cache_tick();
      CHECK(chi_out.prsp.prequester.pvalid &&
            same_response(chi_out.prsp.prequester.pbits, saved));
    }
  }
  chi_in.prsp.prequester.pready = 1;
  accept_comp_ack();

  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
    cache_tick();
  invalidate_cache(ADDRESS);
  for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index) {
    cache_tick();
    CHECK(!core_out.presponse.pvalid);
  }

  invalidate_cache(ADDRESS);
  send_core_request(ADDRESS);
  accept_read_request(ADDRESS);
  return_line(ADDRESS, LINE, 1, UINT64_C(2));
  accept_comp_ack();
  wait_result();
  CHECK(core_out.presponse.pvalid && core_out.presponse.pbits.paccess_ufault);
  cache_tick();
  send_core_request(ADDRESS);
  accept_read_request(ADDRESS);
  return_line(ADDRESS, LINE);
  accept_comp_ack();
  expect_instruction(UINT64_C(286331153));

  invalidate_cache(ADDRESS);
  rom_phase = 1;
  chi_in.prsp.prequester.pready = 0;
  chi_in.preq.pready = 0;
  send_core_request(UINT64_C(65536));
  for (int c = 0; !chi_out.preq.pvalid && c < 100; c++)
    cache_tick();
  {
    CHIReqFlit saved;
    saved = chi_out.preq.pbits;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index) {
      cache_tick();
      CHECK(chi_out.preq.pvalid && same_request(chi_out.preq.pbits, saved));
    }
  }
  chi_in.preq.pready = 1;
  accept_rom_request(UINT64_C(65536));
  return_line(UINT64_C(65536), LINE, 2);
  wait_result();
  expect_instruction(UINT64_C(286331153));
  for (int offset = 15; offset >= 0; offset--) {
    send_core_request(UINT64_C(65536) + ((offset * 4) & low_mask(64)));
    expect_instruction(line_slice(LINE, offset * 32, 32));
    CHECK(!chi_out.preq.pvalid);
  }
  send_core_request(UINT64_C(65600));
  accept_rom_request(UINT64_C(65600));
  return_line(UINT64_C(65600), LINE_B, 1, UINT64_C(2));
  wait_result();
  CHECK(core_out.presponse.pvalid && core_out.presponse.pbits.paccess_ufault);
  cache_tick();
  send_core_request(UINT64_C(65600));
  accept_rom_request(UINT64_C(65600));
  return_line(UINT64_C(65600), LINE_B);
  expect_instruction(UINT64_C(2981212593));
  invalidate_cache(UINT64_C(65536));
  send_core_request(UINT64_C(65536));
  accept_rom_request(UINT64_C(65536));
  core_in.pinvalidate_uall = 1;
  cache_tick();
  core_in.pinvalidate_uall = 0;
  return_line(UINT64_C(65536), LINE, 1);
  for (unsigned repeat_index = 0; repeat_index < (15); ++repeat_index) {
    cache_tick();
    CHECK(!core_out.presponse.pvalid);
  }
  send_core_request(UINT64_C(65536));
  accept_rom_request(UINT64_C(65536));
  return_line(UINT64_C(65536), LINE);
  expect_instruction(UINT64_C(286331153));

  for (int architectural = 0; architectural < 2; architectural++) {
    invalidate_cache(UINT64_C(65536));
    send_core_request(UINT64_C(65536));
    accept_rom_request(UINT64_C(65536));
    return_line(UINT64_C(65536), LINE);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      cache_tick();
    if (architectural != 0)
      core_in.pinvalidate_uall = 1;
    else
      core_in.pflush = 1;
    cache_tick();
    core_in.pinvalidate_uall = 0;
    core_in.pflush = 0;
    for (unsigned repeat_index = 0; repeat_index < (15); ++repeat_index) {
      cache_tick();
      CHECK(!core_out.presponse.pvalid);
    }
    send_core_request(UINT64_C(65536));
    if (architectural != 0) {
      accept_rom_request(UINT64_C(65536));
      return_line(UINT64_C(65536), LINE);
    }
    expect_instruction(UINT64_C(286331153));
    CHECK(!chi_out.preq.pvalid);
  }
}

int main() { return run_test(run_case); }
