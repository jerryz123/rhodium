// Reconstructs dual-slot WB order and deferred RV2Wide effects from unordered passive samples.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../events/dpi.h"
#include <array>
#include <deque>
#include <tuple>
#include <type_traits>

namespace rhodium::cosim::rv2wide {
using namespace observation;
struct LaneSample {
  Word index, retired, split, pc, encoding, next_pc, fetch_fault, rd, write, data,
       deferred, service, memory, address, access, width, atomic, store_data;
  Word fp = 0, fp_destination = 0, fp_delay = 0;
};
struct BoundarySample {
  Word privilege, next_privilege, interrupt, trap, cause, epc, tval, target,
       target_privilege, interrupts, time, interrupt_boundary;
  Word virtualized = 0, next_virtualized = 0, target_virtualized = 0, guest_valid = 0, htval = 0, htinst = 0, hpm_enabled = 0, hpm_counter = 0, hpm_overflow = 0;
};
struct CompletionSample { Word index, valid, pc, rd, write, data; };
struct FpSample { Word index, valid, pc, rd, destination, value, flags_valid, flags; };
struct PhysicalSample {
  Word request_valid, request_address, pipeline_valid, pipeline_address,
       fragment_valid, fragment_address, fragment_virtual, fragment_mask, fragment_data,
       fragment_response, fragment_response_data;
};

// No callback resolves an owner. The settled-sample barrier orders completions,
// both WB slots, and traps explicitly; state is scoped to the generic DPI binding.
class HartAdapter final : public DpiAdapter {
 public:
  template<class Event> void capture(Word sample, Word instance, Word epoch, Event event) {
    auto& frame = frames_[instance];
    if (!frame.sample) { frame.sample = sample; frame.epoch = epoch; }
    require(*frame.sample == sample && frame.epoch == epoch, "mixed or unflushed sample");
    if constexpr (std::is_same_v<Event, LaneSample>) {
      require(event.index < 3 && !frame.lanes[event.index], "duplicate WB lane");
      frame.lanes[event.index] = event;
    } else if constexpr (std::is_same_v<Event, CompletionSample>) {
      require(event.index < 4 && !frame.completions[event.index], "duplicate completion lane");
      frame.completions[event.index] = event;
    } else if constexpr (std::is_same_v<Event, FpSample>) {
      require(event.index < 2 && !frame.fp[event.index], "duplicate FP completion");
      frame.fp[event.index] = event;
    } else {
      auto& field = std::get<std::optional<Event>>(frame.events);
      require(!field, "duplicate callback"); field = event;
    }
  }
  void flush(Collector& collector) override;

 private:
  struct Frame {
    std::optional<Word> sample; Word epoch = 0;
    std::array<std::optional<LaneSample>, 3> lanes;
    std::array<std::optional<CompletionSample>, 4> completions;
    std::array<std::optional<FpSample>, 2> fp;
    std::tuple<std::optional<BoundarySample>, std::optional<PhysicalSample>> events;
  };
  struct Owner { Id id; LaneSample lane; bool physical; Word address; Word due = 0; };
  struct FpOwner { Owner owner; Word due; bool variable; };
  struct Return { Owner owner; CompletionSample value; Word due; };
  struct Fragment { Word address, virtual_address, mask, data; std::optional<Word> response; };
  struct Hart {
    Word epoch = 0, next = 0, cycle = 0, hpm_overflows = 0, hpm_previous_overflow = 0;
    bool hit_valid = false; Word hit_address = 0;
    std::array<std::deque<Owner>, 3> services;
    std::deque<Return> returns;
    std::vector<FpOwner> fp;
    std::optional<Owner> split;
    std::vector<Fragment> fragments;
  };
  std::map<Word, Frame> frames_;
  std::map<Word, Hart> harts_;
  static void require(bool condition, const char* message);
  static void gpr(Collector&, Word instance, Id, bool write, Word rd, Word data);
  static void memory(Collector&, Word instance, const Owner&, Word value, bool fault);
  static void split_memory(Collector&, Word instance, const Hart&, bool fault, Word fault_address);
  void resolve(Collector&, Word instance, Hart&, const Frame&);
};
}

extern "C" {
void rhodium_rv2wide_fp(std::int64_t instance, std::int64_t index, std::int64_t valid, std::int64_t pc, std::int64_t rd, std::int64_t destination, std::int64_t value, std::int64_t flags_valid, std::int64_t flags) noexcept;
void rhodium_rv2wide_lane(std::int64_t instance, std::int64_t index, std::int64_t retired, std::int64_t split, std::int64_t pc, std::int64_t encoding, std::int64_t next_pc, std::int64_t fetch_fault, std::int64_t rd, std::int64_t write, std::int64_t data, std::int64_t deferred, std::int64_t service, std::int64_t memory, std::int64_t address, std::int64_t access, std::int64_t width, std::int64_t atomic, std::int64_t store_data, std::int64_t fp = 0, std::int64_t fp_destination = 0, std::int64_t fp_delay = 0) noexcept;
void rhodium_rv2wide_boundary(std::int64_t instance, std::int64_t privilege, std::int64_t next_privilege, std::int64_t interrupt, std::int64_t trap, std::int64_t cause, std::int64_t epc, std::int64_t tval, std::int64_t target, std::int64_t target_privilege, std::int64_t interrupts, std::int64_t time, std::int64_t interrupt_boundary, std::int64_t virtualized = 0, std::int64_t next_virtualized = 0, std::int64_t target_virtualized = 0, std::int64_t guest_valid = 0, std::int64_t htval = 0, std::int64_t htinst = 0, std::int64_t hpm_enabled = 0, std::int64_t hpm_counter = 0, std::int64_t hpm_overflow = 0) noexcept;
void rhodium_rv2wide_completion(std::int64_t instance, std::int64_t index, std::int64_t valid, std::int64_t pc, std::int64_t rd, std::int64_t write, std::int64_t data) noexcept;
void rhodium_rv2wide_physical(std::int64_t instance, std::int64_t request_valid, std::int64_t request_address, std::int64_t pipeline_valid, std::int64_t pipeline_address, std::int64_t fragment_valid, std::int64_t fragment_address, std::int64_t fragment_virtual, std::int64_t fragment_mask, std::int64_t fragment_data, std::int64_t fragment_response, std::int64_t fragment_response_data) noexcept;
}
