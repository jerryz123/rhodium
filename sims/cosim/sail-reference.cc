// Embeds the pinned Sail model with private RAM and strictly replayed device reads.
// SPDX-License-Identifier: Apache-2.0
#include "sail-reference.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <stdexcept>
#include <utility>

#include "config_utils.h"
#include "sail_config.h"
#include "sail_riscv_model.h"

namespace rhodium::cosim {
namespace {
std::atomic_flag live_model = ATOMIC_FLAG_INIT;

// Own the global runtime lease even when construction or model initialization fails.
struct RuntimeLease {
  RuntimeLease() {
    if (live_model.test_and_set()) throw std::logic_error("only one Sail reference may be live");
  }
  ~RuntimeLease() { live_model.clear(); }
};

std::vector<std::uint8_t> bytes_of(std::uint64_t value, std::size_t size) {
  std::vector<std::uint8_t> bytes(size);
  for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
  return bytes;
}

std::vector<std::uint8_t> bytes_of(lbits value) {
  std::vector<std::uint8_t> bytes((value.len + 7) / 8, 0);
  std::size_t count = 0;
  mpz_export(bytes.data(), &count, -1, 1, 0, 0, *value.bits);
  return bytes;
}

std::uint64_t encoded_bits(const jsoncons::json& value) {
  auto text = value.at("value").as<std::string>();
  std::erase(text, '_');
  const bool binary = text.starts_with("0b");
  if (binary) text.erase(0, 2);
  std::size_t consumed = 0;
  const auto result = std::stoull(text, &consumed, binary ? 2 : 0);
  if (consumed != text.size()) throw std::invalid_argument("invalid configured address");
  return result;
}
}  // namespace

class SailReference::Implementation final : private RuntimeLease, public hart::Model {
 public:
  Implementation(const std::string& configuration, std::uint64_t reset_pc,
                 std::span<const MemoryRange> private_memory) {
    const auto json = jsoncons::json::parse(configuration);
    validate_config_schema(json, "co-simulation configuration");
    // These built-in devices would consume accesses before our memory provider.
    if (json.at("platform").at("clint").at("supported").as<bool>() ||
        json.at("platform").at("simple_interrupt_generator").at("supported").as<bool>())
      throw std::invalid_argument("co-simulation requires external devices, not Sail CLINT/SIG");
    for (const auto& entry : json.at("memory").at("regions").array_range()) {
      const auto base = encoded_bits(entry.at("base"));
      const auto size = encoded_bits(entry.at("size"));
      if (!size || size - 1 > UINT64_MAX - base) throw std::invalid_argument("invalid memory range");
      regions_.push_back({base, size});
    }
    for (const auto& range : private_memory) {
      if (!range.size || range.size - 1 > UINT64_MAX - range.address)
        throw std::invalid_argument("invalid backing-memory range");
      region(range.address, range.size);
      for (const auto& other : backing_)
        if (range.address <= other.address + other.size - 1 && other.address <= range.address + range.size - 1)
          throw std::invalid_argument("overlapping backing-memory ranges");
      backing_.push_back(range);
    }
    sail_config_set_string(configuration.c_str());
    model_init();
    initialized_ = true;
    try {
      if (!zconfig_is_valid(UNIT)) throw std::invalid_argument("invalid Sail architectural configuration");
      if (zxlen == 32 && reset_pc > UINT32_MAX) throw std::invalid_argument("reset PC exceeds XLEN");
      zset_pc_reset_address(reset_pc);
      zinit_model("");
      // No standalone boot arguments, DTB generation, ELF loader, or HTIF instance.
      if (have_exception) throw std::runtime_error("Sail initialization raised a model exception");
    } catch (...) {
      model_fini();
      sail_config_cleanup();
      initialized_ = false;
      throw;
    }
  }

  ~Implementation() {
    if (initialized_) {
      model_fini();
      sail_config_cleanup();
    }
  }

