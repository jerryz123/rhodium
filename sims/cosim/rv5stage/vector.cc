// Reconstructs RV5Stage vector ownership and architectural effects from passive cycle events.
// SPDX-License-Identifier: Apache-2.0
#include "adapter.h"
#include <algorithm>
#include <bit>
#include <stdexcept>
using namespace rhodium::cosim;
using namespace rhodium::cosim::rv5stage;

extern "C" void rhodium_rv5stage_vector_scalar_write(std::int64_t instance, std::int64_t owner, std::int64_t floating, std::int64_t rd, std::int64_t data) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), ScalarWrite{Word(owner), Word(floating), Word(rd), Word(data)});
  });
}

extern "C" void rhodium_rv5stage_vector_fp(std::int64_t instance, std::int64_t allocated, std::int64_t owner, std::int64_t tag, std::int64_t launch, std::int64_t launch_tag, std::int64_t returned, std::int64_t return_tag, std::int64_t flags_valid, std::int64_t flags) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), FpService{
        Word(allocated), Word(owner), Word(tag), Word(launch), Word(launch_tag), Word(returned), Word(return_tag), Word(flags_valid), Word(flags)});
  });
}

extern "C" void rhodium_rv5stage_vector_service(std::int64_t instance, std::int64_t allocated, std::int64_t owner, std::int64_t tag, std::int64_t divide, std::int64_t multiply_launch, std::int64_t multiply_tag, std::int64_t divide_launch, std::int64_t divide_tag, std::int64_t multiply_return, std::int64_t multiply_return_tag, std::int64_t divide_return, std::int64_t divide_return_tag) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), Service{
        Word(allocated), Word(owner), Word(tag), Word(divide), Word(multiply_launch), Word(multiply_tag), Word(divide_launch), Word(divide_tag),
        Word(multiply_return), Word(multiply_return_tag), Word(divide_return), Word(divide_return_tag)});
  });
}

