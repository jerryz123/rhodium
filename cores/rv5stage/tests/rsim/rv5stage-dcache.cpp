// Preserves the rv5stage-dcache cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
#include "amo.hpp"
using CHIRspFlit = std::remove_cvref_t<decltype(chi_in.presponses.pbits)>;
using CHIDatFlit = std::remove_cvref_t<decltype(chi_in.presponse_udata.pbits)>;
using CHISnpFlit = std::remove_cvref_t<decltype(chi_in.psnoops.pbits)>;
using CHIReqFlit = std::remove_cvref_t<decltype(chi_out.prequests.pbits)>;
bool same_dat(const CHIDatFlit &a, const CHIDatFlit &b) {
  if (!(read_bits(a.pdata, 0, 128) == read_bits(b.pdata, 0, 128))) {
    std::cerr << "CHI DAT mismatch: read_bits(a.pdata,0,128) == "
                 "read_bits(b.pdata,0,128)\n";
    return false;
  }
  if (!(a.pbyte_uenable == b.pbyte_uenable)) {
    std::cerr << "CHI DAT mismatch: a.pbyte_uenable == b.pbyte_uenable\n";
    return false;
  }
  if (!(a.preplicate == b.preplicate)) {
    std::cerr << "CHI DAT mismatch: a.preplicate == b.preplicate\n";
    return false;
  }
  if (!(a.pnum_udat == b.pnum_udat)) {
    std::cerr << "CHI DAT mismatch: a.pnum_udat == b.pnum_udat\n";
    return false;
  }
  if (!(a.pcah == b.pcah)) {
    std::cerr << "CHI DAT mismatch: a.pcah == b.pcah\n";
    return false;
  }
  if (!(a.ptrace_utag == b.ptrace_utag)) {
    std::cerr << "CHI DAT mismatch: a.ptrace_utag == b.ptrace_utag\n";
    return false;
  }
  if (!(a.ptag_uupdate == b.ptag_uupdate)) {
    std::cerr << "CHI DAT mismatch: a.ptag_uupdate == b.ptag_uupdate\n";
    return false;
  }
  if (!(a.ptag == b.ptag)) {
    std::cerr << "CHI DAT mismatch: a.ptag == b.ptag\n";
    return false;
  }
  if (!(a.ptag_uop == b.ptag_uop)) {
    std::cerr << "CHI DAT mismatch: a.ptag_uop == b.ptag_uop\n";
    return false;
  }
  if (!(a.pcache_uline_uid == b.pcache_uline_uid)) {
    std::cerr << "CHI DAT mismatch: a.pcache_uline_uid == b.pcache_uline_uid\n";
    return false;
  }
  if (!(a.pdata_uid == b.pdata_uid)) {
    std::cerr << "CHI DAT mismatch: a.pdata_uid == b.pdata_uid\n";
    return false;
  }
  if (!(a.pccid == b.pccid)) {
    std::cerr << "CHI DAT mismatch: a.pccid == b.pccid\n";
    return false;
  }
  if (!(a.pdbid_uor_umecid == b.pdbid_uor_umecid)) {
    std::cerr << "CHI DAT mismatch: a.pdbid_uor_umecid == b.pdbid_uor_umecid\n";
    return false;
  }
  if (!(a.pc_ubusy == b.pc_ubusy)) {
    std::cerr << "CHI DAT mismatch: a.pc_ubusy == b.pc_ubusy\n";
    return false;
  }
  if (!(a.pdata_upull == b.pdata_upull)) {
    std::cerr << "CHI DAT mismatch: a.pdata_upull == b.pdata_upull\n";
    return false;
  }
  if (!(a.pdata_usource_uor_ufwd_ustate == b.pdata_usource_uor_ufwd_ustate)) {
    std::cerr << "CHI DAT mismatch: a.pdata_usource_uor_ufwd_ustate == "
                 "b.pdata_usource_uor_ufwd_ustate\n";
    return false;
  }
  if (!(a.presp == b.presp)) {
    std::cerr << "CHI DAT mismatch: a.presp == b.presp\n";
    return false;
  }
  if (!(a.presp_uerr == b.presp_uerr)) {
    std::cerr << "CHI DAT mismatch: a.presp_uerr == b.presp_uerr\n";
    return false;
  }
  if (!(a.popcode == b.popcode)) {
    std::cerr << "CHI DAT mismatch: a.popcode == b.popcode\n";
    return false;
  }
  if (!(a.phome_unid_uor_upbha_uor_umismatched_umecid ==
        b.phome_unid_uor_upbha_uor_umismatched_umecid)) {
    std::cerr
        << "CHI DAT mismatch: a.phome_unid_uor_upbha_uor_umismatched_umecid == "
           "b.phome_unid_uor_upbha_uor_umismatched_umecid\n";
    return false;
  }
  if (!(a.ptxn_uid == b.ptxn_uid)) {
    std::cerr << "CHI DAT mismatch: a.ptxn_uid == b.ptxn_uid\n";
    return false;
  }
  if (!(a.psrc_uid == b.psrc_uid)) {
    std::cerr << "CHI DAT mismatch: a.psrc_uid == b.psrc_uid\n";
    return false;
  }
  if (!(a.ptgt_uid == b.ptgt_uid)) {
    std::cerr << "CHI DAT mismatch: a.ptgt_uid == b.ptgt_uid\n";
    return false;
  }
  if (!(a.pqos == b.pqos)) {
    std::cerr << "CHI DAT mismatch: a.pqos == b.pqos\n";
    return false;
  }
  return true;
}
template <class T> unsigned mem_attr(const T &a) {
  return (a.pallocate << 3) | (a.pcacheable << 2) | (a.pdevice << 1) |
         a.pearly_uwrite_uacknowledge;
}
// Verifies flow admission, set-isolated hits and same-line waiters under a miss, stores, coherence, and atomics.

// Supplies an independent scalar arithmetic oracle for cache-level AMO sweeps.

constexpr std::uint8_t READ_CLEAN = UINT64_C(2);
constexpr std::uint8_t READ_UNIQUE = UINT64_C(7);
constexpr std::uint8_t WRITE_BACK_FULL = UINT64_C(27);
constexpr std::uint8_t COMP_ACK = UINT64_C(2);
constexpr std::uint8_t COMP = UINT64_C(4);
constexpr std::uint8_t COMP_DBID_RESP = UINT64_C(5);
constexpr std::uint8_t DBID_RESP_ORD = UINT64_C(14);
constexpr std::uint8_t RETRY_ACK = UINT64_C(3);
constexpr std::uint8_t PCRD_GRANT = UINT64_C(7);
constexpr std::uint8_t SNP_CLEAN_INVALID = UINT64_C(9);
constexpr std::uint8_t SNP_RESP_DATA = UINT64_C(1);
constexpr std::uint8_t COPY_BACK_WRITE_DATA = UINT64_C(2);
constexpr std::uint8_t COMP_DATA = UINT64_C(4);
constexpr std::uint8_t HOME_ID = UINT64_C(1);
constexpr std::uint8_t CACHE_ID = UINT64_C(3);
constexpr std::uint8_t MEMORY_LOAD = UINT64_C(1);
constexpr std::uint8_t MEMORY_STORE = UINT64_C(2);
constexpr std::uint8_t MEMORY_LR = UINT64_C(3);
constexpr std::uint8_t MEMORY_SC = UINT64_C(4);
constexpr std::uint8_t MEMORY_ATOMIC = UINT64_C(5);
constexpr std::uint8_t MEMORY_ZERO = UINT64_C(6);
constexpr std::uint8_t ATOMIC_SWAP = UINT64_C(0);
constexpr std::uint8_t ATOMIC_ADD = UINT64_C(1);
constexpr std::uint8_t WRITEBACK_ACK_KIND = UINT64_C(0);
constexpr std::uint8_t WRITEBACK_INTEGER_KIND = UINT64_C(1);

// Defines the RV64 EX/MEM/WB cache protocol used by the direct cache and MMU benches.

constexpr std::uint8_t PIPE_SLOW = 0, PIPE_LOAD_HIT = 1, PIPE_STORE_HIT = 2,
                       PIPE_REPLAY = 3, PIPE_PAGE_FAULT = 4,
                       PIPE_ACCESS_FAULT = 5;

bool probe_only = UINT64_C(0);

std::uint64_t virtual_page_xor = UINT64_C(0x40000000);

bool tx_req_pending = UINT64_C(0);

bool tx_rsp_pending = UINT64_C(0);

bool tx_dat_pending = UINT64_C(0);

bool forbid_core_response = UINT64_C(0);

bool watch_amo_response = 0;

int amo_response_count = 0;

bool watch_progress_snoop = 0;

bool forbid_progress_snoop = 0;

int progress_snoop_accepts = 0;

CHIReqFlit captured_req;

CHIRspFlit captured_rsp;

CHIDatFlit captured_dat;

CHISnpFlit captured_snoop = {};

struct Line {
  std::array<std::uint32_t, 16> words{};
  Line() = default;
  Line(std::initializer_list<std::uint64_t> high_to_low) {
    CHECK(high_to_low.size() == 8);
    unsigned i = 8;
    for (auto value : high_to_low) {
      --i;
      words[2 * i] = value;
      words[2 * i + 1] = value >> 32;
    }
  }
};
constexpr std::uint64_t ADDRESS = UINT64_C(0x100000000);
const Line LINE = {UINT64_C(0xf0e0d0c0b0a0908),  UINT64_C(0x706050403020100),
                   UINT64_C(0xfedcba9876543210), UINT64_C(0x1122334455667788),
                   UINT64_C(0xffeeddccbbaa9988), UINT64_C(0x7766554433221100),
                   UINT64_C(0x123456789abcdef),  UINT64_C(0x8877665544332211)};
constexpr std::uint64_t STORE_DATA = UINT64_C(0xdeadbeefcafef00d);
constexpr std::uint64_t STORE_DATA_2 = UINT64_C(0x123456789abcdef);
constexpr std::uint64_t EVICT_ADDRESS = ADDRESS + UINT64_C(512);
constexpr std::uint64_t THIRD_ADDRESS = ADDRESS + UINT64_C(1024);
constexpr std::uint64_t PREFETCH_READ_ADDRESS = ADDRESS + UINT64_C(64);
constexpr std::uint64_t PREFETCH_WRITE_ADDRESS = ADDRESS + UINT64_C(128);
const Line EVICT_LINE = {
    UINT64_C(0x1716151413121110), UINT64_C(0xf0e0d0c0b0a0908),
    UINT64_C(0xfffefdfcfbfaf9f8), UINT64_C(0xf7f6f5f4f3f2f1f0),
    UINT64_C(0x2726252423222120), UINT64_C(0x1f1e1d1c1b1a1918),
    UINT64_C(0x706050403020100),  UINT64_C(0x3736353433323130)};
const Line THIRD_LINE = [] {
  auto line = EVICT_LINE;
  write_bits(line, 0, 64, UINT64_C(0xabcdef0123456789));
  return line;
}();

void tick();
void stage_pipeline_store(std::uint64_t address, std::uint64_t value,
                          std::uint8_t size = 3);
void store_then_load(std::uint64_t store_address, std::uint64_t value,
                     std::uint64_t load_address, std::uint8_t store_size,
                     std::uint8_t load_size, std::uint8_t overlap,
                     std::uint64_t expected);
void check_pipeline_load(std::uint64_t address, std::uint8_t permitted,
                         std::uint8_t expected_hit, std::uint64_t value = 0);
void check_under_miss(std::uint64_t address, std::uint8_t outcome,
                      std::uint64_t value = 0,
                      std::uint8_t operation = MEMORY_LOAD);
void stream_under_miss();
void prepare_hit_under_miss();
void send_prefetch(std::uint64_t address, std::uint8_t operation);
void grant_req_credit();
void grant_rsp_credit();
void grant_dat_credit();
void wait_rsp_credit();
void wait_dat_credit();
void send_core_request(std::uint64_t address, std::uint8_t access,
                       std::uint8_t atomic, std::uint64_t data, std::uint8_t rd,
                       std::uint8_t locality = UINT64_C(0),
                       std::uint8_t destination = UINT64_C(3),
                       std::uint8_t size = UINT64_C(3));
void accept_request(std::uint8_t opcode, std::uint64_t address,
                    std::uint16_t txn_id, std::uint8_t size,
                    std::uint8_t allow_retry, std::uint8_t pcrd_type);
void return_line(std::uint64_t address, const Line &line,
                 std::uint8_t response_state, std::uint8_t exercise_hits = 0);
void accept_comp_ack();
void expect_core_response(std::uint64_t data, std::uint8_t destination,
                          std::uint8_t rd, std::uint8_t access_fault = 0);
void send_response(std::uint8_t opcode, std::uint16_t txn_id,
                   std::uint16_t dbid, std::uint8_t pcrd_type,
                   std::uint8_t error = 0);
void accept_copyback_data(int packet, const Line &line,
                          std::uint8_t response = UINT64_C(6));
void send_snoop(std::uint64_t address, std::uint16_t txn_id,
                std::uint8_t opcode = SNP_CLEAN_INVALID);
