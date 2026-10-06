// Declares the generated-type-free C++ boundary for one embedded Sail reference hart.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <map>
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
  bool page_table = false;
};

// Environmental reads supply physical bytes before Sail's load extension/ALU use.
struct MemoryRead {
  std::uint64_t address;
  std::vector<std::uint8_t> value;
};

struct StepInputs {
  enum class WaitRelease { None, Timeout, WrsEarlyWake };
  bool machine_external_interrupt = false;
  bool supervisor_external_interrupt = false;
  std::uint64_t interrupt_inputs = 0;
  bool interrupt_boundary = true;
  WaitRelease wait_release = WaitRelease::None;
  // One-way nondeterminism: permit SC to fail, never manufacture a reservation.
  bool sc_failure = false;
  // Environmental time is independent of instruction count and host wall time.
  std::uint64_t time = 0;
  std::uint64_t clock_ticks = 0;
  // Raw implemented HPM values before CSR semantics, and preceding wrap events.
  std::map<unsigned, std::uint64_t> hpm_counters;
  std::uint64_t hpm_overflows = 0;
  std::vector<MemoryRead> device_reads;
  std::vector<MemoryRead> external_reads;
};

struct Trap {
  bool interrupt;
  std::uint64_t cause;
  std::uint64_t epc = 0, tval = 0;
};

// Decoded from Sail's fetched instruction and pre-execution state, using Sail's
// pointer-masking policy. Preserves an effective SC address/width even on failure.
struct AtomicAccess {
  enum class Kind { Amo, Lr, Sc };
  Kind kind;
  std::uint64_t virtual_address;
  unsigned bytes;
  std::optional<std::uint64_t> physical_address;
};

struct StepResult {
  std::uint64_t pc = 0;
  std::uint64_t next_pc = 0;
  unsigned privilege_before = 0;
  unsigned privilege_after = 0;
  bool virtualized_before = false;
  bool virtualized_after = false;
  std::optional<std::uint32_t> instruction;
  unsigned instruction_bytes = 0;
  bool retired = false;
  bool waiting = false;
  std::optional<Trap> trap;
  std::optional<AtomicAccess> atomic;
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
  // Register externally mutable RAM without changing its backing or PMA type.
  void external_memory(MemoryRange range);
  bool externally_mutable(std::uint64_t address, std::size_t bytes) const;
  std::vector<std::uint8_t> read_memory(std::uint64_t address, std::size_t bytes) const;
  StepResult step(const StepInputs& inputs = {});
  std::uint64_t integer_register(unsigned index) const;
  std::vector<std::uint8_t> vector_register(unsigned index) const;
  std::uint64_t csr(unsigned address) const;
  std::uint64_t pc() const;
  unsigned xlen() const;
  unsigned flen() const;
  bool software_managed_ad() const;

 private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace rhodium::cosim
