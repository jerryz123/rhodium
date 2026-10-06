// Owns the single-hart simulator session and checks settled records against its embedded Sail model.
// SPDX-License-Identifier: Apache-2.0
#include "session.h"
#include "../sail/checker.h"
#include "../events/dpi.h"
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
std::unique_ptr<SailChecker> checker;
std::uint64_t sample = 0;
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
void simulation_htif_mailboxes(std::uint64_t tohost, std::uint64_t fromhost) {
  if (!checker) throw std::runtime_error("HTIF mailboxes before cosim initialization");
  for (auto address : {tohost, fromhost})
    if (address) checker->external_memory({address, 8});
}
void simulation_host_write(std::uint64_t address, std::span<const std::uint8_t> data) {
  if (!checker) throw std::runtime_error("host write before cosim initialization");
  checker->host_write(sample, address, data);
}
int open_simulation(std::int64_t corrupt_order) noexcept {
  return boundary([&] {
    if (collector) throw std::runtime_error("cosim already initialized");
    const auto manifest = jsoncons::json::parse(generated::manifest);
    const auto xlen = manifest["xlen"].as<unsigned>();
    const auto configuration = jsoncons::json::parse(generated::configuration);
    const auto& vector = configuration["extensions"]["V"];
    const unsigned vlen = vector["support_level"].as<std::string>() == "Disabled" ? 0 :
        1U << vector["vlen_exp"].as<unsigned>();
    const auto reset_pc = manifest["reset_pc"].as<std::uint64_t>();
    std::vector<MemoryRange> ranges;
    for (const auto& range : manifest["private_memory"].array_range())
      ranges.push_back({range["address"].as<std::uint64_t>(), range["size"].as<std::uint64_t>()});
    checker = std::make_unique<SailChecker>(generated::configuration, reset_pc, ranges);
    checker->load(manifest["rom_image"]["address"].as<std::uint64_t>(), manifest["rom_image"]["bytes"].as<std::vector<std::uint8_t>>());
    collector = std::make_unique<observation::Collector>();
    collector->reset(0, 0, {manifest["hart_id"].as<std::uint64_t>(), reset_pc, {3, false}, xlen, vlen, true});
    binding = std::make_unique<DpiBinding>(*collector);
    corrupt = corrupt_order;
  });
}
int begin_sample(std::uint64_t cycle) noexcept {
  return boundary([&] {
    if (!collector) throw std::runtime_error("cosim not initialized");
    sample = cycle;
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
int drain_simulation() noexcept {
  bool drained = false;
  if (boundary([&] { binding->check(); drained = collector->drain(); })) return -1;
  return drained ? 1 : 0;
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
