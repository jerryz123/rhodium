// SPDX-License-Identifier: Apache-2.0
#include "spike_dpi.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <string>

// Flattened rsim callbacks share a scope; independently created SV models do not.
static std::string current_scope = "soc0";
extern "C" svScope svGetScope() { return &current_scope; }
extern "C" const char *svGetNameFromScope(const svScope) {
  return current_scope.c_str();
}

struct Hart {
  unsigned char reset{};
  long long hart_id{};
  long long reset_vector{};
  long long time{};
  char interrupts{};
  unsigned char xlen_is_64{};
  char max_vaddr_bits{};
  int vector_length{};
  int vector_element_width{};
  std::array<svBitVecVal, 128> isa{};
  int privilege{};
  short max_retired_instructions_per_cycle{};
  char pmp_regions{};
  short instruction_cache_sets{};
  short instruction_cache_ways{};
  short data_cache_sets{};
  short data_cache_ways{};
  unsigned char address_request_ready{};
  unsigned char address_response_valid{};
  unsigned char address_response_grant_valid{};
  long long address_response_grant_base{};
  long long address_response_grant_limit{};
  unsigned char address_response_readable{};
  unsigned char address_response_writable{};
  unsigned char address_response_executable{};
  unsigned char address_response_cacheable{};
  unsigned char address_response_cache_block_zero{};
  unsigned char address_response_instruction_cacheable{};
  unsigned char address_response_device{};
  unsigned char address_response_atomic{};
  unsigned char address_response_fault{};
  unsigned char instruction_request_ready{};
  unsigned char instruction_response_valid{};
  std::array<svBitVecVal, 16> instruction_response_line{};
  unsigned char instruction_response_fault{};
  unsigned char acquire_request_ready{};
  unsigned char acquire_response_valid{};
  std::array<svBitVecVal, 16> acquire_response_line{};
  char acquire_response_state{};
  unsigned char acquire_response_pass_dirty{};
  unsigned char acquire_response_fault{};
  unsigned char release_request_ready{};
  unsigned char release_response_valid{};
  unsigned char release_response_fault{};
  unsigned char snoop_valid{};
  long long snoop_address{};
  unsigned char snoop_share{};
  unsigned char snoop_invalidate{};
  unsigned char snoop_discard_dirty{};
  unsigned char snoop_return_to_source{};
  unsigned char snoop_response_ready{};
  unsigned char uncached_request_ready{};
  unsigned char uncached_response_valid{};
  long long uncached_response_data{};
  unsigned char uncached_response_fault{};
  unsigned char address_request_valid{};
  long long address_request_address{};
  char address_request_size{};
  unsigned char address_request_write{};
  unsigned char address_request_execute{};
  unsigned char address_response_ready{};
  unsigned char instruction_request_valid{};
  long long instruction_request_address{};
  unsigned char instruction_response_ready{};
  unsigned char acquire_request_valid{};
  long long acquire_request_address{};
  unsigned char acquire_request_unique{};
  unsigned char acquire_response_ready{};
  unsigned char release_request_valid{};
  long long release_request_address{};
  std::array<svBitVecVal, 16> release_request_line{};
  char release_request_state{};
  unsigned char release_response_ready{};
  unsigned char snoop_ready{};
  unsigned char snoop_response_valid{};
  char snoop_response_state{};
  unsigned char snoop_response_pass_dirty{};
  unsigned char snoop_response_has_data{};
  std::array<svBitVecVal, 16> snoop_response_line{};
  unsigned char uncached_request_valid{};
  long long uncached_request_address{};
  unsigned char uncached_request_write{};
  char uncached_request_size{};
  long long uncached_request_data{};
  char uncached_request_mask{};
  unsigned char uncached_request_device{};
  Hart(long long id, long long entry) {
    hart_id = id;
    reset_vector = entry;
    xlen_is_64 = 1;
    max_vaddr_bits = 39;
    max_retired_instructions_per_cycle = 1;
    instruction_cache_sets = instruction_cache_ways = data_cache_sets =
        data_cache_ways = 1;
    const std::string extensions =
        "rv64ima_zicsr_ssccptr_sstvecd_sstvala_sscounterenw_ssu64xl";
    for (std::size_t i = 0; i < extensions.size(); ++i)
      isa[i / 4] |= std::uint32_t(extensions[i]) << (8 * (i % 4));
    privilege = 'm' | ('s' << 8) | ('u' << 16);
  }
  void tick() {
    rhodium_spike_tick(
        reset, hart_id, reset_vector, time, interrupts, xlen_is_64,
        max_vaddr_bits, vector_length, vector_element_width, isa.data(),
        privilege, max_retired_instructions_per_cycle, pmp_regions,
        instruction_cache_sets, instruction_cache_ways, data_cache_sets,
        data_cache_ways, address_request_ready, address_response_valid,
        address_response_grant_valid, address_response_grant_base,
        address_response_grant_limit, address_response_readable,
        address_response_writable, address_response_executable,
        address_response_cacheable, address_response_cache_block_zero,
        address_response_instruction_cacheable, address_response_device,
        address_response_atomic, address_response_fault,
        instruction_request_ready, instruction_response_valid,
        instruction_response_line.data(), instruction_response_fault,
        acquire_request_ready, acquire_response_valid,
        acquire_response_line.data(), acquire_response_state,
        acquire_response_pass_dirty, acquire_response_fault,
        release_request_ready, release_response_valid, release_response_fault,
        snoop_valid, snoop_address, snoop_share, snoop_invalidate,
        snoop_discard_dirty, snoop_return_to_source, snoop_response_ready,
        uncached_request_ready, uncached_response_valid, uncached_response_data,
        uncached_response_fault, &address_request_valid,
        &address_request_address, &address_request_size, &address_request_write,
        &address_request_execute, &address_response_ready,
        &instruction_request_valid, &instruction_request_address,
        &instruction_response_ready, &acquire_request_valid,
        &acquire_request_address, &acquire_request_unique,
        &acquire_response_ready, &release_request_valid,
        &release_request_address, release_request_line.data(),
        &release_request_state, &release_response_ready, &snoop_ready,
        &snoop_response_valid, &snoop_response_state,
        &snoop_response_pass_dirty, &snoop_response_has_data,
        snoop_response_line.data(), &uncached_request_valid,
        &uncached_request_address, &uncached_request_write,
        &uncached_request_size, &uncached_request_data, &uncached_request_mask,
        &uncached_request_device);
  }
};

