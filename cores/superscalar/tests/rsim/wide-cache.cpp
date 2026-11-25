// Tests XLEN lanes at the superscalar boundary, independently of CHI refill order.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <deque>

using Word = decltype(memory_in.prequest.pbits.pmemory.pdata);
constexpr unsigned bytes = sizeof(Word);
constexpr unsigned row_bytes = sizeof(row_memory_out.presponse.pbits);
static_assert(row_bytes == 2*bytes);
constexpr unsigned size_code = bytes == 4 ? 2 : 3;
constexpr unsigned word_mask = (1u << bytes) - 1;
constexpr unsigned base = 0x400;
std::array<unsigned char, 64> line{};
std::uint16_t next_context = 0x5300;
std::deque<std::uint16_t> contexts;

template <class F> void until(F accepted, unsigned issuing_line = __builtin_LINE()) {
  for (unsigned cycle = 0; cycle < 128; ++cycle) {
    eval();
    if (accepted()) return;
    tick_model();
  }
  throw std::runtime_error("cache transfer timed out waiting at line " + std::to_string(issuing_line));
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

void complete(Word value, unsigned issuing_line = __builtin_LINE()) {
  until([] { return memory_out.presponse.pvalid; }, issuing_line);
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

// Issue the same PBMT-NC word atomics as the RV5Stage committed-router fixture.
void check_nc_atomics(bool older_cache_work) {
  if (older_cache_work) {
    service(base, 1, 0, word_mask, size_code);
    until([] { return memory_out.presponse.pvalid; });
  }
  auto &req = memory_in.prequest;
  for (unsigned op : {3u, 4u, 5u}) { // LR.W, SC.W, AMOSWAP.W
    req.pbits = {};
    req.pbits.ppbmt = 1;
    req.pbits.pmemory.paddress = base;
    req.pbits.pmemory.paccess = op;
    req.pbits.pmemory.pwidth = 2;
    req.pbits.pmemory.pbyte_umask = 0xf;
    req.pvalid = 1;
    eval();
    CHECK(!memory_out.prequest.pready && memory_out.pfault.pvalid &&
          memory_out.pfault.pbits.pcause == (op == 3 ? 5 : 7) &&
          memory_out.pfault.pbits.pvalue == base);
    CHECK(memory_out.pdrained == !older_cache_work &&
          memory_out.presponse.pvalid == older_cache_work &&
          !chi_out.prequests.pvalid && !uncached_chi_out.preq.pvalid);
    tick_model();
    req.pvalid = 0;
    eval();
    CHECK(!memory_out.pfault.pvalid && !chi_out.prequests.pvalid &&
          !uncached_chi_out.preq.pvalid);
  }
  if (older_cache_work)
    complete(expected(0));
  CHECK(memory_out.pdrained && !memory_out.presponse.pvalid);
}

void check_misaligned_requests() {
  auto &req = memory_in.prequest;
  for (unsigned op : {1u, 2u}) {
    req.pbits = {};
    req.pbits.pmemory.paddress = base + 2;
    req.pbits.pmemory.paccess = op;
    req.pbits.pmemory.pwidth = 2;
    req.pbits.pmemory.pbyte_umask = (0xfu << 2) & word_mask;
    req.pvalid = 1;
    eval();
    CHECK(!memory_out.prequest.pready && memory_out.pfault.pvalid &&
          memory_out.pfault.pbits.pcause == (op == 1 ? 5 : 7) &&
          memory_out.pfault.pbits.pvalue == base + 2);
    tick_model();
    req.pvalid = 0;
    eval();
    CHECK(memory_out.pdrained && !memory_out.presponse.pvalid &&
          !chi_out.prequests.pvalid && !uncached_chi_out.preq.pvalid);
  }
}

void fill(unsigned address = base, unsigned opcode = 7) {
  until([] { return chi_out.prequests.pvalid; });
  CHECK(chi_out.prequests.pbits.paddress == address && chi_out.prequests.pbits.popcode == opcode);
  const auto txn = chi_out.prequests.pbits.ptxn_uid;
  chi_in.prequests.pready = 1; tick_model(); chi_in.prequests.pready = 0;
  for (unsigned row : {3u, 1u, 0u, 2u}) {
    auto &dat = chi_in.presponse_udata;
    dat.pbits = {};
    dat.pbits.popcode = 4; dat.pbits.presp = opcode == 7 ? 2 : 1; dat.pbits.pdata_uid = row;
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
    check_misaligned_requests();
    flushed_fetch_before_device_load();
    for (unsigned b = 0; b < line.size(); ++b) line[b] = b + 1;
    // A raw row miss refills through the ordinary CHI path. Hold the row
    // response under backpressure and verify it cannot escape as scalar data.
    row_memory_in.prequest.pvalid = 1;
    row_memory_in.prequest.pbits.paddress = base + 64 + 16;
    row_memory_in.prequest.pbits.pbyte_umask = (1u<<row_bytes)-1;
    until([] { return row_memory_out.prequest.pready; });
    tick_model(); row_memory_in.prequest.pvalid = 0;
    row_memory_in.prequest.pbits.paddress += 16; row_memory_in.prequest.pvalid = 1;
    until([] { return row_memory_out.prequest.pready; });
    CHECK(!row_memory_out.presponse.pvalid);
    tick_model(); row_memory_in.prequest.pvalid = 0;
    fill(base + 64, 2);
    until([] { return row_memory_out.presponse.pvalid; });
    for (unsigned wait = 0; wait < 3; ++wait) {
      CHECK(row_memory_out.presponse.pvalid && !memory_out.presponse.pvalid);
      for (unsigned b = 0; b < row_bytes; ++b)
        CHECK(reinterpret_cast<const unsigned char*>(&row_memory_out.presponse.pbits)[b] == line[16+b]);
      tick_model(); eval();
    }
    row_memory_in.presponse.pready = 1; tick_model(); row_memory_in.presponse.pready = 0;
    until([] { return row_memory_out.presponse.pvalid; });
    for (unsigned b=0;b<row_bytes;++b)
      CHECK(reinterpret_cast<const unsigned char*>(&row_memory_out.presponse.pbits)[b] == line[32+b]);
    row_memory_in.presponse.pready = 1; tick_model(); row_memory_in.presponse.pready = 0;
    // Issue a byte at the top lane of the vector row to a device. The RN-I
    // request is byte-sized; the return stays in that lane until accepted.
    const unsigned lane=row_bytes-1;
    constexpr std::uint64_t device_base=UINT64_C(0x100002000);
    const std::uint64_t device=device_base+lane;
    uncached_node_id=4;
    auto& req=row_memory_in.prequest;
    req.pbits={}; req.pbits.paddress=device_base; req.pbits.pbyte_umask=1u<<lane;
    req.pbits.pservice=2; req.pvalid=1;
    until([]{return row_memory_out.prequest.pready;}); tick_model(); req.pvalid=0;
    until([]{return uncached_chi_out.preq.pvalid;});
    auto flit=uncached_chi_out.preq.pbits;
    CHECK(flit.paddress==device && flit.psize_uor_unum_ureq==0 && flit.popcode==4 && !chi_out.prequests.pvalid);
    uncached_chi_in.preq.pready=1; tick_model(); uncached_chi_in.preq.pready=0;
    auto& reply=uncached_chi_in.pdat.presponse;
    reply.pvalid=1; reply.pbits={}; reply.pbits.popcode=4; reply.pbits.ptxn_uid=flit.ptxn_uid;
    reply.pbits.ptgt_uid=4; reply.pbits.psrc_uid=flit.ptgt_uid;
    reply.pbits.phome_unid_uor_upbha_uor_umismatched_umecid=flit.ptgt_uid;
    reinterpret_cast<unsigned char*>(&reply.pbits.pdata)[lane]=0xa7;
    for(unsigned i=0;i<3;++i){eval(); CHECK(row_memory_out.presponse.pvalid && !memory_out.presponse.pvalid);
      CHECK(reinterpret_cast<const unsigned char*>(&row_memory_out.presponse.pbits)[lane]==0xa7); tick_model();}
    row_memory_in.presponse.pready=1; tick_model(); row_memory_in.presponse.pready=reply.pvalid=0;
    req.pbits.pstore=req.pvalid=1;
    reinterpret_cast<unsigned char*>(&req.pbits.pdata)[lane]=0xbb;
    until([]{return row_memory_out.prequest.pready;}); tick_model(); req.pvalid=0;
    until([]{return uncached_chi_out.preq.pvalid;}); flit=uncached_chi_out.preq.pbits;
    CHECK(flit.paddress==device && flit.psize_uor_unum_ureq==0 && flit.popcode==28);
    uncached_chi_in.preq.pready=1; tick_model(); uncached_chi_in.preq.pready=0;
    auto& rsp=uncached_chi_in.prsp.presponse;
    rsp.pvalid=1; rsp.pbits={}; rsp.pbits.popcode=6; rsp.pbits.ptxn_uid=flit.ptxn_uid;
    rsp.pbits.ptgt_uid=4; rsp.pbits.psrc_uid=flit.ptgt_uid; rsp.pbits.pdbid_uor_ugroup_uid=11;
    until([]{return uncached_chi_out.prsp.presponse.pready;}); tick_model(); rsp.pvalid=0;
    until([]{return uncached_chi_out.pdat.prequest.pvalid;});
    auto write=uncached_chi_out.pdat.prequest.pbits;
    CHECK(write.pbyte_uenable==(1u<<lane) && reinterpret_cast<const unsigned char*>(&write.pdata)[lane]==0xbb);
    uncached_chi_in.pdat.prequest.pready=1; tick_model(); uncached_chi_in.pdat.prequest.pready=0;
    rsp.pbits.popcode=4; rsp.pvalid=1; eval();
    CHECK(row_memory_out.presponse.pvalid && !memory_out.presponse.pvalid && !memory_out.pdrained);
    row_memory_in.presponse.pready=1; tick_model(); row_memory_in.presponse.pready=rsp.pvalid=0;
    req.pbits={};
    // A committed byte store updates the top native word of the first row.
    service(base + 15, 2, Word(0xa5) << (8 * (bytes - 1)), 1u << (bytes - 1), 0);
    fill(); complete(0); line[15] = 0xa5;
    until([] { return memory_out.pdrained; });
    check_nc_atomics(false);
    check_nc_atomics(true);
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
    // First store acquires a cold line. The second hits the unique copy.
    // Read every native slice back through the scalar service, as well as
    // adjacent unchanged rows; neither acknowledgement carries scalar data.
    for (unsigned r : {1u, 2u}) {
      auto& req = row_memory_in.prequest;
      req.pvalid = req.pbits.pstore = 1;
      req.pbits.paddress = base + 128 + r * row_bytes;
      const unsigned enabled = r == 1 ? 0x5ac3 : 0x9c35;
      req.pbits.pbyte_umask = enabled;
      for (unsigned b=0;b<row_bytes;++b) reinterpret_cast<unsigned char*>(&req.pbits.pdata)[b]=17*(4*r+b/4);
      until([] { return row_memory_out.prequest.pready; });
      tick_model(); req.pvalid = 0;
      CHECK(!row_memory_out.presponse.pvalid);
    }
    fill(base + 128, 7);
    for (unsigned r : {1u, 2u}) {
      const unsigned enabled = r == 1 ? 0x5ac3 : 0x9c35;
      until([] { return row_memory_out.presponse.pvalid; });
      for (unsigned i = 0; i < 3; ++i) {
        CHECK(row_memory_out.presponse.pvalid && !memory_out.presponse.pvalid);
        tick_model(); eval();
      }
      row_memory_in.presponse.pready = 1; tick_model(); row_memory_in.presponse.pready = 0;
      for (unsigned b = 0; b < row_bytes; ++b) if (enabled & (1u << b)) line[row_bytes*r+b] = 17*(4*r+b/4);
    }
    for (unsigned offset = 0; offset < 64; offset += bytes) {
      service(base + 128 + offset, 1, 0, word_mask, size_code); complete(expected(offset));
    }
  });
}
