// Checks architectural pipeline events for multiply dependency latency and launch throughput.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "rv5stage-multiply_manifest.h"
#include <cstdio>
#include <cstdlib>
#include <map>

extern "C" void multiply_trace_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void multiply_trace_finish() {
  const auto& graph = rheg::graph();
  graph.validate();
  std::map<std::uint64_t, std::uint64_t> decode, execute;
  unsigned replayed_launches = 0;
  for (const auto& [ref, node] : graph.nodes) {
    if (ref.site != multiply_sites::decode && ref.site != multiply_sites::execute) continue;
    auto& events = ref.site == multiply_sites::decode ? decode : execute;
    const auto pc = graph.field(ref, "pc").unsigned_value();
    if (ref.site == multiply_sites::execute && pc == 0x100000078ULL) ++replayed_launches;
    events.emplace(pc, node.cycle);
  }
  const auto cycle = [](const auto& events, std::uint64_t offset) {
    const auto found = events.find(0x100000000ULL + offset);
    if (found == events.end()) { std::fprintf(stderr, "missing multiply timing PC %llx\n", static_cast<unsigned long long>(offset)); std::abort(); }
    return found->second;
  };
  const auto producer = cycle(execute, 0x68);
  const auto consumer = cycle(decode, 0x6c);
  if (consumer != producer + 5) {
    std::fprintf(stderr, "multiply dependency: EX %llu, dependent ID %llu, expected gap 5\n",
                 static_cast<unsigned long long>(producer), static_cast<unsigned long long>(consumer));
    std::abort();
  }
  for (std::uint64_t pc = 0x1c; pc <= 0x24; pc += 4) {
    if (cycle(execute, pc) != cycle(execute, pc - 4) + 1) {
      std::fprintf(stderr, "independent multiply did not enter EX consecutively at PC %llx\n", static_cast<unsigned long long>(pc));
      std::abort();
    }
  }
  if (replayed_launches != 2) {
    std::fprintf(stderr, "expected canceled and retried EX multiply, saw %u launches\n", replayed_launches);
    std::abort();
  }
  std::printf("multiply timing passed: five-cycle dependency, four consecutive EX launches, canceled/retried launch\n");
}
