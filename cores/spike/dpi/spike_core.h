// Declares the cycle-level Spike execution model and its typed transaction ABI.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace rhodium::spike {

struct Configuration {
  std::uint64_t hart_id = 0;
  std::uint64_t reset_vector = 0;
  bool xlen_is_64 = true;
  std::uint8_t max_vaddr_bits = 0;
  std::uint32_t vector_length = 0;
  std::uint32_t vector_element_width = 0;
  std::string isa;
  std::string privilege;
  std::uint16_t max_retired_instructions_per_cycle = 1;
  std::uint8_t pmp_regions = 16;
  std::uint16_t instruction_cache_sets = 1;
  std::uint16_t instruction_cache_ways = 1;
  std::uint16_t data_cache_sets = 1;
  std::uint16_t data_cache_ways = 1;
};

struct Inputs {
  std::uint64_t time = 0;
  std::uint8_t interrupts = 0;
  bool address_request_ready = false;
  bool address_response_valid = false;
  bool address_response_cacheable = false;
  bool address_response_instruction_cacheable = false;
  bool address_response_device = false;
  bool address_response_atomic = false;
  bool address_response_fault = false;
  bool instruction_request_ready = false;
  bool instruction_response_valid = false;
  std::array<std::uint64_t, 8> instruction_response_line{};
  bool instruction_response_fault = false;
  bool acquire_request_ready = false;
  bool acquire_response_valid = false;
  std::array<std::uint64_t, 8> acquire_response_line{};
  std::uint8_t acquire_response_state = 0;
  bool acquire_response_pass_dirty = false;
  bool acquire_response_fault = false;
  bool release_request_ready = false;
  bool release_response_valid = false;
  bool release_response_fault = false;
  bool snoop_valid = false;
  std::uint64_t snoop_address = 0;
  bool snoop_share = false;
  bool snoop_invalidate = false;
  bool snoop_discard_dirty = false;
  bool snoop_return_to_source = false;
  bool snoop_response_ready = false;
  bool uncached_request_ready = false;
  bool uncached_response_valid = false;
  std::uint64_t uncached_response_data = 0;
  bool uncached_response_fault = false;
};

struct Outputs {
  bool address_request_valid = false;
  std::uint64_t address_request_address = 0;
  std::uint8_t address_request_size = 0;
  bool address_request_write = false;
  bool address_request_execute = false;
  bool address_response_ready = false;
  bool instruction_request_valid = false;
  std::uint64_t instruction_request_address = 0;
  bool instruction_response_ready = false;
  bool acquire_request_valid = false;
  std::uint64_t acquire_request_address = 0;
  bool acquire_request_unique = false;
  bool acquire_response_ready = false;
  bool release_request_valid = false;
  std::uint64_t release_request_address = 0;
  std::array<std::uint64_t, 8> release_request_line{};
  std::uint8_t release_request_state = 0;
  bool release_response_ready = false;
  bool snoop_ready = false;
  bool snoop_response_valid = false;
  std::uint8_t snoop_response_state = 0;
  bool snoop_response_pass_dirty = false;
  bool snoop_response_has_data = false;
  std::array<std::uint64_t, 8> snoop_response_line{};
  bool uncached_request_valid = false;
  std::uint64_t uncached_request_address = 0;
  bool uncached_request_write = false;
  std::uint8_t uncached_request_size = 0;
  std::uint64_t uncached_request_data = 0;
  std::uint8_t uncached_request_mask = 0;
  bool uncached_request_device = false;
  bool uncached_response_ready = false;
};

class SpikeCoreModel {
 public:
  explicit SpikeCoreModel(Configuration configuration);
  ~SpikeCoreModel();
  SpikeCoreModel(const SpikeCoreModel&) = delete;
  SpikeCoreModel& operator=(const SpikeCoreModel&) = delete;

  Outputs tick(const Inputs& inputs);

 private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace rhodium::spike
