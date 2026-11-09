// Tests XLEN lanes at the superscalar boundary, independently of CHI refill order.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <deque>

using Word = decltype(memory_in.prequest.pbits.pmemory.pdata);
constexpr unsigned bytes = sizeof(Word);
constexpr unsigned size_code = bytes == 4 ? 2 : 3;
constexpr unsigned word_mask = (1u << bytes) - 1;
constexpr unsigned base = 0x400;
std::array<unsigned char, 64> line{};
std::uint16_t next_context = 0x5300;
std::deque<std::uint16_t> contexts;

template <class F> void until(F accepted) {
  for (unsigned cycle = 0; cycle < 128; ++cycle) {
    eval();
    if (accepted()) return;
    tick_model();
  }
  CHECK(false && "cache transfer timed out");
}

Word expected(unsigned offset) {
  Word value = 0;
  for (unsigned b = 0; b < bytes; ++b) value |= Word(line[offset + b]) << (8 * b);
  return value;
}

void service(std::uint64_t address, unsigned op, Word data, unsigned mask, unsigned width) {
  auto &req = memory_in.prequest;
  req.pbits = {};
  auto &memory = req.pbits.pmemory;
  memory.pcontext = ++next_context;
  memory.paddress = address;
  memory.paccess = op;
  memory.pdata = data;
  memory.pbyte_umask = mask;
  memory.pwidth = width;
  memory.punsigned = op == 1 || op == 2;
  req.pvalid = 1;
  until([] { return memory_out.prequest.pready; });
  CHECK(!memory_out.pfault.pvalid);
  contexts.push_back(memory.pcontext);
  tick_model();
  req.pvalid = 0;
}

void complete(Word value) {
  until([] { return memory_out.presponse.pvalid; });
  CHECK(!contexts.empty());
  for (unsigned hold = 0; hold < 3; ++hold) {
    CHECK(memory_out.presponse.pvalid && memory_out.presponse.pbits.pdata == value);
    CHECK(memory_out.presponse.pbits.pcontext == contexts.front());
    tick_model();
  }
  memory_in.presponse.pready = 1; tick_model(); memory_in.presponse.pready = 0;
  contexts.pop_front();
}

void flushed_fetch_before_device_load() {
  constexpr unsigned offset = bytes - 1;
  constexpr std::uint64_t address = UINT64_C(0x100002000) + offset;
  auto &fetch = instruction_in.prequest;
  auto &req = uncached_chi_out.preq;
  auto &dat = uncached_chi_in.pdat.presponse;

  fetch.pbits = {}; fetch.pbits.paddress = 0x10000; fetch.pvalid = 1;
  until([] { return instruction_out.prequest.pready; });
  tick_model(); fetch.pvalid = 0;
  until([&] { return req.pvalid; });
  CHECK(req.pbits.paddress == 0x10000);
  const auto fetch_home = req.pbits.ptgt_uid;
  uncached_chi_in.preq.pready = 1; tick_model(); uncached_chi_in.preq.pready = 0;

  // Admit the tagged byte load while the issued fetch still owns its CHI reply.
  service(address, 1, 0, 1u << offset, 0);
  memory_in.prequest.pbits = {};
  instruction_in.pflush = 1; tick_model(); instruction_in.pflush = 0;
  dat.pbits = {}; dat.pbits.popcode = 4;
  dat.pbits.phome_unid_uor_upbha_uor_umismatched_umecid = fetch_home;
  dat.pvalid = 1; eval();
  CHECK(uncached_chi_out.pdat.presponse.pready && !instruction_out.presponse.pvalid &&
        !memory_out.presponse.pvalid && !memory_out.pdrained);
  tick_model(); dat.pvalid = 0;

  until([&] { return req.pvalid; });
  CHECK(req.pbits.paddress == address && req.pbits.popcode == 4 && req.pbits.psize_uor_unum_ureq == 0);
  const auto data_home = req.pbits.ptgt_uid;
  uncached_chi_in.preq.pready = 1; tick_model(); uncached_chi_in.preq.pready = 0;
  dat.pbits.phome_unid_uor_upbha_uor_umismatched_umecid = data_home;
  dat.pbits.pdata.words[offset / 4] = 0xa5u << (8 * (offset % 4));
  dat.pvalid = 1; eval();
  CHECK(!uncached_chi_out.pdat.presponse.pready && !instruction_out.presponse.pvalid);
  complete(Word(0xa5) << (8 * offset)); // Checks tag and lane on every stalled cycle.
  dat.pvalid = 0; eval();
  CHECK(memory_out.pdrained && !memory_out.presponse.pvalid && !instruction_out.presponse.pvalid);
}

