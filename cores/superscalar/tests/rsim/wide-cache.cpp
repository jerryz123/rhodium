// Tests XLEN lanes at the superscalar boundary, independently of CHI refill order.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

using Word = decltype(memory_in.prequest.pbits.pdata);
constexpr unsigned bytes = sizeof(Word);
constexpr unsigned size_code = bytes == 4 ? 2 : 3;
constexpr unsigned word_mask = (1u << bytes) - 1;
constexpr unsigned base = 0x400;
std::array<unsigned char, 64> line{};

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

void service(unsigned offset, unsigned op, Word data, unsigned mask, unsigned width) {
  auto &req = memory_in.prequest;
  req.pbits = {};
  req.pbits.paddress = base + offset;
  req.pbits.paccess = op;
  req.pbits.pdata = data;
  req.pbits.pmask = mask;
  req.pbits.pwidth = width;
  req.pvalid = 1;
  until([] { return memory_out.prequest.pready; });
  CHECK(!memory_out.pfault.pvalid);
  tick_model();
  req.pvalid = 0;
}

void complete(Word value) {
  until([] { return memory_out.presponse.pvalid; });
  CHECK(memory_out.presponse.pbits == value);
  memory_in.presponse.pready = 1; tick_model(); memory_in.presponse.pready = 0;
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
    for (unsigned b = 0; b < line.size(); ++b) line[b] = b + 1;
    // A committed byte store misses in the top native word of the first row.
    service(15, 2, Word(0xa5) << (8 * (bytes - 1)), 1u << (bytes - 1), 0);
    fill(); complete(0); line[15] = 0xa5;
    for (unsigned offset = 0; offset < 16; offset += bytes) {
      lookup(offset, false, expected(offset), word_mask);
      // Speculative byte store in this native word, authorized one cycle later.
      lookup(offset, true, Word(0xaf) << 8, 2);
      line[offset + 1] = 0xaf;
    }
    for (unsigned offset = 0; offset < 32; offset += bytes) {
      // Same native-word contract as a committed load or a page-table read.
      service(offset, 1, 0, word_mask, size_code); complete(expected(offset));
      lookup(offset, false, expected(offset), word_mask);
    }
    // AMOSWAP.W keeps its byte address and unshifted operand even when the
    // addressed word is the last four bytes of an RV64 doubleword.
    const unsigned word_offset = 12 % bytes;
    const auto old = std::uint32_t(expected(12 - word_offset) >> (word_offset * 8));
    service(12, 5, 0x28394a5b, 0xfu << word_offset, 2);
    complete(Word(std::int32_t(old)));
    for (unsigned b = 0; b < 4; ++b) line[12 + b] = 0x28394a5bu >> (8 * b);
    lookup(12 - word_offset, false, expected(12 - word_offset), word_mask);
  });
}
