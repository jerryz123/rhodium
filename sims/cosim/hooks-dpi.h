// Declares the fixed-lane DPI ABI and its simulation-thread binding.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observation.h"
#include <string>

namespace rhodium::cosim::observation {
// Check after every evaluated edge, before Collector::end_sample/finish.
// No C++ exception is allowed to unwind across a simulator DPI callback.
class DpiBinding {
 public:
  explicit DpiBinding(Collector& collector);
  ~DpiBinding();
  DpiBinding(const DpiBinding&) = delete;
  DpiBinding& operator=(const DpiBinding&) = delete;
  void check() const;
};
}

extern "C" {
std::int64_t rhodium_cosim_epoch(std::int64_t instance) noexcept;
void rhodium_cosim_instruction(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t pc, std::int64_t encoding, std::int64_t encoding_valid_bytes, std::int64_t instruction_bytes, std::int64_t privilege, std::int64_t virtualized, std::int64_t producers) noexcept;
void rhodium_cosim_retire(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t next_pc, std::int64_t privilege, std::int64_t virtualized) noexcept;
void rhodium_cosim_exception(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t cause, std::int64_t epc, std::int64_t tval, std::int64_t target_pc, std::int64_t privilege, std::int64_t virtualized, std::int64_t guest_valid, std::int64_t htval, std::int64_t htinst) noexcept;
void rhodium_cosim_interrupt(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t cause, std::int64_t epc, std::int64_t tval, std::int64_t target_pc, std::int64_t privilege, std::int64_t virtualized, std::int64_t guest_valid, std::int64_t htval, std::int64_t htinst, std::int64_t producers) noexcept;
void rhodium_cosim_reg_write(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t producer, std::int64_t effect_index, std::int64_t bank, std::int64_t register_index, std::int64_t bit_offset, std::int64_t mask, std::int64_t value) noexcept;
void rhodium_cosim_csr_update(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t producer, std::int64_t effect_index, std::int64_t address, std::int64_t operation, std::int64_t mask, std::int64_t value) noexcept;
void rhodium_cosim_memory(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t producer, std::int64_t effect_index, std::int64_t access_id, std::int64_t fragment_offset, std::int64_t kind, std::int64_t virtual_address, std::int64_t physical_valid, std::int64_t physical_address, std::int64_t byte_mask, std::int64_t read_valid, std::int64_t write_valid, std::int64_t read_data, std::int64_t write_data, std::int64_t result) noexcept;
void rhodium_cosim_seal(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t producer, std::int64_t count) noexcept;
}