  void load(std::uint64_t address, std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return;
    if (!backed(address, bytes.size())) throw std::invalid_argument("cannot load a device region");
    write_bytes(address, bytes);
  }

  std::vector<std::uint8_t> read(std::uint64_t address, std::size_t size) const {
    if (!size) return {};
    if (!backed(address, size)) throw std::invalid_argument("cannot inspect device memory");
    std::vector<std::uint8_t> bytes(size, 0);
    for (std::size_t i = 0; i < size; ++i) {
      auto page = pages_.find((address + i) >> 12);
      if (page != pages_.end()) bytes[i] = page->second[(address + i) & 4095];
    }
    return bytes;
  }

  StepResult step(const StepInputs& inputs) {
    if (poisoned_) throw std::logic_error("Sail reference is poisoned after a failed step");
    inputs_ = &inputs;
    next_read_ = 0;
    result_ = {};
    result_.pc = zPC.bits;
    result_.privilege_before = privilege();
    sail_int step_number;
    CREATE(sail_int)(&step_number);
    CONVERT_OF(sail_int, mach_int)(&step_number, attempts_);
    try {
      for (std::uint64_t tick = 0; tick < inputs.clock_ticks; ++tick) ztick_clock(UNIT);
      result_.waiting = ztry_step(step_number, inputs.wake_wait);
      if (have_exception) throw std::runtime_error("Sail raised an internal model exception");
      if (next_read_ != inputs.device_reads.size()) throw std::runtime_error("unconsumed MMIO read replay");
      result_.next_pc = zPC.bits;
      result_.privilege_after = privilege();
      if (result_.trap) {
        result_.trap->epc = privilege() == 3 ? zmepc.bits : zsepc.bits;
        result_.trap->tval = privilege() == 3 ? zmtval.bits : zstval.bits;
      }
      ++attempts_;
      inputs_ = nullptr;
      KILL(sail_int)(&step_number);
      return std::move(result_);
    } catch (...) {
      KILL(sail_int)(&step_number);
      inputs_ = nullptr;
      poisoned_ = true;
      throw;
    }
  }

 private:
  struct Region { std::uint64_t base, size; };
  const Region& region(std::uint64_t address, std::size_t size) const {
    for (const auto& entry : regions_)
      if (size && address >= entry.base && address - entry.base < entry.size &&
          size <= entry.size - (address - entry.base)) return entry;
    throw std::out_of_range("physical memory provider access outside configured region");
  }
  bool backed(std::uint64_t address, std::size_t size) const {
    region(address, size);
    for (const auto& range : backing_)
      if (address >= range.address && address - range.address < range.size &&
          size <= range.size - (address - range.address)) return true;
    for (const auto& range : backing_)
      if (address <= range.address + range.size - 1 && range.address <= address + size - 1)
        throw std::out_of_range("physical access crosses a backing-memory boundary");
    return false;
  }
  void write_bytes(std::uint64_t address, std::span<const std::uint8_t> bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) pages_[(address + i) >> 12][(address + i) & 4095] = bytes[i];
  }
  unsigned privilege() const {
    switch (zcur_privilege) {
      case hart::zUser: return 0;
      case hart::zSupervisor: return 1;
      case hart::zMachine: return 3;
      default: throw std::runtime_error("unknown Sail privilege");
    }
  }

