// Owns the single-hart simulator session and checks settled records against its embedded Sail model.
// SPDX-License-Identifier: Apache-2.0
#include "simulation.h"
#include "scalar-checker.h"
#include "hooks-dpi.h"
#include "runtime-config.h"
#include <jsoncons/json.hpp>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace {
using namespace rhodium::cosim;
using rhodium::cosim::observation::DpiBinding;
std::unique_ptr<observation::Collector> collector;
std::unique_ptr<DpiBinding> binding;
std::unique_ptr<ScalarChecker> checker;
std::uint64_t sample = 0, clock_hz = 0, timebase_hz = 0;
std::int64_t corrupt = -1;
bool corrupted = false, failed = false;
template<class F> int boundary(F&& action) noexcept {
  if (failed) return 1;
  try { action(); return 0; }
  catch (const std::exception& error) { std::fprintf(stderr, "Sail cosim: %s\n", error.what()); }
  catch (...) { std::fprintf(stderr, "Sail cosim: unknown failure\n"); }
  failed = true;
  return 1;
}
}
namespace rhodium::cosim {
void simulation_host_write(std::uint64_t address, std::span<const std::uint8_t> data) {
  if (!checker) throw std::runtime_error("host write before cosim initialization");
  checker->host_write(sample, address, data);
}
int open_simulation(std::int64_t corrupt_order) noexcept {
  return boundary([&] {
    if (collector) throw std::runtime_error("cosim already initialized");
    const auto manifest = jsoncons::json::parse(generated::manifest);
    if (manifest["xlen"].as<unsigned>() != 64) throw std::runtime_error("scalar runtime currently requires RV64");
    const auto reset_pc = manifest["reset_pc"].as<std::uint64_t>();
    std::vector<MemoryRange> ranges;
    for (const auto& range : manifest["private_memory"].array_range())
      ranges.push_back({range["address"].as<std::uint64_t>(), range["size"].as<std::uint64_t>()});
    checker = std::make_unique<ScalarChecker>(generated::configuration, reset_pc, ranges);
    checker->load(manifest["rom_image"]["address"].as<std::uint64_t>(), manifest["rom_image"]["bytes"].as<std::vector<std::uint8_t>>());
    clock_hz = manifest["clock_frequency_hz"].as<std::uint64_t>();
    timebase_hz = manifest["timebase_frequency_hz"].as<std::uint64_t>();
    if (!timebase_hz || clock_hz % timebase_hz) throw std::runtime_error("nonintegral timebase divider");
    collector = std::make_unique<observation::Collector>();
    collector->reset(0, 0, {manifest["hart_id"].as<std::uint64_t>(), reset_pc, {3, false}, 64, 0});
    binding = std::make_unique<DpiBinding>(*collector);
    corrupt = corrupt_order;
  });
}
int begin_sample(std::uint64_t cycle) noexcept {
  return boundary([&] {
    if (!collector) throw std::runtime_error("cosim not initialized");
    sample = cycle;
    collector->environment(0, {0, sample / (clock_hz / timebase_hz), 1});
    collector->begin_sample(sample);
  });
}
int end_sample() noexcept {
  return boundary([&] {
    binding->check();
    for (auto record : collector->end_sample()) {
      if (corrupt >= 0 && record.id.order == static_cast<std::uint64_t>(corrupt)) {
        for (auto& [id, effect] : record.effects) {
          (void)id;
          if (auto* write = std::get_if<observation::RegisterWrite>(&effect)) {
            write->value ^= 1;
            corrupted = true;
            break;
          }
        }
      }
      checker->check(record);
    }
  });
}
int finish_simulation() noexcept {
  return boundary([&] {
    binding->check();
    collector->finish();
    if (!checker->checked()) throw std::runtime_error("cosim checked no instructions");
    if (corrupt >= 0 && !corrupted) throw std::runtime_error("requested corruption was not exercised");
    std::printf("Sail cosim: checked %llu scalar records\n", static_cast<unsigned long long>(checker->checked()));
  });
}
}