void VectorAdapter::require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(std::string("RV5Stage vector cosim: ") + message);
}
bool VectorAdapter::admitted(Word instance) const {
  const auto found = frames_.find(instance);
  require(found != frames_.end(), "missing vector cycle capture");
  return std::get<std::optional<Admission>>(found->second.events).has_value();
}
extern "C" void rhodium_rv5stage_vector_admission(std::int64_t instance) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(),Word(instance),collector.epoch(Word(instance)),Admission{});
  });
}
VectorAdapter::Owner& VectorAdapter::owner(Hart& hart, Word index) {
  require(index < hart.capacity && hart.owners[index].has_value(), "effect has no live instruction owner");
  return *hart.owners[index];
}
VectorAdapter::Slot& VectorAdapter::slot(Hart& hart, Word index) {
  require(index < hart.capacity && hart.slots[index].has_value(), "completion has no issued memory slot");
  auto& result = *hart.slots[index];
  require(owner(hart, result.issue.owner).id == result.owner_id, "completion belongs to a reclaimed owner generation");
  return result;
}
void VectorAdapter::flush(observation::Collector& collector) {
  for (const auto& [instance, frame] : frames_) {
    require(collector.sample() == *frame.sample && collector.epoch(instance) == frame.epoch, "stale sample or epoch");
    const auto& cycle = std::get<std::optional<Cycle>>(frame.events);
    require(cycle.has_value(), "missing active-cycle sample");
    require((cycle->xlen == 32 || cycle->xlen == 64) && cycle->vlen >= cycle->xlen &&
            std::has_single_bit(cycle->vlen) && std::has_single_bit(cycle->slots), "invalid vector geometry");
    auto found = harts_.find(instance);
    if (found == harts_.end() || found->second.epoch != frame.epoch) {
      Hart initial{};
      initial.epoch = frame.epoch; initial.xlen = cycle->xlen; initial.vlen = cycle->vlen; initial.capacity = cycle->slots;
      initial.owners.resize(cycle->slots); initial.slots.resize(cycle->slots);
      initial.services.resize(cycle->slots);
      found = harts_.insert_or_assign(instance, std::move(initial)).first;
    }
    auto& hart = found->second;
    require(hart.xlen == cycle->xlen && hart.vlen == cycle->vlen && hart.capacity == cycle->slots, "geometry changed within an epoch");
    resolve(collector, instance, hart, frame);
  }
  frames_.clear();
}
void VectorAdapter::resolve(observation::Collector& collector, Word instance, Hart& hart, const Frame& frame) {
  using namespace observation;
  const auto& cycle = *std::get<std::optional<Cycle>>(frame.events);
  const auto event = [&]<class T>() -> const std::optional<T>& { return std::get<std::optional<T>>(frame.events); };
  const auto& service = event.template operator()<Service>();
  const auto& fp = event.template operator()<FpService>();
  // Returns use the pre-edge slot generation. A same-edge allocation/launch
  // may reuse that tag only after the old result and write have been checked.
  require((service ? service->multiply_return + service->divide_return : 0) + (fp ? fp->returned : 0) <= 1, "two service returns share the VRF port");
  auto returned = [&](Word tag, ServiceKind kind) {
    require(tag < hart.capacity && hart.services[tag].has_value(), "service result has no queued owner");
    const auto& token = *hart.services[tag];
    require(token.launched && token.kind == kind && owner(hart, token.owner).id == token.id, "service result has wrong kind or owner generation");
    const auto& write = event.template operator()<Write>();
    // Reduction returns advance private accumulator state, not necessarily VRF.
    // Keep every return/tag/flag owned here; the architectural checker requires
    // exactly the final scalar destination write and rejects intermediate ones.
    const bool fold=kind==ServiceKind::FloatingPoint && owner(hart,token.owner).fp_reduction;
    require(fold || (write && write->mask && write->local && write->local_owner == token.owner), "service result has no matching architectural write");
    if (kind == ServiceKind::FloatingPoint && fp->flags_valid) {
      require(fp->flags <= 31, "invalid FP exception flags");
      auto& instruction = owner(hart, token.owner);
      instruction.flags = instruction.flags.value_or(0) | fp->flags;
    }
    hart.services[tag].reset();
  };
  if (service) {
    if (service->multiply_return) returned(service->multiply_return_tag, ServiceKind::Multiply);
    if (service->divide_return) returned(service->divide_return_tag, ServiceKind::Divide);
  }
  if (fp && fp->returned) returned(fp->return_tag, ServiceKind::FloatingPoint);
  const Word address_mask = hart.xlen == 64 ? UINT64_MAX : UINT32_MAX;
  const Word word_bytes = hart.xlen / 8;
  auto emit = [&](Word index, Effect effect) {
    auto& value = owner(hart, index);
    require(value.count != UINT64_MAX, "effect count overflow");
    collector.effect(instance, value.id, {5, value.count++}, std::move(effect));
  };
  // Everything in this section reads the pre-edge owner/slot generation. A
  // simultaneous new issue can reuse the tag only after these old effects.
  if (const auto& write = event.template operator()<ScalarWrite>(); write && (write->rd || write->floating))
    emit(write->owner, RegisterWrite{write->floating ? Bank::FloatingPoint : Bank::Integer, write->rd, 0, address_mask, write->data});
  if (const auto& write = event.template operator()<Write>(); write && write->mask) {
    require(write->local + write->packed + write->splat == 1, "VRF write has no unique source");
    const auto index = write->splat ? write->splat_owner : (write->packed ? write->packed_owner : write->local_owner);
    const auto rows = hart.vlen / hart.xlen;
    emit(index, RegisterWrite{Bank::Vector, write->address / rows, (write->address % rows) * hart.xlen, write->mask, write->data});
  }
  auto memory = [&](Word tag, Word data, const Fragment* fragment, bool direct) {
    auto& token = slot(hart, tag);
    auto& instruction = owner(hart, token.issue.owner);
    const auto& issue = token.issue;
    require(!token.done, "duplicate memory completion");
    if (!issue.enabled) { token.done = true; return; }
    const auto element_mask = (Word{1} << instruction.eew) - 1;
    // Misaligned transfers are observed as physical fragments, never again as
    // their final assembled completion.
    if (!fragment && (issue.address & element_mask)) return;
    const Word mask = fragment ? fragment->mask : issue.mask >> (issue.address & (word_bytes-1));
    const Word address = fragment ? fragment->address : issue.address;
    const Word store_data = fragment ? fragment->store_data : issue.data;
    const bool translated = fragment || (direct ? hart.hit_valid : token.translated);
    require(!fragment || hart.fragment_physical.has_value(), "split response has no accepted physical address");
    const Word physical = fragment ? *hart.fragment_physical :
        (direct ? hart.hit_physical : (token.translated ? token.physical : issue.address));
    for (unsigned byte = 0; byte < word_bytes; ++byte) if (mask & (Word{1} << byte)) {
      const Word va = (address + byte) & address_mask;
      const Word offset = (va - instruction.base) & address_mask;
      const Word access = issue.packed ? offset >> instruction.eew : token.access;
      const Word within = issue.packed ? offset & element_mask : (va - issue.address) & address_mask;
      emit(issue.owner, MemoryEffect{access, within, issue.store ? AccessKind::Store : AccessKind::Load,
           va, translated, (physical + byte) & address_mask, 1, !bool(issue.store), bool(issue.store),
           (data >> (8*byte)) & 255, (store_data >> (8*byte)) & 255, AccessResult::Success});
    }
    if (!fragment) token.done = true;
  };
  if (const auto& direct = event.template operator()<Direct>(); direct) memory(direct->tag, direct->data, nullptr, true);
  if (const auto& complete = event.template operator()<Complete>(); complete) memory(complete->tag, complete->data, nullptr, false);
  if (const auto& fragment = event.template operator()<Fragment>(); fragment) memory(fragment->tag, fragment->data, &*fragment, false);
  if (const auto& direct = event.template operator()<Direct>(); direct) slot(hart, direct->tag).done = true;
  if (const auto& complete = event.template operator()<Complete>(); complete) slot(hart, complete->tag).done = true;
  if (const auto& fault = event.template operator()<Fault>(); fault) {
    auto& token = slot(hart, fault->tag);
    const auto& instruction = owner(hart, token.issue.owner);
    require(!token.done, "fault follows completed memory slot");
    const auto offset = (fault->address - token.issue.address) & address_mask;
    const auto size = Word{1} << instruction.eew;
    require(offset < size, "fault suffix outside field");
    emit(token.issue.owner, MemoryEffect{token.access, offset, token.issue.store ? AccessKind::Store : AccessKind::Load,
        fault->address, false, 0, (Word{1} << (size-offset))-1, false, false, 0, 0, AccessResult::Fault});
    token.done = true;
  }
  // Decisions authorize already-issued attempts. Replay only discards the
  // unauthorized suffix, preserving accepted delayed responses.
  if (const auto& decision = event.template operator()<Decision>(); decision) {
    require(decision->tag < hart.capacity && decision->disposition <= 3, "invalid memory decision");
    if (hart.slots[decision->tag]) {
      auto& token = slot(hart, decision->tag);
      if (decision->disposition == 0) token.accepted = true;
      else require(!token.accepted, "accepted memory slot replayed");
    }
  }
  if (cycle.replay)
    for (auto& token : hart.slots) if (token && !token->accepted) token.reset();
  if (const auto& drain = event.template operator()<Drain>(); drain) {
    auto& instruction = owner(hart, drain->owner);
    for (const auto& token : hart.services)
      require(!token || token->id != instruction.id, "instruction drained before service completed");
    for (auto& token : hart.slots) if (token && token->owner_id == instruction.id) {
      require(token->done || !token->issue.enabled, "instruction drained before memory completed");
      token.reset();
    }
    if (instruction.flags) emit(drain->owner, CsrUpdate{1, CsrOperation::SetBits, 31, *instruction.flags});
    collector.seal(instance, instruction.id, 5, instruction.count);
    hart.owners[drain->owner].reset();
  }
  if (const auto& allocation = event.template operator()<Allocate>(); allocation) {
    require(allocation->allocated, "vector admission has no WB identity");
    const Id id{hart.epoch, allocation->order};
    if (allocation->admitted) {
      require(hart.pending.size() < 2 || event.template operator()<Dispatch>().has_value(), "admission FIFO overflow");
      hart.pending.push_back(id);
    } else collector.seal(instance, id, 5, 0);
  }
  if (const auto& dispatch = event.template operator()<Dispatch>(); dispatch) {
    require(dispatch->owner < hart.capacity && !hart.owners[dispatch->owner] && !hart.pending.empty(), "invalid execution allocation");
    const auto lmul = int(dispatch->vtype & 3) - int(dispatch->vtype & 4);
    const int emul = lmul + int(dispatch->eew) - int((dispatch->vtype >> 3) & 7);
    const auto subop = (dispatch->instruction >> 20) & 31;
    const bool linear_group = !((dispatch->instruction >> 26) & 3) && (subop == 8 || subop == 11);
    // Non-memory instructions do not consume geometry, but still own VRF writes.
    // Whole-register NF counts consecutive registers, not interleaved fields;
    // mask memory likewise uses one linear byte cursor independent of LMUL.
    hart.owners[dispatch->owner] = Owner{hart.pending.front(), dispatch->base, dispatch->eew,
        linear_group ? 1 : 1 + (dispatch->instruction >> 29), (dispatch->instruction >> 7) & 31,
        linear_group ? 0 : Word(std::max(0, emul)), 0, {}};
    // Reductions retain intermediate service results in a private accumulator;
    // only the final result writes the VRF. This is lifecycle classification,
    // not an ISA admission whitelist.
    const auto code = dispatch->instruction, operation = code >> 26;
    hart.owners[dispatch->owner]->fp_reduction = (code & 127) == 0x57 && ((code >> 12) & 7) == 1 &&
        (operation == 1 || operation == 3 || operation == 5 || operation == 7 || operation == 49 || operation == 51);
    hart.pending.pop_front();
  }
  auto allocated = [&](Word tag, Word index, ServiceKind kind) {
    require(tag < hart.capacity && !hart.services[tag], "service slot reused before completion");
    hart.services[tag] = ServiceSlot{owner(hart, index).id, index, kind};
  };
  auto launched = [&](Word tag, ServiceKind kind) {
    require(tag < hart.capacity && hart.services[tag].has_value(), "service request has no queued owner");
    auto& token = *hart.services[tag];
    require(!token.launched && token.kind == kind, "duplicate or wrong-kind service request");
    token.launched = true;
  };
  if (service) {
    if (service->allocated) allocated(service->tag, service->owner, service->divide ? ServiceKind::Divide : ServiceKind::Multiply);
    if (service->multiply_launch) launched(service->multiply_tag, ServiceKind::Multiply);
    if (service->divide_launch) launched(service->divide_tag, ServiceKind::Divide);
  }
  if (fp) {
    if (fp->allocated) allocated(fp->tag, fp->owner, ServiceKind::FloatingPoint);
    if (fp->launch) launched(fp->launch_tag, ServiceKind::FloatingPoint);
  }
  if (const auto& issue = event.template operator()<Issue>(); issue) {
    require(issue->tag < hart.capacity, "invalid issue slot");
    const auto& instruction = owner(hart, issue->owner);
    auto& previous = hart.slots[issue->tag];
    require(!previous || previous->done || !previous->issue.enabled, "memory slot reused before completion or replay");
    require(instruction.eew <= 3 && instruction.group_shift <= 3, "invalid memory group geometry");
    const auto difference = (issue->destination - instruction.destination) & 31;
    const auto field = difference >> instruction.group_shift;
    require(issue->packed || (field < instruction.fields && !(difference & ((Word{1} << instruction.group_shift)-1))), "invalid issue field register");
    previous = Slot{*issue, instruction.id, issue->first * instruction.fields + field, issue->physical, bool(issue->precertified)};
  }
  // These captures have register semantics: returns above see last edge's PA.
  if (const auto& translation = event.template operator()<Translation>(); translation) {
    auto& token = slot(hart, translation->tag);
    token.physical = translation->physical; token.translated = true;
  }
  if (const auto& fragment = event.template operator()<FragmentStart>(); fragment) hart.fragment_physical = fragment->physical;
  require(!cycle.split || cycle.has_fragments, "split access requires MMU observation");
  hart.hit_valid = cycle.pipeline_valid;
  hart.hit_physical = cycle.pipeline_address;
}

