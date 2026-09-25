// Implements a coroutine-driven Spike hart with software L1 caches and typed RTL transactions.
// SPDX-License-Identifier: Apache-2.0
#include "spike_core.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cfg.h"
#include "context.h"
#include "encoding.h"
#include "mmu.h"
#include "processor.h"
#include "simif.h"

namespace rhodium::spike {
namespace {

constexpr std::size_t kLineBytes = 64;

enum class CacheState : std::uint8_t {
  kInvalid = 0,
  kSharedClean = 1,
  kUniqueClean = 2,
  kSharedDirty = 3,
  kUniqueDirty = 4,
};

struct AddressResponse {
  bool cacheable = false;
  bool instruction_cacheable = false;
  bool device = false;
  bool atomic = false;
  bool fault = false;
};

struct CacheLine {
  bool valid = false;
  std::uint64_t tag = 0;
  CacheState state = CacheState::kInvalid;
  std::array<std::uint8_t, kLineBytes> bytes{};
};

std::uint8_t size_code(std::size_t size) {
  switch (size) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
    default: throw std::invalid_argument("Spike transaction size must be 1, 2, 4, or 8 bytes");
  }
}

std::size_t bounded_chunk(std::uint64_t address, std::size_t remaining) {
  const std::size_t boundary = 8 - static_cast<std::size_t>(address & 7);
  std::size_t chunk = std::min(remaining, boundary);
  if (chunk >= 8) return 8;
  if (chunk >= 4 && (address & 3) == 0) return 4;
  if (chunk >= 2 && (address & 1) == 0) return 2;
  return 1;
}

CacheState acquired_state(std::uint8_t response_state, bool pass_dirty) {
  switch (response_state & 3) {
    case 1: return pass_dirty ? CacheState::kSharedDirty : CacheState::kSharedClean;
    case 2: return pass_dirty ? CacheState::kUniqueDirty : CacheState::kUniqueClean;
    case 3: return CacheState::kSharedDirty;
    default: return CacheState::kInvalid;
  }
}

}  // namespace

class SpikeCoreModel::Implementation final : public simif_t {
 public:
  explicit Implementation(Configuration configuration)
      : configuration_(std::move(configuration)),
        instruction_cache_(checked_line_count(configuration_.instruction_cache_sets,
                                              configuration_.instruction_cache_ways)),
        data_cache_(checked_line_count(configuration_.data_cache_sets,
                                       configuration_.data_cache_ways)),
        instruction_next_way_(configuration_.instruction_cache_sets, 0),
        data_next_way_(configuration_.data_cache_sets, 0) {
    cfg_.isa = configuration_.isa.c_str();
    cfg_.priv = configuration_.privilege.c_str();
    cfg_.pmpregions = configuration_.pmp_regions;
    cfg_.pmpgranularity = 4;
    cfg_.cache_blocksz = kLineBytes;
    cfg_.hartids = {static_cast<std::size_t>(configuration_.hart_id)};
    cfg_.explicit_hartids = true;
    cfg_.start_pc.set_global(configuration_.reset_vector);
    processor_ = std::make_unique<processor_t>(
        cfg_.isa, cfg_.priv, &cfg_, this,
        static_cast<std::uint32_t>(configuration_.hart_id), false, nullptr,
        std::cerr);
    const auto& isa = processor_->get_isa();
    if (isa.get_max_xlen() != (configuration_.xlen_is_64 ? 64 : 32))
      throw std::invalid_argument("Spike ISA XLEN does not match the hart configuration");
    if (isa.get_vlen() != configuration_.vector_length ||
        isa.get_elen() != configuration_.vector_element_width)
      throw std::invalid_argument("Spike ISA vector geometry does not match the hart configuration");
    if (configuration_.max_vaddr_bits != 0 && configuration_.max_vaddr_bits != 39)
      throw std::invalid_argument("Spike adapter supports only Bare and Sv39 translation");
    processor_->set_max_vaddr_bits(configuration_.max_vaddr_bits);
    processor_->reset();
    processor_->get_state()->pc = configuration_.reset_vector;
    harts_.emplace(static_cast<std::size_t>(configuration_.hart_id), processor_.get());
    target_.init(&Implementation::run_trampoline, this);
  }

