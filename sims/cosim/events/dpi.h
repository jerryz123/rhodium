// Declares the fixed-lane DPI ABI and its simulation-thread binding.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "collector.h"
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <utility>

namespace rhodium::cosim::observation {
// Native implementation adapters collect unordered callbacks and resolve them
// at the settled sample boundary. Their state belongs to one DPI binding.
class DpiAdapter {
 public:
  virtual ~DpiAdapter() = default;
  virtual void flush(Collector& collector) = 0;
};
void dpi_receive(const std::function<void(Collector&)>& call) noexcept;
DpiAdapter& dpi_adapter(std::type_index type, const std::function<std::unique_ptr<DpiAdapter>()>& create);
template<class Adapter, class F> void dpi_receive_adapter(F&& call) noexcept {
  dpi_receive([&](Collector& collector) {
    auto& adapter = static_cast<Adapter&>(dpi_adapter(typeid(Adapter), [] { return std::make_unique<Adapter>(); }));
    call(adapter, collector);
  });
}
// Check after every evaluated edge, before Collector::end_sample/finish.
// No C++ exception is allowed to unwind across a simulator DPI callback.
struct DpiState;
class DpiBinding {
 public:
  explicit DpiBinding(Collector& collector);
  ~DpiBinding();
  DpiBinding(const DpiBinding&) = delete;
  DpiBinding& operator=(const DpiBinding&) = delete;
  void check() const;
 private:
  std::unique_ptr<DpiState> state_;
};
}

extern "C" {
void rhodium_cosim_environment(std::int64_t instance, std::int64_t interrupts, std::int64_t time, std::int64_t cycle, std::int64_t interrupt_boundary) noexcept;
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
