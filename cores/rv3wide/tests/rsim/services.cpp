// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <map>
#include <deque>

using Request = std::remove_cvref_t<decltype(pipeline_out.prequest.pbits)>;
Request lookup{}, commit_owner{};
bool lookup_valid = false, slow = false, accepting = true, proof = false;
std::map<std::uint64_t, std::uint64_t> words, values;
std::map<std::uint64_t, unsigned> lanes, training;
std::array<std::uint64_t, 32> gprs{};
std::deque<std::pair<std::uint64_t, unsigned>> responses;
unsigned cycle = 0, stores = 0, requests = 0, triples = 0;
std::uint64_t pc = 0x8000, fault_pc = 0, redirect_pc = 0, redirect_target = 0;
unsigned fault_disposition = 1;
auto &resolution(unsigned lane) {
  if (lane == 0) return resolution_0_in;
  if (lane == 1) return resolution_1_in;
  return resolution_2_in;
}
const auto &memory_stage(unsigned lane) {
  if (lane == 0) return memory_stage_0_out;
  if (lane == 1) return memory_stage_1_out;
  return memory_stage_2_out;
}
const auto &retired(unsigned lane) {
  if (lane == 0) return retired_0_out;
  if (lane == 1) return retired_1_out;
  return retired_2_out;
}
void store(const Request &r) {
  auto &word = words[r.paddress & ~UINT64_C(7)];
  for (unsigned byte = 0; byte < 8; ++byte)
    if (r.pmask & (1u << byte)) {
      const auto mask = UINT64_C(255) << (8 * byte);
      word = (word & ~mask) | (r.pdata & mask);
    }
  ++stores;
}
void step() {
  pipeline_in.presponse.pvalid = lookup_valid;
  pipeline_in.presponse.pbits.poutcome = slow ? 0 : lookup.paccess == 1 ? 1 : 2;
  pipeline_in.presponse.pbits.pdata = words[lookup.paddress & ~UINT64_C(7)];
  pipeline_in.pcommit_uready = accepting;
  memory_in.prequest.pready = accepting;
  memory_in.pdrained = responses.empty();
  memory_in.presponse.pvalid = !responses.empty() && responses.front().second <= cycle;
  memory_in.presponse.pbits = responses.empty() ? 0 : responses.front().first;
  vector_precheck_in.prequest.pready = !proof;
  vector_precheck_in.presponse.pvalid = proof;
  vector_precheck_in.presponse.pbits = 1;
  for (unsigned lane = 0; lane < 3; ++lane) resolution(lane) = {};
  eval();
  for (unsigned lane = 0; lane < 3; ++lane)
    if (fault_pc && memory_stage(lane).pvalid && memory_stage(lane).pbits.ppc == fault_pc) {
      resolution(lane).pvalid = 1;
      resolution(lane).pbits.pdisposition = fault_disposition;
      resolution(lane).pbits.pcause = 13;
      resolution(lane).pbits.pvalue = 0x1234;
    }
  eval();
  if (!reset) {
    triples += issued == 3;
    unsigned count = 0;
    for (unsigned lane = 0; lane < 3; ++lane) if (retired(lane).pvalid) {
      const auto &r = retired(lane).pbits;
      CHECK(!lanes.contains(r.pfetched.ppc));
      lanes[r.pfetched.ppc] = lane;
      values[r.pfetched.ppc] = r.pdata;
      if (r.pwrite && !r.pdeferred) gprs[r.prd] = r.pdata;
      ++count;
    }
    CHECK(count == retired_count);
    if (completed_out.pvalid && completed_out.pbits.pwrite)
      gprs[completed_out.pbits.prd] = completed_out.pbits.pdata;
    if (redirect_out.pvalid) {
      redirect_pc = redirect_out.pbits.ppc;
      redirect_target = redirect_out.pbits.ptarget;
    }
    if (branch_update_out.pvalid) ++training[branch_update_out.pbits.ppc];
    if (pipeline_out.pcommit && accepting) store(commit_owner);
    if (memory_in.presponse.pvalid && memory_out.presponse.pready) responses.pop_front();
    if (memory_out.prequest.pvalid && accepting) {
      ++requests;
      const auto &r = memory_out.prequest.pbits;
      if (r.paccess == 2) store(r);
      responses.push_back({words[r.paddress & ~UINT64_C(7)], cycle + 8});
    }
  }
  const bool next_proof = (proof && !vector_precheck_out.presponse.pready) ||
      (vector_precheck_out.prequest.pvalid && vector_precheck_in.prequest.pready);
  commit_owner = lookup;
  lookup = pipeline_out.prequest.pbits;
  lookup_valid = pipeline_out.prequest.pvalid;
  tick_model();
  proof = next_proof;
  ++cycle;
}
void drain(unsigned n = 80) { for (unsigned i = 0; i < n; ++i) step(); }
std::uint64_t send(std::initializer_list<std::uint32_t> instructions) {
  const auto base = pc;
  instructions_in = {};
  instructions_in.pvalid = 1;
  instructions_in.pbits.pcount = instructions.size();
  unsigned index = 0;
  for (auto instruction : instructions) {
    auto &entry = instructions_in.pbits.pentries[index++];
    entry.ppc = pc;
    entry.pinstruction = entry.praw_uinstruction = instruction;
    entry.psequential_upc = pc + 4;
    pc += 4;
  }
  for (unsigned wait = 0;; ++wait) {
    CHECK(wait < 200);
    eval();
    const bool accepted = instructions_out.pready;
    step();
    if (accepted) break;
  }
  instructions_in.pvalid = 0;
  return base;
}
std::uint64_t third(std::uint32_t instruction) { return send({0x13, 0x13, instruction}) + 8; }
std::uint32_t addi(unsigned rd, unsigned rs, int imm) {
  return (std::uint32_t(imm & 4095) << 20) | rs << 15 | rd << 7 | 0x13;
}
std::uint32_t fp(unsigned fn, unsigned rd, unsigned rs1, unsigned rs2) {
  return fn << 25 | rs2 << 20 | rs1 << 15 | rd << 7 | 0x53;
}
std::uint32_t vi(unsigned fn, unsigned vs2, unsigned vs1, unsigned kind, unsigned vd) {
  return fn << 26 | 1u << 25 | vs2 << 20 | vs1 << 15 | kind << 12 | vd << 7 | 0x57;
}
int main() {
  return run_test([] {
    reset = 1; step(); step(); reset = 0;
    send({0x60b7}); drain(); // x1=0x6000
    send({addi(1, 1, 0x600)}); drain();
    send({0x30009073}); drain(); // FS/VS enabled
    send({addi(1, 0, 0x100), addi(2, 0, 42)}); drain();
    const auto st = third(0x0020b023); drain(); // sd x2,0(x1)
    CHECK(lanes.at(st) == 2 && words[0x100] == 42);
    const auto ld = third(0x0000b183); drain();
    CHECK(lanes.at(ld) == 2 && gprs[3] == 42);
    // CoreMark's li / bge / load group needs the alternate rs1 donor.
    send({addi(15, 0, 0x100), addi(21, 0, 42)}); drain();
    const auto borrowed = send({addi(18, 0, 0), (21u << 20) | (18u << 15) | 0x5063, (15u << 15) | (15u << 7) | 0x3003});
    drain();
    CHECK(lanes.at(borrowed) == 0 && lanes.at(borrowed + 4) == 1 && lanes.at(borrowed + 8) == 2);
    CHECK(gprs[15] == 42);
    slow = true;
    const auto miss = third(0x0000b203); drain();
    CHECK(lanes.at(miss) == 2 && gprs[4] == 42 && requests == 1);
    accepting = false;
    const auto retry = third(0x0000b283); drain();
    CHECK(!lanes.contains(retry) && redirect_pc == retry);
    accepting = true;
    pc = retry; send({0x0000b283}); drain(); CHECK(gprs[5] == 42);
    slow = false;
    // An older second-lane fault/replay suppresses third-lane stores.
    for (auto disposition : {1u, 2u}) {
      const auto before = stores;
      fault_pc = pc + 4; fault_disposition = disposition;
      const auto base = send({addi(6, 0, 9), addi(0, 0, 0), 0x0020b023}); drain();
      CHECK(lanes.contains(base) && !lanes.contains(base + 4) && !lanes.contains(base + 8));
      CHECK(stores == before && redirect_pc == base + 4);
      fault_pc = 0;
    }
    // Lane-three correction retires its older prefix and trains its branch.
    const auto branch = third(0x00000863); drain(); // beq x0,x0,+16
    CHECK(lanes.at(branch) == 2 && redirect_pc == branch && redirect_target == branch + 16);
    CHECK(training.at(branch) == 1);
    const auto jump = third(0x010000ef); drain(); // jal x1,+16
    CHECK(lanes.at(jump) == 2 && gprs[1] == jump + 4 && redirect_target == jump + 16);
    const auto indirect = third(0x000082e7); drain(); // jalr x5,0(x1)
    CHECK(lanes.at(indirect) == 2 && gprs[5] == indirect + 4 && redirect_target == jump + 4);
    send({addi(1, 0, 0x100)}); drain();
    const auto old_stores = stores;
    const auto killed_store = send({0x13, 0x00000863, 0x0020b023}); drain();
    CHECK(lanes.contains(killed_store + 4) && !lanes.contains(killed_store + 8));
    CHECK(stores == old_stores);
    const auto batch = send({0x00001463, 0x00001463, 0x00001463}); drain();
    CHECK(lanes.at(batch + 8) == 2);
    for (unsigned i = 0; i < 3; ++i) CHECK(training.at(batch + 4*i) == 1);
    // FP fixed, variable, direct-GPR and deferred-GPR returns retain lane-three ownership.
    const auto cvt = third(fp(0x69, 1, 2, 0)); drain(); CHECK(lanes.at(cvt) == 2);
    const auto sum = third(fp(1, 2, 1, 1)); drain(); CHECK(lanes.at(sum) == 2);
    const auto toint = third(fp(0x61, 7, 2, 0)); drain(); CHECK(lanes.at(toint) == 2 && gprs[7] == 84);
    const auto div = third(fp(13, 3, 2, 1)); drain(); CHECK(lanes.at(div) == 2);
    third(fp(0x61, 8, 3, 0)); drain(); CHECK(gprs[8] == 2);
    words[0x100] = UINT64_C(0x4008000000000000);
    const auto fld = third(0x0000b207); drain(); CHECK(lanes.at(fld) == 2);
    const auto direct = third(fp(0x71, 16, 4, 0)); drain();
    CHECK(lanes.at(direct) == 2 && gprs[16] == words[0x100]);
    const auto fsd = third(0x0040b427); drain();
    CHECK(lanes.at(fsd) == 2 && words[0x108] == words[0x100]);
    const auto mul = third(0x022105b3); drain(); CHECK(lanes.at(mul) == 2 && gprs[11] == 1764);
    const auto idiv = third(0x0225c633); drain(); CHECK(lanes.at(idiv) == 2 && gprs[12] == 42);
    // A launched FP operation cannot update its FPR after an older peer faults.
    fault_pc = pc + 4; fault_disposition = 1;
    const auto killed = third(fp(1, 3, 1, 1)); drain();
    CHECK(!lanes.contains(killed)); fault_pc = 0;
    third(fp(0x61, 8, 3, 0)); drain(); CHECK(gprs[8] == 2);
    // Third-lane vector configuration and arithmetic use the same sequencer.
    const auto config = third(0xc0007057 | 4u << 15 | 6u << 7); drain();
    CHECK(lanes.at(config) == 2 && gprs[6] == 4);
    const auto move = third(vi(23, 0, 11, 3, 1)); drain(); CHECK(lanes.at(move) == 2);
    const auto read = third(vi(16, 1, 0, 2, 9)); drain(); CHECK(lanes.at(read) == 2 && gprs[9] == 11);
    // Retained vector memory may normalize retirement, but must preserve both older scalars.
    words[0x100] = 0x04030201;
    const auto load = third(0x02008087); drain(160); // vle8.v v1,(x1)
    CHECK(lanes.contains(load) && lanes.contains(load - 4) && lanes.contains(load - 8));
    third(vi(16, 1, 0, 2, 10)); drain(); CHECK(gprs[10] == 1);
    const auto vs = third(0x020080a7); drain(160); // vse8.v v1,(x1)
    CHECK(lanes.contains(vs) && (words[0x100] & 0xffffffff) == 0x04030201);
    // An uncertified vector load in lane two retains only itself, not lane one's retirement.
    const auto retained = send({addi(13, 0, 73), 0x02008107, addi(14, 0, 91)});
    drain(160);
    CHECK(lanes.contains(retained) && lanes.contains(retained + 4));
    CHECK(!lanes.contains(retained + 8) && redirect_target == retained + 8);
    pc = retained + 8; send({addi(14, 0, 91)}); drain(); CHECK(gprs[14] == 91);
    // A middle-lane vset forwards its new geometry to the third-lane descriptor.
    const auto configured = send({0x13, 0xc0007057 | 2u << 15, vi(23, 0, 7, 3, 2)});
    drain(); CHECK(lanes.at(configured + 8) == 2);
    third(vi(16, 2, 0, 2, 15)); drain(); CHECK(gprs[15] == 7);
    CHECK(triples >= 15);
  });
}