extern "C" void rhodium_rv5stage_vector_cycle(std::int64_t instance, std::int64_t xlen, std::int64_t vlen, std::int64_t slots, std::int64_t pipeline_valid, std::int64_t pipeline_address, std::int64_t split, std::int64_t has_fragments, std::int64_t replay) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), Cycle{Word(xlen), Word(vlen), Word(slots), Word(pipeline_valid), Word(pipeline_address), Word(split), Word(has_fragments), Word(replay)});
  });
}
extern "C" void rhodium_rv5stage_vector_dispatch(std::int64_t instance, std::int64_t owner, std::int64_t base, std::int64_t eew, std::int64_t instruction, std::int64_t vtype) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), Dispatch{Word(owner), Word(base), Word(eew), Word(instruction), Word(vtype)});
  });
}
extern "C" void rhodium_rv5stage_vector_issue(std::int64_t instance, std::int64_t owner, std::int64_t tag, std::int64_t first, std::int64_t destination, std::int64_t packed, std::int64_t enabled, std::int64_t store, std::int64_t address, std::int64_t physical, std::int64_t precertified, std::int64_t mask, std::int64_t data) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), Issue{Word(owner), Word(tag), Word(first), Word(destination), Word(packed), Word(enabled), Word(store), Word(address), Word(physical), Word(precertified), Word(mask), Word(data)});
  });
}
extern "C" void rhodium_rv5stage_vector_write(std::int64_t instance, std::int64_t address, std::int64_t mask, std::int64_t data, std::int64_t local, std::int64_t packed, std::int64_t splat, std::int64_t local_owner, std::int64_t packed_owner, std::int64_t splat_owner) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), Write{Word(address), Word(mask), Word(data), Word(local), Word(packed), Word(splat), Word(local_owner), Word(packed_owner), Word(splat_owner)});
  });
}
extern "C" void rhodium_rv5stage_vector_direct(std::int64_t instance, std::int64_t tag, std::int64_t data) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), Direct{Word(tag), Word(data)});
  });
}
extern "C" void rhodium_rv5stage_vector_complete(std::int64_t instance, std::int64_t tag, std::int64_t data) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), Complete{Word(tag), Word(data)});
  });
}
extern "C" void rhodium_rv5stage_vector_translation(std::int64_t instance, std::int64_t tag, std::int64_t physical) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), Translation{Word(tag), Word(physical)});
  });
}
extern "C" void rhodium_rv5stage_vector_fragment_start(std::int64_t instance, std::int64_t physical) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), FragmentStart{Word(physical)});
  });
}
extern "C" void rhodium_rv5stage_vector_fragment(std::int64_t instance, std::int64_t tag, std::int64_t address, std::int64_t mask, std::int64_t data, std::int64_t store_data) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), Fragment{Word(tag), Word(address), Word(mask), Word(data), Word(store_data)});
  });
}
extern "C" void rhodium_rv5stage_vector_fault(std::int64_t instance, std::int64_t tag, std::int64_t address) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), Fault{Word(tag), Word(address)});
  });
}
extern "C" void rhodium_rv5stage_vector_decision(std::int64_t instance, std::int64_t tag, std::int64_t disposition) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), Decision{Word(tag), Word(disposition)});
  });
}
extern "C" void rhodium_rv5stage_vector_drain(std::int64_t instance, std::int64_t owner) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.vector.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), Drain{Word(owner)});
  });
}