void accept_snoop_data(int packet, const Line &line, std::uint16_t txn_id);
void tick() {
  {
    if (!reset && chi_in.psnoops.pvalid && chi_out.psnoops.pready)
      captured_snoop = chi_in.psnoops.pbits;
    if (forbid_progress_snoop)
      CHECK(!(chi_in.psnoops.pvalid && chi_out.psnoops.pready));
    if (watch_progress_snoop && chi_in.psnoops.pvalid && chi_out.psnoops.pready)
      progress_snoop_accepts++;
    if (watch_amo_response && core_out.presponse.pvalid) {
      CHECK(!core_out.presponse.pbits.paccess_ufault &&
            core_out.presponse.pbits.pdata == 1 &&
            memory_rd(core_out.presponse.pbits.pcontext.pwriteback) == 2);
      amo_response_count++;
    }
    if (forbid_core_response)
      CHECK(!core_out.presponse.pvalid);
    if (!reset && chi_out.prequests.pvalid) {
      tx_req_pending = UINT64_C(1);
      captured_req = chi_out.prequests.pbits;
    }
    if (!reset && chi_out.prequester_uresponses.pvalid) {
      tx_rsp_pending = UINT64_C(1);
      captured_rsp = chi_out.prequester_uresponses.pbits;
    }
    if (!reset && chi_out.prequest_udata.pvalid) {
      tx_dat_pending = UINT64_C(1);
      captured_dat = chi_out.prequest_udata.pbits;
    }
    rising();
    settle();
    if (watch_progress_snoop && progress_snoop_accepts != 0)
      chi_in.psnoops = {};
  }
}

void stage_pipeline_store(std::uint64_t address, std::uint64_t value,
                          std::uint8_t size) {
  pipeline_in = {};
  pipeline_lookup_in = {.pvalid = 1,
                        .pbits = {.pbyte_umask = static_cast<uint8_t>(
                                      ((((1 << (1 << (size))) - 1)
                                        << ((address ^ virtual_page_xor) % 8)) &
                                       low_mask(8))),
                                  .paddress = address ^ virtual_page_xor,
                                  .paccess = MEMORY_STORE,
                                  .pwidth = size,
                                  .punsigned = 0,
                                  .pdata = value}};
  tick();
  pipeline_lookup_in = {};
  pipeline_in.prequest = {
      .pvalid = 1,
      .pbits = {
          .pbyte_umask = static_cast<uint8_t>(
              ((((1 << (1 << (size))) - 1) << ((address) % 8)) & low_mask(8))),
          .paddress = address,
          .paccess = MEMORY_STORE,
          .pwidth = size,
          .punsigned = 0,
          .pdata = value}};
  settle();
  CHECK(pipeline_out.presponse.pvalid &&
        pipeline_out.presponse.pbits.poutcome == PIPE_STORE_HIT);
}

// Exercise a younger read at the WB enqueue boundary. Keep an EX read
// presented so the buffer cannot disappear into an otherwise idle slot.
void store_then_load(std::uint64_t store_address, std::uint64_t value,
                     std::uint64_t load_address, std::uint8_t store_size,
                     std::uint8_t load_size, std::uint8_t overlap,
                     std::uint64_t expected) {
  stage_pipeline_store(store_address, value, store_size);
  pipeline_lookup_in = {
      .pvalid = 1,
      .pbits = {.pbyte_umask = static_cast<uint8_t>(
                    ((((1 << (1 << (load_size))) - 1)
                      << ((load_address ^ virtual_page_xor) % 8)) &
                     low_mask(8))),
                .paddress = load_address ^ virtual_page_xor,
                .paccess = MEMORY_LOAD,
                .pwidth = load_size,
                .punsigned = 1,
                .pdata = 0}};
  tick();
  pipeline_in.prequest = {
      .pvalid = 1,
      .pbits = {.pbyte_umask = static_cast<uint8_t>(
                    ((((1 << (1 << (load_size))) - 1) << ((load_address) % 8)) &
                     low_mask(8))),
                .paddress = load_address,
                .paccess = MEMORY_LOAD,
                .pwidth = load_size,
                .punsigned = 1,
                .pdata = 0}};
  pipeline_in.pcommit = 1;
  settle();
  CHECK(pipeline_out.pcommit_uready && !core_out.pdrained);
  CHECK(pipeline_out.presponse.pbits.poutcome ==
        (overlap ? PIPE_REPLAY : PIPE_LOAD_HIT));
  if (!overlap)
    CHECK(pipeline_out.presponse.pbits.pdata == expected);
  tick();
  pipeline_in = {};
  pipeline_lookup_in = {};
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick();
  CHECK(core_out.pdrained && !core_out.presponse.pvalid && !tx_req_pending);
}

void check_pipeline_load(std::uint64_t address, std::uint8_t permitted,
                         std::uint8_t expected_hit, std::uint64_t value) {
  pipeline_lookup_in = {.pvalid = UINT64_C(1),
                        .pbits = {.pbyte_umask = static_cast<uint8_t>(
                                      ((((1 << (1 << (UINT64_C(3)))) - 1)
                                        << ((address ^ virtual_page_xor) % 8)) &
                                       low_mask(8))),
                                  .paddress = address ^ virtual_page_xor,
                                  .paccess = MEMORY_LOAD,
                                  .pwidth = UINT64_C(3),
                                  .punsigned = UINT64_C(0),
                                  .pdata = {}}};
  tick();
  pipeline_lookup_in = {};
  pipeline_in.prequest = {
      .pvalid = permitted,
      .pbits = {.pbyte_umask = static_cast<uint8_t>(
                    ((((1 << (1 << (UINT64_C(3)))) - 1) << ((address) % 8)) &
                     low_mask(8))),
                .paddress = address,
                .paccess = MEMORY_LOAD,
                .pwidth = UINT64_C(3),
                .punsigned = UINT64_C(0),
                .pdata = {}}};
  settle();
  CHECK((pipeline_out.presponse.pvalid &&
         pipeline_out.presponse.pbits.poutcome == PIPE_LOAD_HIT) ==
        expected_hit);
  if (expected_hit)
    CHECK(pipeline_out.presponse.pbits.pdata == value);
  tick();
  pipeline_in = {};
  tick();
  CHECK(!core_out.presponse.pvalid && !tx_req_pending);
}

void check_under_miss(std::uint64_t address, std::uint8_t outcome,
                      std::uint64_t value, std::uint8_t operation) {
  pipeline_lookup_in = {.pvalid = 1,
                        .pbits = {.pbyte_umask = static_cast<uint8_t>(
                                      ((((1 << (1 << (3))) - 1)
                                        << ((address ^ virtual_page_xor) % 8)) &
                                       low_mask(8))),
                                  .paddress = address ^ virtual_page_xor,
                                  .paccess = operation,
                                  .pwidth = 3,
                                  .punsigned = 0,
                                  .pdata = 0}};
  tick();
  pipeline_lookup_in = {};
  pipeline_in.prequest = {
      .pvalid = 1,
      .pbits = {
          .pbyte_umask = static_cast<uint8_t>(
              ((((1 << (1 << (3))) - 1) << ((address) % 8)) & low_mask(8))),
          .paddress = address,
          .paccess = operation,
          .pwidth = 3,
          .punsigned = 0,
          .pdata = 0}};
  settle();
  CHECK(pipeline_out.presponse.pvalid &&
        pipeline_out.presponse.pbits.poutcome == outcome);
  if (outcome == PIPE_LOAD_HIT)
    CHECK(pipeline_out.presponse.pbits.pdata == value);
  CHECK(!core_out.pdrained && !core_out.presponse.pvalid && !tx_req_pending);
  tick();
  pipeline_in = {};
  tick();
}

void stream_under_miss() {
  pipeline_lookup_in = {
      .pvalid = 1,
      .pbits = {.pbyte_umask = static_cast<uint8_t>(
                    ((((1 << (1 << (3))) - 1)
                      << ((PREFETCH_READ_ADDRESS ^ virtual_page_xor) % 8)) &
                     low_mask(8))),
                .paddress = PREFETCH_READ_ADDRESS ^ virtual_page_xor,
                .paccess = MEMORY_LOAD,
                .pwidth = 3,
                .punsigned = 0,
                .pdata = 0}};
  tick();
  pipeline_in.prequest = {
      .pvalid = 1,
      .pbits = {.pbyte_umask = static_cast<uint8_t>((
                    (((1 << (1 << (3))) - 1) << ((PREFETCH_READ_ADDRESS) % 8)) &
                    low_mask(8))),
                .paddress = PREFETCH_READ_ADDRESS,
                .paccess = MEMORY_LOAD,
                .pwidth = 3,
                .punsigned = 0,
                .pdata = 0}};
  for (int repeat_index = 0; repeat_index < (12); ++repeat_index) {
    settle();
    CHECK(pipeline_out.presponse.pvalid &&
          pipeline_out.presponse.pbits.poutcome == PIPE_LOAD_HIT &&
          pipeline_out.presponse.pbits.pdata == bit_slice(LINE, 0, 64));
    CHECK(!core_out.pdrained && !core_out.presponse.pvalid && !tx_req_pending);
    tick();
  }
  pipeline_lookup_in = {};
  pipeline_in = {};
}