  Outputs tick(const Inputs& inputs) {
    inputs_ = inputs;
    const bool snoop_fire = inputs_.snoop_valid && !snoop_response_valid_;
    accept_previous_outputs();
    accept_responses();
    service_snoop(snoop_fire);
    update_architectural_inputs();
    host_ = context_t::current();
    target_.switch_to();
    return outputs();
  }

  char* addr_to_mem(reg_t) override { return nullptr; }
  bool reservable(reg_t) override { return true; }
  bool lrsc_accessible(reg_t address, std::size_t length, bool write) override {
    const AddressResponse attributes = classify(address, length, write, false);
    return !attributes.fault && attributes.atomic;
  }

  bool mmio_fetch(reg_t address, std::size_t length, std::uint8_t* bytes) override {
    const AddressResponse attributes = classify(address, length, false, true);
    if (attributes.fault) return false;
    return attributes.cacheable && attributes.instruction_cacheable
               ? instruction_read(address, length, bytes)
               : uncached_read(address, length, attributes.device, bytes);
  }

  bool mmio_load(reg_t address, std::size_t length, std::uint8_t* bytes) override {
    const AddressResponse attributes = classify(address, length, false, false);
    if (attributes.fault) return false;
    return attributes.cacheable ? data_read(address, length, bytes)
                                : uncached_read(address, length, attributes.device, bytes);
  }

  bool mmio_store(reg_t address, std::size_t length, const std::uint8_t* bytes) override {
    const AddressResponse attributes = classify(address, length, true, false);
    if (attributes.fault) return false;
    return attributes.cacheable ? data_write(address, length, bytes)
                                : uncached_write(address, length, attributes.device, bytes);
  }

  void flush_icache() override { invalidate_instruction_cache(); }
  void proc_reset(unsigned) override {}
  const cfg_t& get_cfg() const override { return cfg_; }
  const std::map<std::size_t, processor_t*>& get_harts() const override { return harts_; }
  const char* get_symbol(std::uint64_t) override { return nullptr; }

 private:
  static std::size_t checked_line_count(std::uint16_t sets, std::uint16_t ways) {
    if (sets == 0 || ways == 0) throw std::invalid_argument("Spike cache geometry must be nonzero");
    return static_cast<std::size_t>(sets) * ways;
  }

  static void run_trampoline(void* opaque) {
    static_cast<Implementation*>(opaque)->run();
  }

  [[noreturn]] void run() {
    for (;;) {
      processor_->step(configuration_.max_retired_instructions_per_cycle);
      yield();
    }
  }

  void yield() {
    assert(host_ != nullptr);
    host_->switch_to();
  }

  void accept_previous_outputs() {
    if (address_request_valid_ && inputs_.address_request_ready) address_request_valid_ = false;
    if (instruction_request_valid_ && inputs_.instruction_request_ready) instruction_request_valid_ = false;
    if (acquire_request_valid_ && inputs_.acquire_request_ready) acquire_request_valid_ = false;
    if (release_request_valid_ && inputs_.release_request_ready) release_request_valid_ = false;
    if (snoop_response_valid_ && inputs_.snoop_response_ready) snoop_response_valid_ = false;
    if (uncached_request_valid_ && inputs_.uncached_request_ready) uncached_request_valid_ = false;
  }

  void accept_responses() {
    if (address_response_waiting_ && inputs_.address_response_valid) {
      address_response_ = AddressResponse{
          inputs_.address_response_cacheable,
          inputs_.address_response_instruction_cacheable,
          inputs_.address_response_device,
          inputs_.address_response_atomic,
          inputs_.address_response_fault};
      address_response_waiting_ = false;
    }
    if (instruction_response_waiting_ && inputs_.instruction_response_valid) {
      instruction_response_line_ = inputs_.instruction_response_line;
      instruction_response_fault_ = inputs_.instruction_response_fault;
      instruction_response_waiting_ = false;
    }
    if (acquire_response_waiting_ && inputs_.acquire_response_valid) {
      acquire_response_line_ = inputs_.acquire_response_line;
      acquire_response_state_ = inputs_.acquire_response_state;
      acquire_response_pass_dirty_ = inputs_.acquire_response_pass_dirty;
      acquire_response_fault_ = inputs_.acquire_response_fault;
      acquire_response_waiting_ = false;
    }
    if (release_response_waiting_ && inputs_.release_response_valid) {
      release_response_fault_ = inputs_.release_response_fault;
      release_response_waiting_ = false;
    }
    if (uncached_response_waiting_ && inputs_.uncached_response_valid) {
      uncached_response_data_ = inputs_.uncached_response_data;
      uncached_response_fault_ = inputs_.uncached_response_fault;
      uncached_response_waiting_ = false;
    }
  }

