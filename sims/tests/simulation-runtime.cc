// SPDX-License-Identifier: Apache-2.0
#include "../verilator/simulation_runtime.h"
#include "../verilator/event_trace.h"
#include "../cosim/runtime/session.h"
#include <vpi_user.h>
#include <cassert>
#include <cstdio>
#include <string_view>

namespace {
unsigned edges = 0, active_edges = 0, reset_edges = 0;
#ifdef RHODIUM_COSIM
bool cosim_started = false, sample_open = false;
std::uint64_t sample_index = 0;
#endif
#ifdef RHEG_TRACE
bool trace_started = false;
unsigned exported = 0;
#endif
[[maybe_unused]] bool option(std::string_view name) {
  s_vpi_vlog_info info;
  assert(vpi_get_vlog_info(&info));
  for (int i = 1; i < info.argc; ++i)
    if (std::string_view(info.argv[i]) == name) return true;
  return false;
}
}

extern "C" void runtime_test_edge(svBit reset) {
  ++edges;
  if (reset) { assert(active_edges == 0); ++reset_edges; }
  else { assert(reset_edges == 3); ++active_edges; }
#ifdef RHODIUM_COSIM
  assert(cosim_started && sample_open && sample_index + 1 == edges);
#endif
#ifdef RHEG_TRACE
  assert(trace_started && exported == active_edges - (reset ? 0 : 1));
#endif
}

#ifdef RHODIUM_COSIM
namespace rhodium::cosim {
int open_simulation(std::int64_t corruption) noexcept {
  assert(!cosim_started && edges == 0);
  cosim_started = true;
  std::printf("test cosim opened: corruption=%lld\n", static_cast<long long>(corruption));
  return 0;
}
int begin_sample(std::uint64_t sample) noexcept {
  assert(cosim_started && !sample_open && sample == edges);
  sample_open = true;
  sample_index = sample;
  return 0;
}
int end_sample() noexcept {
  assert(sample_open && edges == sample_index + 1);
  sample_open = false;
  return active_edges == 2 && option("+runtime-test-fail") ? 1 : 0;
}
int drain_simulation() noexcept {
  assert(cosim_started && !sample_open && active_edges >= 5);
  if (option("+runtime-test-drain-fail")) return -1;
  if (option("+runtime-test-drain-stuck")) return 0;
  return !option("+runtime-test-drain") || active_edges >= 8 ? 1 : 0;
}
int finish_simulation() noexcept {
  assert(cosim_started && !sample_open && edges == active_edges + 3);
  cosim_started = false;
  std::printf("test cosim closed: samples=%u\n", edges);
  std::fflush(stdout);
  return option("+runtime-test-finish-fail") ? 1 : 0;
}
}
#endif
#ifdef RHEG_TRACE
namespace rhodium::simulation {
int open_event_trace(const char* path) noexcept {
  assert(!trace_started && edges == 0 && std::string_view(path) == "test.pftrace");
  trace_started = true;
  return 0;
}
int export_event_cycle(std::uint64_t cycle) noexcept {
  assert(trace_started && cycle == exported && active_edges == cycle + 1);
#ifdef RHODIUM_COSIM
  assert(!sample_open);
#endif
  ++exported;
  return active_edges == 2 && option("+runtime-test-fail") ? 1 : 0;
}
int close_event_trace() noexcept {
  assert(trace_started && exported == active_edges);
  trace_started = false;
  std::printf("test trace closed: cycles=%u\n", exported);
  std::fflush(stdout);
  return option("+runtime-test-finish-fail") ? 1 : 0;
}
}
#endif