void prepare_hit_under_miss() {
  reset = 1;
  core_in = {};
  pipeline_in = {};
  pipeline_lookup_in = {};
  chi_in = {};
  prefetch_in = {};
  core_in.presponse.pready = UINT64_C(1);
  tx_req_pending = 0;
  tx_rsp_pending = 0;
  tx_dat_pending = 0;
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
    tick();
  reset = 0;
  grant_req_credit();
  grant_rsp_credit();
  grant_dat_credit();
  // Two ways in set zero plus an independent warm line in set one.
  send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
  accept_request(READ_CLEAN, ADDRESS, 0, 6, 1, 0);
  return_line(ADDRESS, LINE, UINT64_C(2));
  accept_comp_ack();
  expect_core_response(bit_slice(LINE, 0, 64), WRITEBACK_INTEGER_KIND, 1);
  send_core_request(EVICT_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
  accept_request(READ_CLEAN, EVICT_ADDRESS, 0, 6, 1, 0);
  return_line(EVICT_ADDRESS, EVICT_LINE, UINT64_C(2));
  accept_comp_ack();
  expect_core_response(bit_slice(EVICT_LINE, 0, 64), WRITEBACK_INTEGER_KIND, 1);
  send_core_request(PREFETCH_READ_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
  accept_request(READ_CLEAN, PREFETCH_READ_ADDRESS, 0, 6, 1, 0);
  return_line(PREFETCH_READ_ADDRESS, LINE, UINT64_C(2));
  accept_comp_ack();
  expect_core_response(bit_slice(LINE, 0, 64), WRITEBACK_INTEGER_KIND, 1);
}

void send_prefetch(std::uint64_t address, std::uint8_t operation) {
  {
    prefetch_in.pbits.paddress = address;
    prefetch_in.pbits.poperation = operation;
    prefetch_in.pvalid = UINT64_C(1);
    tick();
    prefetch_in = {};
  }
}

void grant_req_credit() {
  {
    chi_in.prequests.pready = UINT64_C(1);
  }
}

void grant_rsp_credit() {
  {
    chi_in.prequester_uresponses.pready = UINT64_C(1);
  }
}

void grant_dat_credit() {
  {
    chi_in.prequest_udata.pready = UINT64_C(1);
  }
}

void wait_rsp_credit() {
  int cycles;
  {
    cycles = 0;
    while (!chi_out.presponses.pready && cycles < 50) {
      tick();
      cycles = cycles + 1;
    }
    CHECK(chi_out.presponses.pready);
  }
}

void wait_dat_credit() {
  int cycles;
  {
    cycles = 0;
    while (!chi_out.presponse_udata.pready && cycles < 50) {
      tick();
      cycles = cycles + 1;
    }
    CHECK(chi_out.presponse_udata.pready);
  }
}

void send_core_request(std::uint64_t address, std::uint8_t access,
                       std::uint8_t atomic, std::uint64_t data, std::uint8_t rd,
                       std::uint8_t locality, std::uint8_t destination,
                       std::uint8_t size) {
  int cycles;
  {
    cycles = 0;
    while (!core_out.prequest.pready && cycles < 100) {
      tick();
      cycles = cycles + 1;
    }
    CHECK(core_out.prequest.pready);
    core_in.prequest.pbits = {
        .pbyte_umask = static_cast<uint8_t>(
            ((((1 << (1 << (size))) - 1) << ((address) % 8)) & low_mask(8))),
        .paddress = address,
        .paccess = access,
        .patomic = atomic,
        .pwidth = size,
        .punsigned = UINT64_C(0),
        .pdata = data,
        .pcontext = {.pwriteback = std::uint16_t(
                         destination == UINT64_C(2) ? memory_fp(rd, UINT64_C(1))
                         : (destination == UINT64_C(0) ||
                            (destination == UINT64_C(3) &&
                             (access == MEMORY_STORE || access == MEMORY_ZERO ||
                              access >= 7)))
                             ? UINT64_C(0)
                             : memory_integer(rd)),
                     .porigin = 0},
        .plocality = locality};
    core_in.prequest.pvalid = UINT64_C(1);
    tick();
    core_in.prequest.pvalid = UINT64_C(0);
  }
}

void accept_request(std::uint8_t opcode, std::uint64_t address,
                    std::uint16_t txn_id, std::uint8_t size,
                    std::uint8_t allow_retry, std::uint8_t pcrd_type) {
  int cycles;
  {
    cycles = 0;
    while (!tx_req_pending && cycles < 100) {
      tick();
      cycles = cycles + 1;
    }
    CHECK(tx_req_pending);
    CHECK(captured_req.popcode == opcode && captured_req.psrc_uid == CACHE_ID &&
          captured_req.ptgt_uid == HOME_ID && captured_req.ptxn_uid == txn_id &&
          captured_req.preturn_utxn_uid_uor_ustash_ulpid == UINT64_C(0) &&
          captured_req.paddress == bit_slice(address, 0, 44) &&
          captured_req.psize_uor_unum_ureq == size &&
          captured_req.psnp_uattr_uor_udo_udwt == UINT64_C(1) &&
          mem_attr(captured_req.pmem_uattr) ==
              (((opcode == READ_CLEAN) || (opcode == READ_UNIQUE) ||
                (opcode == WRITE_BACK_FULL))
                   ? UINT64_C(13)
                   : UINT64_C(5)) &&
          captured_req.pexp_ucomp_uack ==
              ((opcode == READ_CLEAN) || (opcode == READ_UNIQUE)) &&
          captured_req.pallow_uretry == allow_retry &&
          captured_req.ppcrd_utype == pcrd_type);
    tx_req_pending = UINT64_C(0);
  }
}

void return_line(std::uint64_t address, const Line &line,
                 std::uint8_t response_state, std::uint8_t exercise_hits) {
  int packet;
  {
    for (packet = 3; packet >= 0; packet = packet - 1) {
      wait_dat_credit();
      chi_in.presponse_udata.pbits = {};
      write_bits(chi_in.presponse_udata.pbits.pdata, 0, 128,
                 bit_slice(line, packet * 128, 128));
      chi_in.presponse_udata.pbits.pbyte_uenable = UINT64_C(65535);
      chi_in.presponse_udata.pbits.pdata_uid =
          bit_slice(address, 4, 2) + bit_slice(packet, 0, 2);
      chi_in.presponse_udata.pbits.presp = response_state;
      chi_in.presponse_udata.pbits.popcode = COMP_DATA;
      chi_in.presponse_udata.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
          HOME_ID;
      chi_in.presponse_udata.pbits.pdbid_uor_umecid = UINT64_C(85);
      chi_in.presponse_udata.pbits.ptxn_uid = UINT64_C(0);
      chi_in.presponse_udata.pbits.psrc_uid = HOME_ID;
      chi_in.presponse_udata.pbits.ptgt_uid = CACHE_ID;
      chi_in.presponse_udata.pvalid = UINT64_C(1);
      tick();
      chi_in.presponse_udata.pvalid = UINT64_C(0);
      chi_in.presponse_udata.pbits = {};
      if (exercise_hits && packet != 0) {
        check_under_miss(PREFETCH_READ_ADDRESS, PIPE_LOAD_HIT,
                         bit_slice(LINE, 0, 64));
        check_under_miss(address,
                         PIPE_SLOW); // Waiters cannot observe a partial line.
      }
    }
  }
}

void accept_comp_ack() {
  int cycles;
  {
    cycles = 0;
    while (!tx_rsp_pending && cycles < 100) {
      tick();
      cycles = cycles + 1;
    }
    CHECK(tx_rsp_pending && captured_rsp.popcode == COMP_ACK &&
          captured_rsp.ptxn_uid == UINT64_C(85));
    tx_rsp_pending = UINT64_C(0);
  }
}

void expect_core_response(std::uint64_t data, std::uint8_t destination,
                          std::uint8_t rd, std::uint8_t access_fault) {
  int cycles;
  {
    cycles = 0;
    while (!core_out.presponse.pvalid && cycles < 100) {
      tick();
      cycles = cycles + 1;
    }
    CHECK(core_out.presponse.pvalid &&
          core_out.presponse.pbits.paccess_ufault == access_fault &&
          core_out.presponse.pbits.pdata == data &&
          core_out.presponse.pbits.pcontext.pwriteback ==
              (destination == 2   ? memory_fp(rd, UINT64_C(1))
               : destination == 0 ? UINT64_C(0)
                                  : memory_integer(rd)));
    tick();
  }
}

void send_response(std::uint8_t opcode, std::uint16_t txn_id,
                   std::uint16_t dbid, std::uint8_t pcrd_type,
                   std::uint8_t error) {
  {
    wait_rsp_credit();
    chi_in.presponses.pbits = {};
    chi_in.presponses.pbits.pdbid_uor_ugroup_uid = dbid;
    chi_in.presponses.pbits.ppcrd_utype = pcrd_type;
    chi_in.presponses.pbits.presp_uerr = error;
    chi_in.presponses.pbits.popcode = opcode;
    chi_in.presponses.pbits.ptxn_uid = txn_id;
    chi_in.presponses.pbits.psrc_uid = HOME_ID;
    chi_in.presponses.pbits.ptgt_uid = CACHE_ID;
    chi_in.presponses.pvalid = UINT64_C(1);
    tick();
    chi_in.presponses.pvalid = UINT64_C(0);
    chi_in.presponses.pbits = {};
  }
}

void accept_copyback_data(int packet, const Line &line, std::uint8_t response) {
  int cycles;
  CHIDatFlit expected;
  {
    cycles = 0;
    while (!tx_dat_pending && cycles < 100) {
      tick();
      cycles++;
    }
    expected = {};
    expected.popcode = COPY_BACK_WRITE_DATA;
    expected.psrc_uid = CACHE_ID;
    expected.ptgt_uid = HOME_ID;
    expected.ptxn_uid = UINT64_C(85);
    expected.phome_unid_uor_upbha_uor_umismatched_umecid = HOME_ID;
    expected.pdata_uid = bit_slice(packet, 0, 2);
    expected.presp = response;
    write_bits(expected.pdata, 0, 128,
               response == 0 ? uint128(0) : read_bits(line, packet * 128, 128));
    expected.pbyte_uenable = response == 0 ? UINT64_C(0) : UINT64_C(65535);
    CHECK(tx_dat_pending && same_dat(captured_dat, expected));
    tx_dat_pending = UINT64_C(0);
  }
}

void send_snoop(std::uint64_t address, std::uint16_t txn_id,
                std::uint8_t opcode) {
  int cycles;
  {
    chi_in.psnoops.pbits = {};
    chi_in.psnoops.pbits.paddress = bit_slice(address, 3, 41);
    chi_in.psnoops.pbits.popcode = opcode;
    chi_in.psnoops.pbits.ptxn_uid = txn_id;
    chi_in.psnoops.pbits.psrc_uid = HOME_ID;
    chi_in.psnoops.pbits.ptrace_utag = bit_slice(txn_id, 0, 1);
    chi_in.psnoops.pbits.pqos = bit_slice(txn_id, 0, 4);
    chi_in.psnoops.pvalid = UINT64_C(1);
    // Present the real opcode while waiting; idle LCrdReturn is always ready.
    settle();
    cycles = 0;
    while (!chi_out.psnoops.pready && cycles < 256) {
      tick();
      cycles = cycles + 1;
    }
    CHECK(chi_out.psnoops.pready);
    tick();
    chi_in.psnoops = {};
  }
}

void accept_snoop_data(int packet, const Line &line, std::uint16_t txn_id) {
  int cycles;
  CHIDatFlit expected;
  {
    chi_in.prequest_udata.pready = UINT64_C(0);
    cycles = 0;
    while (!chi_out.prequest_udata.pvalid && cycles < 100) {
      tick();
      cycles = cycles + 1;
    }
    CHECK(chi_out.prequest_udata.pvalid &&
          chi_out.prequest_udata.pbits.popcode == SNP_RESP_DATA &&
          chi_out.prequest_udata.pbits.psrc_uid == CACHE_ID &&
          chi_out.prequest_udata.pbits.ptgt_uid == HOME_ID &&
          chi_out.prequest_udata.pbits.ptxn_uid == txn_id &&
          chi_out.prequest_udata.pbits.pdata_uid == bit_slice(packet, 0, 2) &&
          chi_out.prequest_udata.pbits.presp == UINT64_C(4) &&
          read_bits(chi_out.prequest_udata.pbits.pdata, 0, 128) ==
              bit_slice(line, packet * 128, 128));
    expected = {};
    write_bits(expected.pdata, 0, 128, bit_slice(line, packet * 128, 128));
    expected.pbyte_uenable = UINT16_MAX;
    expected.ptrace_utag = captured_snoop.ptrace_utag;
    expected.pdata_uid = bit_slice(packet, 0, 2);
    expected.pccid = bit_slice(packet, 0, 2);
    expected.presp = UINT64_C(4);
    expected.popcode = SNP_RESP_DATA;
    expected.phome_unid_uor_upbha_uor_umismatched_umecid = HOME_ID;
    expected.ptxn_uid = txn_id;
    expected.psrc_uid = CACHE_ID;
    expected.ptgt_uid = HOME_ID;
    expected.pqos = captured_snoop.pqos;
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
      CHECK(chi_out.prequest_udata.pvalid &&
            same_dat(chi_out.prequest_udata.pbits, expected));
      tick();
    }
    chi_in.prequest_udata.pready = UINT64_C(1);
    tick();
    chi_in.prequest_udata.pready = UINT64_C(0);
    tx_dat_pending = UINT64_C(0);
  }
}

Line dirty_line;
Line evict_dirty_line;
int beat;

void drive() {
  virtual_lookup_in.pvalid = core_in.prequest.pvalid | probe_only;
  virtual_lookup_in.pbits = core_in.prequest.pbits.paddress ^ virtual_page_xor;
}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = UINT64_C(1);
  node_id = CACHE_ID;
  pipeline_lookup_in = {};
  pipeline_in = {};
  {
    core_in = {};
    core_in.presponse.pready = UINT64_C(1);
    prefetch_in = {};
    chi_in = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick();
    reset = UINT64_C(0);
    grant_req_credit();
    grant_rsp_credit();
    CHECK(core_out.pdrained);

    probe_only = UINT64_C(1);
    core_in.prequest.pbits.paddress = ADDRESS;
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
      tick();
      CHECK(!core_out.presponse.pvalid && !tx_req_pending && core_out.pdrained);
    }
    probe_only = UINT64_C(0);

    forbid_core_response = UINT64_C(1);
    send_prefetch(PREFETCH_READ_ADDRESS, UINT64_C(2));
    accept_request(READ_CLEAN, PREFETCH_READ_ADDRESS, UINT64_C(0), UINT64_C(6),
                   UINT64_C(1), UINT64_C(0));
    return_line(PREFETCH_READ_ADDRESS, LINE, UINT64_C(1));
    accept_comp_ack();
    until([&] { return core_out.pdrained; });
    tick();
    forbid_core_response = UINT64_C(0);
    send_core_request(PREFETCH_READ_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(1));
    expect_core_response(UINT64_C(0x8877665544332211), WRITEBACK_INTEGER_KIND,
                         UINT64_C(1));

    virtual_page_xor = UINT64_C(0x80000000);
    send_core_request(PREFETCH_READ_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(1));
    expect_core_response(UINT64_C(0x8877665544332211), WRITEBACK_INTEGER_KIND,
                         UINT64_C(1));
    send_core_request(PREFETCH_READ_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(2));
    send_core_request(PREFETCH_READ_ADDRESS + UINT64_C(8), MEMORY_LOAD,
                      ATOMIC_SWAP, UINT64_C(0), UINT64_C(3));
    CHECK(!core_out.presponse.pvalid);
    tick();
    CHECK(core_out.presponse.pvalid);
    expect_core_response(UINT64_C(0x8877665544332211), WRITEBACK_INTEGER_KIND,
                         UINT64_C(2));
    CHECK(core_out.presponse.pvalid);
    expect_core_response(UINT64_C(0x123456789abcdef), WRITEBACK_INTEGER_KIND,
                         UINT64_C(3));

    check_pipeline_load(PREFETCH_READ_ADDRESS, 1, 1,
                        UINT64_C(0x8877665544332211));
    check_pipeline_load(PREFETCH_READ_ADDRESS + 8, 1, 1,
                        UINT64_C(0x123456789abcdef));
    check_pipeline_load(PREFETCH_READ_ADDRESS, 0,
                        0);             // no permitted translation
    check_pipeline_load(ADDRESS, 1, 0); // cold lookup must not allocate

    forbid_core_response = UINT64_C(1);
    send_prefetch(PREFETCH_WRITE_ADDRESS, UINT64_C(3));
    accept_request(READ_UNIQUE, PREFETCH_WRITE_ADDRESS, UINT64_C(0),
                   UINT64_C(6), UINT64_C(1), UINT64_C(0));
    return_line(PREFETCH_WRITE_ADDRESS, LINE, UINT64_C(2));
    accept_comp_ack();
    until([&] { return core_out.pdrained; });
    tick();
    forbid_core_response = UINT64_C(0);
    send_core_request(PREFETCH_WRITE_ADDRESS, MEMORY_STORE, ATOMIC_SWAP,
                      STORE_DATA, UINT64_C(0));
    expect_core_response(UINT64_C(0), WRITEBACK_ACK_KIND, UINT64_C(0));
    tick();
    CHECK(!tx_req_pending && !tx_dat_pending);

    send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, UINT64_C(0),
                      UINT64_C(3));
    CHECK(!core_out.pdrained);
    // The first miss may stop internal queue drain, but its SRAM result must
    // not feed back into request acceptance while structural capacity remains.
    CHECK(core_out.prequest.pready);
    // This younger lookup is already in S3 when the older S4 miss blocks it.
    // It must reread the installed line instead of launching a duplicate miss.
    send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, UINT64_C(0),
                      UINT64_C(4));
    send_core_request(ADDRESS + UINT64_C(8), MEMORY_LOAD, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(5));
    accept_request(READ_CLEAN, ADDRESS, UINT64_C(0), UINT64_C(6), UINT64_C(1),
                   UINT64_C(0));
    send_response(PCRD_GRANT, UINT64_C(0), UINT64_C(0), UINT64_C(6));
    send_response(RETRY_ACK, UINT64_C(0), UINT64_C(0), UINT64_C(6));
    grant_req_credit();
    accept_request(READ_CLEAN, ADDRESS, UINT64_C(0), UINT64_C(6), UINT64_C(0),
                   UINT64_C(6));
    core_in.presponse.pready = UINT64_C(0);
    return_line(ADDRESS, LINE, UINT64_C(1));
    accept_comp_ack();
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick();
      CHECK(!core_out.pdrained);
    }
    core_in.presponse.pready = UINT64_C(1);
    expect_core_response(UINT64_C(0x8877665544332211), WRITEBACK_INTEGER_KIND,
                         UINT64_C(3));
    expect_core_response(UINT64_C(0x8877665544332211), WRITEBACK_INTEGER_KIND,
                         UINT64_C(4));
    expect_core_response(UINT64_C(0x123456789abcdef), WRITEBACK_INTEGER_KIND,
                         UINT64_C(5));
    CHECK(!tx_req_pending);

    // An AMO to a shared line acquires Unique ownership, returns the old
    // doubleword, installs the updated value, and leaves the line dirty.
    grant_req_credit();
    send_core_request(ADDRESS + UINT64_C(24), MEMORY_ATOMIC, ATOMIC_ADD,
                      UINT64_C(1), UINT64_C(7));
    CHECK(!core_out.pdrained);
    accept_request(READ_UNIQUE, ADDRESS, UINT64_C(0), UINT64_C(6), UINT64_C(1),
                   UINT64_C(0));
    return_line(ADDRESS, LINE, UINT64_C(2));
    accept_comp_ack();
    expect_core_response(UINT64_C(0xffeeddccbbaa9988), WRITEBACK_INTEGER_KIND,
                         UINT64_C(7));
    CHECK(core_out.pdrained);
    CHECK(!tx_dat_pending);

    send_core_request(ADDRESS + UINT64_C(24), MEMORY_LOAD, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(6));
    expect_core_response(UINT64_C(0xffeeddccbbaa9989), WRITEBACK_INTEGER_KIND,
                         UINT64_C(6));

    // A second store hits UniqueDirty and remains entirely local.
    send_core_request(ADDRESS + UINT64_C(40), MEMORY_STORE, ATOMIC_SWAP,
                      STORE_DATA_2, UINT64_C(0));
    CHECK(!core_out.presponse.pvalid);
    // The immediately following load initially reads the pre-store SRAM word.
    // Its retained request must reread after the older mutation commits.
    send_core_request(ADDRESS + UINT64_C(40), MEMORY_LOAD, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(6));
    CHECK(!core_out.presponse.pvalid);
    tick();
    CHECK(!core_out.presponse.pvalid);
    expect_core_response(UINT64_C(0), WRITEBACK_ACK_KIND, UINT64_C(0));
    expect_core_response(STORE_DATA_2, WRITEBACK_INTEGER_KIND, UINT64_C(6));
    tick();
    CHECK(!tx_req_pending && !tx_dat_pending);
    CHECK(core_out.pdrained);

    // LR observes the dirty line. Its matching SC succeeds once, returns zero,
    // and a second SC fails without issuing any coherence traffic.
    send_core_request(ADDRESS + UINT64_C(40), MEMORY_LR, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(8));
    expect_core_response(STORE_DATA_2, WRITEBACK_INTEGER_KIND, UINT64_C(8));
    CHECK(core_out.preservation_uvalid);
    // A rejected SC may read the SRAM but cannot consume the LR reservation
    // or write data. The next permitted SC through another alias must succeed.
    core_in.prequest.pbits.paccess = MEMORY_SC;
    core_in.prequest.pbits.pdata = UINT64_C(2989);
    probe_only = UINT64_C(1);
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
      tick();
      CHECK(!core_out.presponse.pvalid && !tx_req_pending && !tx_dat_pending);
    }
    probe_only = UINT64_C(0);
    virtual_page_xor = UINT64_C(0xc0000000);
    send_core_request(ADDRESS + UINT64_C(40), MEMORY_SC, ATOMIC_SWAP,
                      STORE_DATA, UINT64_C(9));
    expect_core_response(UINT64_C(0), WRITEBACK_INTEGER_KIND, UINT64_C(9));
    CHECK(!core_out.preservation_uvalid);
    send_core_request(ADDRESS + UINT64_C(40), MEMORY_SC, ATOMIC_SWAP,
                      STORE_DATA_2, UINT64_C(10));
    expect_core_response(UINT64_C(1), WRITEBACK_INTEGER_KIND, UINT64_C(10));
    tick();
    CHECK(!tx_req_pending && !tx_dat_pending);
    send_core_request(ADDRESS + UINT64_C(40), MEMORY_LOAD, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(11));
    expect_core_response(STORE_DATA, WRITEBACK_INTEGER_KIND, UINT64_C(11));

    // A second colliding line occupies the invalid way without evicting the
    // dirty first line. Resident hits below leave that dirty line least recent,
    // so a third allocating collision drains it before issuing ReadClean.
    dirty_line = LINE;
    bit_slice(dirty_line, 3 * 64, 64) = UINT64_C(0xffeeddccbbaa9989);
    bit_slice(dirty_line, 5 * 64, 64) = STORE_DATA;
    send_core_request(ADDRESS + UINT64_C(40), MEMORY_LR, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(14));
    expect_core_response(STORE_DATA, WRITEBACK_INTEGER_KIND, UINT64_C(14));
    grant_req_credit();
    grant_rsp_credit();
    grant_dat_credit();
    send_core_request(EVICT_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, UINT64_C(0),
                      UINT64_C(5));
    accept_request(READ_CLEAN, EVICT_ADDRESS, UINT64_C(0), UINT64_C(6),
                   UINT64_C(1), UINT64_C(0));
    return_line(EVICT_ADDRESS, EVICT_LINE, UINT64_C(1));
    accept_comp_ack();
    expect_core_response(UINT64_C(0x3736353433323130), WRITEBACK_INTEGER_KIND,
                         UINT64_C(5));
    // Filling an invalid colliding way does not replace the reserved line.
    send_core_request(ADDRESS + UINT64_C(40), MEMORY_SC, ATOMIC_SWAP,
                      STORE_DATA, UINT64_C(16));
    expect_core_response(UINT64_C(0), WRITEBACK_INTEGER_KIND, UINT64_C(16));
    send_core_request(ADDRESS + UINT64_C(40), MEMORY_LR, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(19));
    expect_core_response(STORE_DATA, WRITEBACK_INTEGER_KIND, UINT64_C(19));
    // Both colliding ways are resident, one dirty and reserved. Every NTL
    // selector reads the third line coherently without replacing either way.
    // Repeating that miss proves the transient copy was never installed.
    // The bypass misses do not change PLRU state; each resident hit touches A
    // then EVICT, leaving the dirty A line as the later default miss's victim.
    for (int attempt = 0; attempt < 5; attempt++) {
      std::uint8_t locality = ((1 + attempt % 4) & low_mask(3));
      send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0,
                        UINT64_C(23), ((locality)&low_mask(3)), UINT64_C(2));
      if (attempt == 0)
        send_core_request(EVICT_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0,
                          UINT64_C(26));
      accept_request(READ_CLEAN, THIRD_ADDRESS, 0, 6, 1, 0);
      if (attempt == 0) {
        send_response(RETRY_ACK, 0, 0, UINT64_C(6));
        send_response(PCRD_GRANT, 0, 0, UINT64_C(6));
        accept_request(READ_CLEAN, THIRD_ADDRESS, 0, 6, 0, UINT64_C(6));
        // Snoop service must remain live while the transaction owns a retained
        // younger lookup, and the bypass address is not a resident copy.
        send_snoop(THIRD_ADDRESS, UINT64_C(124));
        for (int cycle = 0; !tx_rsp_pending && cycle < 100; cycle++)
          tick();
        CHECK(tx_rsp_pending && captured_rsp.popcode == 1 &&
              captured_rsp.presp == 0 &&
              captured_rsp.ptxn_uid == UINT64_C(124));
        tx_rsp_pending = 0;
      }
      chi_in.prequester_uresponses.pready = 0;
      return_line(THIRD_ADDRESS, THIRD_LINE,
                  bit_slice(locality, 0, 1) ? UINT64_C(1) : UINT64_C(2));
      for (int repeat_index = 0; repeat_index < (5); ++repeat_index) {
        CHECK(!core_out.presponse.pvalid && !core_out.pdrained &&
              !tx_dat_pending);
        tick();
      }
      CHECK(chi_out.prequester_uresponses.pvalid &&
            chi_out.prequester_uresponses.pbits.popcode == COMP_ACK);
      grant_rsp_credit();
      tick();
      accept_comp_ack();
      expect_core_response(UINT64_C(0xabcdef0123456789), UINT64_C(2),
                           UINT64_C(23));
      if (attempt == 0)
        expect_core_response(UINT64_C(0x3736353433323130),
                             WRITEBACK_INTEGER_KIND, UINT64_C(26));
      // Reservation lifetime is bounded independently of these deliberately
      // stalled transactions; residency is checked by the following hits.
      // Hinted dirty hits must read the local authoritative value, not memory.
      send_core_request(ADDRESS + UINT64_C(40), MEMORY_LOAD, ATOMIC_SWAP, 0,
                        UINT64_C(24), ((locality)&low_mask(3)));
      expect_core_response(STORE_DATA, WRITEBACK_INTEGER_KIND, UINT64_C(24));
      send_core_request(EVICT_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0,
                        UINT64_C(25));
      expect_core_response(UINT64_C(0x3736353433323130), WRITEBACK_INTEGER_KIND,
                           UINT64_C(25));
      CHECK(!tx_req_pending && !tx_dat_pending);
    }
    // Tree PLRU chooses the original dirty victim after EVICT was touched last.
    send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, UINT64_C(0),
                      UINT64_C(17));
    accept_request(WRITE_BACK_FULL, ADDRESS, UINT64_C(1), UINT64_C(6),
                   UINT64_C(1), UINT64_C(0));
    send_response(COMP_DBID_RESP, UINT64_C(1), UINT64_C(85), UINT64_C(0));
    for (beat = 0; beat < 4; beat++)
      accept_copyback_data(beat, dirty_line);
    accept_request(READ_CLEAN, THIRD_ADDRESS, UINT64_C(0), UINT64_C(6),
                   UINT64_C(1), UINT64_C(0));
    return_line(THIRD_ADDRESS, THIRD_LINE, UINT64_C(1));
    accept_comp_ack();
    expect_core_response(UINT64_C(0xabcdef0123456789), WRITEBACK_INTEGER_KIND,
                         UINT64_C(17));
    send_core_request(ADDRESS + UINT64_C(40), MEMORY_SC, ATOMIC_SWAP,
                      STORE_DATA_2, UINT64_C(15));
    expect_core_response(UINT64_C(1), WRITEBACK_INTEGER_KIND, UINT64_C(15));

    // Dirty snoop intervention returns the complete authoritative line and
    // invalidates the local copy without issuing a control-only SnpResp.
    send_core_request(EVICT_ADDRESS + UINT64_C(8), MEMORY_STORE, ATOMIC_SWAP,
                      STORE_DATA, UINT64_C(0));
    accept_request(READ_UNIQUE, EVICT_ADDRESS, UINT64_C(0), UINT64_C(6),
                   UINT64_C(1), UINT64_C(0));
    return_line(EVICT_ADDRESS, EVICT_LINE, UINT64_C(2));
    accept_comp_ack();
    expect_core_response(UINT64_C(0), WRITEBACK_ACK_KIND, UINT64_C(0));
    send_core_request(EVICT_ADDRESS + UINT64_C(8), MEMORY_LR, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(12));
    expect_core_response(STORE_DATA, WRITEBACK_INTEGER_KIND, UINT64_C(12));
    evict_dirty_line = EVICT_LINE;
    bit_slice(evict_dirty_line, 1 * 64, 64) = STORE_DATA;
    chi_in.prequest_udata.pready = UINT64_C(0);
    send_snoop(EVICT_ADDRESS, UINT64_C(119));
    check_pipeline_load(EVICT_ADDRESS + 8, 1, 0); // snoop owns the arrays
    for (beat = 0; beat < 4; beat = beat + 1)
      accept_snoop_data(beat, evict_dirty_line, UINT64_C(119));
    tick();
    CHECK(!core_out.preservation_uvalid);
    send_core_request(EVICT_ADDRESS + UINT64_C(8), MEMORY_SC, ATOMIC_SWAP,
                      STORE_DATA_2, UINT64_C(13));
    expect_core_response(UINT64_C(1), WRITEBACK_INTEGER_KIND, UINT64_C(13));
    tick();
    CHECK(!tx_req_pending && !tx_dat_pending);
    CHECK(core_out.pdrained);
    check_pipeline_load(EVICT_ADDRESS + 8, 1,
                        0); // invalidation cannot expose stale hit data
    send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, UINT64_C(0),
                      UINT64_C(18));
    expect_core_response(UINT64_C(0xabcdef0123456789), WRITEBACK_INTEGER_KIND,
                         UINT64_C(18));

    // A unique hit zeros all words, ignores the byte offset and clears LR.
    send_core_request(PREFETCH_WRITE_ADDRESS, MEMORY_LR, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(12));
    expect_core_response(STORE_DATA, WRITEBACK_INTEGER_KIND, UINT64_C(12));
    for (int offset = 0; offset < 64; offset++) {
      send_core_request(PREFETCH_WRITE_ADDRESS + ((offset)&low_mask(64)),
                        MEMORY_ZERO, ATOMIC_SWAP, ~UINT64_C(0), UINT64_C(0));
      expect_core_response(UINT64_C(0), WRITEBACK_ACK_KIND, UINT64_C(0));
      CHECK(!tx_req_pending);
      for (int word = 0; word < 8; word++) {
        send_core_request(PREFETCH_WRITE_ADDRESS + ((word * 8) & low_mask(64)),
                          MEMORY_LOAD, ATOMIC_SWAP, UINT64_C(0), UINT64_C(1));
        expect_core_response(UINT64_C(0), WRITEBACK_INTEGER_KIND, UINT64_C(1));
      }
    }
    send_core_request(PREFETCH_WRITE_ADDRESS, MEMORY_SC, ATOMIC_SWAP,
                      STORE_DATA, UINT64_C(12));
    expect_core_response(UINT64_C(1), WRITEBACK_INTEGER_KIND, UINT64_C(12));
    send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, UINT64_C(0),
                      UINT64_C(18));
    expect_core_response(UINT64_C(0xabcdef0123456789), WRITEBACK_INTEGER_KIND,
                         UINT64_C(18));

    // A shared hit must acquire Unique before writing, then a coherent observer
    // receives all zeros. It cannot see a partially overwritten SRAM line.
    grant_req_credit();
    send_core_request(PREFETCH_READ_ADDRESS + UINT64_C(63), MEMORY_ZERO,
                      ATOMIC_SWAP, ~UINT64_C(0), UINT64_C(0));
    accept_request(READ_UNIQUE, PREFETCH_READ_ADDRESS, UINT64_C(0), UINT64_C(6),
                   UINT64_C(1), UINT64_C(0));
    CHECK(!core_out.pdrained && !core_out.presponse.pvalid);
    return_line(PREFETCH_READ_ADDRESS, LINE, UINT64_C(2));
    accept_comp_ack();
    expect_core_response(UINT64_C(0), WRITEBACK_ACK_KIND, UINT64_C(0));
    send_snoop(PREFETCH_READ_ADDRESS, UINT64_C(120));
    for (beat = 0; beat < 4; beat++)
      accept_snoop_data(beat, Line{}, UINT64_C(120));
    tick();

    // The invalidated line takes the same ownership/install path on a miss.
    grant_req_credit();
    send_core_request(PREFETCH_READ_ADDRESS + UINT64_C(1), MEMORY_ZERO,
                      ATOMIC_SWAP, ~UINT64_C(0), UINT64_C(0));
    accept_request(READ_UNIQUE, PREFETCH_READ_ADDRESS, UINT64_C(0), UINT64_C(6),
                   UINT64_C(1), UINT64_C(0));
    return_line(PREFETCH_READ_ADDRESS, LINE, UINT64_C(2));
    accept_comp_ack();
    expect_core_response(UINT64_C(0), WRITEBACK_ACK_KIND, UINT64_C(0));
    for (int word = 0; word < 8; word++) {
      send_core_request(PREFETCH_READ_ADDRESS + ((word * 8) & low_mask(64)),
                        MEMORY_LOAD, ATOMIC_SWAP, UINT64_C(0), UINT64_C(1));
      expect_core_response(UINT64_C(0), WRITEBACK_INTEGER_KIND, UINT64_C(1));
    }
    // A zero miss must first preserve the dirty victim. One copyback
    // carries the old zeroed line, then the new block acquires Unique ownership.
    grant_req_credit();
    grant_dat_credit();
    send_core_request(PREFETCH_WRITE_ADDRESS + UINT64_C(256), MEMORY_LOAD,
                      ATOMIC_SWAP, UINT64_C(0), UINT64_C(1));
    accept_request(READ_CLEAN, PREFETCH_WRITE_ADDRESS + UINT64_C(256),
                   UINT64_C(0), UINT64_C(6), UINT64_C(1), UINT64_C(0));
    return_line(PREFETCH_WRITE_ADDRESS + UINT64_C(256), LINE, UINT64_C(1));
    accept_comp_ack();
    expect_core_response(bit_slice(LINE, 0, 64), WRITEBACK_INTEGER_KIND,
                         UINT64_C(1));
    send_core_request(PREFETCH_WRITE_ADDRESS + UINT64_C(513), MEMORY_ZERO,
                      ATOMIC_SWAP, ~UINT64_C(0), UINT64_C(0));
    send_core_request(PREFETCH_WRITE_ADDRESS + UINT64_C(520), MEMORY_LOAD,
                      ATOMIC_SWAP, UINT64_C(0), UINT64_C(2));
    accept_request(WRITE_BACK_FULL, PREFETCH_WRITE_ADDRESS, UINT64_C(1),
                   UINT64_C(6), UINT64_C(1), UINT64_C(0));
    send_response(COMP_DBID_RESP, UINT64_C(1), UINT64_C(85), UINT64_C(0));
    for (beat = 0; beat < 4; beat++)
      accept_copyback_data(beat, Line{});
    accept_request(READ_UNIQUE, PREFETCH_WRITE_ADDRESS + UINT64_C(512),
                   UINT64_C(0), UINT64_C(6), UINT64_C(1), UINT64_C(0));
    // Home may need a snoop before returning the owned block. Serve it while
    // awaiting refill, rather than reserving SRAM throughout the transaction.
    send_snoop(PREFETCH_READ_ADDRESS, UINT64_C(121));
    for (beat = 0; beat < 4; beat++)
      accept_snoop_data(beat, Line{}, UINT64_C(121));
    return_line(PREFETCH_WRITE_ADDRESS + UINT64_C(512), LINE, UINT64_C(2));
    accept_comp_ack();
    expect_core_response(UINT64_C(0), WRITEBACK_ACK_KIND, UINT64_C(0));
    expect_core_response(UINT64_C(0), WRITEBACK_INTEGER_KIND, UINT64_C(2));
    // MakeInvalid discards even a dirty line and must return only SnpResp_I.
    send_snoop(PREFETCH_WRITE_ADDRESS + UINT64_C(512), UINT64_C(122),
               UINT64_C(10));
    for (int wait_cycles = 0;
         !chi_out.prequester_uresponses.pvalid && wait_cycles < 100;
         wait_cycles++) {
      CHECK(!chi_out.prequest_udata.pvalid);
      tick();
    }
    CHECK(chi_out.prequester_uresponses.pvalid &&
          chi_out.prequester_uresponses.pbits.popcode == 1 &&
          chi_out.prequester_uresponses.pbits.presp == 0 &&
          chi_out.prequester_uresponses.pbits.ptxn_uid == UINT64_C(122));
    chi_in.prequester_uresponses.pready = 1;
    tick();
    tx_rsp_pending = UINT64_C(0);
    tick();

    // Identical virtual addresses resolving to two different physical pages
    // must match different tags, even though they select the same set.
    virtual_page_xor = UINT64_C(0x40001000);
    send_core_request(ADDRESS + UINT64_C(4288), MEMORY_LOAD, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(20));
    accept_request(READ_CLEAN, ADDRESS + UINT64_C(4288), UINT64_C(0),
                   UINT64_C(6), UINT64_C(1), UINT64_C(0));
    return_line(ADDRESS + UINT64_C(4288), LINE, UINT64_C(1));
    accept_comp_ack();
    expect_core_response(UINT64_C(0x8877665544332211), WRITEBACK_INTEGER_KIND,
                         UINT64_C(20));
    virtual_page_xor = UINT64_C(0x40002000);
    send_core_request(ADDRESS + UINT64_C(8384), MEMORY_LOAD, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(21));
    accept_request(READ_CLEAN, ADDRESS + UINT64_C(8384), UINT64_C(0),
                   UINT64_C(6), UINT64_C(1), UINT64_C(0));
    return_line(ADDRESS + UINT64_C(8384), THIRD_LINE, UINT64_C(1));
    accept_comp_ack();
    expect_core_response(UINT64_C(0xabcdef0123456789), WRITEBACK_INTEGER_KIND,
                         UINT64_C(21));
    virtual_page_xor = UINT64_C(0x40001000);
    send_core_request(ADDRESS + UINT64_C(4288), MEMORY_LOAD, ATOMIC_SWAP,
                      UINT64_C(0), UINT64_C(22));
    expect_core_response(UINT64_C(0x8877665544332211), WRITEBACK_INTEGER_KIND,
                         UINT64_C(22));

    // A maintenance requester keeps snoop service live until Home completion.
    // All RISC-V maintenance commands preserve dirty data; inval uses flush.
    for (int operation = 7; operation <= 9; operation++) {
      reset = 1;
      tick();
      tick();
      reset = 0;
      tx_req_pending = 0;
      tx_rsp_pending = 0;
      tx_dat_pending = 0;
      // NTL stores still acquire and install a dirty copy.
      send_core_request(ADDRESS, MEMORY_STORE, ATOMIC_SWAP, STORE_DATA, 0,
                        UINT64_C(4));
      accept_request(READ_UNIQUE, ADDRESS, 0, 6, 1, 0);
      return_line(ADDRESS, LINE, UINT64_C(2));
      accept_comp_ack();
      expect_core_response(0, WRITEBACK_ACK_KIND, 0);
      evict_dirty_line = LINE;
      bit_slice(evict_dirty_line, 0, 64) = STORE_DATA;
      send_core_request(ADDRESS + 63, ((operation)&low_mask(4)), ATOMIC_SWAP, 0,
                        0);
      for (int cycles = 0; !tx_req_pending && cycles < 100; cycles++)
        tick();
      CHECK(tx_req_pending &&
            captured_req.popcode ==
                (operation == 8 ? UINT64_C(8) : UINT64_C(9)) &&
            captured_req.paddress == bit_slice(ADDRESS, 0, 44) &&
            captured_req.ptxn_uid == 2 && captured_req.pexcl_usnoop_ume_ucah &&
            mem_attr(captured_req.pmem_uattr) == UINT64_C(4) &&
            !captured_req.pexp_ucomp_uack);
      tx_req_pending = 0;
      if (operation == 8) {
        send_response(PCRD_GRANT, 0, 0, UINT64_C(5));
        send_response(RETRY_ACK, 2, 0, UINT64_C(5));
        for (int cycles = 0; !tx_req_pending && cycles < 100; cycles++)
          tick();
        CHECK(tx_req_pending && captured_req.popcode == UINT64_C(8) &&
              !captured_req.pallow_uretry && captured_req.ppcrd_utype == 5 &&
              captured_req.ptxn_uid == 2);
        tx_req_pending = 0;
      }
      chi_in.prequest_udata.pready = 0;
      send_snoop(ADDRESS, UINT64_C(123),
                 operation == 8 ? UINT64_C(8) : UINT64_C(9));
      for (beat = 0; beat < 4; beat++)
        accept_snoop_data(beat, evict_dirty_line, UINT64_C(123));
      tick();
      for (int repeat_index = 0; repeat_index < (5); ++repeat_index) {
        CHECK(!core_out.presponse.pvalid && !core_out.pdrained);
        tick();
      }
      send_response(COMP, 2, 0, 0);
      expect_core_response(0, WRITEBACK_ACK_KIND, 0);
      send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
      // The existing dirty-snoop policy relinquishes its local copy, including
      // for CleanShared. Clean is allowed to invalidate after preserving data.
      accept_request(READ_CLEAN, ADDRESS, 0, 6, 1, 0);
      return_line(ADDRESS, evict_dirty_line, UINT64_C(1));
      accept_comp_ack();
      expect_core_response(STORE_DATA, WRITEBACK_INTEGER_KIND, 1);
      CHECK(!tx_req_pending);
      // A miss still travels to Home; a failed completion must be observable.
      send_core_request(ADDRESS + UINT64_C(4096), UINT64_C(9), ATOMIC_SWAP, 0,
                        0);
      for (int cycles = 0; !tx_req_pending && cycles < 100; cycles++)
        tick();
      CHECK(tx_req_pending && captured_req.popcode == UINT64_C(9));
      tx_req_pending = 0;
      send_response(COMP, 2, 0, 0, UINT64_C(2));
      expect_core_response(0, WRITEBACK_ACK_KIND, 0, 1);
    }
    // Start with two adjacent UniqueClean lines. Exercise every aligned W/D
    // reservation on both sides of a 64-byte boundary within one 128-byte block.
    reset = 1;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick();
    reset = 0;
    tx_req_pending = 0;
    tx_rsp_pending = 0;
    tx_dat_pending = 0;
    tick();
    CHECK(!core_out.preservation_uvalid);
    for (int line_index = 0; line_index < 2; line_index++) {
      send_core_request(ADDRESS + ((line_index * 64) & low_mask(64)),
                        MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
      accept_request(READ_CLEAN, ADDRESS + ((line_index * 64) & low_mask(64)),
                     0, 6, 1, 0);
      return_line(ADDRESS + ((line_index * 64) & low_mask(64)), Line{},
                  UINT64_C(2));
      accept_comp_ack();
      expect_core_response(0, WRITEBACK_INTEGER_KIND, 1);
    }
    for (int size = 2; size <= 3; size++) {
      for (int offset = 0; offset < 128; offset += (1 << size)) {
        send_core_request(ADDRESS + ((offset)&low_mask(64)), MEMORY_LR,
                          ATOMIC_SWAP, 0, 1, 0, UINT64_C(3),
                          ((size)&low_mask(2)));
        expect_core_response(0, WRITEBACK_INTEGER_KIND, 1);
        CHECK(core_out.preservation_uvalid);
        // A neighboring line is outside the reservation, even in the same 128-byte block.
        send_core_request(ADDRESS + ((offset ^ 64) & low_mask(64)),
                          MEMORY_STORE, ATOMIC_SWAP, 0, 0, 0, UINT64_C(3),
                          ((size)&low_mask(2)));
        expect_core_response(0, WRITEBACK_ACK_KIND, 0);
        CHECK(core_out.preservation_uvalid);
        send_core_request(ADDRESS + ((offset)&low_mask(64)), MEMORY_SC,
                          ATOMIC_SWAP, UINT64_C(4660), 2, 0, UINT64_C(3),
                          ((size)&low_mask(2)));
        expect_core_response(0, WRITEBACK_INTEGER_KIND, 2);
        CHECK(!core_out.preservation_uvalid);
        send_core_request(ADDRESS + ((offset)&low_mask(64)), MEMORY_SC,
                          ATOMIC_SWAP, UINT64_C(22136), 2, 0, UINT64_C(3),
                          ((size)&low_mask(2)));
        expect_core_response(1, WRITEBACK_INTEGER_KIND, 2);
        send_core_request(ADDRESS + ((offset)&low_mask(64)), MEMORY_LOAD,
                          ATOMIC_SWAP, 0, 1, 0, UINT64_C(3),
                          ((size)&low_mask(2)));
        expect_core_response(UINT64_C(4660), WRITEBACK_INTEGER_KIND, 1);
        send_core_request(ADDRESS + ((offset)&low_mask(64)), MEMORY_LR,
                          ATOMIC_SWAP, 0, 1, 0, UINT64_C(3),
                          ((size)&low_mask(2)));
        expect_core_response(UINT64_C(4660), WRITEBACK_INTEGER_KIND, 1);
        send_core_request(ADDRESS + ((offset ^ (1 << size)) & low_mask(64)),
                          MEMORY_SC, ATOMIC_SWAP, UINT64_C(22136), 2, 0,
                          UINT64_C(3), ((size)&low_mask(2)));
        expect_core_response(1, WRITEBACK_INTEGER_KIND, 2);
        CHECK(!core_out.preservation_uvalid && !tx_req_pending);
        send_core_request(ADDRESS + ((offset ^ (1 << size)) & low_mask(64)),
                          MEMORY_LOAD, ATOMIC_SWAP, 0, 1, 0, UINT64_C(3),
                          ((size)&low_mask(2)));
        expect_core_response(0, WRITEBACK_INTEGER_KIND, 1);
        send_core_request(ADDRESS + ((offset)&low_mask(64)), MEMORY_STORE,
                          ATOMIC_SWAP, 0, 0, 0, UINT64_C(3),
                          ((size)&low_mask(2)));
        expect_core_response(0, WRITEBACK_ACK_KIND, 0);
      }
    }
    // Address equality alone is insufficient: a W reservation cannot authorize SC.D.
    send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1, 0, UINT64_C(3),
                      UINT64_C(2));
    expect_core_response(0, WRITEBACK_INTEGER_KIND, 1);
    send_core_request(ADDRESS, MEMORY_SC, ATOMIC_SWAP, UINT64_C(22136), 2);
    expect_core_response(1, WRITEBACK_INTEGER_KIND, 2);
    send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
    expect_core_response(0, WRITEBACK_INTEGER_KIND, 1);

    // Exercise all nine AMOs through the cache, not only the standalone ALU.
    for (int size = 2; size <= 3; size++) {
      for (int operation = 0; operation < 9; operation++) {
        for (int sample = 0; sample < 4; sample++) {
          std::uint64_t left_value, right_value, result, address, expected_word;
          left_value = amo_operand(sample);
          right_value = amo_operand(sample ^ 1);
          result = amo_reference(left_value, right_value, operation, size == 2);
          address = ADDRESS +
                    (size == 2 ? ((sample & 1) != 0 ? UINT64_C(4) : UINT64_C(0))
                               : UINT64_C(56));
          expected_word = UINT64_C(0xcafef00ddeadbeef);
          send_core_request(ADDRESS, MEMORY_STORE, ATOMIC_SWAP, expected_word,
                            0);
          expect_core_response(0, WRITEBACK_ACK_KIND, 0);
          send_core_request(address, MEMORY_STORE, ATOMIC_SWAP, left_value, 0,
                            0, UINT64_C(3), ((size)&low_mask(2)));
          expect_core_response(0, WRITEBACK_ACK_KIND, 0);
          send_core_request(address, MEMORY_ATOMIC, ((operation)&low_mask(4)),
                            right_value, 2, 0, UINT64_C(3),
                            ((size)&low_mask(2)));
          expect_core_response(size == 2 ? sign_extend(left_value, 32)
                                         : left_value,
                               WRITEBACK_INTEGER_KIND, 2);
          send_core_request(address, MEMORY_LOAD, ATOMIC_SWAP, 0, 1, 0,
                            UINT64_C(3), ((size)&low_mask(2)));
          expect_core_response(result, WRITEBACK_INTEGER_KIND, 1);
          if (size == 2) {
            bit_slice(expected_word, (sample & 1) * 32, 32) =
                bit_slice(result, 0, 32);
            send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
            expect_core_response(expected_word, WRITEBACK_INTEGER_KIND, 1);
          }
        }
      }
    }
    // Request acceptance is not the AMO's linearization point. A contending
    // snoop may win first, but cannot expose a partial RMW or lose the request.
    send_core_request(ADDRESS, MEMORY_ZERO, ATOMIC_SWAP, 0, 0);
    expect_core_response(0, WRITEBACK_ACK_KIND, 0);
    send_core_request(ADDRESS + 56, MEMORY_STORE, ATOMIC_SWAP, 1, 0);
    expect_core_response(0, WRITEBACK_ACK_KIND, 0);
    watch_amo_response = 1;
    send_core_request(ADDRESS + 56, MEMORY_ATOMIC, ATOMIC_ADD, 2, 2);
    send_snoop(ADDRESS, UINT64_C(125));
    dirty_line = {};
    for (beat = 0; beat < 4; beat++) {
      if (beat == 3) {
        for (int cycles = 0; !chi_out.prequest_udata.pvalid && cycles < 100;
             cycles++)
          tick();
        CHECK(chi_out.prequest_udata.pvalid &&
              (bit_slice(chi_out.prequest_udata.pbits.pdata, 64, 64) ==
                   UINT64_C(1) ||
               bit_slice(chi_out.prequest_udata.pbits.pdata, 64, 64) ==
                   UINT64_C(3)));
        bit_slice(dirty_line, 448, 64) =
            bit_slice(chi_out.prequest_udata.pbits.pdata, 64, 64);
      }
      accept_snoop_data(beat, dirty_line, UINT64_C(125));
    }
    if (bit_slice(dirty_line, 448, 64) == 1) {
      CHECK(amo_response_count == 0);
      accept_request(READ_UNIQUE, ADDRESS, 0, 6, 1, 0);
      return_line(ADDRESS, dirty_line, UINT64_C(2));
      accept_comp_ack();
    }
    for (int cycles = 0; amo_response_count == 0 && cycles < 100; cycles++)
      tick();
    watch_amo_response = 0;
    CHECK(amo_response_count == 1);
    if (bit_slice(dirty_line, 448, 64) == 1) {
      send_core_request(ADDRESS + 56, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
      expect_core_response(3, WRITEBACK_INTEGER_KIND, 1);
    }
    for (int size = 2; size <= 3; size++) {
      for (int operation = 0; operation < 9; operation++) {
        std::uint64_t old_value, operand, address, result;
        Line initial_line;
        reset = 1;
        for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
          tick();
        reset = 0;
        tx_req_pending = 0;
        tx_rsp_pending = 0;
        tx_dat_pending = 0;
        tick();
        old_value = amo_operand(0);
        operand = amo_operand(1);
        for (unsigned word = 0; word < 8; ++word)
          write_bits(initial_line, word * 64, 64, old_value);
        address = ADDRESS + (size == 2 ? UINT64_C(4) : UINT64_C(56));
        if (size == 2)
          old_value = sign_extend(read_bits(initial_line, 32, 32), 32);
        result = amo_reference(old_value, operand, operation, size == 2);
        send_core_request(address, MEMORY_ATOMIC, ((operation)&low_mask(4)),
                          operand, 2, 0, UINT64_C(3), ((size)&low_mask(2)));
        accept_request(READ_UNIQUE, ADDRESS, 0, 6, 1, 0);
        for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
          tick();
          CHECK(!core_out.presponse.pvalid);
        }
        return_line(ADDRESS, initial_line, UINT64_C(2));
        accept_comp_ack();
        expect_core_response(old_value, WRITEBACK_INTEGER_KIND, 2);
        send_core_request(address, MEMORY_LOAD, ATOMIC_SWAP, 0, 1, 0,
                          UINT64_C(3), ((size)&low_mask(2)));
        expect_core_response(result, WRITEBACK_INTEGER_KIND, 1);
      }
    }

    // Model Home's invalidating probe for a read-only inclusive-LLC victim.
    // It may wait, but must observe SC's new dirty data, or run after a bounded
    // timeout. No write or successful SC by another participant is injected.
    for (int scenario = 0; scenario < 3; scenario++) {
      Line progress_line;
      reset = 1;
      chi_in.psnoops = {};
      chi_in.prequest_udata.pready = 0;
      prefetch_in = {};
      for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
        tick();
      reset = 0;
      tx_req_pending = 0;
      tx_rsp_pending = 0;
      tx_dat_pending = 0;
      tick();
      progress_line = LINE;
      // Cover both an absent LR line and the shared-hit ownership upgrade.
      if (scenario == 1) {
        send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
        accept_request(READ_CLEAN, ADDRESS, 0, 6, 1, 0);
        return_line(ADDRESS, LINE, UINT64_C(1));
        accept_comp_ack();
        expect_core_response(bit_slice(LINE, 0, 64), WRITEBACK_INTEGER_KIND, 1);
      }
      send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1);
      accept_request(READ_UNIQUE, ADDRESS, 0, 6, 1, 0);
      for (int repeat_index = 0; repeat_index < (12); ++repeat_index) {
        CHECK(!core_out.presponse.pvalid && !core_out.preservation_uvalid);
        tick();
      }
      return_line(ADDRESS, LINE, UINT64_C(2));
      // The earliest post-grant probe must not slip between CompAck and install.
      progress_snoop_accepts = 0;
      watch_progress_snoop = 1;
      forbid_progress_snoop = 1;
      chi_in.psnoops.pbits = {};
      chi_in.psnoops.pbits.paddress = bit_slice(ADDRESS, 3, 41);
      chi_in.psnoops.pbits.popcode = SNP_CLEAN_INVALID;
      chi_in.psnoops.pbits.ptxn_uid = UINT64_C(121);
      chi_in.psnoops.pbits.psrc_uid = HOME_ID;
      chi_in.psnoops.pvalid = 1;
      accept_comp_ack();
      expect_core_response(bit_slice(LINE, 0, 64), WRITEBACK_INTEGER_KIND, 1);
      CHECK(core_out.preservation_uvalid);
      if (scenario == 0) {
        // Leave ample time for sixteen scalar instructions while probes and
        // best-effort colliding prefetches remain continuously offered.
        prefetch_in = {
            .pvalid = 1,
            .pbits = {.paddress = THIRD_ADDRESS, .poperation = UINT64_C(2)}};
        for (unsigned repeat_index = 0; repeat_index < (80); ++repeat_index)
          tick();
        prefetch_in = {};
        CHECK(!tx_req_pending);
        forbid_progress_snoop = 0;
        send_core_request(ADDRESS, MEMORY_SC, ATOMIC_SWAP, STORE_DATA, 2);
        expect_core_response(0, WRITEBACK_INTEGER_KIND, 2);
        bit_slice(progress_line, 0, 64) = STORE_DATA;
      } else {
        forbid_progress_snoop = 0;
        if (scenario == 2) {
          // Repeated LR is not allowed to renew a probe-blocking reservation.
          send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1);
          expect_core_response(bit_slice(LINE, 0, 64), WRITEBACK_INTEGER_KIND,
                               1);
        }
      }
      for (int cycle = 0; progress_snoop_accepts == 0 && cycle < 160; cycle++)
        tick();
      CHECK(progress_snoop_accepts == 1 && !tx_req_pending);
      watch_progress_snoop = 0;
      if (scenario == 0) {
        for (int packet = 0; packet < 4; packet++)
          accept_snoop_data(packet, progress_line, UINT64_C(121));
      } else {
        for (int cycle = 0; !tx_rsp_pending && cycle < 100; cycle++)
          tick();
        CHECK(tx_rsp_pending && captured_rsp.popcode == 1 &&
              captured_rsp.presp == 0 &&
              captured_rsp.ptxn_uid == UINT64_C(121));
        tx_rsp_pending = 0;
      }
      CHECK(!core_out.preservation_uvalid);
      send_core_request(ADDRESS, MEMORY_SC, ATOMIC_SWAP, STORE_DATA_2, 2);
      expect_core_response(1, WRITEBACK_INTEGER_KIND, 2);
      CHECK(!tx_req_pending);
      // An intervening writer can now supply a new value. Failed SC must not
      // carry its old authorization across that subsequent acquisition.
      send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1);
      accept_request(READ_UNIQUE, ADDRESS, 0, 6, 1, 0);
      bit_slice(progress_line, 0, 64) = STORE_DATA_2;
      return_line(ADDRESS, progress_line, UINT64_C(2));
      accept_comp_ack();
      expect_core_response(STORE_DATA_2, WRITEBACK_INTEGER_KIND, 1);
      send_core_request(ADDRESS, MEMORY_SC, ATOMIC_SWAP, STORE_DATA, 2);
      expect_core_response(0, WRITEBACK_INTEGER_KIND, 2);
    }

    // Timer expiry permits snoops; it does not revoke ownership by itself.
    // Cover W/D delays longer than the ACT sequence that crossed the window.
    for (int size = 2; size <= 3; size++) {
      send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1, 0, UINT64_C(3),
                        ((size)&low_mask(2)));
      expect_core_response(size == 2 ? sign_extend(STORE_DATA, 32) : STORE_DATA,
                           WRITEBACK_INTEGER_KIND, 1);
      for (unsigned repeat_index = 0; repeat_index < (192); ++repeat_index)
        tick();
      CHECK(core_out.preservation_uvalid);
      // A repeated LR records a reservation even during an existing window.
      send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1, 0, UINT64_C(3),
                        ((size)&low_mask(2)));
      expect_core_response(size == 2 ? sign_extend(STORE_DATA, 32) : STORE_DATA,
                           WRITEBACK_INTEGER_KIND, 1);
      send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1, 0, UINT64_C(3),
                        ((size)&low_mask(2)));
      expect_core_response(size == 2 ? sign_extend(STORE_DATA, 32) : STORE_DATA,
                           WRITEBACK_INTEGER_KIND, 1);
      for (unsigned repeat_index = 0; repeat_index < (192); ++repeat_index)
        tick();
      send_core_request(ADDRESS, MEMORY_SC, ATOMIC_SWAP, STORE_DATA, 2, 0,
                        UINT64_C(3), ((size)&low_mask(2)));
      expect_core_response(0, WRITEBACK_INTEGER_KIND, 2);
      CHECK(!tx_req_pending);
    }
    // Start with no reservation or post-grant protection. Continuous probes
    // of an unrelated line must still give a waiting local LR a lookup turn.
    reset = 1;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick();
    reset = 0;
    tx_req_pending = 0;
    tx_rsp_pending = 0;
    tx_dat_pending = 0;
    tick();
    send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
    accept_request(READ_CLEAN, ADDRESS, 0, 6, 1, 0);
    return_line(ADDRESS, LINE, UINT64_C(2));
    accept_comp_ack();
    expect_core_response(bit_slice(LINE, 0, 64), WRITEBACK_INTEGER_KIND, 1);
    for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
      tick();
    chi_in.psnoops.pbits = {};
    chi_in.psnoops.pbits.paddress = bit_slice(THIRD_ADDRESS, 3, 41);
    chi_in.psnoops.pbits.popcode = SNP_CLEAN_INVALID;
    chi_in.psnoops.pbits.ptxn_uid = UINT64_C(122);
    chi_in.psnoops.pbits.psrc_uid = HOME_ID;
    chi_in.psnoops.pvalid = 1;
    for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index)
      tick();
    send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1);
    expect_core_response(bit_slice(LINE, 0, 64), WRITEBACK_INTEGER_KIND, 1);
    CHECK(core_out.preservation_uvalid && !chi_out.psnoops.pready &&
          !tx_req_pending);
    send_core_request(ADDRESS, MEMORY_SC, ATOMIC_SWAP, STORE_DATA, 2);
    expect_core_response(0, WRITEBACK_INTEGER_KIND, 2);
    chi_in.psnoops = {};

    reset = 1;
    chi_in.psnoops = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick();
    reset = 0;
    tx_req_pending = 0;
    tx_rsp_pending = 0;
    tx_dat_pending = 0;
    tick();
    send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
    accept_request(READ_CLEAN, ADDRESS, 0, 6, 1, 0);
    return_line(ADDRESS, LINE, UINT64_C(2));
    accept_comp_ack();
    expect_core_response(bit_slice(LINE, 0, 64), WRITEBACK_INTEGER_KIND, 1);
    // A squashed store candidate has no architectural or array effect.
    stage_pipeline_store(ADDRESS, STORE_DATA);
    tick();
    pipeline_in = {};
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick();
    check_pipeline_load(ADDRESS, 1, 1, bit_slice(LINE, 0, 64));
    // Same word / disjoint bytes, different word, and actual byte overlap.
    store_then_load(ADDRESS, UINT64_C(48879), ADDRESS + 2, UINT64_C(1),
                    UINT64_C(1), 0, UINT64_C(17459));
    check_pipeline_load(ADDRESS, 1, 1, UINT64_C(0x887766554433beef));
    store_then_load(ADDRESS, UINT64_C(4660), ADDRESS + 8, UINT64_C(1),
                    UINT64_C(3), 0, bit_slice(LINE, 64, 64));
    store_then_load(ADDRESS, UINT64_C(22136), ADDRESS, UINT64_C(1), UINT64_C(1),
                    1, 0);
    check_pipeline_load(ADDRESS, 1, 1, UINT64_C(0x8877665544335678));
    // Same page-offset, different physical tag is not a dependency.
    send_core_request(EVICT_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
    accept_request(READ_CLEAN, EVICT_ADDRESS, 0, 6, 1, 0);
    return_line(EVICT_ADDRESS, EVICT_LINE, UINT64_C(2));
    accept_comp_ack();
    expect_core_response(bit_slice(EVICT_LINE, 0, 64), WRITEBACK_INTEGER_KIND,
                         1);
    store_then_load(ADDRESS, UINT64_C(43981), EVICT_ADDRESS, UINT64_C(1),
                    UINT64_C(3), 0, bit_slice(EVICT_LINE, 0, 64));
    stage_pipeline_store(ADDRESS, STORE_DATA);
    tick();
    pipeline_in.prequest.pvalid = 0;
    pipeline_in.pcommit = 1;
    chi_in.psnoops.pbits = {};
    chi_in.psnoops.pbits.paddress = bit_slice(THIRD_ADDRESS, 3, 41);
    chi_in.psnoops.pbits.popcode = SNP_CLEAN_INVALID;
    chi_in.psnoops.pbits.ptxn_uid = UINT64_C(119);
    chi_in.psnoops.pbits.psrc_uid = HOME_ID;
    chi_in.psnoops.pvalid = 1;
    settle();
    CHECK(pipeline_out.pcommit_uready);
    tick();
    pipeline_in = {};
    chi_in.psnoops = {};
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    tx_rsp_pending = 0;
    check_pipeline_load(ADDRESS, 1, 1, STORE_DATA);
    // A matching snoop must see the committed bytes, even when it arrives
    // immediately after enqueue and before an ordinary idle drain slot.
    stage_pipeline_store(ADDRESS, STORE_DATA);
    tick();
    pipeline_in.prequest.pvalid = 0;
    pipeline_in.pcommit = 1;
    settle();
    CHECK(pipeline_out.pcommit_uready);
    tick();
    pipeline_in = {};
    send_snoop(ADDRESS, UINT64_C(121));
    dirty_line = LINE;
    bit_slice(dirty_line, 0, 64) = STORE_DATA;
    for (beat = 0; beat < 4; beat++)
      accept_snoop_data(beat, dirty_line, UINT64_C(121));
    check_pipeline_load(ADDRESS, 1, 0);
    CHECK(core_out.pdrained);
    // A probe between MEM proof and WB authorization rejects the candidate.
    stage_pipeline_store(EVICT_ADDRESS, STORE_DATA);
    tick();
    pipeline_in.prequest.pvalid = 0;
    pipeline_in.pcommit = 1;
    chi_in.psnoops.pbits = {};
    chi_in.psnoops.pbits.paddress = bit_slice(EVICT_ADDRESS, 3, 41);
    chi_in.psnoops.pbits.popcode = SNP_CLEAN_INVALID;
    chi_in.psnoops.pbits.ptxn_uid = UINT64_C(120);
    chi_in.psnoops.pbits.psrc_uid = HOME_ID;
    chi_in.psnoops.pvalid = 1;
    settle();
    CHECK(!pipeline_out.pcommit_uready);
    tick();
    pipeline_in = {};
    chi_in.psnoops = {};
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    CHECK(tx_rsp_pending && captured_rsp.popcode == 1 &&
          captured_rsp.ptxn_uid == UINT64_C(120) && !tx_dat_pending);
    tx_rsp_pending = 0;

    // One ordinary miss permits sustained read hits in other sets and retains
    // three authorized same-line waiters through retry, gapped packets, and
    // installation. Independent misses still cannot claim the transaction.
    for (int store_miss = 0; store_miss < 2; store_miss++) {
      int replies, blocked, resumed, primary_replies, waiter_replies,
          ack_replies;
      prepare_hit_under_miss();
      send_core_request(THIRD_ADDRESS,
                        store_miss != 0 ? MEMORY_STORE : MEMORY_LOAD,
                        ATOMIC_SWAP, STORE_DATA, 2);
      accept_request(store_miss != 0 ? READ_UNIQUE : READ_CLEAN, THIRD_ADDRESS,
                     0, 6, 1, 0);
      stream_under_miss();
      check_under_miss(ADDRESS, PIPE_REPLAY); // Victim still has its old tag.
      check_under_miss(EVICT_ADDRESS,
                       PIPE_REPLAY); // Other way in the reserved set.
      check_under_miss(PREFETCH_WRITE_ADDRESS,
                       PIPE_REPLAY); // Second miss, another set.
      check_under_miss(PREFETCH_READ_ADDRESS, PIPE_REPLAY, 0, MEMORY_STORE);
      if (store_miss != 0) {
        check_under_miss(THIRD_ADDRESS, PIPE_SLOW);
        send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 3);
        check_under_miss(THIRD_ADDRESS + 8, PIPE_SLOW, 0, MEMORY_STORE);
        send_core_request(THIRD_ADDRESS + 8, MEMORY_STORE, ATOMIC_SWAP,
                          STORE_DATA_2, 0);
        check_under_miss(THIRD_ADDRESS + 16, PIPE_SLOW);
        send_core_request(THIRD_ADDRESS + 16, MEMORY_LOAD, ATOMIC_SWAP, 0, 4);
      } else {
        check_under_miss(THIRD_ADDRESS + 8, PIPE_SLOW);
        send_core_request(THIRD_ADDRESS + 8, MEMORY_LOAD, ATOMIC_SWAP, 0, 3);
        check_under_miss(THIRD_ADDRESS + 16, PIPE_SLOW);
        send_core_request(THIRD_ADDRESS + 16, MEMORY_LOAD, ATOMIC_SWAP, 0, 4);
        check_under_miss(THIRD_ADDRESS + 24, PIPE_SLOW);
        send_core_request(THIRD_ADDRESS + 24, MEMORY_LOAD, ATOMIC_SWAP, 0, 5);
      }
      settle();
      CHECK(!core_out.prequest.pready);
      send_response(RETRY_ACK, 0, 0, 3);
      stream_under_miss();
      send_response(PCRD_GRANT, 0, 0, 3);
      accept_request(store_miss != 0 ? READ_UNIQUE : READ_CLEAN, THIRD_ADDRESS,
                     0, 6, 0, 3);
      chi_in.prequester_uresponses.pready = 0;
      return_line(THIRD_ADDRESS, THIRD_LINE,
                  store_miss != 0 ? UINT64_C(2) : UINT64_C(1), 1);
      stream_under_miss(); // Full line buffered, CompAck backpressured.
      pipeline_lookup_in = {
          .pvalid = 1,
          .pbits = {.pbyte_umask = static_cast<uint8_t>(
                        ((((1 << (1 << (3))) - 1)
                          << ((PREFETCH_READ_ADDRESS ^ virtual_page_xor) % 8)) &
                         low_mask(8))),
                    .paddress = PREFETCH_READ_ADDRESS ^ virtual_page_xor,
                    .paccess = MEMORY_LOAD,
                    .pwidth = 3,
                    .punsigned = 0,
                    .pdata = 0}};
      tick();
      pipeline_in.prequest = {.pvalid = 1,
                              .pbits = {.pbyte_umask = static_cast<uint8_t>((
                                            (((1 << (1 << (3))) - 1)
                                             << ((PREFETCH_READ_ADDRESS) % 8)) &
                                            low_mask(8))),
                                        .paddress = PREFETCH_READ_ADDRESS,
                                        .paccess = MEMORY_LOAD,
                                        .pwidth = 3,
                                        .punsigned = 0,
                                        .pdata = 0}};
      grant_rsp_credit();
      tick();
      accept_comp_ack();
      replies = 0;
      blocked = 0;
      resumed = 0;
      primary_replies = 0;
      waiter_replies = 0;
      ack_replies = 0;
      for (int repeat_index = 0; repeat_index < (40); ++repeat_index) {
        settle();
        if (pipeline_out.presponse.pbits.poutcome == PIPE_LOAD_HIT) {
          CHECK(pipeline_out.presponse.pbits.pdata == bit_slice(LINE, 0, 64));
          resumed++;
        } else {
          CHECK(pipeline_out.presponse.pbits.poutcome == PIPE_REPLAY);
          blocked++;
        }
        if (core_out.presponse.pvalid) {
          if (store_miss != 0) {
            if (core_out.presponse.pbits.pcontext.pwriteback ==
                memory_integer(UINT64_C(3))) {
              CHECK(core_out.presponse.pbits.pdata == STORE_DATA);
              waiter_replies++;
            } else if (core_out.presponse.pbits.pcontext.pwriteback ==
                       memory_integer(UINT64_C(4))) {
              CHECK(core_out.presponse.pbits.pdata ==
                    bit_slice(THIRD_LINE, 128, 64));
              waiter_replies++;
            } else {
              CHECK(core_out.presponse.pbits.pcontext.pwriteback ==
                        UINT64_C(0) &&
                    core_out.presponse.pbits.pdata == 0);
              ack_replies++;
            }
          } else {
            switch (memory_rd(core_out.presponse.pbits.pcontext.pwriteback)) {
            case UINT64_C(2): {
              {
                CHECK(core_out.presponse.pbits.pdata ==
                      bit_slice(THIRD_LINE, 0, 64));
                primary_replies++;
              }
            } break;
            case UINT64_C(3): {
              {
                CHECK(core_out.presponse.pbits.pdata ==
                      bit_slice(THIRD_LINE, 64, 64));
                waiter_replies++;
              }
            } break;
            case UINT64_C(4): {
              {
                CHECK(core_out.presponse.pbits.pdata ==
                      bit_slice(THIRD_LINE, 128, 64));
                waiter_replies++;
              }
            } break;
            case UINT64_C(5): {
              {
                CHECK(core_out.presponse.pbits.pdata ==
                      bit_slice(THIRD_LINE, 192, 64));
                waiter_replies++;
              }
            } break;
            default: {
              fail(1, "same-line load returned to an unexpected destination");
            } break;
            }
          }
          replies++;
        }
        tick();
      }
      pipeline_lookup_in = {};
      pipeline_in = {};
      tick();
      CHECK(replies == 4 &&
            (store_miss != 0 ? (waiter_replies == 2 && ack_replies == 2)
                             : (primary_replies == 1 && waiter_replies == 3)) &&
            blocked >= 8 && resumed >= 8 && core_out.pdrained &&
            !tx_req_pending);
      check_pipeline_load(THIRD_ADDRESS, 1, 1,
                          store_miss != 0 ? STORE_DATA
                                          : bit_slice(THIRD_LINE, 0, 64));
      check_pipeline_load(THIRD_ADDRESS + 8, 1, 1,
                          store_miss != 0 ? STORE_DATA_2
                                          : bit_slice(THIRD_LINE, 64, 64));
      check_pipeline_load(EVICT_ADDRESS, 1, 1, bit_slice(EVICT_LINE, 0, 64));
    }

    // Non-allocating refill completion can coincide with an independent hit.
    {
      int simultaneous;
      prepare_hit_under_miss();
      send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 2, 1);
      accept_request(READ_CLEAN, THIRD_ADDRESS, 0, 6, 1, 0);
      chi_in.prequester_uresponses.pready = 0;
      return_line(THIRD_ADDRESS, THIRD_LINE, UINT64_C(1), 1);
      pipeline_lookup_in = {
          .pvalid = 1,
          .pbits = {.pbyte_umask = static_cast<uint8_t>(
                        ((((1 << (1 << (3))) - 1)
                          << ((PREFETCH_READ_ADDRESS ^ virtual_page_xor) % 8)) &
                         low_mask(8))),
                    .paddress = PREFETCH_READ_ADDRESS ^ virtual_page_xor,
                    .paccess = MEMORY_LOAD,
                    .pwidth = 3,
                    .punsigned = 0,
                    .pdata = 0}};
      tick();
      pipeline_in.prequest = {.pvalid = 1,
                              .pbits = {.pbyte_umask = static_cast<uint8_t>((
                                            (((1 << (1 << (3))) - 1)
                                             << ((PREFETCH_READ_ADDRESS) % 8)) &
                                            low_mask(8))),
                                        .paddress = PREFETCH_READ_ADDRESS,
                                        .paccess = MEMORY_LOAD,
                                        .pwidth = 3,
                                        .punsigned = 0,
                                        .pdata = 0}};
      grant_rsp_credit();
      tick();
      accept_comp_ack();
      simultaneous = 0;
      for (int repeat_index = 0; repeat_index < (8); ++repeat_index) {
        settle();
        CHECK(pipeline_out.presponse.pbits.poutcome == PIPE_LOAD_HIT &&
              pipeline_out.presponse.pbits.pdata == bit_slice(LINE, 0, 64));
        if (core_out.presponse.pvalid) {
          CHECK(core_out.presponse.pbits.pdata ==
                    bit_slice(THIRD_LINE, 0, 64) &&
                memory_rd(core_out.presponse.pbits.pcontext.pwriteback) == 2);
          simultaneous++;
        }
        tick();
      }
      pipeline_lookup_in = {};
      pipeline_in = {};
      tick();
      CHECK(simultaneous == 1 && core_out.pdrained);
      check_pipeline_load(THIRD_ADDRESS, 1, 0);
    }

    // An older queued mutation remains an ordering barrier to speculative hits.
    prepare_hit_under_miss();
    send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 2);
    accept_request(READ_CLEAN, THIRD_ADDRESS, 0, 6, 1, 0);
    send_core_request(PREFETCH_READ_ADDRESS, MEMORY_STORE, ATOMIC_SWAP,
                      STORE_DATA, 0);
    check_under_miss(PREFETCH_READ_ADDRESS, PIPE_REPLAY);
    return_line(THIRD_ADDRESS, THIRD_LINE, UINT64_C(1));
    accept_comp_ack();
    expect_core_response(bit_slice(THIRD_LINE, 0, 64), WRITEBACK_INTEGER_KIND,
                         2);
    expect_core_response(0, WRITEBACK_ACK_KIND, 0);
    check_pipeline_load(PREFETCH_READ_ADDRESS, 1, 1, STORE_DATA);

    // Dirty victim transmission no longer monopolizes idle SRAM cycles.
    prepare_hit_under_miss();
    send_core_request(ADDRESS, MEMORY_STORE, ATOMIC_SWAP, STORE_DATA, 0);
    expect_core_response(0, WRITEBACK_ACK_KIND, 0);
    check_pipeline_load(EVICT_ADDRESS, 1, 1, bit_slice(EVICT_LINE, 0, 64));
    dirty_line = LINE;
    bit_slice(dirty_line, 0, 64) = STORE_DATA;
    send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 2);
    accept_request(WRITE_BACK_FULL, ADDRESS, 1, 6, 1, 0);
    check_under_miss(PREFETCH_READ_ADDRESS, PIPE_LOAD_HIT,
                     bit_slice(LINE, 0, 64));
    check_under_miss(ADDRESS, PIPE_REPLAY);
    send_response(COMP_DBID_RESP, 1, UINT64_C(85), 0);
    for (beat = 0; beat < 4; beat++)
      accept_copyback_data(beat, dirty_line);
    accept_request(READ_CLEAN, THIRD_ADDRESS, 0, 6, 1, 0);
    stream_under_miss();
    // An invalidating probe wins over the speculative stream, without waiting
    // for the unrelated refill. Once invalidated, that load must replay.
    send_snoop(PREFETCH_READ_ADDRESS, UINT64_C(127));
    for (int cycle = 0; !tx_rsp_pending && cycle < 32; cycle++) {
      pipeline_lookup_in = {
          .pvalid = 1,
          .pbits = {.pbyte_umask = static_cast<uint8_t>(
                        ((((1 << (1 << (3))) - 1)
                          << ((PREFETCH_READ_ADDRESS ^ virtual_page_xor) % 8)) &
                         low_mask(8))),
                    .paddress = PREFETCH_READ_ADDRESS ^ virtual_page_xor,
                    .paccess = MEMORY_LOAD,
                    .pwidth = 3,
                    .punsigned = 0,
                    .pdata = 0}};
      tick();
    }
    pipeline_lookup_in = {};
    CHECK(tx_rsp_pending && captured_rsp.ptxn_uid == UINT64_C(127) &&
          captured_rsp.presp == 0);
    tx_rsp_pending = 0;
    check_under_miss(PREFETCH_READ_ADDRESS, PIPE_REPLAY);
    return_line(THIRD_ADDRESS, THIRD_LINE, UINT64_C(1));
    accept_comp_ack();
    expect_core_response(bit_slice(THIRD_LINE, 0, 64), WRITEBACK_INTEGER_KIND,
                         2);

    // LR/atomic acquisition is still globally serialized; reset retires the
    // reservation bookkeeping even with an unanswered ordinary miss.
    for (int atomic_miss = 0; atomic_miss < 2; atomic_miss++) {
      prepare_hit_under_miss();
      send_core_request(THIRD_ADDRESS,
                        atomic_miss != 0 ? MEMORY_ATOMIC : MEMORY_LR,
                        ATOMIC_SWAP, STORE_DATA, 2);
      accept_request(READ_UNIQUE, THIRD_ADDRESS, 0, 6, 1, 0);
      check_under_miss(PREFETCH_READ_ADDRESS, PIPE_REPLAY);
      return_line(THIRD_ADDRESS, THIRD_LINE, UINT64_C(2));
      accept_comp_ack();
      expect_core_response(bit_slice(THIRD_LINE, 0, 64), WRITEBACK_INTEGER_KIND,
                           2);
    }
    prepare_hit_under_miss();
    send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 2);
    accept_request(READ_CLEAN, THIRD_ADDRESS, 0, 6, 1, 0);
    prepare_hit_under_miss();
    check_pipeline_load(PREFETCH_READ_ADDRESS, 1, 1, bit_slice(LINE, 0, 64));

    // A simultaneous demand wins over a one-cycle hint. The hint is dropped,
    // not saved for later admission when the demand has completed.
    prefetch_in = {.pvalid = 1,
                   .pbits = {.paddress = PREFETCH_WRITE_ADDRESS,
                             .poperation = UINT64_C(2)}};
    send_core_request(PREFETCH_READ_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 6);
    prefetch_in = {};
    expect_core_response(bit_slice(LINE, 0, 64), WRITEBACK_INTEGER_KIND, 6);
    for (int repeat_index = 0; repeat_index < (12); ++repeat_index) {
      tick();
      CHECK(!tx_req_pending && !core_out.presponse.pvalid && core_out.pdrained);
    }

    // Hints offered while miss service blocks S3 must disappear. A younger
    // authorized demand remains retained and completes after the miss instead.
    send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 2);
    accept_request(READ_CLEAN, THIRD_ADDRESS, 0, 6, 1, 0);
    send_prefetch(PREFETCH_WRITE_ADDRESS, UINT64_C(3));
    prefetch_in = {.pvalid = 1,
                   .pbits = {.paddress = PREFETCH_WRITE_ADDRESS,
                             .poperation = UINT64_C(2)}};
    send_core_request(PREFETCH_READ_ADDRESS + 8, MEMORY_LOAD, ATOMIC_SWAP, 0,
                      7);
    prefetch_in = {};
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
      tick();
      CHECK(!tx_req_pending && !core_out.presponse.pvalid);
    }
    return_line(THIRD_ADDRESS, THIRD_LINE, UINT64_C(1));
    accept_comp_ack();
    expect_core_response(bit_slice(THIRD_LINE, 0, 64), WRITEBACK_INTEGER_KIND,
                         2);
    expect_core_response(bit_slice(LINE, 64, 64), WRITEBACK_INTEGER_KIND, 7);
    for (int repeat_index = 0; repeat_index < (12); ++repeat_index) {
      tick();
      CHECK(!tx_req_pending && !core_out.presponse.pvalid && core_out.pdrained);
    }

    // A competing transaction snoops the victim while WriteBackFull awaits
    // its grant. Return the authoritative dirty bytes once, then an Invalid
    // copyback; the saved victim buffer must never resurrect the old version.
    prepare_hit_under_miss();
    send_core_request(ADDRESS, MEMORY_STORE, ATOMIC_SWAP, STORE_DATA, 0);
    expect_core_response(0, WRITEBACK_ACK_KIND, 0);
    check_pipeline_load(EVICT_ADDRESS, 1, 1, bit_slice(EVICT_LINE, 0, 64));
    dirty_line = LINE;
    bit_slice(dirty_line, 0, 64) = STORE_DATA;
    send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 2);
    accept_request(WRITE_BACK_FULL, ADDRESS, 1, 6, 1, 0);
    chi_in.prequest_udata.pready = 0;
    send_snoop(ADDRESS, UINT64_C(120));
    for (beat = 0; beat < 4; beat++)
      accept_snoop_data(beat, dirty_line, UINT64_C(120));
    // Force a different snoop lookup after the victim gather; eviction identity
    // must come from the retained address/context, not mutable gather registers.
    send_snoop(PREFETCH_READ_ADDRESS, UINT64_C(121));
    for (int cycle = 0; !tx_rsp_pending && cycle < 100; cycle++)
      tick();
    CHECK(tx_rsp_pending);
    tx_rsp_pending = 0;
    grant_dat_credit();
    send_response(COMP_DBID_RESP, 1, UINT64_C(85), 0);
    for (beat = 0; beat < 4; beat++)
      accept_copyback_data(beat, dirty_line, 0);
    accept_request(READ_CLEAN, THIRD_ADDRESS, 0, 6, 1, 0);
    return_line(THIRD_ADDRESS, THIRD_LINE, UINT64_C(1));
    accept_comp_ack();
    expect_core_response(bit_slice(THIRD_LINE, 0, 64), WRITEBACK_INTEGER_KIND,
                         2);
    check_pipeline_load(ADDRESS, 1, 0);

    throw Finished{};
  }
}

int main() {
  return run_test([] {
    cycle_limit = 100002;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
