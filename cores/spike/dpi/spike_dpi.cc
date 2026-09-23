// Implements the stable DPI shim that owns one Spike model per SystemVerilog scope.
// SPDX-License-Identifier: Apache-2.0
#include "spike_dpi.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "spike_core.h"

namespace {

using rhodium::spike::Configuration;
using rhodium::spike::Inputs;
using rhodium::spike::Outputs;
using rhodium::spike::SpikeCoreModel;

std::map<std::string, std::unique_ptr<SpikeCoreModel>>& models() {
  static std::map<std::string, std::unique_ptr<SpikeCoreModel>> instances;
  return instances;
}

std::string scope_name() {
  const svScope scope = svGetScope();
  if (scope == nullptr) return "<default>";
  const char* name = svGetNameFromScope(scope);
  return name == nullptr ? "<unnamed>" : name;
}

std::string packed_string(const svBitVecVal* packed) {
  std::string result;
  for (std::size_t index = 0; index < 64; ++index) {
    const std::uint32_t word = packed[index / 4];
    const char value = static_cast<char>((word >> ((index % 4) * 8)) & 0xffU);
    if (value == '\0') break;
    result.push_back(value);
  }
  return result;
}

std::array<std::uint64_t, 8> unpack_line(const svBitVecVal* packed) {
  std::array<std::uint64_t, 8> line{};
  for (std::size_t index = 0; index < line.size(); ++index) {
    line[index] = static_cast<std::uint64_t>(packed[index * 2]) |
                  (static_cast<std::uint64_t>(packed[index * 2 + 1]) << 32);
  }
  return line;
}

void pack_line(const std::array<std::uint64_t, 8>& line, svBitVecVal* packed) {
  for (std::size_t index = 0; index < line.size(); ++index) {
    packed[index * 2] = static_cast<svBitVecVal>(line[index]);
    packed[index * 2 + 1] = static_cast<svBitVecVal>(line[index] >> 32);
  }
}

template <typename T>
void store(T* target, T value) {
  *target = value;
}

}  // namespace

