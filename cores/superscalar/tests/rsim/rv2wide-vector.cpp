// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <deque>
#include <map>

using Request = std::remove_cvref_t<decltype(pipeline_out.prequest.pbits.pmemory)>;
Request lookup_owner{}, commit_owner{};
bool lookup_live = false;
std::map<std::uint64_t, std::uint64_t> words;
using MemoryContext = std::remove_cvref_t<decltype(memory_out.prequest.pbits.pmemory.pcontext)>;
struct Response { std::uint64_t data; unsigned due; MemoryContext context; };
std::deque<Response> responses;
std::deque<unsigned> scalar_bookings;
unsigned cycle = 0, completions = 0, checks = 0, releases = 0, stores = 0;
unsigned outcomes = 0, scalar_returns = 0, replay_budget = 0, response_delay = 5;
std::uint64_t scalar_value = 0;
bool slow_memory = false;
std::deque<Response> row_replies;

template<class M> void row_port(M& m, bool sample) {
  if constexpr (requires { m.inputs.prow_umemory_uin; }) {
    auto& in = m.inputs.prow_umemory_uin;
    const auto& out = m.outputs().prow_umemory_uout;
    static_assert(sizeof(in.presponse.pbits)==8);
    if (!sample) {
      in.prequest.pready=1;
      in.presponse.pvalid=!row_replies.empty() && row_replies.front().due<=cycle;
      in.presponse.pbits=row_replies.empty() ? 0 : row_replies.front().data;
    } else {
      if (in.presponse.pvalid && out.presponse.pready) row_replies.pop_front();
      if (!out.prequest.pvalid) return;
      const auto& r=out.prequest.pbits;
      auto& value=words[r.paddress];
      if (r.pstore) {
        for (unsigned b=0;b<8;++b) if (r.pbyte_umask & (1u<<b)) {
          const auto mask=UINT64_C(255)<<(8*b);
          value=(value&~mask)|(r.pdata&mask);
        }
        ++stores;
      }
      row_replies.push_back({value,cycle+response_delay});
    }
  }
}

template<class M> void exact_translation(M& m) {
  if constexpr (requires { m.inputs.pexact_umapping; })
    m.inputs.pexact_umapping.ppage=m.outputs().pexact_uspan.pfirst>>12;
}

template<class M> void translate_page(M& m, std::uint64_t va) {
  if constexpr (requires { m.inputs.pwindow_upages; })
    m.inputs.pwindow_upages[0]=va>>12;
}

std::uint32_t vector_insn(unsigned funct6, unsigned vs2, unsigned vs1,
                          unsigned funct3, unsigned vd) {
  return funct6 << 26 | 1 << 25 | vs2 << 20 | vs1 << 15 | funct3 << 12 |
         vd << 7 | 0x57;
}
template <class T> void write_word(const T &request) {
  auto &value = words[request.paddress & ~UINT64_C(7)];
  for (unsigned byte = 0; byte < 8; ++byte)
    if ((request.pbyte_umask >> byte) & 1) {
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
  row_port(dut,false);
  memory_in.pdrained = responses.empty() && row_replies.empty();
  memory_in.presponse.pvalid = !responses.empty() && responses.front().due <= cycle;
  memory_in.presponse.pbits.pdata = responses.empty() ? 0 : responses.front().data;
  memory_in.presponse.pbits.pcontext = responses.empty() ? MemoryContext{} : responses.front().context;
  split_in.prequest.pready = 1;
  split_in.presponse.pvalid = 0;
  eval(); exact_translation(dut); eval();
}
void step() {
  settle();
  if (!reset) {
    row_port(dut,true);
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
      const auto request = memory_out.prequest.pbits.pmemory;
      if (request.paccess == 2)
        write_word(request);
      responses.push_back({words[request.paddress & ~UINT64_C(7)], cycle + response_delay, request.pcontext});
    }
    CHECK(!split_out.prequest.pvalid);
  }
  const auto next_owner = pipeline_out.prequest.pbits.pmemory;
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
  if (!certified && ((insn&127)==0x27 || (insn&127)==7)) translate_page(dut,source);
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

    // Across owner wrap, an older store must read the old v8, even with a
    // younger queued writer to v8. Its checked window is released once; a
    // delayed load and dependent scalar move must see the stored value.
    precheck_safe = true;
    for (unsigned i = 0; i < 4; ++i) {
      const unsigned addr = 0x4000 + 0x100 * i, value = 0x40 + i;
      const auto initial_checks = checks, initial_releases = releases;
      const auto initial_completions = completions;
      slow_memory = true;
      launch(vector_insn(23, 0, 1, 4, 8), value);
      launch(0x02000027 | (8 << 7), addr);
      launch(vector_insn(23, 0, 1, 4, 8), 0x7e);
      launch(0x02000007 | (10 << 7), addr, true);
      launch(vector_insn(16, 10, 0, 2, 5));
      drain();
      CHECK(scalar_value == value);
      CHECK(words[addr] == UINT64_C(0x0101010101010101) * value);
      CHECK(words[addr + 8] == words[addr]);
      CHECK(checks == initial_checks + 1 && releases == initial_releases + 1);
      CHECK(completions == initial_completions + 5);
    }
  });
}
