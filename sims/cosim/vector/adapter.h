// Captures shared vector microarchitectural events for settled-sample reconstruction.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../events/dpi.h"
#include <deque>
#include <tuple>

namespace rhodium::cosim::vector {
using observation::Word;
using observation::Id;
struct Cycle { Word xlen, vlen, slots, dispatch_depth, pipeline_valid, pipeline_address, split, has_fragments, replay; };
struct Allocate { Word order, allocated, admitted, instruction; };
struct Admission {};
struct Service { Word allocated, owner, tag, divide, multiply_launch, multiply_tag, divide_launch, divide_tag, multiply_return, multiply_return_tag, divide_return, divide_return_tag; Word packed = 0; };
struct FpService { Word allocated, owner, tag, launch, launch_tag, returned, return_tag, flags_valid, flags; };
struct Dispatch { Word owner, base, eew, instruction, vtype; };
// Raw address preserves packed element identity; effective address owns faults/VA.
struct Issue { Word owner, tag, first, destination, packed, enabled, store, address, effective_address, physical, precertified, mask, data; };
struct Write { Word address, mask, data, local, packed, splat, local_owner, packed_owner, splat_owner; };
struct ScalarWrite { Word owner, floating, rd, data; };
struct Direct { Word tag, data; };
struct Complete { Word tag, data; };
struct StoreComplete { Word tag; };
struct Translation { Word tag, physical; };
struct FragmentStart { Word physical; };
struct Fragment { Word tag, address, mask, data, store_data; };
struct Fault { Word tag, address; };
struct Decision { Word tag, disposition; };
struct Drain { Word owner; };

// All event payloads describe pre-edge signals. Callback order is immaterial;
// finish_sample resolves old effects before installing this edge's new owners/slots.
class VectorAdapter final : public observation::DpiAdapter {
 public:
  template<class Event> void capture(Word sample, Word instance, Word epoch, Event event) {
    auto& frame = frames_[instance];
    if (!frame.sample) { frame.sample = sample; frame.epoch = epoch; }
    require(*frame.sample == sample && frame.epoch == epoch, "unflushed vector sample or mixed epoch");
    auto& field = std::get<std::optional<Event>>(frame.events);
    require(!field, "duplicate vector callback");
    field = event;
  }
  void flush(observation::Collector&) override {}
  void finish_sample(observation::Collector& collector) override;

  bool admitted(Word instance) const;

 private:
  using Events = std::tuple<std::optional<Cycle>, std::optional<Allocate>, std::optional<Admission>, std::optional<Service>, std::optional<FpService>, std::optional<Dispatch>, std::optional<Issue>, std::optional<Write>, std::optional<ScalarWrite>, std::optional<Direct>, std::optional<Complete>, std::optional<StoreComplete>, std::optional<Translation>, std::optional<FragmentStart>, std::optional<Fragment>, std::optional<Fault>, std::optional<Decision>, std::optional<Drain>>;
  struct Frame { std::optional<Word> sample; Word epoch = 0; Events events; };
  struct Owner { Id id; Word base, eew, fields, destination, group_shift, count = 0; std::optional<Word> flags; bool fp_reduction = false; };
  struct Slot { Issue issue; Id owner_id; Word access, physical; bool translated, accepted = false, done = false; };
  enum class ServiceKind { Multiply, Divide, FloatingPoint };
  struct ServiceSlot { Id id; Word owner; ServiceKind kind; bool launched = false; };
  struct Hart {
    Word epoch, xlen, vlen, capacity, dispatch_depth;
    std::deque<Id> pending;
    std::vector<std::optional<Owner>> owners;
    std::vector<std::optional<Slot>> slots;
    std::vector<std::optional<ServiceSlot>> services;
    bool hit_valid = false;
    Word hit_physical = 0;
    std::optional<Word> fragment_physical;
  };
  std::map<Word, Frame> frames_;
  std::map<Word, Hart> harts_;
  static void require(bool condition, const char* message);
  static Owner& owner(Hart& hart, Word index);
  static Slot& slot(Hart& hart, Word index);
  void resolve(observation::Collector& collector, Word instance, Hart& hart, const Frame& frame);
};
// Shares vector captures with hart adapters; finalization follows every hart's allocation phase.
VectorAdapter& vector_adapter();
}

extern "C" {
void rhodium_vector_scalar_write(std::int64_t instance, std::int64_t owner, std::int64_t floating, std::int64_t rd, std::int64_t data) noexcept;
void rhodium_vector_fp(std::int64_t instance, std::int64_t allocated, std::int64_t owner, std::int64_t tag, std::int64_t launch, std::int64_t launch_tag, std::int64_t returned, std::int64_t return_tag, std::int64_t flags_valid, std::int64_t flags) noexcept;
void rhodium_vector_service(std::int64_t instance, std::int64_t allocated, std::int64_t owner, std::int64_t tag, std::int64_t divide, std::int64_t multiply_launch, std::int64_t multiply_tag, std::int64_t divide_launch, std::int64_t divide_tag, std::int64_t multiply_return, std::int64_t multiply_return_tag, std::int64_t divide_return, std::int64_t divide_return_tag, std::int64_t packed) noexcept;
void rhodium_vector_admission(std::int64_t instance) noexcept;
void rhodium_vector_cycle(std::int64_t instance, std::int64_t xlen, std::int64_t vlen, std::int64_t slots, std::int64_t dispatch_depth, std::int64_t pipeline_valid, std::int64_t pipeline_address, std::int64_t split, std::int64_t has_fragments, std::int64_t replay) noexcept;
void rhodium_vector_dispatch(std::int64_t instance, std::int64_t owner, std::int64_t base, std::int64_t eew, std::int64_t instruction, std::int64_t vtype) noexcept;
void rhodium_vector_issue(std::int64_t instance, std::int64_t owner, std::int64_t tag, std::int64_t first, std::int64_t destination, std::int64_t packed, std::int64_t enabled, std::int64_t store, std::int64_t address, std::int64_t effective_address, std::int64_t physical, std::int64_t precertified, std::int64_t mask, std::int64_t data) noexcept;
void rhodium_vector_write(std::int64_t instance, std::int64_t address, std::int64_t mask, std::int64_t data, std::int64_t local, std::int64_t packed, std::int64_t splat, std::int64_t local_owner, std::int64_t packed_owner, std::int64_t splat_owner) noexcept;
void rhodium_vector_direct(std::int64_t instance, std::int64_t tag, std::int64_t data) noexcept;
void rhodium_vector_complete(std::int64_t instance, std::int64_t tag, std::int64_t data) noexcept;
void rhodium_vector_store_complete(std::int64_t instance, std::int64_t tag) noexcept;
void rhodium_vector_translation(std::int64_t instance, std::int64_t tag, std::int64_t physical) noexcept;
void rhodium_vector_fragment_start(std::int64_t instance, std::int64_t physical) noexcept;
void rhodium_vector_fragment(std::int64_t instance, std::int64_t tag, std::int64_t address, std::int64_t mask, std::int64_t data, std::int64_t store_data) noexcept;
void rhodium_vector_fault(std::int64_t instance, std::int64_t tag, std::int64_t address) noexcept;
void rhodium_vector_decision(std::int64_t instance, std::int64_t tag, std::int64_t disposition) noexcept;
void rhodium_vector_drain(std::int64_t instance, std::int64_t owner) noexcept;
}
