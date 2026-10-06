// Owns optional native instrumentation outside RTL callback ordering and the generic clock driver.
// SPDX-License-Identifier: Apache-2.0
#include "simulation_runtime.h"
#ifdef RHODIUM_COSIM
#include "../cosim/runtime/session.h"
#endif
#ifdef RHEG_TRACE
#include "event_trace.h"
#endif
#include <vpi_user.h>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
enum class Phase { New, Idle, Sampling, Finished };
Phase phase = Phase::New;
bool failed = false, reset_active = true;
std::uint64_t sample = 0;
#ifdef RHEG_TRACE
bool trace_open = false;
std::uint64_t trace_cycle = 0;
#endif
#ifdef RHODIUM_COSIM
bool cosim_open = false;
#endif

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template<class F> int checked(F&& action) noexcept {
  if (failed) return 1;
  try { action(); return 0; }
  catch (const std::exception& error) { std::fprintf(stderr, "Simulation runtime: %s\n", error.what()); }
  catch (...) { std::fprintf(stderr, "Simulation runtime: unknown failure\n"); }
  failed = true;
  return 1;
}

struct Options {
  std::string trace_path;
  std::int64_t corrupt_order = -1;
};
Options options() {
  s_vpi_vlog_info info;
  require(vpi_get_vlog_info(&info), "cannot read simulator arguments");
  Options result;
  bool saw_trace = false, saw_corrupt = false;
  for (int i = 1; i < info.argc; ++i) {
    const std::string_view arg(info.argv[i]);
    if (arg.starts_with("+rheg-trace=")) {
      require(!saw_trace, "duplicate +rheg-trace");
      saw_trace = true;
      result.trace_path = arg.substr(std::string_view("+rheg-trace=").size());
    } else if (arg.starts_with("+cosim-corrupt-order=")) {
      require(!saw_corrupt, "duplicate +cosim-corrupt-order");
      saw_corrupt = true;
      const auto value = arg.substr(std::string_view("+cosim-corrupt-order=").size());
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result.corrupt_order);
      require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && result.corrupt_order >= 0,
              "+cosim-corrupt-order requires a nonnegative integer");
    }
  }
#ifdef RHEG_TRACE
  require(saw_trace && !result.trace_path.empty(), "+rheg-trace=PATH is required");
#else
  require(!saw_trace, "event tracing is not enabled in this simulator");
#endif
#ifndef RHODIUM_COSIM
  require(!saw_corrupt, "cosimulation is not enabled in this simulator");
#endif
  return result;
}
}

extern "C" int rhodium_sim_open() noexcept {
  return checked([] {
    require(phase == Phase::New, "runtime already initialized");
    [[maybe_unused]] const auto config = options();
#ifdef RHEG_TRACE
    trace_open = rhodium::simulation::open_event_trace(config.trace_path.c_str()) == 0;
    require(trace_open, "event trace initialization failed");
#endif
#ifdef RHODIUM_COSIM
    cosim_open = rhodium::cosim::open_simulation(config.corrupt_order) == 0;
    require(cosim_open, "cosimulation initialization failed");
#endif
    phase = Phase::Idle;
  });
}

extern "C" int rhodium_sim_begin(svBit reset) noexcept {
  return checked([&] {
    require(phase == Phase::Idle, "sample start outside idle phase");
    reset_active = reset;
#ifdef RHODIUM_COSIM
    require(rhodium::cosim::begin_sample(sample) == 0, "cosimulation sample start failed");
#endif
    phase = Phase::Sampling;
  });
}

extern "C" int rhodium_sim_end() noexcept {
  return checked([] {
    require(phase == Phase::Sampling, "sample end without sample start");
    int status = 0;
#ifdef RHODIUM_COSIM
    status |= rhodium::cosim::end_sample();
#endif
#ifdef RHEG_TRACE
    if (!reset_active) status |= rhodium::simulation::export_event_cycle(trace_cycle++);
#endif
    ++sample;
    phase = Phase::Idle;
    require(status == 0, "instrumentation sample failed");
  });
}

extern "C" int rhodium_sim_drain() noexcept {
  int ready = 1;
  const int status = checked([&] {
    require(phase == Phase::Idle, "drain outside settled sample");
#ifdef RHODIUM_COSIM
    ready = rhodium::cosim::drain_simulation();
    require(ready >= 0, "cosimulation drain failed");
#endif
  });
  return status ? -1 : ready;
}

extern "C" int rhodium_sim_finish() noexcept {
  if (phase == Phase::Finished) return failed ? 1 : 0;
  // Finalize every initialized consumer even after another consumer has failed.
  // A repeated finish is idempotent, but never clears a previous failure.
  failed |= phase != Phase::Idle;
#ifdef RHODIUM_COSIM
  if (cosim_open) failed |= rhodium::cosim::finish_simulation() != 0;
#endif
#ifdef RHEG_TRACE
  if (trace_open) failed |= rhodium::simulation::close_event_trace() != 0;
#endif
  phase = Phase::Finished;
  return failed ? 1 : 0;
}
