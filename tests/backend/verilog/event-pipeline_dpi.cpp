// Predicts payload transfers and exact event edges independently of inserted RTL.
#include "../../../rheg/runtime/rheg.h"
#include <array>
#include <deque>
#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "%s\n", message.c_str());
  std::abort();
}

struct Pending {
  std::uint64_t cycle, parent;
  std::uint32_t payload;
};
rheg::Graph expected;
std::array<std::uint64_t, 3> sequences{};
std::uint64_t cycle = 0;
std::deque<Pending> middle, completed;
unsigned canceled = 0, flush_inputs = 0, flush_outputs = 0;

std::uint64_t node(std::uint32_t site, std::uint32_t payload,
                   int parent = -1, std::uint64_t parent_sequence = 0) {
  const auto sequence = sequences[site]++;
  auto& value = expected.nodes[{site, sequence}];
  value.present = true;
  value.cycle = cycle;
  value.width = 8;
  value.words[0] = payload;
  if (parent >= 0)
    expected.edges.insert({{static_cast<std::uint32_t>(parent), parent_sequence}, {site, sequence}});
  return sequence;
}
}

extern "C" void event_pipeline_sample(std::uint32_t reset, std::uint32_t flush, std::uint32_t valid,
    std::uint32_t payload, std::uint32_t out_valid, std::uint32_t out_payload) {
  if (reset) {
    expected.clear();
    sequences.fill(0);
    middle.clear();
    completed.clear();
    cycle = 0;
    return;
  }
  const bool output = !completed.empty() && completed.front().cycle == cycle;
  if (output != static_cast<bool>(out_valid))
    fail("fixed pipeline valid mismatch at cycle " + std::to_string(cycle));
  if (output) {
    const auto item = completed.front();
    completed.pop_front();
    if (out_payload != item.payload) fail("fixed pipeline payload mismatch");
    node(1, item.payload, 2, item.parent);
  }
  if (!middle.empty() && middle.front().cycle == cycle) {
    const auto item = middle.front();
    middle.pop_front();
    const auto sequence = node(2, item.payload, 0, item.parent);
    const auto mapped = (item.payload + 1) & 255;
    if (mapped != 4) completed.push_back({cycle + 1, sequence, mapped});
  }
  if (valid) {
    const auto sequence = node(0, payload);
    if (payload != 0) middle.push_back({cycle + 3, sequence, payload});
  }
  // Pre-edge observations remain visible. Flush cancels every pending arrival
  // and this edge's input, but never clears the recorded graph or sequences.
  if (flush) {
    canceled += static_cast<unsigned>(middle.size() + completed.size());
    flush_inputs += valid != 0;
    flush_outputs += output;
    middle.clear();
    completed.clear();
  }
  ++cycle;
}

extern "C" void event_pipeline_check() {
  if (rheg::graph().json() != expected.json())
    fail("fixed pipeline event graph mismatch\nactual: "
         + rheg::graph().json() + "expected: " + expected.json());
}

extern "C" void event_pipeline_finish() {
  if (!canceled || !flush_inputs || !flush_outputs)
    fail("flush coverage requires pending work and simultaneous input/output");
}