  void update_architectural_inputs() {
    state_t* state = processor_->get_state();
    state->time->sync(inputs_.time);
    constexpr reg_t pin_mask = MIP_SSIP | MIP_STIP;
    constexpr reg_t device_mask = MIP_MSIP | MIP_MTIP | MIP_SEIP | MIP_MEIP;
    reg_t pending = 0;
    if (inputs_.interrupts & (1U << 0)) pending |= MIP_SSIP;
    if (inputs_.interrupts & (1U << 1)) pending |= MIP_MSIP;
    if (inputs_.interrupts & (1U << 2)) pending |= MIP_STIP;
    if (inputs_.interrupts & (1U << 3)) pending |= MIP_MTIP;
    if (inputs_.interrupts & (1U << 4)) pending |= MIP_SEIP;
    if (inputs_.interrupts & (1U << 5)) pending |= MIP_MEIP;
    // SSIP and STIP are also writable CSRs. Keep pin assertions separate so
    // a low external input cannot erase software's pending interrupt state.
    state->mip->set_external_pending_with_mask(pin_mask, pending);
    state->mip->backdoor_write_with_mask(device_mask, pending);
  }

  AddressResponse classify(std::uint64_t address, std::size_t length,
                           bool write, bool execute) {
    if (length == 0) return {};
    std::size_t encoded_length = std::min<std::size_t>(length, 8);
    while (encoded_length != 1 && encoded_length != 2 &&
           encoded_length != 4 && encoded_length != 8) {
      --encoded_length;
    }
    address_request_address_ = address;
    address_request_size_ = size_code(encoded_length);
    address_request_write_ = write;
    address_request_execute_ = execute;
    address_request_valid_ = true;
    address_response_waiting_ = true;
    while (address_request_valid_ || address_response_waiting_) yield();
    return address_response_;
  }

  CacheLine* find_line(std::vector<CacheLine>& cache, std::uint16_t sets,
                       std::uint16_t ways, std::uint64_t line_address) {
    const std::size_t set = (line_address / kLineBytes) % sets;
    for (std::size_t way = 0; way < ways; ++way) {
      CacheLine& line = cache[set * ways + way];
      if (line.valid && line.tag == line_address) return &line;
    }
    return nullptr;
  }

  CacheLine& victim_line(std::vector<CacheLine>& cache, std::vector<std::size_t>& next_way,
                         std::uint16_t sets, std::uint16_t ways,
                         std::uint64_t line_address) {
    const std::size_t set = (line_address / kLineBytes) % sets;
    for (std::size_t way = 0; way < ways; ++way) {
      CacheLine& line = cache[set * ways + way];
      if (!line.valid) return line;
    }
    const std::size_t way = next_way[set];
    next_way[set] = (way + 1) % ways;
    return cache[set * ways + way];
  }

  bool instruction_read(std::uint64_t address, std::size_t length, std::uint8_t* bytes) {
    std::size_t copied = 0;
    while (copied < length) {
      const std::uint64_t current = address + copied;
      const std::uint64_t line_address = current & ~(kLineBytes - 1);
      CacheLine* line = find_line(instruction_cache_, configuration_.instruction_cache_sets,
                                  configuration_.instruction_cache_ways, line_address);
      if (line == nullptr) {
        CacheLine& victim = victim_line(instruction_cache_, instruction_next_way_,
                                        configuration_.instruction_cache_sets,
                                        configuration_.instruction_cache_ways, line_address);
        instruction_request_address_ = line_address;
        instruction_request_valid_ = true;
        instruction_response_waiting_ = true;
        while (instruction_request_valid_ || instruction_response_waiting_) yield();
        if (instruction_response_fault_) return false;
        victim.valid = true;
        victim.tag = line_address;
        victim.state = CacheState::kSharedClean;
        std::memcpy(victim.bytes.data(), instruction_response_line_.data(), kLineBytes);
        line = &victim;
      }
      const std::size_t offset = current - line_address;
      const std::size_t chunk = std::min(length - copied, kLineBytes - offset);
      std::memcpy(bytes + copied, line->bytes.data() + offset, chunk);
      copied += chunk;
    }
    return true;
  }

