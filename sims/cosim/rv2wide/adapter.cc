// Orders RV2Wide WB admissions, three deferred services, and precise split/trap outcomes.
// SPDX-License-Identifier: Apache-2.0
#include "adapter.h"
#include "../events/atomic.h"
#include <bit>
#include <algorithm>
#include <stdexcept>

namespace rhodium::cosim::rv2wide {
void HartAdapter::require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(std::string("RV2Wide cosim: ") + message);
}
void HartAdapter::gpr(Collector& c, Word instance, Id id, bool write, Word rd, Word data) {
  const bool actual = write && rd;
  if (actual) c.effect(instance,id,{0,0},RegisterWrite{Bank::Integer,rd,0,UINT64_MAX,data});
  c.seal(instance,id,0,actual);
}
void HartAdapter::memory(Collector& c, Word instance, const Owner& owner, Word value, bool fault) {
  const auto& l = owner.lane;
  require(l.access >= 1 && l.access <= 5 && l.width <= 3, "invalid memory operation");
  const bool reads = l.access == 1 || l.access == 3 || l.access == 5;
  const bool writes = l.access == 2 || l.access == 4 || l.access == 5;
  const bool failed_sc = l.access == 4 && value;
  const auto kind = l.access == 1 ? AccessKind::Load : l.access == 2 ? AccessKind::Store :
                    l.access == 3 ? AccessKind::Lr : l.access == 4 ? AccessKind::Sc : AccessKind::Amo;
  const Word data = l.access == 5 && !fault ? atomic_written_value(l.atomic,value,l.store_data,l.width == 2 ? 32 : 64) : l.store_data;
  c.effect(instance,owner.id,{1,0},MemoryEffect{0,0,kind,l.address,owner.physical && !fault,owner.address,
    (Word{1} << (Word{1} << l.width))-1,reads && !fault,writes && !fault && !failed_sc,value,data,
    fault ? AccessResult::Fault : failed_sc ? AccessResult::ScFailure : AccessResult::Success});
  c.seal(instance,owner.id,1,1);
}
void HartAdapter::split_memory(Collector& c, Word instance, const Hart& h, bool fault, Word fault_address) {
  require(h.split.has_value(), "split outcome has no WB owner");
  const auto& owner = *h.split;
  const bool store = owner.lane.access == 2;
  Word index = 0;
  for (const auto& fragment : h.fragments) {
    require(fragment.response.has_value() && fragment.mask, "split prefix did not complete");
    const unsigned offset = std::countr_zero(fragment.mask);
    c.effect(instance,owner.id,{1,index++},MemoryEffect{
      0,fragment.virtual_address-owner.lane.address,store ? AccessKind::Store : AccessKind::Load,
      fragment.virtual_address,true,fragment.address+offset,fragment.mask >> offset,!store,store,
      *fragment.response >> (8*offset),fragment.data >> (8*offset),AccessResult::Success});
  }
  if (fault) {
    const Word offset = fault_address-owner.lane.address, bytes = Word{1} << owner.lane.width;
    require(offset < bytes, "split fault outside the architectural access");
    c.effect(instance,owner.id,{1,index++},MemoryEffect{
      0,offset,store ? AccessKind::Store : AccessKind::Load,fault_address,false,0,
      (Word{1} << (bytes-offset))-1,false,false,0,0,AccessResult::Fault});
  }
  c.seal(instance,owner.id,1,index);
}
void HartAdapter::flush(Collector& c) {
  for (const auto& [instance, frame] : frames_) {
    require(c.sample() == *frame.sample && c.epoch(instance) == frame.epoch, "stale sample or epoch");
    auto found = harts_.find(instance);
    if (found == harts_.end() || found->second.epoch != frame.epoch) {
      Hart initial{}; initial.epoch = frame.epoch;
      found = harts_.insert_or_assign(instance,std::move(initial)).first;
    }
    resolve(c,instance,found->second,frame);
  }
  frames_.clear();
}
void HartAdapter::resolve(Collector& c, Word instance, Hart& h, const Frame& f) {
  const auto& boundary = std::get<std::optional<BoundarySample>>(f.events);
  const auto& physical = std::get<std::optional<PhysicalSample>>(f.events);
  require(boundary && physical, "incomplete sample");
  for (const auto& lane : f.lanes) require(lane.has_value(), "missing WB lane");
  for (const auto& value : f.completions) require(value.has_value(), "missing completion lane");
  const auto& b = *boundary; const auto& p = *physical;
  c.sampled_environment(instance,{b.interrupts,b.time,h.cycle,bool(b.interrupt_boundary)});

  // Old returns resolve before new admissions, permitting same-edge destination reuse.
  const auto& wb = *f.completions[3];
  if (wb.valid) {
    require(!h.returns.empty() && h.returns.front().due == h.cycle, "WB completion lost its scheduled owner");
    const auto returned = h.returns.front(); h.returns.pop_front();
    require(wb.pc == returned.value.pc && wb.rd == returned.value.rd && wb.write == returned.value.write && wb.data == returned.value.data,
            "WB completion changed in the feed-forward pipeline");
    gpr(c,instance,returned.owner.id,wb.write,wb.rd,wb.data);
  }
  require(h.returns.empty() || h.returns.front().due > h.cycle, "missing scheduled WB completion");
  unsigned selected = 0;
  for (unsigned service = 0; service < 3; ++service) {
    const auto& value = *f.completions[service];
    if (!value.valid) continue;
    require(++selected == 1 && !h.services[service].empty(), "completion arbiter has no unique owner");
    const auto owner = h.services[service].front(); h.services[service].pop_front();
    require(value.pc == owner.lane.pc && value.rd == owner.lane.rd && value.write == owner.lane.write, "deferred owner mismatch");
    if (service == 0) memory(c,instance,owner,value.data,false);
    h.returns.push_back({owner,value,h.cycle+3});
  }

  if (p.fragment_valid) {
    require(h.split && h.fragments.size() < 2 && (h.fragments.empty() || h.fragments.back().response), "unowned or overlapping split fragment");
    h.fragments.push_back({p.fragment_address,p.fragment_virtual,p.fragment_mask,p.fragment_data,{}});
  }
  if (p.fragment_response) {
    require(!h.fragments.empty() && !h.fragments.back().response, "unowned split response");
    h.fragments.back().response = p.fragment_response_data;
  }

  auto allocate = [&](const LaneSample& lane) {
    require(h.next != UINT64_MAX, "instruction order wrapped");
    const Id id{h.epoch,h.next++};
    const Word bytes = lane.fetch_fault ? 0 : (lane.encoding & 3) == 3 ? 4 : 2;
    c.instruction(instance,id,{lane.pc,lane.encoding,bytes,bytes,{b.privilege,false},3});
    return Owner{id,lane,false,0};
  };
  std::optional<Owner> inline_fp_load;
  for (unsigned slot = 0; slot < 2; ++slot) {
    const auto& lane = *f.lanes[slot];
    require(!(lane.split && lane.retired), "split owner retired at capture");
    if (!lane.retired && !lane.split) continue;
    if (lane.split) {
      require(!h.split, "overlapping split WB owners"); h.split = allocate(lane); h.fragments.clear();
      continue;
    }
    const bool split = h.split.has_value();
    require(!split || (slot == 0 && h.split->lane.pc == lane.pc), "retirement bypassed a split owner");
    auto owner = split ? *h.split : allocate(lane);
    c.retire(instance,owner.id,{lane.next_pc,{b.next_privilege,false}});
    if (lane.fp == 1) {
      require(!split, "arithmetic retained as split access");
      c.seal(instance,owner.id,1,0);
      h.fp.push_back({owner,h.cycle+lane.fp_delay,lane.fp_delay==UINT64_MAX});
    } else if (lane.deferred) {
      require(!split && lane.service < 3 && h.services[lane.service].size() < 64, "invalid deferred admission");
      owner.physical = lane.service == 0 && p.request_valid;
      owner.address = p.request_address;
      if (lane.memory) require(owner.physical, "accepted memory has no physical address");
      else c.seal(instance,owner.id,1,0);
      h.services[lane.service].push_back(owner);
    } else {
      if (lane.fp == 2) inline_fp_load = owner;
      else gpr(c,instance,owner.id,lane.write,lane.rd,lane.data);
      if (split) { split_memory(c,instance,h,false,0); h.split.reset(); h.fragments.clear(); }
      else if (lane.memory) {
        owner.physical = h.hit_valid; owner.address = h.hit_address;
        require(owner.physical, "cache hit has no translated address");
        memory(c,instance,owner,lane.data,false);
      } else c.seal(instance,owner.id,1,0);
    }
  }
  // Fixed owners are selected by their booked return cycle, not PC matching or callback order.
  // Variable arithmetic has one active owner; load ordering remains the memory-owner FIFO.
  for (unsigned index=0; index<2; ++index) {
    const auto value = f.fp[index].value_or(FpSample{});
    if (!value.valid) continue;
    Owner owner{};
    if (index == 0) {
      auto found=std::find_if(h.fp.begin(),h.fp.end(),[&](const FpOwner& candidate) {
        return candidate.variable ? candidate.owner.lane.rd==value.rd && candidate.owner.lane.fp_destination==value.destination :
                                    candidate.due==h.cycle;
      });
      require(found!=h.fp.end(),"FP return has no authorized owner");
      owner=found->owner; h.fp.erase(found);
    } else if (inline_fp_load) {
      owner=*inline_fp_load; inline_fp_load.reset();
    } else {
      require(!h.services[0].empty() && h.services[0].front().lane.fp==2,"FP load has no memory owner");
      owner=h.services[0].front(); h.services[0].pop_front();
      memory(c,instance,owner,value.value,false);
    }
    require(owner.lane.pc==value.pc && owner.lane.rd==value.rd && owner.lane.fp_destination==value.destination,"FP completion owner mismatch");
    Word effects=0;
    if (value.destination==2 || (value.destination==1 && value.rd))
      c.effect(instance,owner.id,{0,effects++},RegisterWrite{value.destination==2 ? Bank::FloatingPoint : Bank::Integer,value.rd,0,UINT64_MAX,value.value});
    if (value.flags_valid)
      c.effect(instance,owner.id,{0,effects++},CsrUpdate{1,CsrOperation::SetBits,31,value.flags});
    c.seal(instance,owner.id,0,effects);
  }
  require(!inline_fp_load,"FP hit omitted its architectural write");
  for (const auto& owner:h.fp) require(owner.variable || owner.due>h.cycle,"missing fixed FP completion");
  const Trap trap{b.cause & (UINT64_MAX >> 1),b.epc,b.tval,b.target,{b.target_privilege,false},false,0,0};
  require(!(b.interrupt && b.trap), "interrupt and synchronous trap coincide");
  if (b.trap) {
    const auto& fault = *f.lanes[2];
    require(!h.split || h.split->lane.pc == fault.pc, "trap bypassed a split owner");
    const auto owner = h.split ? *h.split : allocate(fault);
    c.exception(instance,owner.id,trap); gpr(c,instance,owner.id,false,0,0);
    if (h.split) { split_memory(c,instance,h,true,b.tval); h.split.reset(); h.fragments.clear(); }
    else if (fault.memory) memory(c,instance,owner,0,true);
    else c.seal(instance,owner.id,1,0);
  }
  if (b.interrupt) {
    require(!h.split, "interrupt bypassed a split owner");
    require(h.next != UINT64_MAX, "instruction order wrapped");
    c.interrupt(instance,{h.epoch,h.next++},{trap,0});
  }
  h.hit_valid = p.pipeline_valid; h.hit_address = p.pipeline_address;
  ++h.cycle;
}
}

