// Buffers every Highway measurement and accounts for setup, kernel, and reporting costs.
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <charconv>
#include <cstring>
#include "benchmark.h"
#include "hwy/contrib/sort/vqsort.h"
extern "C" int _write(int, const void*, size_t);
extern "C" void _exit(int) __attribute__((noreturn));
namespace {
uint64_t measurements;
char output[4096];
size_t used;
constexpr size_t kPhases = static_cast<size_t>(HighwayPhase::Count);
HighwayStamp phases[kPhases], previous;
HighwayPhase current = HighwayPhase::Setup;
const char* const names[kPhases] = {
  "setup", "inputs", "kernel", "reporting", "phast-build",
  "shardmul-build", "cuckoo2x2-init", "cuckoo-build", "packed-init", "packed-convert"
};
void Flush() {
  size_t sent = 0;
  while (sent < used) {
    const int written = _write(1, output + sent, used - sent);
    if (written <= 0) _exit(1);
    sent += static_cast<size_t>(written);
  }
  used = 0;
}
struct Line {
  char bytes[256];
  size_t size = 0;
  void Text(const char* value) {
    const size_t length = strlen(value);
    if (length > sizeof(bytes) - size) _exit(1);
    memcpy(bytes + size, value, length);
    size += length;
  }
  void Number(uint64_t value) {
    const auto result = std::to_chars(bytes + size, bytes + sizeof(bytes), value);
    if (result.ec != std::errc{}) _exit(1);
    size = static_cast<size_t>(result.ptr - bytes);
  }
  void Counters(HighwayStamp stamp) {
    Text(" cycles="); Number(stamp.cycles);
    Text(" instructions="); Number(stamp.instructions); Text("\n");
  }
  void Emit() const {
    if (size > sizeof(output) - used) Flush();
    memcpy(output + used, bytes, size);
    used += size;
  }
};
}  // namespace
extern "C" HighwayStamp highway_begin() {
  HighwayStamp stamp;
  asm volatile("fence rw,rw; rdcycle %0; rdinstret %1"
               : "=r"(stamp.cycles), "=r"(stamp.instructions) : : "memory");
  return stamp;
}
HighwayStamp HighwaySetPhase(HighwayPhase phase) {
  const auto stop = highway_begin();
  auto& elapsed = phases[static_cast<size_t>(current)];
  elapsed.cycles += stop.cycles - previous.cycles;
  elapsed.instructions += stop.instructions - previous.instructions;
  previous = stop;
  current = phase;
  return stop;
}
extern "C" uint64_t highway_end(const char* name, size_t size, HighwayStamp start) {
  const auto stop = HighwaySetPhase(HighwayPhase::Reporting);
  ++measurements;
  Line line;
  line.Text("HIGHWAY-BENCH: "); line.Text(name); line.Text(" size="); line.Number(size);
  line.Counters({stop.cycles - start.cycles, stop.instructions - start.instructions});
  line.Emit();
  HighwaySetPhase(HighwayPhase::Setup);
  return stop.cycles - start.cycles;
}
int main() {
  const auto entry = highway_begin();
  previous = entry;
  uint64_t* state = hwy::GetGeneratorState();
  state[0] = 0x123456789abcdef0ull;
  state[1] = 0xfedcba9876543210ull;
  state[2] = 1;
  RunBenchmarks();
  HighwaySetPhase(HighwayPhase::Reporting);
  Flush();
  // Freeze totals before their own formatting. Probe overhead belongs to the
  // phase in which it executes; no kernel count is estimated.
  const auto phase_end = HighwaySetPhase(HighwayPhase::Reporting);
  Line startup;
  startup.Text("HIGHWAY-PHASE: startup"); startup.Counters(entry); startup.Emit();
  for (size_t i = 0; i < kPhases; ++i) {
    if (!phases[i].instructions && !phases[i].cycles) continue;
    Line phase;
    phase.Text("HIGHWAY-PHASE: "); phase.Text(names[i]); phase.Counters(phases[i]); phase.Emit();
  }
  Line completion;
  completion.Text(measurements ? "HIGHWAY-RESULT: PASS measurements=" : "HIGHWAY-RESULT: FAIL measurements=");
  completion.Number(measurements); completion.Text("\n"); completion.Emit(); Flush();
  const auto total = highway_begin();
  Line summary;
  summary.Text("HIGHWAY-PHASE: summary-report");
  summary.Counters({total.cycles - phase_end.cycles, total.instructions - phase_end.instructions}); summary.Emit();
  // Includes startup, all cases, their output, and summary formatting, but not
  // this final record or the exit/HTIF completion tail.
  Line whole;
  whole.Text("HIGHWAY-TOTAL: before-final-report"); whole.Counters(total); whole.Emit(); Flush();
  return measurements ? 0 : 1;
}