void fill() {
  until([] { return chi_out.prequests.pvalid; });
  CHECK(chi_out.prequests.pbits.paddress == base && chi_out.prequests.pbits.popcode == 7);
  const auto txn = chi_out.prequests.pbits.ptxn_uid;
  chi_in.prequests.pready = 1; tick_model(); chi_in.prequests.pready = 0;
  for (unsigned row : {3u, 1u, 0u, 2u}) {
    auto &dat = chi_in.presponse_udata;
    dat.pbits = {};
    dat.pbits.popcode = 4; dat.pbits.presp = 2; dat.pbits.pdata_uid = row;
    dat.pbits.ptxn_uid = txn; dat.pbits.ptgt_uid = 3; dat.pbits.psrc_uid = 1;
    dat.pbits.phome_unid_uor_upbha_uor_umismatched_umecid = 1;
    dat.pbits.pbyte_uenable = 0xffff;
    for (unsigned b = 0; b < 16; ++b)
      dat.pbits.pdata.words[b / 4] |= std::uint32_t(line[row * 16 + b]) << (8 * (b % 4));
    dat.pvalid = 1; until([] { return chi_out.presponse_udata.pready; });
    tick_model(); dat.pvalid = 0;
  }
}

void lookup(unsigned offset, bool store, Word value, unsigned mask) {
  auto &req = pipeline_lookup_in;
  req.pbits = {};
  req.pbits.paddress = base + offset;
  req.pbits.paccess = store ? 2 : 1; req.pbits.pwidth = size_code;
  req.pbits.punsigned = 1; req.pbits.pbyte_umask = mask; req.pbits.pdata = value;
  req.pvalid = 1; tick_model(); req.pvalid = 0;
  pipeline_in.prequest.pbits = req.pbits; pipeline_in.prequest.pvalid = 1;
  eval();
  CHECK(pipeline_out.presponse.pvalid);
  CHECK(pipeline_out.presponse.pbits.poutcome == (store ? 2 : 1));
  if (!store) CHECK(pipeline_out.presponse.pbits.pdata == value);
  tick_model(); pipeline_in.prequest.pvalid = 0;
  if (store) {
    pipeline_in.pcommit = 1; eval(); CHECK(pipeline_out.pcommit_uready);
    tick_model(); pipeline_in.pcommit = 0;
    until([] { return memory_out.pdrained; });
  }
}

int main() {
  return run_test([] {
    reset = 1; tick_model(); reset = 0;
    node_id = 3; chi_in.prequester_uresponses.pready = 1;
    flushed_fetch_before_device_load();
    for (unsigned b = 0; b < line.size(); ++b) line[b] = b + 1;
    // A committed byte store misses in the top native word of the first row.
    service(base + 15, 2, Word(0xa5) << (8 * (bytes - 1)), 1u << (bytes - 1), 0);
    fill(); complete(0); line[15] = 0xa5;
    for (unsigned offset = 0; offset < 16; offset += bytes) {
      lookup(offset, false, expected(offset), word_mask);
      // Speculative byte store in this native word, authorized one cycle later.
      lookup(offset, true, Word(0xaf) << 8, 2);
      line[offset + 1] = 0xaf;
    }
    for (unsigned offset = 0; offset < 32; offset += bytes) {
      // Same native-word contract as a committed load or a page-table read.
      service(base + offset, 1, 0, word_mask, size_code); complete(expected(offset));
      lookup(offset, false, expected(offset), word_mask);
    }
    // Distinct accepted owners remain paired while the first return is backpressured.
    service(base, 1, 0, word_mask, size_code);
    service(base + bytes, 1, 0, word_mask, size_code);
    complete(expected(0));
    complete(expected(bytes));
    // AMOSWAP.W keeps its byte address and unshifted operand even when the
    // addressed word is the last four bytes of an RV64 doubleword.
    const unsigned word_offset = 12 % bytes;
    const auto old = std::uint32_t(expected(12 - word_offset) >> (word_offset * 8));
    service(base + 12, 5, 0x28394a5b, 0xfu << word_offset, 2);
    complete(Word(std::int32_t(old)));
    for (unsigned b = 0; b < 4; ++b) line[12 + b] = 0x28394a5bu >> (8 * b);
    lookup(12 - word_offset, false, expected(12 - word_offset), word_mask);
  });
}