using namespace rhodium::cosim::observation;
using rhodium::cosim::rv2wide::HartAdapter;
using rhodium::cosim::rv2wide::LaneSample;
using rhodium::cosim::rv2wide::BoundarySample;
using rhodium::cosim::rv2wide::CompletionSample;
using rhodium::cosim::rv2wide::PhysicalSample;
extern "C" void rhodium_rv2wide_fp(std::int64_t instance, std::int64_t index, std::int64_t valid, std::int64_t pc, std::int64_t rd, std::int64_t destination, std::int64_t value, std::int64_t flags_valid, std::int64_t flags) noexcept {
  dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, Collector& c) {
    adapter.capture(c.sample(),Word(instance),c.epoch(Word(instance)),rhodium::cosim::rv2wide::FpSample{Word(index),Word(valid),Word(pc),Word(rd),Word(destination),Word(value),Word(flags_valid),Word(flags)});
  });
}
extern "C" void rhodium_rv2wide_lane(std::int64_t instance, std::int64_t index, std::int64_t retired, std::int64_t split, std::int64_t pc, std::int64_t encoding, std::int64_t next_pc, std::int64_t fetch_fault, std::int64_t rd, std::int64_t write, std::int64_t data, std::int64_t deferred, std::int64_t service, std::int64_t memory, std::int64_t address, std::int64_t access, std::int64_t width, std::int64_t atomic, std::int64_t store_data, std::int64_t fp, std::int64_t fp_destination, std::int64_t fp_delay) noexcept {
  dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, Collector& c) {
    adapter.capture(c.sample(),Word(instance),c.epoch(Word(instance)),LaneSample{Word(index),Word(retired),Word(split),Word(pc),Word(encoding),Word(next_pc),Word(fetch_fault),Word(rd),Word(write),Word(data),Word(deferred),Word(service),Word(memory),Word(address),Word(access),Word(width),Word(atomic),Word(store_data),Word(fp),Word(fp_destination),Word(fp_delay)});
  });
}
extern "C" void rhodium_rv2wide_boundary(std::int64_t instance, std::int64_t privilege, std::int64_t next_privilege, std::int64_t interrupt, std::int64_t trap, std::int64_t cause, std::int64_t epc, std::int64_t tval, std::int64_t target, std::int64_t target_privilege, std::int64_t interrupts, std::int64_t time, std::int64_t interrupt_boundary) noexcept {
  dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, Collector& c) {
    adapter.capture(c.sample(),Word(instance),c.epoch(Word(instance)),BoundarySample{Word(privilege),Word(next_privilege),Word(interrupt),Word(trap),Word(cause),Word(epc),Word(tval),Word(target),Word(target_privilege),Word(interrupts),Word(time),Word(interrupt_boundary)});
  });
}
extern "C" void rhodium_rv2wide_completion(std::int64_t instance, std::int64_t index, std::int64_t valid, std::int64_t pc, std::int64_t rd, std::int64_t write, std::int64_t data) noexcept {
  dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, Collector& c) {
    adapter.capture(c.sample(),Word(instance),c.epoch(Word(instance)),CompletionSample{Word(index),Word(valid),Word(pc),Word(rd),Word(write),Word(data)});
  });
}
extern "C" void rhodium_rv2wide_physical(std::int64_t instance, std::int64_t request_valid, std::int64_t request_address, std::int64_t pipeline_valid, std::int64_t pipeline_address, std::int64_t fragment_valid, std::int64_t fragment_address, std::int64_t fragment_virtual, std::int64_t fragment_mask, std::int64_t fragment_data, std::int64_t fragment_response, std::int64_t fragment_response_data) noexcept {
  dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, Collector& c) {
    adapter.capture(c.sample(),Word(instance),c.epoch(Word(instance)),PhysicalSample{Word(request_valid),Word(request_address),Word(pipeline_valid),Word(pipeline_address),Word(fragment_valid),Word(fragment_address),Word(fragment_virtual),Word(fragment_mask),Word(fragment_data),Word(fragment_response),Word(fragment_response_data)});
  });
}
