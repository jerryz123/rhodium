// Checks Spike coroutine backpressure, PMA classification, line fill, and instruction-cache hits.
// SPDX-License-Identifier: Apache-2.0
#include "spike_core.h"

#include <cassert>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

using rhodium::spike::Configuration;
using rhodium::spike::Inputs;
using rhodium::spike::Outputs;
using rhodium::spike::SpikeCoreModel;

int main() {
  Configuration configuration;
  configuration.reset_vector = 0x1000;
  configuration.isa = "rv64ima_zicsr";
  configuration.privilege = "msu";
  configuration.max_vaddr_bits = 39;
  configuration.instruction_cache_sets = 2;
  configuration.instruction_cache_ways = 1;
  configuration.data_cache_sets = 2;
  configuration.data_cache_ways = 1;
  // Configuration fields are assertions about the selected ISA, not hints.
  auto mismatched = configuration;
  mismatched.xlen_is_64 = false;
  bool rejected = false;
  try { SpikeCoreModel invalid(mismatched); }
  catch (const std::invalid_argument&) { rejected = true; }
  assert(rejected);
  auto vector_configuration = configuration;
  vector_configuration.isa = "rv64gcv_zvl256b";
  vector_configuration.vector_length = 256;
  vector_configuration.vector_element_width = 64;
  SpikeCoreModel vector_model(vector_configuration);
  vector_configuration.vector_length = 128;
  rejected = false;
  try { SpikeCoreModel invalid(vector_configuration); }
  catch (const std::invalid_argument&) { rejected = true; }
  assert(rejected);
  SpikeCoreModel model(configuration);

  Inputs inputs;
  Outputs outputs = model.tick(inputs);
  assert(outputs.address_request_valid);
  assert(outputs.address_request_address == 0x1000);
  assert(outputs.address_request_execute);

  outputs = model.tick(inputs);
  assert(outputs.address_request_valid);
  assert(outputs.address_request_address == 0x1000);

  inputs.address_request_ready = true;
  inputs.address_response_valid = true;
  inputs.address_response_cacheable = true;
  inputs.address_response_instruction_cacheable = true;
  outputs = model.tick(inputs);
  assert(outputs.instruction_request_valid);
  assert(outputs.instruction_request_address == 0x1000);

  inputs = Inputs{};
  inputs.instruction_request_ready = true;
  inputs.instruction_response_valid = true;
  constexpr std::array<std::uint32_t, 7> program = {
      0x000020b7,  // lui x1, 0x2
      0x02a00113,  // addi x2, x0, 42
      0x0020b023,  // sd x2, 0(x1)
      0x0000b183,  // ld x3, 0(x1)
      0x08008093,  // addi x1, x1, 128
      0x0020b023,  // sd x2, 0(x1)
      0x0000006f,  // jal x0, 0
  };
  std::uint32_t nop = 0x00000013;
  for (std::size_t index = 0; index < 16; ++index) {
    std::memcpy(reinterpret_cast<std::uint8_t*>(inputs.instruction_response_line.data()) + index * 4,
                &nop, sizeof(nop));
  }
  std::memcpy(inputs.instruction_response_line.data(), program.data(), sizeof(program));
  outputs = model.tick(inputs);
  assert(!outputs.instruction_request_valid);

  std::size_t acquire_count = 0;
  std::size_t release_count = 0;
  inputs = Inputs{};
  for (std::size_t cycle = 0; cycle < 96; ++cycle) {
    outputs = model.tick(inputs);
    Inputs next;
    if (outputs.address_request_valid) {
      next.address_request_ready = true;
      next.address_response_valid = true;
      next.address_response_cacheable = true;
      next.address_response_instruction_cacheable = true;
    }
    assert(!outputs.instruction_request_valid);
    if (outputs.acquire_request_valid) {
      assert(outputs.acquire_request_address == (acquire_count == 0 ? 0x2000 : 0x2080));
      assert(outputs.acquire_request_unique);
      ++acquire_count;
      next.acquire_request_ready = true;
      next.acquire_response_valid = true;
      next.acquire_response_state = 2;
    }
    if (outputs.release_request_valid) {
      assert(outputs.release_request_address == 0x2000);
      assert(outputs.release_request_state == 4);
      assert(outputs.release_request_line[0] == 42);
      ++release_count;
      next.release_request_ready = true;
      next.release_response_valid = true;
    }
    assert(!outputs.uncached_request_valid);
    inputs = next;
  }
  assert(acquire_count == 2);
  assert(release_count == 1);

  assert(outputs.snoop_ready);
  inputs = Inputs{};
  inputs.snoop_valid = true;
  inputs.snoop_address = 0x2080;
  inputs.snoop_invalidate = true;
  inputs.snoop_return_to_source = true;
  outputs = model.tick(inputs);
  assert(!outputs.snoop_ready);
  assert(outputs.snoop_response_valid);
  assert(outputs.snoop_response_state == 0);
  assert(outputs.snoop_response_pass_dirty);
  assert(outputs.snoop_response_has_data);
  assert(outputs.snoop_response_line[0] == 42);

  inputs = Inputs{};
  outputs = model.tick(inputs);
  assert(outputs.snoop_response_valid);
  assert(outputs.snoop_response_line[0] == 42);
  inputs.snoop_response_ready = true;
  outputs = model.tick(inputs);
  assert(!outputs.snoop_response_valid);

  // Spike fetches a 32-bit instruction as two halfwords when C is enabled.
  // Uncached responses retain their position within the selected 64-bit lane.
  Configuration uncached_configuration;
  uncached_configuration.reset_vector = 0x1000;
  uncached_configuration.isa = "rv64imac_zicsr";
  uncached_configuration.privilege = "msu";
  uncached_configuration.instruction_cache_sets = 1;
  uncached_configuration.instruction_cache_ways = 1;
  uncached_configuration.data_cache_sets = 1;
  uncached_configuration.data_cache_ways = 1;
  SpikeCoreModel uncached_model(uncached_configuration);
  Inputs uncached_inputs;
  std::size_t low_half_fetches = 0;
  std::size_t high_half_fetches = 0;
  bool trapped_to_zero = false;
  for (std::size_t cycle = 0; cycle < 128; ++cycle) {
    const Outputs uncached_outputs = uncached_model.tick(uncached_inputs);
    Inputs next;
    if (uncached_outputs.address_request_valid) {
      trapped_to_zero |= uncached_outputs.address_request_address == 0;
      next.address_request_ready = true;
      next.address_response_valid = true;
      next.address_response_instruction_cacheable = true;
    }
    if (uncached_outputs.uncached_request_valid) {
      assert(uncached_outputs.uncached_request_size == 1);
      if (uncached_outputs.uncached_request_address == 0x1000) ++low_half_fetches;
      if (uncached_outputs.uncached_request_address == 0x1002) ++high_half_fetches;
      next.uncached_request_ready = true;
      next.uncached_response_valid = true;
      next.uncached_response_data = 0x000000000000006f;
    }
    uncached_inputs = next;
  }
  assert(!trapped_to_zero);
  assert(low_half_fetches >= 1);
  assert(high_half_fetches >= 1);

  // Spike's architectural FENCE.I flush also invalidates the external
  // instruction cache, forcing the following instruction to refill its line.
  Configuration fence_configuration;
  fence_configuration.reset_vector = 0x1000;
  fence_configuration.isa = "rv64ima_zicsr_zifencei";
  fence_configuration.privilege = "msu";
  fence_configuration.max_vaddr_bits = 39;
  fence_configuration.instruction_cache_sets = 1;
  fence_configuration.instruction_cache_ways = 1;
  fence_configuration.data_cache_sets = 1;
  fence_configuration.data_cache_ways = 1;
  SpikeCoreModel fence_model(fence_configuration);
  Inputs fence_inputs;
  std::size_t instruction_fills = 0;
  bool fence_trapped_to_zero = false;
  for (std::size_t cycle = 0; cycle < 128; ++cycle) {
    const Outputs fence_outputs = fence_model.tick(fence_inputs);
    Inputs next;
    if (fence_outputs.address_request_valid) {
      fence_trapped_to_zero |= fence_outputs.address_request_address == 0;
      next.address_request_ready = true;
      next.address_response_valid = true;
      next.address_response_cacheable = true;
      next.address_response_instruction_cacheable = true;
    }
    if (fence_outputs.instruction_request_valid) {
      ++instruction_fills;
      next.instruction_request_ready = true;
      next.instruction_response_valid = true;
      next.instruction_response_line[0] = 0x0000100f12000073ULL;
      next.instruction_response_line[1] = 0x0000006fULL;
    }
    fence_inputs = next;
  }
  assert(instruction_fills >= 2);
  assert(!fence_trapped_to_zero);
}
