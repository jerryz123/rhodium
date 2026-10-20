// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <array>
#include <deque>
#include <map>
#include <set>

using Request = std::remove_cvref_t<decltype(pipeline_out.prequest.pbits)>;
constexpr unsigned bytes = sizeof(memory_in.presponse.pbits);
Request lookup{}, commit_owner{};
bool lookup_live = false, proof_pending = false, slow = false, safe = true;
unsigned cycle = 0, stores = 0, proof_count = 0, deferred_count = 0;
std::uint64_t next_pc = 0x8000, replay_pc = 0, fault_at = 0;
std::array<std::uint64_t, 32> gprs{};
std::map<std::uint64_t, std::uint64_t> words;
std::set<std::uint64_t> retired_pcs;
struct Response { std::uint64_t data; unsigned due; };
std::deque<Response> responses;
unsigned fault_count = 0;
std::uint64_t last_cause = 0, last_tval = 0;

std::uint32_t vi(unsigned funct6, unsigned vs2, unsigned vs1,
                 unsigned funct3, unsigned vd) {
  return funct6 << 26 | 1 << 25 | vs2 << 20 | vs1 << 15 | funct3 << 12 |
         vd << 7 | 0x57;
}
std::uint32_t addi(unsigned rd, unsigned rs, unsigned value) {
  return (value & 4095) << 20 | rs << 15 | rd << 7 | 0x13;
}
void write(const Request &request) {
  auto &word = words[request.paddress & ~(std::uint64_t(bytes) - 1)];
  for (unsigned byte = 0; byte < bytes; ++byte)
    if (request.pmask >> byte & 1) {
      const auto mask = UINT64_C(255) << (8 * byte);
      word = (word & ~mask) | (request.pdata & mask);
    }
  ++stores;
}
void settle() {
  pipeline_in.presponse.pvalid = lookup_live;
  pipeline_in.presponse.pbits = {};
  pipeline_in.presponse.pbits.poutcome = slow ? 0 : lookup.paccess == 2 ? 2 : 1;
  pipeline_in.presponse.pbits.pdata = words[lookup.paddress & ~(std::uint64_t(bytes) - 1)];
  pipeline_in.pcommit_uready = 1;
  memory_in.pdrained = responses.empty();
  memory_in.pordered_ubusy = 0;
  memory_in.preservation_uvalid = 0;
  memory_in.presponse.pvalid = !responses.empty() && responses.front().due <= cycle;
  memory_in.presponse.pbits = responses.empty() ? 0 : responses.front().data;
  memory_in.prequest.pready = responses.size() < 8;
  memory_in.pfault = {};
  vector_precheck_in.prequest.pready = !proof_pending;
  vector_precheck_in.presponse.pvalid = proof_pending;
  vector_precheck_in.presponse.pbits = safe;
  split_in.prequest.pready = 1;
  split_in.presponse.pvalid = 0;
  eval();
  if (fault_at && memory_out.prequest.pvalid &&
      memory_out.prequest.pbits.paddress == fault_at) {
    memory_in.prequest.pready = 0;
    memory_in.pfault.pvalid = 1;
    memory_in.pfault.pbits.pdisposition = 1;
    memory_in.pfault.pbits.pcause = memory_out.prequest.pbits.paccess == 2 ? 15 : 13;
    memory_in.pfault.pbits.pvalue = fault_at;
    eval();
  }
}
void step() {
  settle();
  if (!reset) {
    for (const auto *port : {&retired_0_out, &retired_1_out}) {
      if (!port->pvalid) continue;
      const auto &result = port->pbits;
      CHECK(retired_pcs.insert(result.pfetched.ppc).second);
      if (result.pwrite && !result.pdeferred) gprs[result.prd] = result.pdata;
    }
    if (completed_out.pvalid && completed_out.pbits.pwrite) {
      ++deferred_count;
      gprs[completed_out.pbits.prd] = completed_out.pbits.pdata;
    }
    if (redirect_out.pvalid) {
      if (redirect_out.pbits.presolution.pdisposition == 2)
        replay_pc = redirect_out.pbits.ptarget;
      if (redirect_out.pbits.presolution.pdisposition == 1) {
        ++fault_count;
        last_cause = redirect_out.pbits.presolution.pcause;
        last_tval = redirect_out.pbits.presolution.pvalue;
      }
    }
    if (pipeline_out.pcommit) write(commit_owner);
    if (memory_in.presponse.pvalid && memory_out.presponse.pready)
      responses.pop_front();
    if (memory_out.prequest.pvalid && memory_in.prequest.pready) {
      const auto request = memory_out.prequest.pbits;
      if (request.paccess == 2) write(request);
      responses.push_back({words[request.paddress & ~(std::uint64_t(bytes) - 1)], cycle + 12});
    }
    CHECK(!split_out.prequest.pvalid);
  }
  const bool proof_accept = vector_precheck_out.prequest.pvalid && vector_precheck_in.prequest.pready;
  const bool proof_return = vector_precheck_in.presponse.pvalid && vector_precheck_out.presponse.pready;
  const auto next_lookup = pipeline_out.prequest.pbits;
  const auto next_live = pipeline_out.prequest.pvalid;
  tick_model();
  commit_owner = lookup;
  lookup = next_lookup;
  lookup_live = next_live;
  if (proof_return) proof_pending = false;
  if (proof_accept) { proof_pending = true; ++proof_count; }
  ++cycle;
  CHECK(cycle < 30000);
  settle();
}
void run(unsigned cycles = 60) { while (cycles--) step(); }
void send(std::uint32_t instruction, bool fault = false) {
  const auto pc = next_pc;
  next_pc += 4;
  const auto faults_before = fault_count;
  bool offered = true;
  for (unsigned wait = 0; ; ++wait) {
    CHECK(wait < 3000);
    if (replay_pc) {
      CHECK(replay_pc == pc);
      replay_pc = 0;
      offered = true;
    }
    instructions_in = {};
    instructions_in.pvalid = offered;
    instructions_in.pbits.pcount = 1;
    auto &entry = instructions_in.pbits.pentries[0];
    entry.ppc = pc;
    entry.pinstruction = entry.praw_uinstruction = instruction;
    entry.psequential_upc = pc + 4;
    settle();
    const bool accepted = offered && instructions_out.pready;
    step();
    if (accepted) offered = false;
    if (retired_pcs.contains(pc) || fault_count != faults_before) break;
  }
  instructions_in.pvalid = 0;
  CHECK(fault == (fault_count != faults_before));
  settle();
}
void setvl(unsigned sew, unsigned vl = 16) {
  send(0xc0007057 | (vl << 15) | (sew << 23) | (5 << 7));
  CHECK(gprs[5] == vl);
}
void scalar_burst() {
  constexpr unsigned packets = 128;
  const auto pc = next_pc;
  next_pc += 8 * packets;
  unsigned sent = 0;
  for (unsigned wait = 0; !retired_pcs.contains(pc + 8 * packets - 4); ++wait) {
    CHECK(wait < 2000 && !replay_pc);
    instructions_in = {};
    instructions_in.pvalid = sent < packets;
    instructions_in.pbits.pcount = 2;
    for (unsigned lane = 0; lane < 2; ++lane) {
      auto &entry = instructions_in.pbits.pentries[lane];
      entry.ppc = pc + 8 * sent + 4 * lane;
      entry.psequential_upc = entry.ppc + 4;
      entry.pinstruction = entry.praw_uinstruction = addi(20 + lane, 0, 100 + lane);
    }
    settle();
    const bool accepted = sent < packets && instructions_out.pready;
    step();
    if (accepted) ++sent;
  }
  instructions_in.pvalid = 0;
  CHECK(retired_pcs.contains(pc) && gprs[20] == 100 && gprs[21] == 101);
}
void extract(unsigned vector_register, unsigned rd, std::uint64_t value,
             bool require_progress = false) {
  send(vi(16, vector_register, 0, 2, rd));
  // Sustained paired scalar traffic competes with the vector return for the 2W calendar.
  scalar_burst();
  if (require_progress) CHECK(gprs[rd] == value);
  for (unsigned wait = 0; gprs[rd] != value && wait < 2000; ++wait) step();
  CHECK(gprs[rd] == value);
}
int main() {
  return run_test([] {
    reset = 1;
    instructions_in = {};
    step(); step();
    reset = 0;
    send(0x000060b7);             // FS=Dirty
    send(addi(1, 1, 0x600));      // VS=Dirty
    send(0x30009073);             // csrw mstatus,x1
    send(addi(1, 0, 0x400));
    send(0x30509073);             // mtvec = 0x400
    setvl(0, bytes * 2);
    send(addi(2, 0, 3));
    send(addi(3, 0, 7));
    send(vi(23, 0, 2, 4, 8));    // vmv.v.x
    send(vi(37, 8, 3, 6, 9));    // vmul.vx, shared multiply
    send(0x02310333);             // mul x6,x2,x3
    extract(9, 7, 21, true);
    CHECK(gprs[6] == 21);
    send(vi(16, 9, 0, 2, 0));   // discarded scalar result must not clear an unowned x0
    run(20);
    send(vi(32, 9, 2, 6, 10));   // vdivu.vx, shared divide
    send(0x02235333);             // divu x6,x6,x2
    extract(10, 7, 7);
    CHECK(gprs[6] == 7);
    send(addi(1, 0, 0x100));
    send(0x02000027 | (1 << 15) | (9 << 7)); // packed vector store
    run();
    for (unsigned offset = 0; offset < bytes * 2; offset += bytes)
      CHECK(words[0x100 + offset] == (bytes == 8 ? UINT64_C(0x1515151515151515) : UINT64_C(0x15151515)));
    slow = true;
    send(0x02000007 | (1 << 15) | (11 << 7)); // deferred packed vector load
    send(addi(12, 0, 91));                   // younger independent scalar
    extract(11, 7, 21);
    CHECK(gprs[12] == 91);
    // Failed proof falls back to checked beats; the second element faults.
    safe = false;
    fault_at = 0x101;
    send(0x02000007 | (1 << 15) | (12 << 7), true);
    CHECK(last_cause == 13 && last_tval == 0x101);
    fault_at = 0;
    send(0x008023f3);             // csrr x7,vstart
    CHECK(gprs[7] == 1);
    send(0x00801073);             // csrw vstart,x0
    slow = false;
    safe = true;
    if constexpr (bytes == 8) {
      setvl(3, 2);
      send(addi(2, 0, 3));
      send((0x69u << 25) | (2 << 15) | (1 << 7) | 0x53); // fcvt.d.w f1,x2
      send(vi(23, 0, 1, 5, 8)); // vfmv.v.f
      send(vi(0, 8, 1, 5, 9));  // vfadd.vf, shared FP
      send((1u << 25) | (1 << 20) | (1 << 15) | (2 << 7) | 0x53); // fadd.d f2,f1,f1
      send(vi(16, 9, 0, 1, 3)); // vfmv.f.s f3,v9
      send((0x61u << 25) | (3 << 15) | (7 << 7) | 0x53); // fcvt.w.d x7,f3
      run(120);
      CHECK(gprs[7] == 6);
    }
    CHECK(proof_count >= 3);
    CHECK(deferred_count >= 5);
    CHECK(stores >= 2);
    CHECK(fault_count == 1);
  });
}
