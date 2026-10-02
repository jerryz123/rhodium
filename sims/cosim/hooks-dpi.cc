// Converts passive DPI callbacks into typed observations without unwinding into SV.
// SPDX-License-Identifier: Apache-2.0
#include "hooks-dpi.h"
#include <stdexcept>
#include <exception>
#include <utility>

namespace {
using namespace rhodium::cosim::observation;
Collector* target = nullptr;
std::string failure;
template<class F> void receive(F&& call) noexcept {
  if (!failure.empty()) return;
  try {
    if (!target) throw std::runtime_error("cosim hook has no bound collector");
    call(*target);
  } catch (const std::exception& e) { failure = e.what(); }
    catch (...) { failure = "unknown cosim DPI failure"; }
}
}
namespace rhodium::cosim::observation {
DpiBinding::DpiBinding(Collector& collector) {
  if (target) throw std::runtime_error("cosim DPI already bound");
  if (!failure.empty()) throw std::runtime_error(failure);
  target = &collector;
}
DpiBinding::~DpiBinding() { target = nullptr; failure.clear(); }
void DpiBinding::check() const { if (!failure.empty()) throw std::runtime_error(failure); }
}

extern "C" void rhodium_cosim_instruction(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t pc, std::int64_t encoding, std::int64_t encoding_valid_bytes, std::int64_t instruction_bytes, std::int64_t privilege, std::int64_t virtualized, std::int64_t producers) noexcept {
  receive([&](Collector& c) {
    const auto u_instance = static_cast<Word>(instance);
    const auto u_epoch = static_cast<Word>(epoch);
    const auto u_order = static_cast<Word>(order);
    const auto u_pc = static_cast<Word>(pc);
    const auto u_encoding = static_cast<Word>(encoding);
    const auto u_encoding_valid_bytes = static_cast<Word>(encoding_valid_bytes);
    const auto u_instruction_bytes = static_cast<Word>(instruction_bytes);
    const auto u_privilege = static_cast<Word>(privilege);
    const auto u_virtualized = static_cast<Word>(virtualized);
    if (u_virtualized > 1) throw std::runtime_error("invalid boolean DPI lane");
    const auto u_producers = static_cast<Word>(producers);
    const Id id{u_epoch, u_order};
    c.instruction(u_instance, id, {u_pc, u_encoding, u_encoding_valid_bytes, u_instruction_bytes, {u_privilege, bool(u_virtualized)}, u_producers});
  });
}

extern "C" void rhodium_cosim_retire(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t next_pc, std::int64_t privilege, std::int64_t virtualized) noexcept {
  receive([&](Collector& c) {
    const auto u_instance = static_cast<Word>(instance);
    const auto u_epoch = static_cast<Word>(epoch);
    const auto u_order = static_cast<Word>(order);
    const auto u_next_pc = static_cast<Word>(next_pc);
    const auto u_privilege = static_cast<Word>(privilege);
    const auto u_virtualized = static_cast<Word>(virtualized);
    if (u_virtualized > 1) throw std::runtime_error("invalid boolean DPI lane");
    const Id id{u_epoch, u_order};
    c.retire(u_instance, id, {u_next_pc, {u_privilege, bool(u_virtualized)}});
  });
}

extern "C" void rhodium_cosim_exception(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t cause, std::int64_t epc, std::int64_t tval, std::int64_t target_pc, std::int64_t privilege, std::int64_t virtualized, std::int64_t guest_valid, std::int64_t htval, std::int64_t htinst) noexcept {
  receive([&](Collector& c) {
    const auto u_instance = static_cast<Word>(instance);
    const auto u_epoch = static_cast<Word>(epoch);
    const auto u_order = static_cast<Word>(order);
    const auto u_cause = static_cast<Word>(cause);
    const auto u_epc = static_cast<Word>(epc);
    const auto u_tval = static_cast<Word>(tval);
    const auto u_target_pc = static_cast<Word>(target_pc);
    const auto u_privilege = static_cast<Word>(privilege);
    const auto u_virtualized = static_cast<Word>(virtualized);
    if (u_virtualized > 1) throw std::runtime_error("invalid boolean DPI lane");
    const auto u_guest_valid = static_cast<Word>(guest_valid);
    if (u_guest_valid > 1) throw std::runtime_error("invalid boolean DPI lane");
    const auto u_htval = static_cast<Word>(htval);
    const auto u_htinst = static_cast<Word>(htinst);
    const Id id{u_epoch, u_order};
    c.exception(u_instance, id, Trap{u_cause, u_epc, u_tval, u_target_pc, {u_privilege, bool(u_virtualized)}, bool(u_guest_valid), u_htval, u_htinst});
  });
}

extern "C" void rhodium_cosim_interrupt(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t cause, std::int64_t epc, std::int64_t tval, std::int64_t target_pc, std::int64_t privilege, std::int64_t virtualized, std::int64_t guest_valid, std::int64_t htval, std::int64_t htinst, std::int64_t producers) noexcept {
  receive([&](Collector& c) {
    const auto u_instance = static_cast<Word>(instance);
    const auto u_epoch = static_cast<Word>(epoch);
    const auto u_order = static_cast<Word>(order);
    const auto u_cause = static_cast<Word>(cause);
    const auto u_epc = static_cast<Word>(epc);
    const auto u_tval = static_cast<Word>(tval);
    const auto u_target_pc = static_cast<Word>(target_pc);
    const auto u_privilege = static_cast<Word>(privilege);
    const auto u_virtualized = static_cast<Word>(virtualized);
    if (u_virtualized > 1) throw std::runtime_error("invalid boolean DPI lane");
    const auto u_guest_valid = static_cast<Word>(guest_valid);
    if (u_guest_valid > 1) throw std::runtime_error("invalid boolean DPI lane");
    const auto u_htval = static_cast<Word>(htval);
    const auto u_htinst = static_cast<Word>(htinst);
    const auto u_producers = static_cast<Word>(producers);
    const Id id{u_epoch, u_order};
    c.interrupt(u_instance, id, {Trap{u_cause, u_epc, u_tval, u_target_pc, {u_privilege, bool(u_virtualized)}, bool(u_guest_valid), u_htval, u_htinst}, u_producers});
  });
}

