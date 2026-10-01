// Declares the model-independent C++ boundary for one embedded Sail reference hart.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rhodium::cosim {

// Physical ranges backed by private ROM/RAM, independently of Sail's PMA type.
struct MemoryRange { std::uint64_t address, size; };

// Bytes are always in architectural little-endian order, including vector values.
struct RegisterWrite {
  enum class Bank { Integer, FloatingPoint, Vector, Csr };
  Bank bank;
  unsigned index;
  std::vector<std::uint8_t> value;
};

struct MemoryAccess {
  std::uint64_t address;
  std::vector<std::uint8_t> value;
  bool write;
  bool instruction;
  bool device;
};

// A device read supplies bytes at the memory boundary, before load extension/ALU use.
struct DeviceRead {
  std::uint64_t address;
  std::vector<std::uint8_t> value;
};

struct StepInputs {
  bool machine_external_interrupt = false;
  bool supervisor_external_interrupt = false;
  bool wake_wait = false;
  // Environmental time is independent of instruction count and host wall time.
  std::uint64_t time = 0;
  std::uint64_t clock_ticks = 0;
  std::vector<DeviceRead> device_reads;
};

struct Trap {
  bool interrupt;
  std::uint64_t cause;
};

struct StepResult {
  std::uint64_t pc = 0;
  std::uint64_t next_pc = 0;
  unsigned privilege_before = 0;
  unsigned privilege_after = 0;
  std::optional<std::uint32_t> instruction;
  unsigned instruction_bytes = 0;
  bool retired = false;
  bool waiting = false;
  std::optional<Trap> trap;
  std::vector<RegisterWrite> writes;
  std::vector<MemoryAccess> memory;
};

// Sail's configuration/runtime is process-global: this first adapter permits one
// live instance, used on one host thread. A failed step poisons that instance.
class SailReference {
 public:
  SailReference(const std::string& configuration_json, std::uint64_t reset_pc,
                std::span<const MemoryRange> private_memory);
  ~SailReference();
  SailReference(const SailReference&) = delete;
  SailReference& operator=(const SailReference&) = delete;

  void load(std::uint64_t address, std::span<const std::uint8_t> bytes);
  std::vector<std::uint8_t> read_memory(std::uint64_t address, std::size_t bytes) const;
  StepResult step(const StepInputs& inputs = {});
  std::uint64_t integer_register(unsigned index) const;
  std::uint64_t pc() const;

 private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace rhodium::cosim