  bool data_read(std::uint64_t address, std::size_t length, std::uint8_t* bytes) {
    std::size_t copied = 0;
    while (copied < length) {
      const std::uint64_t current = address + copied;
      const std::uint64_t line_address = current & ~(kLineBytes - 1);
      CacheLine* line = acquire_data_line(line_address, false);
      if (line == nullptr) return false;
      const std::size_t offset = current - line_address;
      const std::size_t chunk = std::min(length - copied, kLineBytes - offset);
      std::memcpy(bytes + copied, line->bytes.data() + offset, chunk);
      copied += chunk;
    }
    return true;
  }

  bool data_write(std::uint64_t address, std::size_t length, const std::uint8_t* bytes) {
    std::size_t copied = 0;
    while (copied < length) {
      const std::uint64_t current = address + copied;
      const std::uint64_t line_address = current & ~(kLineBytes - 1);
      CacheLine* line = find_line(data_cache_, configuration_.data_cache_sets,
                                  configuration_.data_cache_ways, line_address);
      if (line == nullptr || (line->state != CacheState::kUniqueClean &&
                              line->state != CacheState::kUniqueDirty)) {
        line = acquire_data_line(line_address, true);
      }
      if (line == nullptr) return false;
      const std::size_t offset = current - line_address;
      const std::size_t chunk = std::min(length - copied, kLineBytes - offset);
      std::memcpy(line->bytes.data() + offset, bytes + copied, chunk);
      line->state = CacheState::kUniqueDirty;
      copied += chunk;
    }
    return true;
  }

  CacheLine* acquire_data_line(std::uint64_t line_address, bool unique) {
    CacheLine* hit = find_line(data_cache_, configuration_.data_cache_sets,
                               configuration_.data_cache_ways, line_address);
    if (hit != nullptr && (!unique || hit->state == CacheState::kUniqueClean ||
                           hit->state == CacheState::kUniqueDirty)) {
      return hit;
    }
    if (hit != nullptr && unique) {
      const bool dirty = hit->state == CacheState::kSharedDirty ||
                         hit->state == CacheState::kUniqueDirty;
      if (dirty) {
        if (!release_line(*hit)) return nullptr;
      } else {
        hit->valid = false;
        hit->state = CacheState::kInvalid;
      }
    }
    CacheLine& victim = hit != nullptr
                            ? *hit
                            : victim_line(data_cache_, data_next_way_,
                                          configuration_.data_cache_sets,
                                          configuration_.data_cache_ways, line_address);
    if (victim.valid && !release_line(victim)) return nullptr;
    acquire_request_address_ = line_address;
    acquire_request_unique_ = unique;
    acquire_request_valid_ = true;
    acquire_response_waiting_ = true;
    while (acquire_request_valid_ || acquire_response_waiting_) yield();
    if (acquire_response_fault_) return nullptr;
    const CacheState state = acquired_state(acquire_response_state_, acquire_response_pass_dirty_);
    if (state == CacheState::kInvalid) return nullptr;
    victim.valid = true;
    victim.tag = line_address;
    victim.state = state;
    std::memcpy(victim.bytes.data(), acquire_response_line_.data(), kLineBytes);
    return &victim;
  }

  bool release_line(CacheLine& line) {
    processor_->get_mmu()->yield_load_reservation();
    const bool dirty = line.state == CacheState::kSharedDirty ||
                       line.state == CacheState::kUniqueDirty;
    if (!dirty) {
      line.valid = false;
      line.state = CacheState::kInvalid;
      return true;
    }
    release_request_address_ = line.tag;
    std::memcpy(release_request_line_.data(), line.bytes.data(), kLineBytes);
    release_request_state_ = static_cast<std::uint8_t>(line.state);
    release_request_valid_ = true;
    release_response_waiting_ = true;
    while (release_request_valid_ || release_response_waiting_) yield();
    if (release_response_fault_) return false;
    line.valid = false;
    line.state = CacheState::kInvalid;
    return true;
  }

