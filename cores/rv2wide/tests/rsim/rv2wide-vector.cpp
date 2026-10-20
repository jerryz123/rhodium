// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <deque>
#include <map>

using Request = std::remove_cvref_t<decltype(pipeline_out.prequest.pbits)>;
Request lookup_owner{}, commit_owner{};
bool lookup_live = false;
std::map<std::uint64_t, std::uint64_t> words;
struct Response { std::uint64_t data; unsigned due; };
std::deque<Response> responses;
std::deque<unsigned> scalar_bookings;
unsigned cycle = 0, completions = 0, checks = 0, releases = 0, stores = 0;
unsigned outcomes = 0, scalar_returns = 0, replay_budget = 0, response_delay = 5;
std::uint64_t scalar_value = 0;
bool slow_memory = false;

std::uint32_t vector_insn(unsigned funct6, unsigned vs2, unsigned vs1,
                          unsigned funct3, unsigned vd) {
  return funct6 << 26 | 1 << 25 | vs2 << 20 | vs1 << 15 | funct3 << 12 |
         vd << 7 | 0x57;
}
void write_word(const Request &request) {
  auto &value = words[request.paddress & ~UINT64_C(7)];
  for (unsigned byte = 0; byte < 8; ++byte)
    if ((request.pmask >> byte) & 1) {
      const auto mask = UINT64_C(255) << (byte * 8);
      value = (value & ~mask) | (request.pdata & mask);
    }
  ++stores;
}
void settle() {
  pipeline_in.presponse.pvalid = lookup_live;
  pipeline_in.presponse.pbits.poutcome = replay_budget ? 3 : slow_memory ? 0 : lookup_owner.paccess == 2 ? 2 : 1;
  pipeline_in.presponse.pbits.pdata = words[lookup_owner.paddress & ~UINT64_C(7)];
  pipeline_in.presponse.pbits.pguest = {};
  pipeline_in.pcommit_uready = 1;
  memory_in.prequest.pready = 1;
  memory_in.pfault.pvalid = 0;
  memory_in.pordered_ubusy = 0;
  memory_in.preservation_uvalid = 0;
  memory_in.pdrained = responses.empty();
  memory_in.presponse.pvalid = !responses.empty() && responses.front().due <= cycle;
  memory_in.presponse.pbits = responses.empty() ? 0 : responses.front().data;
  split_in.prequest.pready = 1;
  split_in.presponse.pvalid = 0;
  eval();
}
void step() {
  settle();
  if (!reset) {
    checks += checking;
    releases += released;
    completions += done;
    if (scalar_reserve)
      scalar_bookings.push_back(cycle + scalar_delay);
    if (outcome_valid) {
      CHECK(disposition == 0);
      ++outcomes;
    }
    if (scalar_valid) {
      CHECK(!scalar_bookings.empty() && scalar_bookings.front() == cycle);
      scalar_bookings.pop_front();
      CHECK(scalar_rd == 5);
      scalar_value = scalar_data;
      ++scalar_returns;
    }
    if (pipeline_out.pcommit)
      write_word(commit_owner);
    if (memory_in.presponse.pvalid && memory_out.presponse.pready)
      responses.pop_front();
    if (memory_out.prequest.pvalid && memory_in.prequest.pready) {
      const auto request = memory_out.prequest.pbits;
      if (request.paccess == 2)
        write_word(request);
      responses.push_back({words[request.paddress & ~UINT64_C(7)], cycle + response_delay});
    }
    CHECK(!split_out.prequest.pvalid);
  }
  const auto next_owner = pipeline_out.prequest.pbits;
  const bool next_live = pipeline_out.prequest.pvalid;
  if (lookup_live && replay_budget)
    --replay_budget;
  tick_model();
  commit_owner = lookup_owner;
  lookup_owner = next_owner;
  lookup_live = next_live;
  ++cycle;
  settle();
}
void launch(std::uint32_t insn, std::uint64_t source = 0, bool certified = false) {
  instruction = insn;
  scalar = source;
  pc += 4;
  certificate_valid = certified;
  request_valid = 1;
  for (unsigned wait = 0; ; ++wait) {
    settle();
    const bool accepted = request_ready;
    CHECK(wait < 500);
    step();
    if (accepted)
      break;
  }
  request_valid = 0;
  settle();
}
void drain() {
  for (unsigned wait = 0; active || !responses.empty(); ++wait) {
    CHECK(wait < 1000);
    step();
  }
  step();
}
int main() {
  return run_test([] {
    reset = 1;
    request_valid = certificate_valid = 0;
    precheck_safe = 1;
    vl = 16;
    vtype = 0;
    scalar = 0;
    pc = 0x4000;
    step(); step();
    reset = 0;
    // Shared SIMD/VRF state survives descriptor handoff into a dependent store.
    launch(vector_insn(23, 0, 1, 4, 8), 0x23); // vmv.v.x v8,x1
    launch(vector_insn(0, 8, 1, 4, 9), 7);    // vadd.vx v9,v8,x1
    launch(0x02000027 | (9 << 7), 0x1000);    // vse8.v v9,(x1)
    drain();
    CHECK(words[0x1000] == UINT64_C(0x2a2a2a2a2a2a2a2a));
    CHECK(words[0x1008] == UINT64_C(0x2a2a2a2a2a2a2a2a));
    CHECK(stores == 2 && checks == 1 && releases == 1);

    // Certified capture detaches from delayed memory without losing row hazards.
    words[0x2000] = UINT64_C(0x0807060504030211);
    words[0x2008] = UINT64_C(0x1817161514131211);
    slow_memory = true;
    response_delay = 30;
    launch(0x02000007 | (10 << 7), 0x2000, true); // vle8.v v10,(x1)
    launch(vector_insn(16, 10, 0, 2, 5));         // vmv.x.s x5,v10
    drain();
    CHECK(scalar_returns == 1 && scalar_value == 0x11);

    // An unsuccessful proof selects precise execution, not a macro fault.
    slow_memory = false;
    precheck_safe = false;
    const auto before_stores = stores;
    const auto before_outcomes = outcomes;
    replay_budget = 1;
    launch(0x02000027 | (9 << 7), 0x3000);
    drain();
    CHECK(stores == before_stores + 16 && outcomes == before_outcomes + 1);
    CHECK(words[0x3000] == UINT64_C(0x2a2a2a2a2a2a2a2a));
    CHECK(words[0x3008] == UINT64_C(0x2a2a2a2a2a2a2a2a));
    CHECK(completions == 6);
    CHECK(scalar_bookings.empty());
  });
}