extern "C" void rhodium_cosim_reg_write(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t producer, std::int64_t effect_index, std::int64_t bank, std::int64_t register_index, std::int64_t bit_offset, std::int64_t mask, std::int64_t value) noexcept {
  receive([&](Collector& c) {
    const auto u_instance = static_cast<Word>(instance);
    const auto u_epoch = static_cast<Word>(epoch);
    const auto u_order = static_cast<Word>(order);
    const auto u_producer = static_cast<Word>(producer);
    const auto u_effect_index = static_cast<Word>(effect_index);
    const auto u_bank = static_cast<Word>(bank);
    const auto u_register_index = static_cast<Word>(register_index);
    const auto u_bit_offset = static_cast<Word>(bit_offset);
    const auto u_mask = static_cast<Word>(mask);
    const auto u_value = static_cast<Word>(value);
    const Id id{u_epoch, u_order};
    c.effect(u_instance, id, {u_producer, u_effect_index}, RegisterWrite{Bank(u_bank), u_register_index, u_bit_offset, u_mask, u_value});
  });
}

extern "C" void rhodium_cosim_csr_update(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t producer, std::int64_t effect_index, std::int64_t address, std::int64_t operation, std::int64_t mask, std::int64_t value) noexcept {
  receive([&](Collector& c) {
    const auto u_instance = static_cast<Word>(instance);
    const auto u_epoch = static_cast<Word>(epoch);
    const auto u_order = static_cast<Word>(order);
    const auto u_producer = static_cast<Word>(producer);
    const auto u_effect_index = static_cast<Word>(effect_index);
    const auto u_address = static_cast<Word>(address);
    const auto u_operation = static_cast<Word>(operation);
    const auto u_mask = static_cast<Word>(mask);
    const auto u_value = static_cast<Word>(value);
    const Id id{u_epoch, u_order};
    c.effect(u_instance, id, {u_producer, u_effect_index}, CsrUpdate{u_address, CsrOperation(u_operation), u_mask, u_value});
  });
}

extern "C" void rhodium_cosim_memory(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t producer, std::int64_t effect_index, std::int64_t access_id, std::int64_t fragment_offset, std::int64_t kind, std::int64_t virtual_address, std::int64_t physical_valid, std::int64_t physical_address, std::int64_t byte_mask, std::int64_t read_valid, std::int64_t write_valid, std::int64_t read_data, std::int64_t write_data, std::int64_t result) noexcept {
  receive([&](Collector& c) {
    const auto u_instance = static_cast<Word>(instance);
    const auto u_epoch = static_cast<Word>(epoch);
    const auto u_order = static_cast<Word>(order);
    const auto u_producer = static_cast<Word>(producer);
    const auto u_effect_index = static_cast<Word>(effect_index);
    const auto u_access_id = static_cast<Word>(access_id);
    const auto u_fragment_offset = static_cast<Word>(fragment_offset);
    const auto u_kind = static_cast<Word>(kind);
    const auto u_virtual_address = static_cast<Word>(virtual_address);
    const auto u_physical_valid = static_cast<Word>(physical_valid);
    if (u_physical_valid > 1) throw std::runtime_error("invalid boolean DPI lane");
    const auto u_physical_address = static_cast<Word>(physical_address);
    const auto u_byte_mask = static_cast<Word>(byte_mask);
    const auto u_read_valid = static_cast<Word>(read_valid);
    if (u_read_valid > 1) throw std::runtime_error("invalid boolean DPI lane");
    const auto u_write_valid = static_cast<Word>(write_valid);
    if (u_write_valid > 1) throw std::runtime_error("invalid boolean DPI lane");
    const auto u_read_data = static_cast<Word>(read_data);
    const auto u_write_data = static_cast<Word>(write_data);
    const auto u_result = static_cast<Word>(result);
    const Id id{u_epoch, u_order};
    c.effect(u_instance, id, {u_producer, u_effect_index}, MemoryEffect{u_access_id, u_fragment_offset, AccessKind(u_kind), u_virtual_address, bool(u_physical_valid), u_physical_address, u_byte_mask, bool(u_read_valid), bool(u_write_valid), u_read_data, u_write_data, AccessResult(u_result)});
  });
}

extern "C" void rhodium_cosim_seal(std::int64_t instance, std::int64_t epoch, std::int64_t order, std::int64_t producer, std::int64_t count) noexcept {
  receive([&](Collector& c) {
    const auto u_instance = static_cast<Word>(instance);
    const auto u_epoch = static_cast<Word>(epoch);
    const auto u_order = static_cast<Word>(order);
    const auto u_producer = static_cast<Word>(producer);
    const auto u_count = static_cast<Word>(count);
    const Id id{u_epoch, u_order};
    c.seal(u_instance, id, u_producer, u_count);
  });
}