  bool uncached_read(std::uint64_t address, std::size_t length, bool device,
                     std::uint8_t* bytes) {
    std::size_t copied = 0;
    while (copied < length) {
      const std::size_t chunk = bounded_chunk(address + copied, length - copied);
      if (!uncached_access(address + copied, chunk, false, device, bytes + copied)) return false;
      copied += chunk;
    }
    return true;
  }

  bool uncached_write(std::uint64_t address, std::size_t length, bool device,
                      const std::uint8_t* bytes) {
    std::size_t copied = 0;
    while (copied < length) {
      const std::size_t chunk = bounded_chunk(address + copied, length - copied);
      if (!uncached_access(address + copied, chunk, true, device, bytes + copied)) return false;
      copied += chunk;
    }
    return true;
  }

  bool uncached_access(std::uint64_t address, std::size_t length, bool write,
                       bool device, const std::uint8_t* bytes) {
    const std::size_t byte_offset = static_cast<std::size_t>(address & 7);
    uncached_request_address_ = address;
    uncached_request_write_ = write;
    uncached_request_size_ = size_code(length);
    uncached_request_data_ = 0;
    uncached_request_mask_ = static_cast<std::uint8_t>(((1U << length) - 1U) << byte_offset);
    uncached_request_device_ = device;
    if (write) {
      std::memcpy(reinterpret_cast<std::uint8_t*>(&uncached_request_data_) + byte_offset,
                  bytes, length);
    }
    uncached_request_valid_ = true;
    uncached_response_waiting_ = true;
    while (uncached_request_valid_ || uncached_response_waiting_) yield();
    if (uncached_response_fault_) return false;
    if (!write) {
      std::memcpy(const_cast<std::uint8_t*>(bytes),
                  reinterpret_cast<const std::uint8_t*>(&uncached_response_data_) + byte_offset,
                  length);
    }
    return true;
  }

  void service_snoop(bool snoop_fire) {
    if (!snoop_fire) return;
    processor_->get_mmu()->yield_load_reservation();
    const std::uint64_t line_address = inputs_.snoop_address & ~(kLineBytes - 1);
    CacheLine* line = find_line(data_cache_, configuration_.data_cache_sets,
                                configuration_.data_cache_ways, line_address);
    snoop_response_state_ = 0;
    snoop_response_pass_dirty_ = false;
    snoop_response_has_data_ = false;
    snoop_response_line_.fill(0);
    if (line != nullptr) {
      const bool dirty = line->state == CacheState::kSharedDirty ||
                         line->state == CacheState::kUniqueDirty;
      if (inputs_.snoop_return_to_source && !inputs_.snoop_discard_dirty) {
        snoop_response_pass_dirty_ = dirty;
        snoop_response_has_data_ = true;
        std::memcpy(snoop_response_line_.data(), line->bytes.data(), kLineBytes);
        line->valid = false;
        line->state = CacheState::kInvalid;
      } else if (dirty && !inputs_.snoop_discard_dirty) {
        snoop_response_pass_dirty_ = true;
        snoop_response_has_data_ = true;
        std::memcpy(snoop_response_line_.data(), line->bytes.data(), kLineBytes);
        line->valid = false;
        line->state = CacheState::kInvalid;
      } else if (inputs_.snoop_invalidate || inputs_.snoop_return_to_source ||
                 inputs_.snoop_discard_dirty) {
        line->valid = false;
        line->state = CacheState::kInvalid;
      } else if (inputs_.snoop_share) {
        line->state = CacheState::kSharedClean;
        snoop_response_state_ = 1;
      } else {
        snoop_response_state_ = line->state == CacheState::kSharedClean ? 1 : 2;
      }
    }
    snoop_response_valid_ = true;
  }

  void invalidate_instruction_cache() {
    for (CacheLine& line : instruction_cache_) line.valid = false;
  }