int main() {
  Hart first(0, 0x1000), second(1, 0x2000);
  first.tick();
  second.tick();
  assert(first.address_request_valid &&
         first.address_request_address == 0x1000);
  assert(second.address_request_valid &&
         second.address_request_address == 0x2000);
  // Advance just hart zero beyond its classification request.
  first.address_request_ready = first.address_response_valid = 1;
  first.address_response_cacheable =
      first.address_response_instruction_cacheable = 1;
  first.tick();
  assert(first.instruction_request_valid &&
         first.instruction_request_address == 0x1000);
  first.address_request_ready = first.address_response_valid = 0;
  second.reset = 1;
  second.tick();
  assert(!second.address_request_valid && !second.instruction_request_valid);
  first.tick();
  assert(first.instruction_request_valid &&
         first.instruction_request_address == 0x1000);
  second.reset = 0;
  second.tick();
  assert(second.address_request_valid &&
         second.address_request_address == 0x2000);
  // A second simulator scope may reuse hart zero without sharing its pending fetch.
  current_scope = "soc1";
  Hart separate(0, 0x3000);
  separate.tick();
  assert(separate.address_request_valid &&
         separate.address_request_address == 0x3000);
  separate.reset = 1;
  separate.tick();
  current_scope = "soc0";
  first.tick();
  assert(first.instruction_request_valid &&
         first.instruction_request_address == 0x1000);
  first.reset = second.reset = 1;
  first.tick();
  second.tick();
}
