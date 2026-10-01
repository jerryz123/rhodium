// SPDX-License-Identifier: Apache-2.0
#include "spike_core.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <map>
#include <vector>

using namespace rhodium::spike;

struct Region {
  std::uint64_t base, limit;
  bool read = true, write = true, execute = false, cacheable = true;
};

struct Result {
  std::map<std::uint64_t, unsigned> classifications;
  std::vector<std::uint64_t> writes;
  unsigned acquires = 0, snoops = 0;
  bool trapped = false;
  unsigned completion_cycle = 0;
};

Result run(const std::vector<std::uint32_t>& program, std::vector<Region> regions,
           bool grants = true, bool snoop = false) {
  Configuration config;
  config.reset_vector = 0x1000;
  config.isa = "rv64ima_zicsr_zic64b_zicboz";
  config.privilege = "m";
  config.pmp_regions = 0;
  config.data_cache_sets = 16;
  static std::vector<std::unique_ptr<SpikeCoreModel>> models;
  models.push_back(std::make_unique<SpikeCoreModel>(config));
  auto& model = *models.back();
  regions.insert(regions.begin(), {0x1000, 0x103f, true, false, true, true});
  regions.push_back({0x3000, 0x3007, true, true, false, false});
  Result result;
  Inputs inputs;
  for (unsigned cycle = 0; cycle < 256; ++cycle) {
    const Outputs out = model.tick(inputs);
    Inputs next;
    if (out.address_request_valid) {
      ++result.classifications[out.address_request_address];
      if (out.address_request_address == 0) { result.trapped = true; break; }
      next.address_request_ready = true;
      next.address_response_valid = true;
      next.address_response_fault = true;
      for (const auto& region : regions) {
        const auto address = out.address_request_address;
        const auto length = std::uint64_t{1} << out.address_request_size;
        if (address < region.base || address > region.limit || length - 1 > region.limit - address)
          continue;
        next.address_response_grant_valid = grants;
        next.address_response_grant_base = region.base;
        next.address_response_grant_limit = region.limit;
        next.address_response_readable = region.read;
        next.address_response_writable = region.write;
        next.address_response_executable = region.execute;
        next.address_response_cacheable = region.cacheable;
        next.address_response_instruction_cacheable = region.execute;
        next.address_response_atomic = region.cacheable;
        next.address_response_cache_block_zero = region.cacheable;
        next.address_response_device = !region.cacheable;
        next.address_response_fault = !(out.address_request_execute ? region.execute :
                                       out.address_request_write ? region.write : region.read);
      }
    }
    if (out.instruction_request_valid) {
      assert(out.instruction_request_address == 0x1000);
      next.instruction_request_ready = true;
      next.instruction_response_valid = true;
      assert(program.size() <= 16);
      std::memcpy(next.instruction_response_line.data(), program.data(), program.size() * 4);
    }
    if (out.acquire_request_valid) {
      ++result.acquires;
      next.acquire_request_ready = true;
      next.acquire_response_valid = true;
      next.acquire_response_state = out.acquire_request_unique ? 2 : 1;
      next.acquire_response_line.fill(result.snoops ? 99 : 42);
    }
    if (out.uncached_request_valid) {
      assert(out.uncached_request_write && out.uncached_request_address == 0x3000);
      result.writes.push_back(out.uncached_request_data);
      result.completion_cycle = cycle;
      next.uncached_request_ready = true;
      next.uncached_response_valid = true;
      if (snoop && result.writes.size() == 1) {
        next.snoop_valid = true;
        next.snoop_address = 0x2000;
        next.snoop_invalidate = true;
        ++result.snoops;
      }
    }
    next.snoop_response_ready = true;
    inputs = next;
  }
  return result;
}