extern "C" unsigned char rhodium_spike_tick(
    unsigned char reset, long long hart_id, long long reset_vector,
    long long time, char interrupts, unsigned char xlen_is_64,
    char max_vaddr_bits,
    const svBitVecVal* isa, const svBitVecVal* privilege,
    short max_retired_instructions_per_cycle, char pmp_regions,
    short instruction_cache_sets, short instruction_cache_ways,
    short data_cache_sets, short data_cache_ways,
    unsigned char address_request_ready,
    unsigned char address_response_valid,
    unsigned char address_response_cacheable,
    unsigned char address_response_instruction_cacheable,
    unsigned char address_response_device,
    unsigned char address_response_atomic,
    unsigned char address_response_fault,
    unsigned char instruction_request_ready,
    unsigned char instruction_response_valid,
    const svBitVecVal* instruction_response_line,
    unsigned char instruction_response_fault,
    unsigned char acquire_request_ready,
    unsigned char acquire_response_valid,
    const svBitVecVal* acquire_response_line,
    char acquire_response_state,
    unsigned char acquire_response_pass_dirty,
    unsigned char acquire_response_fault,
    unsigned char release_request_ready,
    unsigned char release_response_valid,
    unsigned char release_response_fault,
    unsigned char snoop_valid, long long snoop_address,
    unsigned char snoop_share, unsigned char snoop_invalidate,
    unsigned char snoop_discard_dirty,
    unsigned char snoop_return_to_source,
    unsigned char snoop_response_ready,
    unsigned char uncached_request_ready,
    unsigned char uncached_response_valid,
    long long uncached_response_data,
    unsigned char uncached_response_fault,
    unsigned char* address_request_valid,
    long long* address_request_address,
    char* address_request_size,
    unsigned char* address_request_write,
    unsigned char* address_request_execute,
    unsigned char* address_response_ready,
    unsigned char* instruction_request_valid,
    long long* instruction_request_address,
    unsigned char* instruction_response_ready,
    unsigned char* acquire_request_valid,
    long long* acquire_request_address,
    unsigned char* acquire_request_unique,
    unsigned char* acquire_response_ready,
    unsigned char* release_request_valid,
    long long* release_request_address,
    svBitVecVal* release_request_line,
    char* release_request_state,
    unsigned char* release_response_ready,
    unsigned char* snoop_ready,
    unsigned char* snoop_response_valid,
    char* snoop_response_state,
    unsigned char* snoop_response_pass_dirty,
    unsigned char* snoop_response_has_data,
    svBitVecVal* snoop_response_line,
    unsigned char* uncached_request_valid,
    long long* uncached_request_address,
    unsigned char* uncached_request_write,
    char* uncached_request_size,
    long long* uncached_request_data,
    char* uncached_request_mask,
    unsigned char* uncached_request_device) {
  const std::string scope = scope_name();
  if (reset != 0) {
    models().erase(scope);
  } else if (!models().contains(scope)) {
    Configuration configuration;
    configuration.hart_id = static_cast<std::uint64_t>(hart_id);
    configuration.reset_vector = static_cast<std::uint64_t>(reset_vector);
    configuration.xlen_is_64 = xlen_is_64 != 0;
    configuration.max_vaddr_bits = static_cast<std::uint8_t>(max_vaddr_bits);
    configuration.isa = packed_string(isa);
    configuration.privilege = packed_string(privilege);
    configuration.max_retired_instructions_per_cycle =
        static_cast<std::uint16_t>(max_retired_instructions_per_cycle);
    configuration.pmp_regions = pmp_regions;
    configuration.instruction_cache_sets = static_cast<std::uint16_t>(instruction_cache_sets);
    configuration.instruction_cache_ways = static_cast<std::uint16_t>(instruction_cache_ways);
    configuration.data_cache_sets = static_cast<std::uint16_t>(data_cache_sets);
    configuration.data_cache_ways = static_cast<std::uint16_t>(data_cache_ways);
    models().emplace(scope, std::make_unique<SpikeCoreModel>(std::move(configuration)));
  }

  Outputs outputs;
  if (reset == 0) {
    Inputs inputs;
    inputs.time = static_cast<std::uint64_t>(time);
    inputs.interrupts = interrupts;
    inputs.address_request_ready = address_request_ready != 0;
    inputs.address_response_valid = address_response_valid != 0;
    inputs.address_response_cacheable = address_response_cacheable != 0;
    inputs.address_response_instruction_cacheable = address_response_instruction_cacheable != 0;
    inputs.address_response_device = address_response_device != 0;
    inputs.address_response_atomic = address_response_atomic != 0;
    inputs.address_response_fault = address_response_fault != 0;
    inputs.instruction_request_ready = instruction_request_ready != 0;
    inputs.instruction_response_valid = instruction_response_valid != 0;
    inputs.instruction_response_line = unpack_line(instruction_response_line);
    inputs.instruction_response_fault = instruction_response_fault != 0;
    inputs.acquire_request_ready = acquire_request_ready != 0;
    inputs.acquire_response_valid = acquire_response_valid != 0;
    inputs.acquire_response_line = unpack_line(acquire_response_line);
    inputs.acquire_response_state = acquire_response_state;
    inputs.acquire_response_pass_dirty = acquire_response_pass_dirty != 0;
    inputs.acquire_response_fault = acquire_response_fault != 0;
    inputs.release_request_ready = release_request_ready != 0;
    inputs.release_response_valid = release_response_valid != 0;
    inputs.release_response_fault = release_response_fault != 0;
    inputs.snoop_valid = snoop_valid != 0;
    inputs.snoop_address = static_cast<std::uint64_t>(snoop_address);
    inputs.snoop_share = snoop_share != 0;
    inputs.snoop_invalidate = snoop_invalidate != 0;
    inputs.snoop_discard_dirty = snoop_discard_dirty != 0;
    inputs.snoop_return_to_source = snoop_return_to_source != 0;
    inputs.snoop_response_ready = snoop_response_ready != 0;
    inputs.uncached_request_ready = uncached_request_ready != 0;
    inputs.uncached_response_valid = uncached_response_valid != 0;
    inputs.uncached_response_data = static_cast<std::uint64_t>(uncached_response_data);
    inputs.uncached_response_fault = uncached_response_fault != 0;
    outputs = models().at(scope)->tick(inputs);
  }

  store(address_request_valid, static_cast<unsigned char>(outputs.address_request_valid));
  store(address_request_address, static_cast<long long>(outputs.address_request_address));
  store(address_request_size, static_cast<char>(outputs.address_request_size));
  store(address_request_write, static_cast<unsigned char>(outputs.address_request_write));
  store(address_request_execute, static_cast<unsigned char>(outputs.address_request_execute));
  store(address_response_ready, static_cast<unsigned char>(outputs.address_response_ready));
  store(instruction_request_valid, static_cast<unsigned char>(outputs.instruction_request_valid));
  store(instruction_request_address, static_cast<long long>(outputs.instruction_request_address));
  store(instruction_response_ready, static_cast<unsigned char>(outputs.instruction_response_ready));
  store(acquire_request_valid, static_cast<unsigned char>(outputs.acquire_request_valid));
  store(acquire_request_address, static_cast<long long>(outputs.acquire_request_address));
  store(acquire_request_unique, static_cast<unsigned char>(outputs.acquire_request_unique));
  store(acquire_response_ready, static_cast<unsigned char>(outputs.acquire_response_ready));
  store(release_request_valid, static_cast<unsigned char>(outputs.release_request_valid));
  store(release_request_address, static_cast<long long>(outputs.release_request_address));
  pack_line(outputs.release_request_line, release_request_line);
  store(release_request_state, static_cast<char>(outputs.release_request_state));
  store(release_response_ready, static_cast<unsigned char>(outputs.release_response_ready));
  store(snoop_ready, static_cast<unsigned char>(outputs.snoop_ready));
  store(snoop_response_valid, static_cast<unsigned char>(outputs.snoop_response_valid));
  store(snoop_response_state, static_cast<char>(outputs.snoop_response_state));
  store(snoop_response_pass_dirty, static_cast<unsigned char>(outputs.snoop_response_pass_dirty));
  store(snoop_response_has_data, static_cast<unsigned char>(outputs.snoop_response_has_data));
  pack_line(outputs.snoop_response_line, snoop_response_line);
  store(uncached_request_valid, static_cast<unsigned char>(outputs.uncached_request_valid));
  store(uncached_request_address, static_cast<long long>(outputs.uncached_request_address));
  store(uncached_request_write, static_cast<unsigned char>(outputs.uncached_request_write));
  store(uncached_request_size, static_cast<char>(outputs.uncached_request_size));
  store(uncached_request_data, static_cast<long long>(outputs.uncached_request_data));
  store(uncached_request_mask, static_cast<char>(outputs.uncached_request_mask));
  store(uncached_request_device, static_cast<unsigned char>(outputs.uncached_request_device));
  return static_cast<unsigned char>(outputs.uncached_response_ready);
}
