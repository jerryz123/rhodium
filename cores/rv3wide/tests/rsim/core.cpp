// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <deque>

struct Expected { std::uint64_t pc; unsigned rd; std::uint64_t value; };
std::deque<Expected> expected;
std::array<std::uint64_t, 32> registers{};
unsigned triples = 0, issues3 = 0, redirects = 0;
bool inject = false;
std::uint64_t inject_pc = 0;
unsigned inject_disposition = 2;
std::uint64_t last_redirect = 0;
auto &resolution(unsigned lane) {
  if (lane == 0) return resolution_0_in;
  if (lane == 1) return resolution_1_in;
  return resolution_2_in;
}
const auto &mem(unsigned lane) {
  if (lane == 0) return memory_stage_0_out;
  if (lane == 1) return memory_stage_1_out;
  return memory_stage_2_out;
}
const auto &retired(unsigned lane) {
  if (lane == 0) return retired_0_out;
  if (lane == 1) return retired_1_out;
  return retired_2_out;
}
std::uint32_t addi(unsigned rd, unsigned rs, int value) {
  return (std::uint32_t(value & 4095) << 20) | (rs << 15) | (rd << 7) | 0x13;
}
std::uint32_t add(unsigned rd, unsigned a, unsigned b) {
  return (b << 20) | (a << 15) | (rd << 7) | 0x33;
}
void step() {
  for (unsigned lane = 0; lane < 3; ++lane) resolution(lane) = {};
  eval();
  for (unsigned lane = 0; lane < 3; ++lane) {
    if (inject && mem(lane).pvalid && mem(lane).pbits.ppc == inject_pc) {
      resolution(lane).pvalid = 1;
      resolution(lane).pbits.pdisposition = inject_disposition;
      resolution(lane).pbits.pcause = 13;
      resolution(lane).pbits.pvalue = 0xdead000;
    }
  }
  eval();
  if (!reset) {
    CHECK(!memory_out.prequest.pvalid);
    CHECK(!pipeline_out.pcommit);
    CHECK(!completed_out.pvalid);
    unsigned count = 0;
    for (unsigned lane = 0; lane < 3; ++lane) {
      if (!retired(lane).pvalid) continue;
      CHECK(!expected.empty());
      const auto want = expected.front(); expected.pop_front();
      const auto &got = retired(lane).pbits;
      CHECK(got.pfetched.ppc == want.pc);
      CHECK(!got.pdeferred);
      CHECK(got.prd == want.rd);
      CHECK(bool(got.pwrite) == bool(want.rd));
      if (want.rd) CHECK(got.pdata == want.value);
      ++count;
    }
    CHECK(retired_count == count);
    triples += count == 3;
    issues3 += issued == 3;
    if (redirect_out.pvalid) {
      CHECK(inject);
      CHECK(redirect_out.pbits.ppc == inject_pc);
      CHECK(redirect_out.pbits.presolution.pdisposition == inject_disposition);
      last_redirect = redirect_out.pbits.ppc;
      ++redirects;
    }
  }
  tick_model();
}
void expect(std::uint64_t pc, std::uint32_t insn) {
  const unsigned rd = (insn >> 7) & 31, a = (insn >> 15) & 31, b = (insn >> 20) & 31;
  const auto value = (insn & 127) == 0x13
      ? registers[a] + std::int64_t(std::int32_t(insn) >> 20)
      : registers[a] + registers[b];
  expected.push_back({pc, rd, value});
  if (rd) registers[rd] = value;
}
void send(std::uint64_t pc, std::initializer_list<std::uint32_t> insns,
          unsigned successful = 3, unsigned bytes = 4) {
  instructions_in = {};
  instructions_in.pvalid = 1;
  instructions_in.pbits.pcount = insns.size();
  unsigned index = 0;
  for (auto insn : insns) {
    auto &entry = instructions_in.pbits.pentries[index];
    entry.ppc = pc + bytes * index;
    entry.pinstruction = insn;
    entry.praw_uinstruction = insn;
    entry.psequential_upc = entry.ppc + bytes;
    if (index < successful) expect(entry.ppc, insn);
    ++index;
  }
  for (unsigned wait = 0;; ++wait) {
    CHECK(wait < 200);
    eval();
    const bool accepted = instructions_out.pready;
    step();
    if (accepted) break;
  }
  instructions_in.pvalid = 0;
}
void drain() {
  for (unsigned wait = 0; !expected.empty(); ++wait) { CHECK(wait < 300); step(); }
  for (unsigned i = 0; i < 8; ++i) step();
}
void reset_core() {
  reset = 1;
  inject = false;
  instructions_in = {};
  memory_in = {};
  memory_in.pdrained = 1;
  pipeline_in = {};
  split_in = {};
  vector_precheck_in = {};
  vector_fast_in = {};
  interrupts = {};
  expected.clear(); registers.fill(0);
  step(); step(); reset = 0;
  for (unsigned rd = 1; rd < 32; ++rd) send(0x10000 + 4 * rd, {addi(rd, 0, 0)});
  drain();
}
int main() {
  return run_test([] {
    reset_core();
    triples = issues3 = 0;
    // Standalone backend groups sustain three expanded instructions per cycle.
    // The integrated fetch fixture separately checks the byte-oriented frontend.
    for (unsigned n = 0; n < 32; ++n)
      send(0x1000 + 12*n, {addi(1, 0, n), addi(2, 0, -int(n)), addi(1, 0, n+1)});
    drain(); CHECK(triples >= 28); CHECK(issues3 >= 28);
    send(0x2000, {addi(3, 1, 9), addi(4, 2, 8)}); drain();
    // Borrow port one instead, then consume both values after RF writeback.
    send(0x2040, {addi(1, 0, 21), addi(2, 0, 22), addi(2, 0, 23)}); drain();
    send(0x2080, {add(3, 1, 2)}); drain();
    // Both read paths are borrowed; one older no-write leaves a physical write port.
    send(0x20c0, {addi(0, 0, 0), addi(5, 0, 9), add(6, 1, 2)}); drain();
    send(0x2100, {add(7, 5, 6)}); drain();
    // All three alias: each retires its own value, only the youngest updates storage.
    send(0x2140, {addi(8, 0, 11), addi(8, 0, 12), addi(8, 0, 13)}); drain();
    send(0x2180, {addi(9, 8, 1)}); drain();
    // Three distinct writers must split; the third remains in the issue window.
    const auto before = issues3;
    send(0x2200, {addi(10, 0, 1), addi(11, 0, 2), addi(12, 0, 3)}); drain();
    CHECK(issues3 == before);
    // A dependent third cannot bypass either older instruction in its group.
    send(0x2240, {addi(10, 0, 5), addi(11, 0, 6), add(10, 10, 11)}); drain();
    send(0x2280, {addi(12, 10, 0)}); drain();
    // Move forwarding preserves per-instruction values, RF writes, and three-wide issue.
    send(0x2300, {addi(10, 0, 37), addi(11, 0, 91)}); drain();
    for (auto move : {addi(1, 10, 0), add(1, 0, 10), add(1, 10, 0)}) {
      auto prior = issues3;
      send(0x2340, {move, addi(0, 0, 0), add(2, 1, 1)}); drain();
      CHECK(issues3 == prior + 1);
      prior = issues3;
      send(0x2380, {addi(0, 0, 0), move, add(2, 1, 1)}); drain();
      CHECK(issues3 == prior + 1);
      send(0x23c0, {add(3, 1, 2)}); drain();
    }
    auto prior_moves = issues3;
    send(0x2400, {addi(1, 10, 0), add(2, 0, 1), addi(1, 2, 5)}); drain();
    CHECK(issues3 == prior_moves + 1);
    send(0x2440, {add(3, 1, 2)}); drain();
    prior_moves = issues3;
    send(0x2480, {addi(1, 10, 0), addi(10, 11, 1), addi(1, 1, 2)}); drain();
    CHECK(issues3 == prior_moves + 1);
    send(0x24c0, {add(3, 1, 10)}); drain();
    send(0x2500, {addi(1, 10, 0), add(1, 10, 11), addi(0, 1, 1)}); drain();
    send(0x2540, {addi(3, 1, 0)}); drain();
    // Shortened successful prefixes must restore the issue-assigned older write.
    for (unsigned failed = 0; failed < 3; ++failed) {
      for (unsigned disposition : {1u, 2u}) {
        reset_core();
        send(0x2f00, {addi(10, 0, 41), addi(11, 0, 42)}); drain();
        inject = true; inject_pc = 0x3000 + 4*failed; inject_disposition = disposition;
        const auto prior = redirects;
        const auto prior_issue = issues3;
        send(0x3000, {addi(1, 10, 0), addi(2, 11, 0), addi(1, 1, 2)}, failed);
        drain(); CHECK(redirects == prior + 1); CHECK(last_redirect == inject_pc);
        CHECK(issues3 == prior_issue + 1);
        inject = false;
        send(0x4000, {addi(3, 1, 0), addi(4, 2, 0)}); drain();
        if (disposition == 2) {
          // Refetch only the rejected suffix; older retirement must not repeat.
          if (failed == 0) send(0x3000, {addi(1, 10, 0), addi(2, 11, 0), addi(1, 1, 2)});
          if (failed == 1) send(0x3004, {addi(2, 11, 0), addi(1, 1, 2)});
          if (failed == 2) send(0x3008, {addi(1, 1, 2)});
          drain();
          send(0x4040, {addi(3, 1, 0), addi(4, 2, 0)}); drain();
        }
      }
    }
    // Region-prefix reconstruction covers a third slot on the next 4 KiB page.
    send(0x4ff8, {addi(1, 0, 1), addi(2, 0, 2), addi(1, 0, 3)}); drain();
  });
}