  Outputs outputs() const {
    Outputs result;
    result.address_request_valid = address_request_valid_;
    result.address_request_address = address_request_address_;
    result.address_request_size = address_request_size_;
    result.address_request_write = address_request_write_;
    result.address_request_execute = address_request_execute_;
    result.address_response_ready = address_response_waiting_;
    result.instruction_request_valid = instruction_request_valid_;
    result.instruction_request_address = instruction_request_address_;
    result.instruction_response_ready = instruction_response_waiting_;
    result.acquire_request_valid = acquire_request_valid_;
    result.acquire_request_address = acquire_request_address_;
    result.acquire_request_unique = acquire_request_unique_;
    result.acquire_response_ready = acquire_response_waiting_;
    result.release_request_valid = release_request_valid_;
    result.release_request_address = release_request_address_;
    result.release_request_line = release_request_line_;
    result.release_request_state = release_request_state_;
    result.release_response_ready = release_response_waiting_;
    result.snoop_ready = !snoop_response_valid_;
    result.snoop_response_valid = snoop_response_valid_;
    result.snoop_response_state = snoop_response_state_;
    result.snoop_response_pass_dirty = snoop_response_pass_dirty_;
    result.snoop_response_has_data = snoop_response_has_data_;
    result.snoop_response_line = snoop_response_line_;
    result.uncached_request_valid = uncached_request_valid_;
    result.uncached_request_address = uncached_request_address_;
    result.uncached_request_write = uncached_request_write_;
    result.uncached_request_size = uncached_request_size_;
    result.uncached_request_data = uncached_request_data_;
    result.uncached_request_mask = uncached_request_mask_;
    result.uncached_request_device = uncached_request_device_;
    result.uncached_response_ready = uncached_response_waiting_;
    return result;
  }

  Configuration configuration_;
  cfg_t cfg_;
  std::unique_ptr<processor_t> processor_;
  std::map<std::size_t, processor_t*> harts_;
  context_t target_;
  context_t* host_ = nullptr;
  Inputs inputs_;
  std::vector<CacheLine> instruction_cache_;
  std::vector<CacheLine> data_cache_;
  std::vector<std::size_t> instruction_next_way_;
  std::vector<std::size_t> data_next_way_;

  bool address_request_valid_ = false;
  std::uint64_t address_request_address_ = 0;
  std::uint8_t address_request_size_ = 0;
  bool address_request_write_ = false;
  bool address_request_execute_ = false;
  bool address_response_waiting_ = false;
  AddressResponse address_response_;
  bool instruction_request_valid_ = false;
  std::uint64_t instruction_request_address_ = 0;
  bool instruction_response_waiting_ = false;
  std::array<std::uint64_t, 8> instruction_response_line_{};
  bool instruction_response_fault_ = false;
  bool acquire_request_valid_ = false;
  std::uint64_t acquire_request_address_ = 0;
  bool acquire_request_unique_ = false;
  bool acquire_response_waiting_ = false;
  std::array<std::uint64_t, 8> acquire_response_line_{};
  std::uint8_t acquire_response_state_ = 0;
  bool acquire_response_pass_dirty_ = false;
  bool acquire_response_fault_ = false;
  bool release_request_valid_ = false;
  std::uint64_t release_request_address_ = 0;
  std::array<std::uint64_t, 8> release_request_line_{};
  std::uint8_t release_request_state_ = 0;
  bool release_response_waiting_ = false;
  bool release_response_fault_ = false;
  bool snoop_response_valid_ = false;
  std::uint8_t snoop_response_state_ = 0;
  bool snoop_response_pass_dirty_ = false;
  bool snoop_response_has_data_ = false;
  std::array<std::uint64_t, 8> snoop_response_line_{};
  bool uncached_request_valid_ = false;
  std::uint64_t uncached_request_address_ = 0;
  bool uncached_request_write_ = false;
  std::uint8_t uncached_request_size_ = 0;
  std::uint64_t uncached_request_data_ = 0;
  std::uint8_t uncached_request_mask_ = 0;
  bool uncached_request_device_ = false;
  bool uncached_response_waiting_ = false;
  std::uint64_t uncached_response_data_ = 0;
  bool uncached_response_fault_ = false;
};

SpikeCoreModel::SpikeCoreModel(Configuration configuration)
    : implementation_(std::make_unique<Implementation>(std::move(configuration))) {}

SpikeCoreModel::~SpikeCoreModel() = default;

Outputs SpikeCoreModel::tick(const Inputs& inputs) {
  return implementation_->tick(inputs);
}

}  // namespace rhodium::spike
