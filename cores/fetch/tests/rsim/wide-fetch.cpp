// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <vector>

static constexpr uint64_t base = 0x1000;
static constexpr uint16_t cli6 = 0x4309, cli5 = 0x428d;
static constexpr uint32_t addi = 0x00100293;
static constexpr uint64_t mixed = uint64_t(cli5) << 48 | uint64_t(cli6) << 32 | addi;

static void offer(uint64_t pc, uint64_t word) {
  blocks_in = {};
  blocks_in.pvalid = 1;
  blocks_in.pbits.ppc = pc;
  blocks_in.pbits.pdata = word;
}

static void flush() {
  blocks_in = {};
  consume = 0;
  clear = 1;
  tick_model();
  clear = 0;
  eval();
  CHECK(head.pcount == 0);
}

static void stream(const std::vector<unsigned> &sizes, bool throttle, unsigned per_block = 0) {
  flush();
  std::vector<uint16_t> parcels;
  std::vector<uint32_t> encodings;
  std::vector<uint64_t> pcs;
  for (unsigned size : sizes) {
    pcs.push_back(base + 2 * parcels.size());
    const uint32_t raw = size == 4 ? addi : (encodings.size() % 3 == 1 ? cli6 : cli5);
    encodings.push_back(raw);
    parcels.push_back(uint16_t(raw));
    if (size == 4) parcels.push_back(raw >> 16);
  }
  CHECK(parcels.size() % 4 == 0);
  unsigned block = 0, retired = 0, appended = 0;
  for (unsigned cycle = 0; retired < sizes.size() && cycle < 200; ++cycle) {
    if (block * 4 < parcels.size()) {
      uint64_t data = 0;
      for (unsigned p = 0; p < 4; ++p) data |= uint64_t(parcels[block * 4 + p]) << (16 * p);
      offer(base + 8 * block, data);
    } else blocks_in = {};
    consume = 0;
    eval();
    CHECK(head.pcount == std::min<size_t>(appended - retired, valid.size()));
    for (unsigned lane = 0; lane < valid.size(); ++lane) {
      CHECK(valid[lane] == (lane < head.pcount));
      if (!valid[lane]) continue;
      const auto &token = head.pentries[lane];
      CHECK(token.ppc == pcs.at(retired + lane));
      CHECK(token.praw_uinstruction == encodings.at(retired + lane));
      const uint32_t canonical = encodings.at(retired + lane) == cli6 ? 0x00200313 :
                                 encodings.at(retired + lane) == cli5 ? 0x00300293 : addi;
      CHECK(token.pinstruction == canonical);
      CHECK(token.psequential_upc == token.ppc + sizes.at(retired + lane));
      CHECK(!token.pfault.pvalid && !token.pcompressed_uillegal);
    }
    consume = throttle ? std::min<unsigned>(head.pcount, cycle % 5) : head.pcount;
    eval();
    if (per_block && valid.size() >= per_block && block * 4 < parcels.size()) {
      CHECK(blocks_out.pready && assembly_count == per_block);
      if (cycle != 0) CHECK(consume == per_block);
    }
    block += blocks_in.pvalid && blocks_out.pready;
    retired += consume;
    appended += assembly_count;
    tick_model();
  }
  CHECK(retired == sizes.size() && appended == retired && block * 4 == parcels.size());
}

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    std::vector<unsigned> sizes;
    for (unsigned n = 0; n < 20; ++n) sizes.insert(sizes.end(), {4, 2, 2});
    stream(sizes, false, 3);
    stream(std::vector<unsigned>(60, 2), true); // Fourth parcel, partial issue, backpressure.
    stream(std::vector<unsigned>(40, 4), false, 2);
    sizes.clear();
    for (unsigned n = 0; n < 8; ++n) sizes.insert(sizes.end(), {4, 2, 4, 2, 4});
    stream(sizes, true); // A 32-bit instruction starts in the last parcel.

    // A discovered jump in the third slot must keep the accepted group and discard its suffix.
    flush();
    offer(base, uint64_t(0x0080006f) << 32 | uint32_t(cli6) << 16 | cli5);
    eval();
    CHECK(assembly_count == 3 && repairing && repair_target == base + 12);
    tick_model();
    offer(base + 8, mixed);
    eval();
    CHECK(!assembly_count && head.pcount == valid.size());
    consume = head.pcount;
    tick_model();
    blocks_in = {};
    consume = 0;
    eval();
    if (valid.size() == 2) CHECK(head.pcount == 1 && head.pentries[0].ppc == base + 4);

    // A cut in slot 1 must not let the complete third instruction enter the window.
    flush();
    offer(base, uint64_t(cli5) << 48 | uint64_t(0x0080006f) << 16 | cli6);
    eval();
    CHECK(assembly_count == 2 && repairing && repair_target == base + 10);
    tick_model();
    blocks_in = {};
    eval();
    CHECK(head.pcount == 2 && head.pentries[1].ppc == base + 2);

    // Two unpredicted calls would need two RAS pushes. Keep the second call for the next group.
    flush();
    offer(base, uint64_t(0x9182) << 48 | uint64_t(0x000180e7) << 16 | cli5);
    eval();
    CHECK(assembly_count == 2 && speculating && return_address == base + 6);
    tick_model();
    blocks_in = {};
    consume = 2;
    eval();
    CHECK(assembly_count == 1 && speculating && return_address == base + 8);
    tick_model();
    consume = 0;
    eval();
    CHECK(head.pcount == 1 && head.pentries[0].ppc == base + 6);

    // Wait for the high parcel at +8; a continuation fault belongs to the instruction at +6.
    flush();
    offer(base, uint64_t(uint16_t(addi)) << 48 | uint64_t(cli6) << 32 | addi);
    eval();
    CHECK(assembly_count == 2);
    tick_model();
    offer(base + 8, 0);
    blocks_in.pbits.pfault.pvalid = 1;
    blocks_in.pbits.pfault.pvalue = base + 8;
    consume = 2;
    eval();
    CHECK(assembly_count == 1);
    tick_model();
    consume = 0;
    blocks_in = {};
    eval();
    CHECK(head.pcount == 1 && head.pentries[0].ppc == base + 6);
    CHECK(head.pentries[0].pfault.pvalid && head.pentries[0].pfault.pvalue == base + 8);
  });
}
