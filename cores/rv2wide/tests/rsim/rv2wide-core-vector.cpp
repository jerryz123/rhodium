// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "../../../../rheg/runtime/rheg.h"
#include "test-sites.h"
#include <array>
#include <deque>
#include <map>
#include <set>

using Request = std::remove_cvref_t<decltype(pipeline_out.prequest.pbits)>;
constexpr unsigned bytes = sizeof(memory_in.presponse.pbits);
Request lookup{}, commit_owner{};
bool lookup_live = false, proof_pending = false, slow = false, safe = true;
bool guest_fault = false;
bool fast_enabled = false, fast_current = true;
unsigned fast_checks = 0;
unsigned cycle = 0, stores = 0, proof_count = 0, deferred_count = 0;
unsigned redirect_count = 0, response_delay = 12;
unsigned request_block_until = 0;
unsigned commit_block_until = 0;
std::uint64_t next_pc = 0x8000, replay_pc = 0, recovery_pc = 0, fault_at = 0;
std::uint64_t resolve_pc = 0;
unsigned resolve_disposition = 0;
std::array<std::uint64_t, 32> gprs{};
std::map<std::uint64_t, std::uint64_t> words;
std::set<std::uint64_t> retired_pcs;
std::map<std::uint64_t, unsigned> memory_cycles;
std::map<std::uint64_t, unsigned> retirement_cycles;
std::map<std::uint64_t, unsigned> retirement_lanes;
std::map<std::uint64_t, std::uint64_t> retirement_values;
struct Response { std::uint64_t data; unsigned due; };
std::deque<Response> responses;
unsigned fault_count = 0;
std::uint64_t last_cause = 0, last_tval = 0, last_fault_pc = 0;
std::uint64_t authorization_owner = 0, guarded_store_address = 0;
unsigned authorization_merges = 0;
unsigned nonflushing_configurations = 0, configuration_branch_repairs = 0;
std::uint64_t history_repair_branch_pc = 0;
bool interrupt_after_admission = false;
std::map<std::uint64_t, unsigned> trained_branches;
struct Redirect { std::uint64_t pc, target; unsigned disposition; };
std::vector<Redirect> observed_redirects;
std::map<std::uint64_t, std::vector<unsigned>> store_offers, store_writes;

namespace vector_trace {
struct Owner {
  rheg::Ref parent{};
  std::uint64_t pc = 0, instruction = 0;
  unsigned lane = 0;
};
struct Offer { bool accepted = false; std::uint64_t pc = 0, instruction = 0; };
std::array<std::optional<Owner>, 2> wb, next_wb;
std::array<std::uint64_t, 2> mem_sequences{};
std::deque<Owner> admitted;
std::optional<Owner> active;
Offer admission, dispatch;
bool released = false;
std::uint64_t trace_cycle = 0, sequence = 0, stall = 0;
unsigned admissions = 0, dispatches = 0, queued = 0, replacements = 0;
unsigned rejections = 0;
std::map<std::uint64_t, unsigned> admission_counts;
std::array<unsigned, 2> lanes{};
std::map<rheg::Ref, unsigned> beats;
constexpr unsigned mem_sites[] = {rv2wide_vector_sites::mem0, rv2wide_vector_sites::mem1};

bool equal(rheg::Ref a, rheg::Ref b) {
  return a.site == b.site && a.sequence == b.sequence;
}
void sample() {
  next_wb = {};
  for (unsigned lane = 0; lane < 2; ++lane) {
    const auto &port = lane ? memory_stage_1_out : memory_stage_0_out;
    if (port.pvalid)
      next_wb[lane] = Owner{{}, port.pbits.ppc, port.pbits.praw_uinstruction, lane};
  }
}
void check_occurrence(unsigned site, std::uint64_t &serial) {
  const auto &graph = rheg::graph();
  rheg::Ref child{site, serial};
  auto found = graph.nodes.find(child);
  if (found == graph.nodes.end()) return;
  CHECK(active && found->second.cycle == trace_cycle && !found->second.ancestry_unknown);
  unsigned parents = 0;
  for (const auto &edge : graph.edges) if (equal(edge.second, child)) {
    CHECK(equal(edge.first, active->parent));
    ++parents;
  }
  if (parents != 1)
    std::cerr << "vector trace site " << site << " cycle " << trace_cycle
              << " parents " << parents << " expected " << active->parent.site
              << ':' << active->parent.sequence << '\n';
  CHECK(parents == 1);
  CHECK(graph.field(child, "pc").unsigned_value() == active->pc);
  CHECK(graph.field(child, "instruction").unsigned_value() == active->instruction);
  CHECK(graph.nodes.at(active->parent).cycle < found->second.cycle);
  if (site == rv2wide_vector_sites::sequence) ++beats[active->parent];
  ++serial;
}
void check() {
  if (reset) {
    wb = {}; next_wb = {}; mem_sequences = {};
    admitted.clear(); active.reset();
    trace_cycle = sequence = stall = 0;
    return;
  }
  const auto &graph = rheg::graph();
  // Public MEM observations identify the live WB owner one cycle later.
  // Accepted descriptors enter an independent FIFO; PC is only a checked capture.
  if (admission.accepted) {
    std::optional<Owner> owner;
    for (const auto &candidate : wb) if (candidate && candidate->pc == admission.pc) {
      CHECK(!owner && candidate->instruction == admission.instruction);
      owner = candidate;
    }
    CHECK(owner);
    admitted.push_back(*owner);
    ++lanes[owner->lane]; ++admissions;
    ++admission_counts[owner->pc];
  }
  // A replacement edge still sequences/stalls the old registered descriptor.
  check_occurrence(rv2wide_vector_sites::sequence, sequence);
  check_occurrence(rv2wide_vector_sites::stall, stall);
  if (released) { CHECK(active); active.reset(); }
  if (dispatch.accepted) {
    CHECK(!active && !admitted.empty());
    const auto owner = admitted.front(); admitted.pop_front();
    CHECK(owner.pc == dispatch.pc && owner.instruction == dispatch.instruction);
    if (trace_cycle > graph.nodes.at(owner.parent).cycle + 1) ++queued;
    if (released) ++replacements;
    active = owner; ++dispatches;
  }
  for (unsigned lane = 0; lane < 2; ++lane) {
    rheg::Ref ref{mem_sites[lane], mem_sequences[lane]};
    auto found = graph.nodes.find(ref);
    if (found == graph.nodes.end()) { next_wb[lane].reset(); continue; }
    CHECK(next_wb[lane] && found->second.cycle == trace_cycle);
    CHECK(graph.field(ref, "pc").unsigned_value() == next_wb[lane]->pc);
    CHECK(graph.field(ref, "instruction").unsigned_value() == next_wb[lane]->instruction);
    next_wb[lane]->parent = ref;
    ++mem_sequences[lane];
  }
  wb = next_wb;
  ++trace_cycle;
}
void finish() {
  rheg::graph().validate();
  CHECK(admissions == dispatches && admitted.empty() && !active);
  CHECK(lanes[0] && lanes[1] && queued && replacements && stall);
  CHECK(std::any_of(beats.begin(), beats.end(), [](const auto &item) { return item.second > 1; }));
  std::cout << "RV2Wide vector lineage: " << admissions << " admissions, "
            << sequence << " beats, " << stall << " stalls, " << queued
            << " queued dispatches, " << replacements << " replacements\n";
}
}