int main() {
  // Attribute grants cover different addresses/sizes/directions, without changing cache contents.
  const std::vector<std::uint32_t> stores = {
      0x000022b7, 0x02a00313, 0x0062b023, 0x0002b383,
      0x00003e37, 0x007e3023, 0x0000006f};
  const auto cached = run(stores, {{0x2000, 0x203f}});
  const auto uncached = run(stores, {{0x2000, 0x203f}}, false);
  assert(cached.writes == std::vector<std::uint64_t>{42});
  assert(cached.writes == uncached.writes && cached.acquires == uncached.acquires);
  assert(cached.classifications.at(0x2000) == 1);
  assert(uncached.classifications.at(0x2000) == 2);
  assert(!cached.trapped);
  assert(cached.completion_cycle < uncached.completion_cycle);
  std::printf("Attribute grants: signature completion %u -> %u ticks, repeated data classifications 2 -> 1\n",
              uncached.completion_cycle, cached.completion_cycle);

  // A readable grant must still fault a later write, without another classification transaction.
  const std::vector<std::uint32_t> read_write = {
      0x000022b7, 0x0002b303, 0x0062b023, 0x0000006f};
  auto denied = run(read_write, {{0x2000, 0x203f, true, false}});
  assert(denied.trapped && denied.classifications.at(0x2000) == 1 && denied.acquires == 1);
  denied = run(stores, {{0x2000, 0x203f, false, true}});
  assert(denied.trapped && denied.classifications.at(0x2000) == 1 && denied.writes.empty());
  // Data readability never grants execution permission.
  denied = run({0x000022b7, 0x0002b303, 0x00028067}, {{0x2000, 0x203f}});
  assert(denied.trapped && denied.classifications.at(0x2000) == 1);

  // Crossing an interval cannot hit even when the start address was previously granted.
  const std::vector<std::uint32_t> sizes = {
      0x000022b7, 0x0002a303, 0x0002b303, 0x0000006f};
  const auto crossing = run(sizes, {{0x2000, 0x2003}});
  assert(crossing.trapped && crossing.classifications.at(0x2000) == 2);
  const auto adjacent = run({0x000022b7, 0x0002b303, 0x0082b303, 0x0000006f},
                            {{0x2000, 0x2007}, {0x2008, 0x200f}});
  assert(!adjacent.trapped && adjacent.classifications.at(0x2000) == 1 &&
         adjacent.classifications.at(0x2008) == 1);
  const auto unmapped = run(read_write, {});
  assert(unmapped.trapped && unmapped.acquires == 0);

  // Snoop invalidation discards data, not attributes; the next load reacquires the new contents.
  const std::vector<std::uint32_t> reads = {
      0x000022b7, 0x0002b303, 0x00003e37, 0x006e3023,
      0x0002b303, 0x006e3023, 0x0000006f};
  const auto coherent = run(reads, {{0x2000, 0x203f}}, true, true);
  assert((coherent.writes == std::vector<std::uint64_t>{42, 99}));
  assert(coherent.acquires == 2 && coherent.snoops == 1 &&
         coherent.classifications.at(0x2000) == 1);
  const auto fenced = run({0x000022b7, 0x0002b303, 0x0000100f, 0x0002b303,
                           0x00003e37, 0x006e3023, 0x0000006f}, {{0x2000, 0x203f}});
  assert(fenced.classifications.at(0x1000) == 1 && fenced.classifications.at(0x2000) == 1 &&
         fenced.writes == std::vector<std::uint64_t>{42});
  // Fresh model creation (the reset contract) requires new RTL grants.
  const auto reset = run(stores, {{0x2000, 0x203f}});
  assert(reset.classifications.at(0x1000) == 1 && reset.classifications.at(0x2000) == 1);

  // The highest representable interval uses subtraction-based containment, not wrapping addition.
  const auto top = run({0xff800293, 0x0002b303, 0x0002b303, 0x00003e37, 0x006e3023, 0x0000006f},
                       {{UINT64_MAX - 7, UINT64_MAX}});
  assert(top.classifications.at(UINT64_MAX - 7) == 1 && top.writes.size() == 1);
  // More grants than entries must replace safely and reclassify an evicted interval.
  std::vector<std::uint32_t> many = {0x000022b7};
  std::vector<Region> regions;
  for (unsigned i = 0; i < 9; ++i) {
    many.push_back(((i * 8) << 20) | 0x0002b303);
    regions.push_back({0x2000 + i * 8, 0x2007 + i * 8});
  }
  many.insert(many.end(), {0x0002b303, 0x00003e37, 0x006e3023, 0x0000006f});
  const auto replaced = run(many, regions);
  assert(replaced.classifications.at(0x2000) == 2 && replaced.writes.size() == 1);

  // A smaller earlier grant cannot authorize a full-line CBO.ZERO operation.
  const std::vector<std::uint32_t> zero = {
      0x000022b7, 0x0002b303, 0x0042a00f, 0x0002b303,
      0x00003e37, 0x006e3023, 0x0000006f};
  const auto full_line = run(zero, {{0x2000, 0x203f}});
  assert(full_line.classifications.at(0x2000) == 1 && full_line.writes == std::vector<std::uint64_t>{0});
  const auto partial_line = run(zero, {{0x2000, 0x2007}});
  assert(partial_line.classifications.at(0x2000) == 2 && partial_line.trapped && partial_line.writes.empty());
  // Read and write checks of an AMO both use the grant, while ownership is still acquired.
  const auto atomic = run({0x000022b7, 0x0002b303, 0x00100393, 0x0072b32f,
                           0x0002b303, 0x00003e37, 0x006e3023, 0x0000006f}, {{0x2000, 0x203f}});
  assert(atomic.classifications.at(0x2000) == 1 && atomic.writes == std::vector<std::uint64_t>{43});
  // A non-executable cached trap vector must keep returning control even with no retirement.
  const auto fault_loop = run({0x000022b7, 0x30529073, 0x0002b303, 0x00028067},
                              {{0x2000, 0x203f}});
  assert(fault_loop.classifications.at(0x2000) == 1 && fault_loop.writes.empty());
}