  bool host_memory_enabled(unit) override { return true; }
  void host_memory_read(lbits* output, fbits address, int64_t width, bool instruction) override {
    const bool device = !backed(address, width);
    std::vector<std::uint8_t> bytes;
    if (device) {
      if (instruction || !inputs_ || next_read_ == inputs_->device_reads.size())
        throw std::runtime_error("unexpected MMIO read");
      const auto& replay = inputs_->device_reads[next_read_++];
      if (replay.address != address || replay.value.size() != static_cast<std::size_t>(width))
        throw std::runtime_error("MMIO replay address or width mismatch");
      bytes = replay.value;
    } else {
      bytes = read(address, width);
    }
    output->len = width * 8;
    mpz_import(*output->bits, bytes.size(), -1, 1, 0, 0, bytes.data());
    result_.memory.push_back({address, std::move(bytes), false, instruction, device});
  }
  bool host_memory_write(fbits address, int64_t width, lbits value) override {
    const bool device = !backed(address, width);
    auto bytes = bytes_of(value);
    if (!device) write_bytes(address, bytes);
    result_.memory.push_back({address, std::move(bytes), true, false, device});
    return true;
  }
  fbits host_external_interrupts(unit) override {
    return inputs_ ? (std::uint64_t(inputs_->machine_external_interrupt) << 11) |
                     (std::uint64_t(inputs_->supervisor_external_interrupt) << 9) : 0;
  }
  bool host_time_enabled(unit) override { return true; }
  fbits host_time(unit) override { return inputs_ ? inputs_->time : 0; }
  unit fetch_callback(sbits opcode) override {
    result_.instruction = static_cast<std::uint32_t>(opcode.bits);
    result_.instruction_bytes = opcode.len / 8;
    return UNIT;
  }
  unit xreg_full_write_callback(const_sail_string, sbits reg, sbits value) override {
    if (reg.bits) result_.writes.push_back({RegisterWrite::Bank::Integer, static_cast<unsigned>(reg.bits), bytes_of(value.bits, value.len / 8)});
    return UNIT;
  }
  unit freg_write_callback(unsigned reg, sbits value) override {
    result_.writes.push_back({RegisterWrite::Bank::FloatingPoint, reg, bytes_of(value.bits, value.len / 8)});
    return UNIT;
  }
  unit vreg_write_callback(unsigned reg, lbits value) override {
    result_.writes.push_back({RegisterWrite::Bank::Vector, reg, bytes_of(value)});
    return UNIT;
  }
  unit csr_full_write_callback(const_sail_string, unsigned reg, sbits value) override {
    result_.writes.push_back({RegisterWrite::Bank::Csr, reg, bytes_of(value.bits, value.len / 8)});
    return UNIT;
  }
  unit trap_callback(bool interrupt, fbits cause) override {
    result_.trap = Trap{interrupt, cause};
    return UNIT;
  }
  unit instret_callback(unit) override { result_.retired = true; return UNIT; }
  bool sys_enable_experimental_extensions(unit) override { return false; }

  bool initialized_ = false;
  bool poisoned_ = false;
  int64_t attempts_ = 0;
  const StepInputs* inputs_ = nullptr;
  std::size_t next_read_ = 0;
  StepResult result_;
  std::vector<Region> regions_;
  std::vector<MemoryRange> backing_;
  std::map<std::uint64_t, std::array<std::uint8_t, 4096>> pages_;
};

SailReference::SailReference(const std::string& json, std::uint64_t reset_pc, std::span<const MemoryRange> private_memory)
    : implementation_(std::make_unique<Implementation>(json, reset_pc, private_memory)) {}
SailReference::~SailReference() = default;
void SailReference::load(std::uint64_t address, std::span<const std::uint8_t> bytes) { implementation_->load(address, bytes); }
std::vector<std::uint8_t> SailReference::read_memory(std::uint64_t address, std::size_t size) const { return implementation_->read(address, size); }
StepResult SailReference::step(const StepInputs& inputs) { return implementation_->step(inputs); }
std::uint64_t SailReference::pc() const { return implementation_->zPC.bits; }
std::uint64_t SailReference::csr(unsigned address) const {
  if (address >= 4096) throw std::out_of_range("CSR address");
  return implementation_->zread_CSR(address).bits;
}
std::uint64_t SailReference::integer_register(unsigned index) const {
  if (index >= 32) throw std::out_of_range("integer register index");
  return implementation_->zrX(index).bits;
}

}  // namespace rhodium::cosim