extern "C" void rv2wide_vector_admission(std::uint64_t resetting, std::uint64_t valid,
    std::uint64_t ready, std::uint64_t pc, std::uint64_t instruction) {
  vector_trace::admission = {!resetting && valid && ready, pc, instruction};
  if (!resetting && valid && !ready) ++vector_trace::rejections;
}
extern "C" void rv2wide_vector_dispatch(std::uint64_t resetting, std::uint64_t valid,
    std::uint64_t ready, std::uint64_t pc, std::uint64_t instruction, std::uint64_t released) {
  vector_trace::dispatch = {!resetting && valid && ready, pc, instruction};
  vector_trace::released = !resetting && released;
}

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
  store_writes[request.paddress].push_back(cycle);
}
void settle() {
  pipeline_in.presponse.pvalid = lookup_live;
  pipeline_in.presponse.pbits = {};
  pipeline_in.presponse.pbits.poutcome = slow ? 0 : lookup.paccess == 2 ? 2 : 1;
  pipeline_in.presponse.pbits.pdata = words[lookup.paddress & ~(std::uint64_t(bytes) - 1)];
  pipeline_in.pcommit_uready = cycle >= commit_block_until;
  memory_in.pdrained = responses.empty();
  memory_in.pordered_ubusy = 0;
  memory_in.preservation_uvalid = 0;
  memory_in.presponse.pvalid = !responses.empty() && responses.front().due <= cycle;
  memory_in.presponse.pbits = responses.empty() ? 0 : responses.front().data;
  memory_in.prequest.pready = responses.size() < 8 && cycle >= request_block_until;
  memory_in.pfault = {};
  vector_precheck_in.prequest.pready = !proof_pending;
  vector_precheck_in.presponse.pvalid = proof_pending;
  vector_precheck_in.presponse.pbits = safe;
  vector_fast_in.presponse.pvalid = 0;
  vector_fast_in.presponse.pbits = {};
  vector_fast_in.pcandidate_ucurrent = fast_current;
  split_in.prequest.pready = 1;
  split_in.presponse.pvalid = 0;
  eval();
  vector_fast_in.presponse.pvalid = fast_enabled && vector_fast_out.prequest.pvalid;
  vector_fast_in.presponse.pbits.prange = vector_fast_out.prequest.pbits;
  vector_fast_in.presponse.pbits.pppn = (vector_fast_out.prequest.pbits.pfirst >> 12) + 1;
  for (unsigned lane = 0; lane < 2; ++lane) {
    auto &resolution = lane ? resolution_1_in : resolution_0_in;
    const auto &stage = lane ? memory_stage_1_out : memory_stage_0_out;
    resolution = {};
    if (resolve_pc && stage.pvalid && stage.pbits.ppc == resolve_pc) {
      resolution.pvalid = 1;
      resolution.pbits.pdisposition = resolve_disposition;
      resolution.pbits.pcause = 2;
      resolution.pbits.pvalue = stage.pbits.pinstruction;
    }
  }
  eval();
  if (fault_at && memory_out.prequest.pvalid &&
      memory_out.prequest.pbits.paddress == fault_at) {
    memory_in.prequest.pready = 0;
    memory_in.pfault.pvalid = 1;
    memory_in.pfault.pbits.pdisposition = 1;
    memory_in.pfault.pbits.pcause = memory_out.prequest.pbits.paccess == 2 ? 15 : 13;
    if (guest_fault) {
      memory_in.pfault.pbits.pcause += 8;
      memory_in.pfault.pbits.pguest = {1, 0x23450, 0};
    }
    memory_in.pfault.pbits.pvalue = fault_at;
    eval();
  }
}
void step() {
  settle();
  if (!reset) {
    if (authorization_owner && retired_0_out.pvalid && retired_1_out.pvalid &&
        retired_0_out.pbits.pfetched.ppc == authorization_owner)
      ++authorization_merges;
    for (const auto *port : {&retired_0_out, &retired_1_out}) {
      if (!port->pvalid) continue;
      const auto &result = port->pbits;
      const auto instruction = result.pfetched.pinstruction;
      const bool configuration = (instruction & 0x707f) == 0x7057;
      if (configuration && !fetch_flush_out.pvalid) {
        CHECK(!history_restore_out.pvalid);
        ++nonflushing_configurations;
      }
      if (configuration && history_repair_branch_pc && redirect_out.pvalid &&
          redirect_out.pbits.ppc == history_repair_branch_pc &&
          redirect_out.pbits.presolution.pdisposition == 0) {
        // The correcting branch is in MEM, one cycle behind this vset.
        // These tests offer history=5, so append its resolved outcome to it.
        CHECK(history_restore_out.pvalid && history_restore_out.pbits == 10);
        ++configuration_branch_repairs;
      }
      CHECK(retired_pcs.insert(result.pfetched.ppc).second);
      retirement_cycles.emplace(result.pfetched.ppc, cycle);
      retirement_lanes.emplace(result.pfetched.ppc, port == &retired_1_out);
      if (result.pwrite && !result.pdeferred) {
        retirement_values.emplace(result.pfetched.ppc, result.pdata);
        gprs[result.prd] = result.pdata;
      }
    }
    if (completed_out.pvalid && completed_out.pbits.pwrite) {
      ++deferred_count;
      gprs[completed_out.pbits.prd] = completed_out.pbits.pdata;
    }
    if (branch_update_out.pvalid) {
      CHECK(retired_pcs.contains(branch_update_out.pbits.ppc));
      ++trained_branches[branch_update_out.pbits.ppc];
    }
    if (redirect_out.pvalid) {
      ++redirect_count;
      observed_redirects.push_back({redirect_out.pbits.ppc,
          redirect_out.pbits.ptarget, redirect_out.pbits.presolution.pdisposition});
      const bool interrupt = interrupt_after_admission &&
          redirect_out.pbits.presolution.pdisposition == 3 &&
          (redirect_out.pbits.presolution.pcause >> (bytes * 8 - 1));
      if (redirect_out.pbits.presolution.pdisposition == 2)
        replay_pc = redirect_out.pbits.ptarget;
      if (redirect_out.pbits.presolution.pdisposition == 2 ||
          (redirect_out.pbits.presolution.pdisposition == 3 && !interrupt))
        recovery_pc = redirect_out.pbits.ptarget;
      if (redirect_out.pbits.presolution.pdisposition == 1 || interrupt) {
        if (interrupt_after_admission) {
          CHECK(retired_pcs.contains(authorization_owner) && responses.empty());
        }
        ++fault_count;
        last_cause = redirect_out.pbits.presolution.pcause;
        last_tval = redirect_out.pbits.presolution.pvalue;
        last_fault_pc = redirect_out.pbits.ppc;
      }
    }
    if (pipeline_out.pcommit) {
      store_offers[commit_owner.paddress].push_back(cycle);
      if (authorization_owner && commit_owner.paddress == guarded_store_address)
        CHECK(retired_pcs.contains(authorization_owner));
      if (pipeline_in.pcommit_uready) write(commit_owner);
    }
    if (memory_out.prequest.pvalid && memory_out.prequest.pbits.paccess == 2) {
      const auto address = memory_out.prequest.pbits.paddress;
      store_offers[address].push_back(cycle);
      if (authorization_owner && address == guarded_store_address)
        CHECK(retired_pcs.contains(authorization_owner));
    }
    if (memory_in.presponse.pvalid && memory_out.presponse.pready)
      responses.pop_front();
    if (memory_out.prequest.pvalid && memory_in.prequest.pready) {
      const auto request = memory_out.prequest.pbits;
      if (authorization_owner && request.paddress == guarded_store_address)
        CHECK(retired_pcs.contains(authorization_owner));
      if (request.paccess == 2) write(request);
      responses.push_back({words[request.paddress & ~(std::uint64_t(bytes) - 1)], cycle + response_delay});
    }
    for (const auto *port : {&memory_stage_0_out, &memory_stage_1_out})
      if (port->pvalid) memory_cycles.try_emplace(port->pbits.ppc, cycle);
    CHECK(!split_out.prequest.pvalid);
    if (vector_fast_out.prequest.pvalid) ++fast_checks;
  }
  const bool proof_accept = vector_precheck_out.prequest.pvalid && vector_precheck_in.prequest.pready;
  const bool proof_return = vector_precheck_in.presponse.pvalid && vector_precheck_out.presponse.pready;
  const auto next_lookup = pipeline_out.prequest.pbits;
  const auto next_live = pipeline_out.prequest.pvalid;
  vector_trace::sample();
  tick_model();
  vector_trace::check();
  if (interrupt_after_admission && vector_trace::admission.accepted &&
      vector_trace::admission.pc == authorization_owner)
    interrupts.pmachine_utimer = 1;
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
std::uint32_t vsetivli(unsigned rd, unsigned avl, unsigned type) {
  return 0xc0007057 | type << 20 | avl << 15 | rd << 7;
}
std::uint32_t vsetvli(unsigned rd, unsigned rs1, unsigned type) {
  return 0x7057 | type << 20 | rs1 << 15 | rd << 7;
}
void stream(std::initializer_list<std::uint32_t> words, bool fault = false,
            bool recover = false, bool require_recovery = true) {
  const std::vector<std::uint32_t> program(words);
  const auto base = next_pc;
  next_pc += 4 * program.size();
  const unsigned faults_before = fault_count, redirects_before = redirect_count;
  recovery_pc = 0;
  unsigned sent = 0;
  for (unsigned wait = 0; ; ++wait) {
    CHECK(wait < 3000);
    if (recovery_pc) {
      if (!recover || recovery_pc < base || recovery_pc >= next_pc)
        std::cerr << "unexpected recovery to " << std::hex << recovery_pc
                  << " in stream [" << base << ", " << next_pc << ")"
                  << std::dec << '\n';
      CHECK(recover && recovery_pc >= base && recovery_pc < next_pc);
      sent = (recovery_pc - base) / 4;
      recovery_pc = replay_pc = 0;
      resolve_pc = 0; // An injected replay rejects only the first attempt.
    }
    CHECK(!replay_pc);
    instructions_in = {};
    instructions_in.pvalid = sent < program.size();
    const unsigned count = std::min<unsigned>(2, program.size() - sent);
    instructions_in.pbits.pcount = count;
    for (unsigned lane = 0; lane < count; ++lane) {
      auto &entry = instructions_in.pbits.pentries[lane];
      entry.ppc = base + 4 * (sent + lane);
      entry.psequential_upc = entry.ppc + 4;
      entry.pinstruction = entry.praw_uinstruction = program[sent + lane];
    }
    settle();
    const bool accepted = instructions_in.pvalid && instructions_out.pready;
    step();
    if (accepted) sent += count;
    if (retired_pcs.contains(next_pc - 4) || fault_count != faults_before) break;
  }
  instructions_in.pvalid = 0;
  CHECK(fault == (fault_count != faults_before));
  if (!fault) {
    if (!recover) CHECK(redirect_count == redirects_before);
    else if (require_recovery) CHECK(redirect_count > redirects_before);
    for (unsigned index = 0; index < program.size(); ++index)
      CHECK(retired_pcs.contains(base + 4 * index));
  }
  settle();
}

void extract(unsigned vector_register, unsigned rd, std::uint64_t value,
             bool require_progress = false);
void kill_younger(std::uint32_t instruction) {
  const auto branch_pc = next_pc;
  next_pc += 12;
  instructions_in = {};
  instructions_in.pvalid = 1;
  instructions_in.pbits.pcount = 2;
  for (unsigned lane = 0; lane < 2; ++lane) {
    auto &entry = instructions_in.pbits.pentries[lane];
    entry.ppc = branch_pc + 4 * lane;
    entry.psequential_upc = entry.ppc + 4;
    entry.pinstruction = entry.praw_uinstruction = lane ? instruction : 0x00000663;
  }
  for (unsigned wait = 0; ; ++wait) {
    CHECK(wait < 100);
    settle();
    const bool accepted = instructions_out.pready;
    step();
    if (accepted) break;
  }
  instructions_in.pvalid = 0;
  run(12);
  CHECK(retired_pcs.contains(branch_pc) && !retired_pcs.contains(branch_pc + 4));
}
void vector_configuration_overlap() {
  // MEM forwarding supplies dependent AVL and vtype; a gap exercises WB forwarding.
  const auto first = next_pc;
  stream({vsetivli(5, 4, 0), vsetvli(6, 5, 8), addi(7, 6, 3),
          vsetvli(0, 0, 7), vi(23, 0, 7, 4, 18)});
  CHECK(gprs[5] == 4 && gprs[6] == 4 && gprs[7] == 7);
  for (unsigned index = 1; index < 3; ++index)
    CHECK(memory_cycles.at(first + 4 * index) == memory_cycles.at(first) + index);
  CHECK(memory_cycles.at(first + 12) == memory_cycles.at(first + 8));
  CHECK(memory_cycles.at(first + 16) == memory_cycles.at(first + 12) + 1);
  extract(18, 7, 7);

  // vsetvl consumes ordinary forwarded scalar sources; keep-VL uses its new state.
  stream({addi(10, 0, 8), addi(11, 0, 3),
          0x80007057 | (10 << 20) | (11 << 15) | (12 << 7),
          addi(13, 12, 1), vsetvli(0, 0, 7), vi(23, 0, 13, 4, 19)});
  CHECK(gprs[12] == 3 && gprs[13] == 4);
  extract(19, 7, 4);

  // Legality must use the new EX state, not the previously committed vill state.
  send(vsetivli(5, 4, 256));
  CHECK(gprs[5] == 0);
  stream({vsetivli(5, 4, 0), vi(23, 0, 13, 4, 20)});
  extract(20, 7, 4);
  const auto illegal = vi(23, 0, 13, 4, 21);
  stream({vsetivli(5, 4, 256), illegal}, true);
  CHECK(last_cause == 2 && last_tval == illegal);
  stream({vsetivli(5, 4, 0), vsetvli(0, 0, 8)}, true);
  CHECK(last_cause == 2 && last_tval == vsetvli(0, 0, 8));
  send(0xc21023f3); // csrr x7,vtype: faulting keep-VL must not commit
  CHECK(gprs[7] == 0);

  // A certified older load retains its original byte geometry while vset retires.
  setvl(0, bytes * 2);
  send(addi(1, 0, 0x400));
  words[0x400] = bytes == 8 ? UINT64_C(0x1111111111111111) : UINT64_C(0x11111111);
  words[0x400 + bytes] = bytes == 8 ? UINT64_C(0x2222222222222222) : UINT64_C(0x22222222);
  slow = true;
  safe = true;
  response_delay = 80;
  send(0x02000007 | (1 << 15) | (24 << 7));
  for (unsigned wait = 0; responses.empty(); ++wait) {
    CHECK(wait < 100);
    step();
  }
  const auto due = responses.front().due;
  stream({vsetivli(5, 4, 8), vi(23, 0, 13, 4, 25)});
  CHECK(!responses.empty() && cycle < due);
  run(120);
  slow = false;
  response_delay = 12;
  send(addi(1, 0, 0x600));
  send(0x02005027 | (1 << 15) | (25 << 7));
  run();
  CHECK(words[0x600] == (bytes == 8 ? UINT64_C(0x0004000400040004) : UINT64_C(0x00040004)));
  if constexpr (bytes == 4) CHECK(words[0x604] == UINT64_C(0x00040004));
  setvl(0, bytes * 2);
  send(addi(1, 0, 0x700));
  send(0x02000027 | (1 << 15) | (24 << 7));
  run();
  CHECK(words[0x700] == words[0x400] && words[0x700 + bytes] == words[0x400 + bytes]);
  extract(24, 7, 0x11);

  // A MEM branch kills the younger EX configuration before it can forward or commit.
  kill_younger(vsetivli(5, 4, 8));
  send(0xc21023f3);
  CHECK(gprs[7] == 0);
}
void vector_memory_fast_path() {
  run(120);
  setvl(1, 4);
  send(addi(1, 0, 0x400));
  send(addi(2, 0, 0x123));
  send(vi(23, 0, 2, 4, 8));
  run();
  fast_enabled = true;
  const auto proofs_before = proof_count;
  const auto store_pc = next_pc;
  const auto store = 0x02005027 | (1 << 15) | (8 << 7);
  stream({store, addi(3, 0, 42), addi(4, 3, 1)});
  CHECK(retirement_cycles.at(store_pc) == memory_cycles.at(store_pc) + 1);
  CHECK(gprs[3] == 42 && gprs[4] == 43 && proof_count == proofs_before);
  run(60);
  CHECK(words[0x1400] == (bytes == 8 ? UINT64_C(0x0123012301230123) : UINT64_C(0x01230123)));
  CHECK(words[0x400] != words[0x1400]); // certificate, not live VA, selects memory
  if constexpr (bytes == 4) CHECK(words[0x1404] == words[0x1400]);

  slow = true;
  response_delay = 80;
  const auto load_pc = next_pc;
  stream({0x02005007 | (1 << 15) | (9 << 7), addi(3, 0, 51)});
  CHECK(retirement_cycles.at(load_pc) == memory_cycles.at(load_pc) + 1);
  CHECK(gprs[3] == 51 && proof_count == proofs_before);
  run(120);
  extract(9, 7, 0x123);
  slow = false;
  response_delay = 12;

  // vstart affects the certified range and stores only the remaining elements.
  words[0x1400] = words[0x1400 + bytes] = 0;
  send(addi(2, 0, 2));
  send(0x00811073); // csrw vstart,x2
  send(store);
  run(60);
  if constexpr (bytes == 8)
    CHECK(words[0x1400] == UINT64_C(0x0123012300000000));
  else
    CHECK(words[0x1400] == 0 && words[0x1404] == UINT64_C(0x01230123));
  send(0x008023f3); // accepted certification retires vstart exactly once
  CHECK(gprs[7] == 0 && proof_count == proofs_before);

  // Fast eligibility never authorizes a store killed by an older MEM redirect.
  const auto stores_before = stores;
  kill_younger(store);
  run(20);
  CHECK(stores == stores_before && proof_count == proofs_before);

  // A stale certificate takes precise authorization without an unconditional
  // frontend restart when no younger instruction reaches the WB barrier.
  fast_current = false;
  const auto redirects_before = redirect_count;
  const auto fallback_pc = next_pc;
  send(store);
  run(60);
  CHECK(proof_count == proofs_before + 1 && redirect_count == redirects_before);
  CHECK(retirement_cycles.at(fallback_pc) > memory_cycles.at(fallback_pc) + 1);
  CHECK(words[0x400] == (bytes == 8 ? UINT64_C(0x0123012301230123) : UINT64_C(0x01230123)));
  fast_current = true;

  // Crossing a page is deliberately not a fast candidate, even if both pages are safe.
  send(0x000010b7); // lui x1,1
  send(addi(1, 1, -4));
  const auto checks_before = fast_checks;
  send(store);
  run(60);
  CHECK(fast_checks == checks_before && proof_count == proofs_before + 2);
  fast_enabled = false;
}
void paired_vector_configurations() {
  run(120);
  setvl(0, 4);
  send(addi(2, 0, 0x123));
  const auto base = next_pc;
  stream({vsetivli(10, 4, 8), vi(23, 0, 2, 4, 8),
          vi(23, 0, 2, 4, 9), vsetivli(11, 2, 0),
          vi(23, 0, 2, 4, 10)});
  for (unsigned index : {0u, 2u}) {
    CHECK(memory_cycles.at(base + 4 * index) == memory_cycles.at(base + 4 * (index + 1)));
    CHECK(retirement_cycles.at(base + 4 * index) == retirement_cycles.at(base + 4 * (index + 1)));
    CHECK(retirement_lanes.at(base + 4 * index) == 0);
    CHECK(retirement_lanes.at(base + 4 * (index + 1)) == 1);
  }
  CHECK(gprs[10] == 4 && gprs[11] == 2);
  setvl(1, 4);
  extract(8, 7, 0x123);
  extract(9, 7, 0x123); // older vector keeps the halfword state
  setvl(0, 2);
  extract(10, 7, 0x23); // MEM slot-one configuration supplies the next vector

  // A slot-one configuration also supplies dependent configurations in MEM and WB.
  stream({vi(23, 0, 2, 4, 11), vsetivli(12, 3, 8),
          vsetvli(0, 0, 7), vi(23, 0, 2, 4, 12)});
  CHECK(gprs[12] == 3);
  extract(12, 7, 0x23);
  stream({vi(23, 0, 2, 4, 13), vsetivli(12, 3, 8),
          addi(20, 0, 1), addi(21, 0, 2), vsetvli(0, 0, 7)});
  send(0xc20023f3); // csrr x7,vl
  CHECK(gprs[7] == 3);

  // Same-pair scalar RAW and deferred WAW still split rather than bypassing vset.
  const auto raw = next_pc;
  stream({vsetivli(13, 3, 0), vi(23, 0, 13, 4, 14)});
  CHECK(memory_cycles.at(raw + 4) > memory_cycles.at(raw));
  extract(14, 7, 3);
  const auto waw = next_pc;
  stream({vi(16, 14, 0, 2, 13), vsetivli(13, 2, 0)});
  CHECK(memory_cycles.at(waw + 4) > memory_cycles.at(waw));
  CHECK(gprs[13] == 2);

  // Register-supplied vtype/AVL configurations pair in either lane as well.
  send(addi(10, 0, 8));
  send(addi(11, 0, 3));
  const auto register_pair = next_pc;
  const auto configure = 0x80007057 | (10 << 20) | (11 << 15) | (12 << 7);
  stream({configure, vi(23, 0, 2, 4, 18),
          vi(23, 0, 2, 4, 19), configure});
  CHECK(retirement_cycles.at(register_pair) == retirement_cycles.at(register_pair + 4));
  CHECK(retirement_cycles.at(register_pair + 8) == retirement_cycles.at(register_pair + 12));
  CHECK(gprs[12] == 3);
  extract(18, 7, 0x123);
  extract(19, 7, 0x123);

  // A younger vector trap cannot discard the older configuration's CSR/GPR commit.
  const auto fault_pair = next_pc;
  const auto illegal = vi(23, 0, 2, 4, 15);
  stream({vsetivli(14, 4, 256), illegal}, true);
  CHECK(retired_pcs.contains(fault_pair) && !retired_pcs.contains(fault_pair + 4));
  CHECK(gprs[14] == 0 && last_fault_pc == fault_pair + 4);
  CHECK(last_cause == 2 && last_tval == illegal);
  send(0xc21023f3); // csrr x7,vtype
  CHECK(gprs[7] == (UINT64_C(1) << (bytes * 8 - 1)));

  // A faulting older vector suppresses its paired configuration entirely.
  const auto older_fault = next_pc;
  stream({illegal, vsetivli(14, 4, 0)}, true);
  CHECK(!retired_pcs.contains(older_fault) && !retired_pcs.contains(older_fault + 4));
  CHECK(last_fault_pc == older_fault && gprs[14] == 0);
  send(0xc21023f3);
  CHECK(gprs[7] == (UINT64_C(1) << (bytes * 8 - 1)));

  // Recovery reoffers only the rejected suffix, never an already committed vset.
  setvl(0, 4);
  const auto younger_replay = next_pc;
  resolve_pc = younger_replay + 4;
  resolve_disposition = 2;
  stream({vsetivli(14, 3, 8), vi(23, 0, 2, 4, 15)}, false, true);
  CHECK(gprs[14] == 3);
  CHECK(retirement_cycles.at(younger_replay) < retirement_cycles.at(younger_replay + 4));
  extract(15, 7, 0x123);
  const auto older_replay = next_pc;
  resolve_pc = older_replay;
  stream({vi(23, 0, 2, 4, 16), vsetivli(14, 2, 0)}, false, true);
  CHECK(gprs[14] == 2);
  setvl(1, 3);
  extract(16, 7, 0x123);

  // Fast memory certification uses the paired new VL/vtype, not committed geometry.
  run(60);
  send(addi(1, 0, 0x500));
  fast_enabled = true;
  const auto memory_pair = next_pc;
  stream({vsetivli(14, 3, 8), 0x02005027 | (1 << 15) | (16 << 7)});
  CHECK(retirement_cycles.at(memory_pair) == retirement_cycles.at(memory_pair + 4));
  run(60);
  CHECK(words[0x1500] == (bytes == 8 ? UINT64_C(0x012301230123) : UINT64_C(0x01230123)));
  if constexpr (bytes == 4) CHECK(words[0x1504] == 0x123);

  // A paired younger configuration trap drains the just-accepted older vector load.
  slow = true;
  response_delay = 80;
  const auto draining_pair = next_pc;
  stream({0x02005007 | (1 << 15) | (17 << 7), vsetvli(0, 0, 0)}, true);
  CHECK(last_fault_pc == draining_pair + 4 && last_cause == 2);
  CHECK(retired_pcs.contains(draining_pair) && !retired_pcs.contains(draining_pair + 4));
  CHECK(cycle >= retirement_cycles.at(draining_pair) + response_delay && responses.empty());
  slow = false;
  response_delay = 12;
  fast_enabled = false;

  // An uncertified older load retains its old geometry and restarts the younger vset.
  const auto retained_pair = next_pc;
  const auto proofs_before = proof_count;
  stream({0x02005007 | (1 << 15) | (17 << 7), vsetivli(14, 2, 0)}, false, true);
  CHECK(proof_count == proofs_before + 1 && gprs[14] == 2);
  CHECK(retirement_cycles.at(retained_pair) < retirement_cycles.at(retained_pair + 4));
  run(60);
  send(0xc21023f3);
  CHECK(gprs[7] == 0);

  // Actual full descriptor credit replays configuration/vector pairs without double commit.
  fast_enabled = true;
  slow = true;
  response_delay = 160;
  stream({0x02000007 | (1 << 15) | (20 << 7),
          vsetivli(14, 2, 0), vi(0, 20, 0, 3, 21),
          vi(0, 21, 0, 3, 22), vsetivli(14, 2, 0),
          vsetivli(14, 2, 0), vi(0, 22, 0, 3, 23),
          vi(0, 23, 0, 3, 24), vsetivli(14, 2, 0),
          vsetivli(14, 2, 0), vi(0, 24, 0, 3, 25),
          vi(0, 25, 0, 3, 26), vsetivli(14, 2, 0),
          vsetivli(14, 2, 0), vi(0, 26, 0, 3, 27),
          vi(0, 27, 0, 3, 28), vsetivli(14, 2, 0),
          vsetivli(14, 2, 0), vi(0, 28, 0, 3, 29)}, false, true);
  CHECK(gprs[14] == 2);
  run(240);
  fast_enabled = slow = false;
  response_delay = 12;
}
void issue_pair(std::uint32_t older, std::uint32_t younger) {
  const auto pc = next_pc;
  stream({older, younger});
  if (memory_cycles.at(pc) != memory_cycles.at(pc + 4))
    std::cerr << "split pair at " << std::hex << pc << ": " << older << ", " << younger
              << std::dec << "; MEM cycles " << memory_cycles.at(pc) << ", " << memory_cycles.at(pc + 4) << '\n';
  CHECK(memory_cycles.at(pc) == memory_cycles.at(pc + 4));
  CHECK(retirement_cycles.at(pc) == retirement_cycles.at(pc + 4));
  CHECK(retirement_lanes.at(pc) == 0 && retirement_lanes.at(pc + 4) == 1);
}
void paired_vector_arithmetic() {
  run(120);
  setvl(0, 4);
  send(addi(2, 0, 7));
  send(vi(23, 0, 2, 4, 8)); // v8 = 7
  issue_pair(addi(2, 0, 5), vi(0, 8, 2, 4, 9)); // vadd.vx
  CHECK(gprs[2] == 5);
  extract(9, 7, 12);
  issue_pair((2 << 20) | (2 << 15) | (3 << 7) | 0x33,
                  vi(2, 8, 3, 4, 10)); // add x3,x2,x2; vsub.vx
  CHECK(gprs[3] == 10);
  extract(10, 7, (UINT64_MAX - 2) & low_mask(bytes * 8));
  issue_pair(addi(4, 0, 3), vi(37, 8, 4, 6, 11)); // vmul.vx
  extract(11, 7, 21);
  issue_pair(addi(4, 0, 7), vi(32, 11, 4, 6, 12)); // vdivu.vx
  extract(12, 7, 3);

  // Capture XLEN values unchanged; element truncation/sign interpretation stays in execution.
  setvl(bytes == 8 ? 3 : 2, 2);
  const auto high = (UINT64_C(0xffffffff80000000) & low_mask(bytes * 8));
  issue_pair(0x80000337, vi(23, 0, 6, 4, 13)); // lui x6,0x80000; vmv.v.x
  CHECK(gprs[6] == high);
  extract(13, 7, high);
  const auto auipc = next_pc;
  issue_pair(0x00001317, vi(23, 0, 6, 4, 14)); // auipc x6,1
  CHECK(gprs[6] == auipc + 0x1000);
  extract(14, 7, auipc + 0x1000);
  const auto negative = bytes == 8 ? 0xfff0031bu : addi(6, 0, -1); // addiw on RV64
  issue_pair(negative, vi(23, 0, 6, 4, 15));
  extract(15, 7, low_mask(bytes * 8));
  stream({addi(0, 0, 73), vi(0, 14, 0, 4, 16)}); // x0 is not an ALU producer
  extract(16, 7, auipc + 0x1000);

  // A delayed load still interlocks the producer; its returning GPR port may split the pair.
  send(addi(1, 0, 0x600));
  words[0x600] = 41;
  slow = true;
  response_delay = 80;
  send((1 << 15) | (2 << 12) | (6 << 7) | 0x03); // lw x6,0(x1)
  CHECK(!responses.empty());
  const auto due = responses.front().due;
  const auto waiting = next_pc;
  stream({addi(7, 6, 1), vi(23, 0, 7, 4, 17)});
  CHECK(memory_cycles.at(waiting) >= due && gprs[7] == 42);
  slow = false;
  response_delay = 12;
  extract(17, 7, 42);

  // M producers keep their late-result RAW interlock instead of forwarding their EX ALU.
  const auto multiply = next_pc;
  stream({0x02000033 | (2 << 20) | (2 << 15) | (6 << 7), vi(23, 0, 6, 4, 18)});
  CHECK(memory_cycles.at(multiply + 4) > memory_cycles.at(multiply));
  CHECK(gprs[6] == 25);
  extract(18, 7, 25);

  // An older fault must suppress both retirement and vector dispatch.
  const auto old_scalar = gprs[6];
  const auto older_fault = next_pc;
  resolve_pc = older_fault;
  resolve_disposition = 1;
  stream({addi(6, 0, 55), vi(23, 0, 6, 4, 18)}, true);
  resolve_pc = 0;
  CHECK(last_fault_pc == older_fault && gprs[6] == old_scalar);
  CHECK(!retired_pcs.contains(older_fault) && !retired_pcs.contains(older_fault + 4));
  extract(18, 7, 25);

  // A younger vector legality fault preserves the older scalar's normal WB write.
  send(vsetivli(5, 4, 256));
  const auto younger_fault = next_pc;
  stream({addi(6, 0, 55), vi(23, 0, 6, 4, 18)}, true);
  CHECK(retired_pcs.contains(younger_fault) && !retired_pcs.contains(younger_fault + 4));
  CHECK(last_fault_pc == younger_fault + 4 && gprs[6] == 55);
  setvl(bytes == 8 ? 3 : 2, 2);
  extract(18, 7, 25);

  // Replaying either owner preserves the successful prefix and captures the retried value.
  const auto younger_replay = next_pc;
  resolve_pc = younger_replay + 4;
  resolve_disposition = 2;
  stream({addi(6, 0, 56), vi(23, 0, 6, 4, 19)}, false, true);
  CHECK(retirement_cycles.at(younger_replay) < retirement_cycles.at(younger_replay + 4));
  CHECK(gprs[6] == 56);
  extract(19, 7, 56);
  resolve_pc = next_pc;
  stream({addi(6, 0, 57), vi(23, 0, 6, 4, 20)}, false, true);
  CHECK(gprs[6] == 57);
  extract(20, 7, 57);
}
void paired_scalar_configurations() {
  run(120);
  issue_pair(vsetivli(10, 3, 0), addi(11, 0, 77));
  CHECK(gprs[10] == 3 && gprs[11] == 77);
  issue_pair(addi(12, 0, 88), vsetivli(13, 2, 8));
  CHECK(gprs[12] == 88 && gprs[13] == 2);
  send(0xc21023f3); // csrr x7,vtype
  CHECK(gprs[7] == 8);

  // Register AVL and register vtype retain their ordinary scalar source rules.
  issue_pair(vsetvli(10, 11, 0), addi(12, 0, 99));
  CHECK(gprs[10] == bytes * 2 && gprs[12] == 99);
  send(addi(14, 0, 2));
  send(addi(15, 0, 8));
  const auto configure = 0x80007057 | (15 << 20) | (14 << 15) | (13 << 7);
  issue_pair(addi(12, 0, 100), configure);
  CHECK(gprs[12] == 100 && gprs[13] == 2);

  // Coalescing RF writes must not merge or substitute the two retirement values.
  const auto config_first = next_pc;
  issue_pair(vsetivli(10, 3, 0), addi(10, 0, 91));
  CHECK(retirement_values.at(config_first) == 3 && retirement_values.at(config_first + 4) == 91);
  CHECK(gprs[10] == 91);
  send(addi(11, 10, 1));
  CHECK(gprs[11] == 92); // read the actual RF, not just the retirement oracle
  const auto config_second = next_pc;
  issue_pair(addi(10, 0, 92), vsetivli(10, 2, 8));
  CHECK(retirement_values.at(config_second) == 92 && retirement_values.at(config_second + 4) == 2);
  CHECK(gprs[10] == 2);
  send(addi(11, 10, 1));
  CHECK(gprs[11] == 3);
  issue_pair(vsetivli(0, 3, 0), addi(11, 0, 78));
  send(0xc20023f3); // csrr x7,vl
  CHECK(gprs[0] == 0 && gprs[7] == 3 && gprs[11] == 78);

  // Neither direction gains a dependent EX ALU/configuration bypass.
  const auto vl_dependency = next_pc;
  stream({vsetivli(10, 3, 0), addi(11, 10, 1)});
  CHECK(memory_cycles.at(vl_dependency + 4) > memory_cycles.at(vl_dependency));
  CHECK(gprs[10] == 3 && gprs[11] == 4);
  const auto avl_dependency = next_pc;
  stream({addi(14, 0, 2), vsetvli(10, 14, 8)});
  CHECK(memory_cycles.at(avl_dependency + 4) > memory_cycles.at(avl_dependency));
  CHECK(gprs[14] == 2 && gprs[10] == 2);
  const auto configs = next_pc;
  stream({vsetivli(10, 3, 0), vsetivli(13, 2, 8)});
  CHECK(memory_cycles.at(configs + 4) > memory_cycles.at(configs));
  CHECK(gprs[10] == 3 && gprs[13] == 2);

  // An older illegal keep-VL suppresses the li; a younger illegal keep-VL preserves it.
  setvl(0, 3);
  const auto old_scalar = gprs[12];
  const auto older_fault = next_pc;
  stream({vsetvli(0, 0, 8), addi(12, 0, 111)}, true);
  CHECK(last_fault_pc == older_fault && gprs[12] == old_scalar);
  CHECK(!retired_pcs.contains(older_fault) && !retired_pcs.contains(older_fault + 4));
  const auto younger_fault = next_pc;
  stream({addi(12, 0, 112), vsetvli(0, 0, 8)}, true);
  CHECK(last_fault_pc == younger_fault + 4 && gprs[12] == 112);
  CHECK(retired_pcs.contains(younger_fault) && !retired_pcs.contains(younger_fault + 4));
  send(0xc21023f3);
  CHECK(gprs[7] == 0);

  // The single CSR port commits an older vset before retaining a younger scalar trap.
  const auto scalar_fault = next_pc;
  resolve_pc = scalar_fault + 4;
  resolve_disposition = 1;
  stream({vsetivli(10, 2, 8), addi(12, 0, 113)}, true);
  resolve_pc = 0;
  CHECK(last_fault_pc == scalar_fault + 4 && gprs[10] == 2 && gprs[12] == 112);
  CHECK(retired_pcs.contains(scalar_fault) && !retired_pcs.contains(scalar_fault + 4));
  send(0xc21023f3);
  CHECK(gprs[7] == 8);
  const auto scalar_older_fault = next_pc;
  resolve_pc = scalar_older_fault;
  stream({addi(12, 0, 114), vsetivli(10, 3, 0)}, true);
  resolve_pc = 0;
  CHECK(last_fault_pc == scalar_older_fault && gprs[10] == 2 && gprs[12] == 112);
  CHECK(!retired_pcs.contains(scalar_older_fault) && !retired_pcs.contains(scalar_older_fault + 4));
  send(0xc21023f3);
  CHECK(gprs[7] == 8);

  // Replaying either younger kind preserves exactly the successful older prefix.
  const auto scalar_replay = next_pc;
  resolve_pc = scalar_replay + 4;
  resolve_disposition = 2;
  stream({vsetivli(10, 3, 0), addi(12, 0, 115)}, false, true);
  CHECK(retirement_cycles.at(scalar_replay) < retirement_cycles.at(scalar_replay + 4));
  CHECK(gprs[10] == 3 && gprs[12] == 115);
  const auto config_replay = next_pc;
  resolve_pc = config_replay + 4;
  stream({addi(12, 0, 116), vsetivli(10, 2, 8)}, false, true);
  CHECK(retirement_cycles.at(config_replay) < retirement_cycles.at(config_replay + 4));
  CHECK(gprs[10] == 2 && gprs[12] == 116);
  const auto older_replay = next_pc;
  resolve_pc = older_replay;
  stream({vsetivli(10, 3, 0), addi(12, 0, 117)}, false, true);
  CHECK(retirement_cycles.at(older_replay) == retirement_cycles.at(older_replay + 4));
  CHECK(gprs[10] == 3 && gprs[12] == 117);
}
void independent_vector_scalar_pairs() {
  run(120);
  setvl(0, 4);
  send(addi(2, 0, 7));
  issue_pair(vi(23, 0, 2, 4, 8), addi(3, 0, 31)); // vmv.v.x; unrelated li
  CHECK(gprs[3] == 31);
  extract(8, 7, 7);
  issue_pair(vi(23, 0, 2, 4, 9), addi(2, 0, 9)); // WAR: snapshot old x2
  CHECK(gprs[2] == 9);
  extract(9, 7, 7);
  issue_pair(addi(3, 0, 32), vi(23, 0, 2, 4, 10)); // unrelated li; vmv.v.x
  CHECK(gprs[3] == 32);
  extract(10, 7, 9);

  // VRF and immediate fields must not become accidental GPR dependencies.
  issue_pair(vi(0, 8, 10, 0, 11), addi(11, 0, 33)); // vadd.vv v11,v8,v10; li x11
  CHECK(gprs[11] == 33);
  extract(11, 7, 16);
  issue_pair(addi(8, 0, 34), vi(0, 8, 2, 3, 12)); // li x8; vadd.vi v12,v8,2
  extract(12, 7, 9);
  issue_pair(vi(37, 8, 2, 6, 13), addi(2, 0, 5)); // vmul.vx snapshots x2=9
  CHECK(gprs[2] == 5);
  extract(13, 7, 63);

  // A vector GPR result can accompany an unrelated write, but its RAW/WAW hazards remain.
  issue_pair(vi(16, 13, 0, 2, 6), addi(4, 0, 35)); // vmv.x.s x6,v13; li x4
  run(120);
  CHECK(gprs[6] == 63 && gprs[4] == 35);
  const auto raw = next_pc;
  stream({vi(16, 13, 0, 2, 6), addi(4, 6, 1)});
  CHECK(memory_cycles.at(raw + 4) > memory_cycles.at(raw));
  CHECK(gprs[4] == 64);
  const auto waw = next_pc;
  stream({vi(16, 13, 0, 2, 6), addi(6, 0, 36)});
  CHECK(memory_cycles.at(waw + 4) > memory_cycles.at(waw));
  run(60);
  CHECK(gprs[6] == 36);
  send(addi(4, 6, 1));
  CHECK(gprs[4] == 37);

  // Rejecting the older vector suppresses the scalar and all VRF effects.
  send(addi(2, 0, 4));
  send(vi(23, 0, 2, 4, 14));
  run(20);
  send(addi(2, 0, 5));
  const auto scalar_before = gprs[3];
  const auto older_fault = next_pc;
  resolve_pc = older_fault;
  resolve_disposition = 1;
  stream({vi(23, 0, 2, 4, 14), addi(3, 0, 41)}, true);
  resolve_pc = 0;
  CHECK(last_fault_pc == older_fault && gprs[3] == scalar_before);
  CHECK(!retired_pcs.contains(older_fault) && !retired_pcs.contains(older_fault + 4));
  extract(14, 7, 4);

  // A younger scalar trap drains the already accepted older vector GPR result.
  const auto younger_fault = next_pc;
  resolve_pc = younger_fault + 4;
  stream({vi(16, 13, 0, 2, 6), addi(3, 0, 42)}, true);
  resolve_pc = 0;
  CHECK(last_fault_pc == younger_fault + 4 && gprs[6] == 63 && gprs[3] == scalar_before);
  CHECK(retired_pcs.contains(younger_fault) && !retired_pcs.contains(younger_fault + 4));

  // Younger scalar replay preserves the vector's old-source snapshot and accepted ownership.
  const auto scalar_replay = next_pc;
  resolve_pc = scalar_replay + 4;
  resolve_disposition = 2;
  stream({vi(23, 0, 2, 4, 14), addi(2, 0, 6)}, false, true);
  CHECK(retirement_cycles.at(scalar_replay) < retirement_cycles.at(scalar_replay + 4));
  CHECK(gprs[2] == 6);
  extract(14, 7, 5);
  resolve_pc = next_pc;
  stream({vi(23, 0, 2, 4, 14), addi(2, 0, 7)}, false, true);
  CHECK(gprs[2] == 7);
  extract(14, 7, 6);
  const auto vector_replay = next_pc;
  resolve_pc = vector_replay + 4;
  stream({addi(3, 0, 44), vi(23, 0, 2, 4, 15)}, false, true);
  CHECK(retirement_cycles.at(vector_replay) < retirement_cycles.at(vector_replay + 4));
  CHECK(gprs[3] == 44);
  extract(15, 7, 7);

  // Dependent divides fill descriptor credit without any vector memory traffic.
  // A full-queue replay must not retire the younger li or duplicate an accepted vector.
  setvl(bytes == 8 ? 3 : 2, 2);
  send(addi(2, 0, 2));
  send(addi(4, 0, 128));
  send(vi(23, 0, 4, 4, 8));
  run(20);
  stream({vi(32, 8, 2, 6, 9), addi(3, 0, 100),
          vi(32, 9, 2, 6, 10), addi(3, 0, 101),
          vi(32, 10, 2, 6, 11), addi(3, 0, 102),
          vi(32, 11, 2, 6, 12), addi(3, 0, 103),
          vi(32, 12, 2, 6, 13), addi(3, 0, 104),
          vi(32, 13, 2, 6, 14), addi(3, 0, 105),
          vi(32, 14, 2, 6, 15), addi(3, 0, 106),
          vi(32, 15, 2, 6, 16), addi(3, 0, 107),
          vi(32, 16, 2, 6, 17), addi(3, 0, 108),
          vi(32, 17, 2, 6, 18), addi(3, 0, 109)}, false, true);
  CHECK(gprs[3] == 109);
  extract(18, 7, 0);
}
void paired_vector_stores() {
  run(120);
  setvl(bytes == 8 ? 3 : 2, 2);
  send(addi(1, 0, 0x500));
  send(addi(2, 0, 7));
  const auto move = vi(23, 0, 2, 4, 8);
  const auto store = [](unsigned offset) {
    return ((offset >> 5) << 25) | (2 << 20) | (1 << 15) |
        ((bytes == 8 ? 3 : 2) << 12) | ((offset & 31) << 7) | 0x23u;
  };

  // Both age orders: hit/slow admission, either store rejection, and either
  // owner's fault/replay. Younger stores cannot precede descriptor acceptance.
  for (bool vector_first : {false, true}) for (unsigned mode = 0; mode < 8; ++mode) {
    run(80);
    const auto base = next_pc;
    const auto vector_pc = base + (vector_first ? 0 : 4);
    const auto store_pc = base + (vector_first ? 4 : 0);
    const auto admissions = vector_trace::admissions;
    const auto writes = stores;
    store_offers[0x500].clear();
    store_writes[0x500].clear();
    slow = mode == 1 || mode == 3 || mode == 4;
    commit_block_until = mode == 2 ? cycle + 24 : 0;
    request_block_until = mode == 3 ? cycle + 24 : 0;
    fault_at = mode == 4 ? 0x500 : 0;
    resolve_pc = mode >= 5 ? (mode == 7 ? store_pc : vector_pc) : 0;
    resolve_disposition = mode == 5 ? 1 : 2;
    authorization_owner = vector_first ? vector_pc : 0;
    guarded_store_address = 0x500;
    stream({vector_first ? move : store(0), vector_first ? store(0) : move}, mode == 4 || mode == 5,
           mode == 2 || mode == 3 || mode >= 6);
    CHECK(memory_cycles.at(base) == memory_cycles.at(base + 4));
    CHECK(vector_trace::admissions == admissions + (mode == 5 || (mode == 4 && !vector_first) ? 0 : 1));
    CHECK(stores == writes + (mode == 4 || (mode == 5 && vector_first) ? 0 : 1));
    if (mode == 4 || mode == 5) {
      CHECK(last_fault_pc == (mode == 4 ? store_pc : vector_pc));
      CHECK(retired_pcs.contains(vector_pc) == (mode == 4 && vector_first));
      CHECK(retired_pcs.contains(store_pc) == (mode == 5 && !vector_first));
    } else {
      CHECK(words[0x500] == 7);
      if (mode < 2) {
        CHECK(retirement_cycles.at(base) == retirement_cycles.at(base + 4));
        CHECK(retirement_lanes.at(base + 4) == 1);
      } else {
        const auto replay = std::find_if(observed_redirects.begin(), observed_redirects.end(),
            [base](const auto &redirect) { return redirect.disposition == 2 &&
                (redirect.pc == base || redirect.pc == base + 4); });
        CHECK(replay != observed_redirects.end());
        CHECK(replay->target == (mode == 6 ? vector_pc : store_pc));
      }
    }
    if (mode == 5 && vector_first) CHECK(store_offers[0x500].empty());
    resolve_pc = authorization_owner = guarded_store_address = fault_at = 0;
    request_block_until = commit_block_until = 0;
    slow = false;
  }

  // A real GPR dependency still splits and forwards the vector scalar result;
  // descriptor admission alone must not expose the stale store-data register.
  run(80);
  send(addi(2, 0, 99));
  const auto dependent = next_pc;
  stream({vi(16, 8, 0, 2, 2), store(0)});
  CHECK(memory_cycles.at(dependent) < memory_cycles.at(dependent + 4));
  CHECK(words[0x500] == 7 && gprs[2] == 7);

  // Fill real descriptor credit behind dependent divides. Rejected descriptors
  // must not even offer their younger store, and retry must not duplicate either.
  send(addi(2, 0, 2));
  send(addi(4, 0, 128));
  send(vi(23, 0, 4, 4, 8));
  run(40);
  for (unsigned index = 0; index < 10; ++index) {
    store_offers[0x500 + index * bytes].clear();
    store_writes[0x500 + index * bytes].clear();
  }
  const auto base = next_pc;
  const auto rejections = vector_trace::rejections;
  stream({vi(32, 8, 2, 6, 9), store(0),
          vi(32, 9, 2, 6, 10), store(bytes),
          vi(32, 10, 2, 6, 11), store(2 * bytes),
          vi(32, 11, 2, 6, 12), store(3 * bytes),
          vi(32, 12, 2, 6, 13), store(4 * bytes),
          vi(32, 13, 2, 6, 14), store(5 * bytes),
          vi(32, 14, 2, 6, 15), store(6 * bytes),
          vi(32, 15, 2, 6, 16), store(7 * bytes),
          vi(32, 16, 2, 6, 17), store(8 * bytes),
          vi(32, 17, 2, 6, 18), store(9 * bytes)}, false, true);
  CHECK(vector_trace::rejections > rejections);
  for (unsigned index = 0; index < 10; ++index) {
    const auto pc = base + index * 8;
    const auto address = 0x500 + index * bytes;
    CHECK(vector_trace::admission_counts.at(pc) == 1);
    CHECK(store_writes.at(address).size() == 1 && words[address] == 2);
    CHECK(!store_offers.at(address).empty());
    for (auto offered : store_offers.at(address))
      CHECK(offered >= retirement_cycles.at(pc));
  }
  extract(18, 7, 0);

  // The same prefix applies in reverse: a hit store can precede a move,
  // but a younger vector memory descriptor must wait for scalar memory drain.
  run(80);
  issue_pair(store(0), move);
  for (bool vector_first : {false, true}) {
    run(80);
    const auto pc = next_pc;
    const auto load = 0x02000007u | (1 << 15) | (8 << 7);
    stream({vector_first ? load : store(0), vector_first ? store(0) : load}, false, true);
    CHECK(memory_cycles.at(pc) == memory_cycles.at(pc + 4));
    CHECK(retirement_cycles.at(pc) < retirement_cycles.at(pc + 4));
  }
}
void conflict_based_pairing() {
  run(120);
  setvl(0, 4);
  send(addi(1, 0, 0x400));
  send(addi(2, 0, 7));
  words[0x400] = words[0x1400] = 0x15151515;
  fast_enabled = safe = true;
  const auto vector_move = vi(23, 0, 2, 4, 8);
  const auto vector_load = 0x02000007u | (1 << 15) | (8 << 7);
  const auto vector_store = 0x02000027u | (1 << 15) | (8 << 7);
  const auto configure = vsetivli(10, 4, 0);
  // Separate consecutive packets put vset in WB while the branch corrects MEM.
  // The live stream's branch checkpoint must win, not the older vset checkpoint.
  const auto history_base = next_pc;
  next_pc += 8;
  history_repair_branch_pc = history_base + 4;
  const auto repairs_before = configuration_branch_repairs;
  for (unsigned sent = 0, wait = 0; !retired_pcs.contains(history_base + 4); ++wait) {
    CHECK(wait < 100);
    instructions_in = {};
    instructions_in.pvalid = sent < 2;
    instructions_in.pbits.pcount = 1;
    auto &entry = instructions_in.pbits.pentries[0];
    entry.ppc = history_base + sent * 4;
    entry.psequential_upc = entry.ppc + 4;
    entry.pinstruction = entry.praw_uinstruction = sent == 0 ? configure : 0x00001463;
    entry.pdirection.phistory = sent == 0 ? 3 : 5;
    entry.pdirection.pvalid = sent == 1;
    entry.pdirection.ptaken = sent == 1;
    entry.pprediction.pvalid = sent == 1;
    entry.pprediction.ptarget = history_base + 12;
    settle();
    const bool accepted = instructions_in.pvalid && instructions_out.pready;
    step();
    if (accepted) ++sent;
  }
  instructions_in.pvalid = 0;
  CHECK(configuration_branch_repairs == repairs_before + 1);
  CHECK(nonflushing_configurations > 0);
  history_repair_branch_pc = 0;
  run(20);
  const auto load = (1 << 15) | (2 << 12) | (12 << 7) | 3u;
  const auto multiply = (1u << 25) | (2 << 20) | (2 << 15) | (12 << 7) | 0x33;
  const auto divide = multiply | (4 << 12);
  const std::array<std::uint32_t, 4> ordinary = {
    addi(0, 0, 0), addi(12, 0, 31), 0x00001463, 0x0040006f
  }; // nop, li, not-taken branch, and jal x0 to its sequential successor

  for (auto vector : {vector_move, vector_load, vector_store}) {
    for (auto scalar : ordinary) {
      for (bool vector_first : {false, true}) {
        run(40);
        const auto admissions = vector_trace::admissions;
        issue_pair(vector_first ? vector : scalar, vector_first ? scalar : vector);
        CHECK(vector_trace::admissions == admissions + 1);
      }
    }
  }
  fast_enabled = false;
  // Configuration owns the CSR command, not either scalar execution lane.
  for (auto scalar : {ordinary[0], ordinary[2], ordinary[3], load, multiply, divide}) {
    for (bool config_first : {false, true}) {
      run(80);
      issue_pair(config_first ? configure : scalar, config_first ? scalar : configure);
      run(40);
      CHECK(gprs[10] == 4);
      if (scalar == multiply) CHECK(gprs[12] == 49);
      if (scalar == divide) CHECK(gprs[12] == 1);
    }
  }
  // A descriptor and scalar service have separate admission resources.
  // Either age order uses the same WB acceptance prefix, not pair permissions.
  for (auto scalar : {load, multiply, divide}) {
    for (bool vector_first : {false, true}) {
      run(80);
      issue_pair(vector_first ? vector_move : scalar, vector_first ? scalar : vector_move);
    }
  }
  // A rejected older divider must suppress a younger descriptor just like
  // rejected memory. A different rd avoids waiting on the first result's WAW.
  run(80);
  send(addi(14, 0, -1));
  const auto busy_divide = (1u << 25) | (2 << 20) | (14 << 15) | (5 << 12) | (12 << 7) | 0x33;
  send(busy_divide);
  const auto retry = next_pc;
  stream({(busy_divide & ~(31u << 7)) | (13 << 7), vector_move}, false, true);
  CHECK(vector_trace::admission_counts.at(retry + 4) == 1);
  run(100);
  CHECK(gprs[13] == (bytes == 8 ? UINT64_MAX : UINT32_MAX) / 7);
  for (auto vector : {vector_move, configure}) {
    const auto base = next_pc;
    stream({vector, vector});
    CHECK(memory_cycles.at(base) < memory_cycles.at(base + 4));
  }

  if constexpr (bytes == 8) {
    const auto convert = (0x69u << 25) | (2 << 15) | (3 << 7) | 0x53; // fcvt.d.w f3,x2
    const auto read = (0x61u << 25) | (3 << 15) | (12 << 7) | 0x53; // fcvt.w.d x12,f3
    for (auto vector : {vector_move, vector_load, configure}) {
      for (bool vector_first : {false, true}) {
        run(80);
        fast_enabled = vector == vector_load;
        issue_pair(vector_first ? vector : convert, vector_first ? convert : vector);
        run(40);
        send(read);
        CHECK(gprs[12] == 7);
      }
    }
    fast_enabled = false;

    // Variable FP uses the same ordered admission, including an older busy
    // divide rejecting its younger descriptor without accepting it twice.
    send(addi(14, 0, 3));
    send((0x69u << 25) | (14 << 15) | (4 << 7) | 0x53); // fcvt.d.w f4,x14
    const auto fp_divide = (0x0du << 25) | (4 << 20) | (3 << 15) | (5 << 7) | 0x53;
    const auto retry_divide = (fp_divide & ~(31u << 7)) | (6 << 7);
    for (bool vector_first : {false, true}) {
      run(80);
      issue_pair(vector_first ? vector_move : fp_divide, vector_first ? fp_divide : vector_move);
      const auto retry = next_pc;
      stream({retry_divide, vector_move}, false, true);
      CHECK(vector_trace::admission_counts.at(retry + 4) == 1);
      run(80);
      send((0x61u << 25) | (6 << 15) | (12 << 7) | 0x53);
      CHECK(gprs[12] == 2);
    }

    setvl(3, 2);
    send(convert);
    send(vi(23, 0, 3, 5, 8)); // vfmv.v.f v8,f3 = 7.0
    run(40);
    const auto vector_read = vi(23, 0, 3, 5, 9);
    const auto vector_write = vi(16, 8, 0, 1, 3); // vfmv.f.s f3,v8
    // Two FPR read bundles conflict even with different source registers.
    send((0x69u << 25) | (2 << 15) | (4 << 7) | 0x53);
    const auto read_other = (0x61u << 25) | (4 << 15) | (12 << 7) | 0x53;
    const auto reads = next_pc;
    stream({vector_read, read_other});
    CHECK(memory_cycles.at(reads) < memory_cycles.at(reads + 4) && gprs[12] == 7);
    // A vector FPR writer does not use that read bundle, but its RAW/WAW
    // hazards must still prevent a stale read or an out-of-order overwrite.
    const auto raw = next_pc;
    stream({vector_write, read});
    CHECK(memory_cycles.at(raw) < memory_cycles.at(raw + 4) && gprs[12] == 7);
    send(addi(2, 0, 3));
    const auto waw = next_pc;
    stream({vector_write, convert});
    CHECK(memory_cycles.at(waw) < memory_cycles.at(waw + 4));
    send(read);
    CHECK(gprs[12] == 3);
    const auto reverse_raw = next_pc;
    stream({convert, vector_read});
    CHECK(memory_cycles.at(reverse_raw) < memory_cycles.at(reverse_raw + 4));
    extract(9, 7, UINT64_C(0x4008000000000000));
    // Distinct FPR operands are not an instruction-class conflict.
    issue_pair(vector_write, read_other);
    run(40);
    CHECK(gprs[12] == 7);
    issue_pair(read_other, vector_write);
    run(40);
    CHECK(gprs[12] == 7);
    const auto vector_zero = vi(16, 8, 0, 1, 0); // f0 is writable, unlike x0
    const auto read_zero = (0x61u << 25) | (12 << 7) | 0x53;
    const auto convert_zero = (0x69u << 25) | (2 << 15) | 0x53;
    const auto zero_raw = next_pc;
    stream({vector_zero, read_zero});
    CHECK(memory_cycles.at(zero_raw) < memory_cycles.at(zero_raw + 4) && gprs[12] == 7);
    const auto zero_waw = next_pc;
    stream({vector_zero, convert_zero});
    CHECK(memory_cycles.at(zero_waw) < memory_cycles.at(zero_waw + 4));
    send(read_zero);
    CHECK(gprs[12] == 3);

    // Fault/replay still qualifies FP authorization, including beside vset.
    const auto old_fault = next_pc;
    resolve_pc = old_fault;
    resolve_disposition = 1;
    stream({configure, convert}, true);
    resolve_pc = 0;
    CHECK(!retired_pcs.contains(old_fault + 4));
    const auto retry = next_pc;
    resolve_pc = retry;
    resolve_disposition = 2;
    stream({convert, configure}, false, true);
    CHECK(retired_pcs.contains(retry) && retired_pcs.contains(retry + 4));
    send(read);
    CHECK(gprs[12] == 3);
  }
  run(80);
}

void enter_guest(unsigned host_status = 0x6600, unsigned guest_status = 0x6600) {
  send(addi(2, 0, guest_status >> 8));
  send((8 << 20) | (2 << 15) | (1 << 12) | (2 << 7) | 0x13); // slli x2,x2,8
  send(0x20011073); // csrw vsstatus,x2
  send(addi(2, 0, (host_status | 0x800) >> 8)); // MPP=S
  send((8 << 20) | (2 << 15) | (1 << 12) | (2 << 7) | 0x13);
  send(addi(1, 0, 1));
  send((39 << 20) | (1 << 15) | (1 << 12) | (1 << 7) | 0x13); // MPV
  send((2 << 20) | (1 << 15) | (6 << 12) | (1 << 7) | 0x33); // or x1,x1,x2
  send(0x30009073); // csrw mstatus,x1
  send(addi(1, 0, 0x500));
  send(0x34109073); // mepc
  send(0x30200073); // mret into VS
}
void vector_memory_branch_pairs() {
  run(120);
  setvl(0, 4);
  send(addi(1, 0, 0x700));
  send(addi(2, 0, 0x33));
  send(vi(23, 0, 2, 4, 8));
  send(vi(20, 0, 17, 2, 16)); // byte indices for the precise path
  send(addi(14, 0, 1));
  run(40);
  // Mode 0 certifies in MEM; 1 delays precise authorization; 2 rejects the
  // first WB attempt; 3 faults the second element after accepting the first.
  for (unsigned mode = 0; mode < 4; ++mode) {
    for (bool store : {false, true}) {
      for (bool taken : {false, true}) {
        for (bool predicted : {false, true}) {
          setvl(0, 4);
          send(addi(12, 0, 9));
          send(addi(13, 0, 10));
          const auto base = next_pc;
          next_pc += 16;
          const auto admissions_before = vector_trace::admissions;
          const auto stores_before = stores, faults_before = fault_count;
          const auto redirects_before = observed_redirects.size();
          fast_enabled = mode == 0;
          safe = false;
          slow = true;
          fast_current = true;
          fault_at = mode == 3 ? 0x701 : 0;
          resolve_pc = mode == 2 ? base : 0;
          resolve_disposition = 2;
          request_block_until = cycle + (mode == 1 ? 24 : 0);
          authorization_owner = base;
          const unsigned indexed = mode == 0 ? 0 : (1u << 26) | (16 << 20);
          const std::array<std::uint32_t, 4> program = {
            indexed | (1u << 25) | (1 << 15) | (8 << 7) | (store ? 0x27u : 7u),
            (14 << 15) | (unsigned(taken) << 12) | 0x463u, // bne/beq x14,x0,+8
            addi(12, 0, 77), addi(13, 0, 88)
          };
          unsigned sent = 0;
          recovery_pc = replay_pc = 0;
          for (unsigned wait = 0; ; ++wait) {
            CHECK(wait < 3000);
            instructions_in = {};
            instructions_in.pvalid = sent < program.size();
            const unsigned count = sent == 1 && predicted ? 1 :
                std::min<unsigned>(2, program.size() - sent);
            instructions_in.pbits.pcount = count;
            for (unsigned lane = 0; lane < count; ++lane) {
              const auto index = sent + lane;
              auto &entry = instructions_in.pbits.pentries[lane];
              entry.ppc = base + 4 * index;
              entry.psequential_upc = entry.ppc + 4;
              entry.pinstruction = entry.praw_uinstruction = program[index];
              if (index == 1) {
                entry.pprediction.pvalid = predicted;
                entry.pprediction.ptarget = base + 12;
                entry.pdirection.pvalid = 1;
                entry.pdirection.ptaken = predicted;
                entry.pdirection.phistory = 5;
              }
            }
            settle();
            const bool accepted = instructions_in.pvalid && instructions_out.pready;
            const auto redirects = observed_redirects.size();
            step();
            if (accepted) {
              if (sent <= 1 && sent + count > 1 && predicted) sent = 3;
              else sent += count;
            }
            if (observed_redirects.size() != redirects) {
              const auto &redirect = observed_redirects.back();
              if (redirect.disposition != 1) {
                CHECK(redirect.target >= base && redirect.target < next_pc);
                sent = (redirect.target - base) / 4;
                if (redirect.disposition == 2) resolve_pc = 0;
              }
              recovery_pc = replay_pc = 0;
            }
            if (fault_count != faults_before || retired_pcs.contains(base + 12)) break;
          }
          instructions_in.pvalid = 0;
          run(50);
          CHECK(memory_cycles.at(base) == memory_cycles.at(base + 4));
          CHECK(vector_trace::admissions == admissions_before + 1);
          CHECK(fault_count == faults_before + (mode == 3));
          if (mode == 3) {
            CHECK(last_fault_pc == base && last_tval == 0x701);
            CHECK(last_cause == (store ? 15 : 13));
            CHECK(!retired_pcs.contains(base) && !retired_pcs.contains(base + 4));
            CHECK(!trained_branches.contains(base + 4));
            CHECK(gprs[12] == 9 && gprs[13] == 10);
          } else {
            CHECK(retired_pcs.contains(base) && retired_pcs.contains(base + 4));
            CHECK(retirement_cycles.at(base + 4) >= retirement_cycles.at(base));
            CHECK(trained_branches[base + 4] == 1);
            if (gprs[12] != (taken ? 9 : 77) || gprs[13] != 88)
              std::cerr << "vector/branch mode=" << mode << " store=" << store
                        << " taken=" << taken << " predicted=" << predicted
                        << " x12=" << gprs[12] << " x13=" << gprs[13] << '\n';
            CHECK(gprs[12] == (taken ? 9 : 77) && gprs[13] == 88);
            CHECK(retired_pcs.contains(base + 8) == !taken);
            if (mode == 0)
              CHECK(retirement_cycles.at(base) == retirement_cycles.at(base + 4));
          }
          CHECK(stores == stores_before + (store ? (mode == 0 || mode == 3 ? 1 : 4) : 0));
          bool corrected = false, replayed = false;
          for (auto index = redirects_before; index < observed_redirects.size(); ++index) {
            const auto &redirect = observed_redirects[index];
            corrected |= redirect.disposition == 0 && redirect.pc == base + 4;
            replayed |= redirect.disposition == 2;
          }
          CHECK(corrected == (taken != predicted));
          if (mode == 1 || mode == 2) CHECK(replayed);
          fault_at = resolve_pc = authorization_owner = request_block_until = 0;
          fast_enabled = slow = false;
        }
      }
    }
  }
  safe = true;
}

void speculative_scalar_authorization() {
  run(120);
  safe = fast_enabled = false;
  slow = false;
  setvl(0, 4);
  send(addi(1, 0, 0x500));
  send(addi(2, 0, 0x33));
  send(vi(23, 0, 2, 4, 8)); // vmv.v.x v8,x2
  send(vi(20, 0, 17, 2, 16)); // vid.v v16: four byte indices
  run(40);
  const auto indexed_store = (1u << 26) | (1u << 25) | (16 << 20) |
                             (1 << 15) | (8 << 7) | 0x27;
  const auto scalar_store = (2 << 20) | (1 << 15) | (2 << 12) | (8 << 7) | 0x23;

  // One speculative rejection parks the retry until authorization rather than
  // repeatedly refetching. Accepted vector work and stores survive unchanged.
  for (unsigned delay : {0u, 30u, 100u}) {
    const auto base = next_pc;
    const auto stores_before = stores, admitted_before = vector_trace::admissions;
    const auto redirects_before = redirect_count;
    authorization_owner = base;
    guarded_store_address = 0x508;
    slow = true;
    request_block_until = cycle + delay;
    stream({indexed_store, addi(12, 0, 91), addi(13, 12, 1),
            0x00001463, // bne zero,zero,+8: independent, correctly not taken
            scalar_store, addi(14, 13, 1)}, false, true);
    run(100);
    CHECK(memory_cycles.at(base + 4) < retirement_cycles.at(base));
    for (unsigned i = 1; i < 6; ++i)
      CHECK(retirement_cycles.at(base + 4 * i) >= retirement_cycles.at(base));
    CHECK(vector_trace::admissions == admitted_before + 1);
    // One retry for unresolved authorization, plus any service suffix recovery;
    // the bound is independent of how long the vector request is blocked.
    CHECK(redirect_count <= redirects_before + 3);
    CHECK(stores == stores_before + 5 && gprs[12] == 91 && gprs[13] == 92 && gprs[14] == 93);
    CHECK((words[0x500] & UINT64_C(0xffffffff)) == UINT64_C(0x33333333));
    CHECK((words[0x508] & UINT64_C(0xffffffff)) == 0x33);
    authorization_owner = guarded_store_address = request_block_until = 0;
    slow = false;
  }

  // Sweep scalar arrival across vector authorization, including an empty WB,
  // a same-cycle merge, and two live scalar slots competing for retirement.
  const auto merges_before = authorization_merges;
  for (unsigned delay = 0; delay <= 16; ++delay) {
    for (bool pair : {false, true}) {
      const auto base = next_pc;
      next_pc += 4;
      authorization_owner = base;
      instructions_in = {};
      instructions_in.pvalid = 1;
      instructions_in.pbits.pcount = 1;
      auto &entry = instructions_in.pbits.pentries[0];
      entry.ppc = base;
      entry.psequential_upc = base + 4;
      entry.pinstruction = entry.praw_uinstruction = indexed_store;
      for (;;) {
        settle();
        const bool accepted = instructions_out.pready;
        step();
        if (accepted) break;
      }
      instructions_in.pvalid = 0;
      run(delay);
      if (pair)
        stream({addi(15, 0, 35), addi(16, 0, 36)}, false, true, false);
      else
        stream({addi(15, 0, 35)}, false, true, false);
      CHECK(retired_pcs.contains(base) && gprs[15] == 35);
      if (pair) CHECK(gprs[16] == 36);
      run(30);
      authorization_owner = 0;
    }
  }
  CHECK(authorization_merges > merges_before);

  // The vector's second element faults after an irrevocable first store.
  // Younger scalar stores, arithmetic, multiply, and exceptions cannot escape.
  send(addi(1, 0, 0x600));
  send(addi(12, 0, 17));
  send(addi(13, 0, 18));
  if constexpr (bytes == 8) {
    send(addi(2, 0, 3));
    send((0x69u << 25) | (2 << 15) | (1 << 7) | 0x53); // fcvt.d.w f1,x2
    send((1u << 25) | (1 << 20) | (1 << 15) | (2 << 7) | 0x53); // f2 = 6
  }
  const auto base = next_pc;
  const auto stores_before = stores, admissions_before = vector_trace::admissions;
  authorization_owner = base;
  guarded_store_address = 0x608;
  words[0x600] = words[0x608] = 0;
  fault_at = 0x601;
  slow = true;
  stream({indexed_store, addi(12, 0, 99),
          (1u << 25) | (2 << 20) | (2 << 15) | (13 << 7) | 0x33,
          scalar_store, bytes == 8 ? (1u << 25) | (2 << 20) | (2 << 15) | (2 << 7) | 0x53 : addi(14, 0, 99),
          0}, true, true);
  fault_at = 0;
  authorization_owner = guarded_store_address = 0;
  run(100);
  CHECK(last_fault_pc == base && last_cause == 15 && last_tval == 0x601);
  CHECK(gprs[12] == 17 && gprs[13] == 18 && words[0x608] == 0);
  CHECK(stores == stores_before + 1 && (words[0x600] & 255) == 0x33);
  CHECK(vector_trace::admissions == admissions_before + 1);
  for (unsigned i = 0; i < 6; ++i)
    CHECK(!retired_pcs.contains(base + 4 * i));
  slow = false;
  send(0x008023f3); // csrr x7,vstart: preserve the failed element, not scalar fault
  CHECK(gprs[7] == 1);
  if constexpr (bytes == 8) {
    send((0x61u << 25) | (2 << 15) | (15 << 7) | 0x53); // fcvt.w.d x15,f2
    CHECK(gprs[15] == 6);
  }

  // Interrupts wait behind authorization and drain accepted responses before
  // taking the oldest remaining scalar boundary, not the vector's original PC.
  safe = true;
  setvl(0, 4);
  send(addi(1, 0, 0x700));
  send(addi(2, 0, 128));
  send(0x30411073); // csrw mie,x2: MTIE
  send(addi(2, 0, 8));
  send(0x30012073); // csrs mstatus,x2: MIE
  authorization_owner = next_pc;
  interrupt_after_admission = true;
  slow = true;
  request_block_until = cycle + 20;
  stream({indexed_store, addi(12, 0, 37), addi(13, 0, 38)}, true, true);
  CHECK(last_cause == ((UINT64_C(1) << (bytes * 8 - 1)) | 7));
  CHECK(last_fault_pc == authorization_owner + (retired_pcs.contains(authorization_owner + 4) ? 8 : 4));
  interrupt_after_admission = false;
  interrupts.pmachine_utimer = 0;
  authorization_owner = request_block_until = 0;
  slow = false;
  run(30);
  safe = true;
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
             bool require_progress) {
  send(vi(16, vector_register, 0, 2, rd));
  // Sustained paired scalar traffic competes with the vector return for the 2W calendar.
  scalar_burst();
  if (require_progress) CHECK(gprs[rd] == value);
  for (unsigned wait = 0; gprs[rd] != value && wait < 2000; ++wait) step();
  if (gprs[rd] != value)
    std::cerr << "extract v" << vector_register << " at PC " << std::hex << next_pc
              << ": got " << gprs[rd] << ", expected " << value << std::dec << '\n';
  CHECK(gprs[rd] == value);
}
int main() {
  return run_test([] {
    rheg::graph().bind_manifest(rheg_generated::manifest());
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
      // Half arithmetic shares the native FP service with scalar D; scalar
      // decode remains Zfhmin while vector execution needs full half arithmetic.
      setvl(1, 8);
      send(0x00004137); // lui x2,4
      send(addi(2, 2, -1024)); // 0x3c00 = half 1.0
      send(vi(23, 0, 2, 4, 8)); // vmv.v.x v8,x2
      send(vi(0, 8, 8, 1, 9)); // vfadd.vv v9,v8,v8 = half 2.0
      send((1u << 25) | (1 << 20) | (1 << 15) | (2 << 7) | 0x53); // competing fadd.d
      extract(9, 7, 0x4000, true);
      send(vi(18, 9, 12, 1, 10)); // vfwcvt.f.f.v v10,v9
      send(vi(18, 10, 20, 1, 12)); // vfncvt.f.f.w v12,v10
      extract(12, 7, 0x4000, true);
      send(0x00101073); // clear fflags
      send(vi(23, 0, 0, 4, 13)); // half zero
      send(vi(32, 8, 13, 1, 14)); // vfdiv.vv v14,v8,v13 = +infinity
      send(0x001023f3); // read before the result-dependent extract: wait for vector flags
      CHECK(gprs[7] == 8); // DZ
      extract(14, 7, 0x7c00, true);
      // Guest vector loads use ordinary VS translation state; accepted work
      // must finish before an ECALL changes privilege or a CSR can expose it.
      enter_guest();
      setvl(1, 8);
      send(addi(1, 0, 0x100));
      slow = true;
      const auto saved_response_delay = response_delay;
      response_delay = 80;
      send(0x02005007 | (1 << 15) | (16 << 7)); // vle16.v v16,(x1)
      for (unsigned wait = 0; responses.empty() && wait < 100; ++wait) step();
      CHECK(!responses.empty()); // delayed accepted work really is outstanding
      const auto redirects_before_read = redirect_count;
      send(0xc22023f3); // csrr x7,vlenb must not drain that older vector load
      CHECK(gprs[7] == 16 && !responses.empty() &&
            redirect_count == redirects_before_read);
      send(0x00000073, true); // VS ecall drains the vector load, then enters M
      CHECK(last_cause == 10 && responses.empty());
      response_delay = saved_response_delay;
      slow = false;
      extract(16, 7, 0x1515);

      // Both host and guest FS/VS gates apply to guest vector instructions.
      for (const auto status : {0x6000u, 0x0600u}) {
        for (bool host : {false, true}) {
          enter_guest(host ? status : 0x6600, host ? 0x6600 : status);
          send(status == 0x6000 ? vi(23, 0, 0, 4, 17) : vi(0, 8, 8, 1, 17), true);
          CHECK(last_cause == 2);
        }
      }
      enter_guest();
      setvl(1, 8);
      send(vi(0, 8, 8, 1, 17)); // enabled VS FP arithmetic
      extract(17, 7, 0x4000, true);
      safe = false;
      fault_at = 0x100;
      guest_fault = true;
      send(addi(1, 0, 0x100));
      slow = true;
      send(0x02005007 | (1 << 15) | (18 << 7), true);
      CHECK(last_cause == 21 && last_tval == 0x100);
      fault_at = 0;
      guest_fault = false;
      send(0x34b023f3); // mtval2 preserves the guest physical address >> 2
      CHECK(gprs[7] == (0x23450 >> 2));
      send(0x008023f3); // faulting vector element remains restartable
      CHECK(gprs[7] == 0);
    }
    CHECK(proof_count >= 3);
    CHECK(deferred_count >= 5);
    CHECK(stores >= 2);
    CHECK(fault_count == (bytes == 8 ? 7 : 1));
    vector_configuration_overlap();
    vector_memory_fast_path();
    paired_vector_configurations();
    paired_vector_arithmetic();
    paired_scalar_configurations();
    independent_vector_scalar_pairs();
    paired_vector_stores();
    conflict_based_pairing();
    vector_memory_branch_pairs();
    speculative_scalar_authorization();
    run(100);
    vector_trace::finish();
  });
}
