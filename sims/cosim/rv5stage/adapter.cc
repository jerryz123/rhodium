// Reconstructs RV5Stage identities and scalar/FP effects without observer RTL state.
// SPDX-License-Identifier: Apache-2.0
#include "adapter.h"
#include <algorithm>
#include <stdexcept>
using namespace rhodium::cosim;
using namespace rhodium::cosim::rv5stage;
using namespace rhodium::cosim::observation;

void HartAdapter::require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(std::string("RV5Stage cosim: ") + message);
}
Word HartAdapter::mask(Word width) {
  require(width > 0 && width <= 64, "invalid lane width");
  return width == 64 ? UINT64_MAX : (Word{1} << width)-1;
}
Word HartAdapter::atomic(Word xlen, const RequestSample& request, Word value) {
  // RiscvAtomicOperation's transport encoding; never executes a reference step.
  const auto width = request.width == 2 ? 32 : xlen;
  const auto left = value & mask(width), right = request.data & mask(width);
  const bool less = (left ^ (Word{1} << (width-1))) < (right ^ (Word{1} << (width-1)));
  Word result = right;
  switch (request.atomic) {
    case 0: break;
    case 1: result = left + right; break;
    case 2: result = left ^ right; break;
    case 3: result = left & right; break;
    case 4: result = left | right; break;
    case 5: result = less ? left : right; break;
    case 6: result = less ? right : left; break;
    case 7: result = std::min(left,right); break;
    case 8: result = std::max(left,right); break;
    default: require(false, "invalid AMO operation");
  }
  return result & mask(width);
}
void HartAdapter::memory(Collector& collector, Word instance, Id id, Word xlen,
                         const RequestSample& request, Word value, bool fault,
                         const Physical& physical, Word fault_address) {
  // CacheOperation is the shared physical operation. Normalize only accepted DUT
  // transactions here; Sail independently derives architectural expectations.
  const auto op = request.access;
  require(op >= 1 && op <= 9 && request.width <= 3, "invalid scalar memory operation");
  const bool block = op >= 6, zero = op == 6, failed_sc = op == 4 && value != 0;
  const bool reads = op == 1 || op == 3 || op == 5;
  const bool writes = op == 2 || op == 4 || op == 5 || op == 6;
  const auto kind = op == 1 ? AccessKind::Load : (op == 2 || zero) ? AccessKind::Store :
      op == 3 ? AccessKind::Lr : op == 4 ? AccessKind::Sc : op == 5 ? AccessKind::Amo : AccessKind::CacheOperation;
  const Word full = (Word{1} << std::min(Word{1} << request.width, xlen/8))-1;
  const Word first_bytes = xlen/8 - (request.address & (xlen/8-1));
  const Word second_mask = full >> first_bytes;
  const Word fault_offset = (fault_address - request.address) & mask(xlen);
  const bool prefix = fault && physical.valid && fault_offset;
  const bool split = physical.valid && physical.split && second_mask && (!fault || prefix);
  const auto count = zero && !fault ? 8 : !block && split ? 2 : 1;
  const Word write_data = op == 6 ? 0 : op == 5 && (!fault || prefix) ? atomic(xlen, request, value) : request.data;
  auto shifted = [](Word data, Word bytes) { return bytes < 8 ? data >> (8*bytes) : 0; };
  for (int fragment = 0; fragment < count; ++fragment) {
    const bool rejected = fault && (fragment == 1 || !prefix);
    const Word offset = rejected ? fault_offset : block ? fragment*8 : fragment == 1 ? first_bytes : 0;
    const Word byte_mask = block ? 255 : rejected ? (fault_offset < 64 ? full >> fault_offset : 0) :
        split ? (fragment == 1 ? second_mask : full & ~(Word{255} << first_bytes)) : full;
    collector.effect(instance, id, {2, Word(fragment)}, MemoryEffect{
      0, offset, kind, (block ? request.address & ~Word{63} : request.address) + offset,
      physical.valid && !rejected, block ? (physical.address & ~Word{63}) + offset : fragment == 1 ? physical.second : physical.address, byte_mask,
      !rejected && reads, !rejected && !failed_sc && writes, shifted(value,offset), shifted(write_data,offset),
      rejected ? AccessResult::Fault : failed_sc ? AccessResult::ScFailure : AccessResult::Success});
  }
  collector.seal(instance,id,2,count);
}

void HartAdapter::flush(Collector& collector) {
  for (const auto& [instance, frame] : frames_) {
    require(collector.sample() == *frame.sample && collector.epoch(instance) == frame.epoch, "stale hart sample or epoch");
    const auto& c = std::get<std::optional<ScalarCycle>>(frame.events);
    require(c.has_value() && (c->xlen == 32 || c->xlen == 64) &&
            (c->flen == 0 || c->flen == 32 || c->flen == 64), "missing or invalid hart geometry");
    auto found = harts_.find(instance);
    if (found == harts_.end() || found->second.epoch != frame.epoch) {
      Hart initial{};
      initial.epoch = frame.epoch; initial.xlen = c->xlen; initial.flen = c->flen; initial.vector = c->vector;
      found = harts_.insert_or_assign(instance, std::move(initial)).first;
    }
    auto& hart = found->second;
    require(hart.xlen == c->xlen && hart.flen == c->flen && hart.vector == c->vector, "geometry changed within epoch");
    resolve(collector,instance,hart,frame);
  }
  frames_.clear();
  // Scalar WB allocation supplies vector identities before vector resolution.
  // This order is explicit, not dependent on registry or DPI callback ordering.
  vector.flush(collector);
}
void HartAdapter::resolve(Collector& collector, Word instance, Hart& h, const Frame& frame) {
  auto event = [&]<class T>() -> const T& {
    const auto& value = std::get<std::optional<T>>(frame.events);
    require(value.has_value(), "incomplete hart sample");
    return *value;
  };
  const auto& c = event.template operator()<ScalarCycle>();
  const auto& header = event.template operator()<HeaderSample>();
  const auto& b = event.template operator()<BoundarySample>();
  const auto& response = event.template operator()<ResponseSample>();
  const auto& pa = event.template operator()<PhysicalSample>();
  require(frame.requests[0] && frame.requests[1] && frame.arithmetic[0] && frame.arithmetic[1], "incomplete indexed hart samples");
  const auto& request = *frame.requests[0];
  const auto& offer = *frame.requests[1];
  const FpSample empty_fp{};
  const auto& fp = h.flen ? event.template operator()<FpSample>() : empty_fp;
  const Word xmask = mask(h.xlen);
  const bool allocate = c.wb_vector_admission || c.wb_retained_memory_accepted || c.wb_reservation_wait_accept ||
      (c.wb_commit_valid && !c.wb_memory_pending && !c.wb_reservation_wait_pending && !c.wb_vector_return);
  const Id fresh{h.epoch, h.next};
  auto retained = [&](const std::optional<Id>& value) {
    require(value.has_value(), "WB outcome has no retained owner"); return *value;
  };
  const Id id = c.wb_pending_exception_valid ? retained(h.exception) : c.wb_vector_return ? retained(h.vector_retained) :
      c.wb_memory_pending || c.wb_reservation_wait_pending ? retained(h.retained) : fresh;
  collector.sampled_environment(instance,{b.interrupts,b.time,h.cycles++,bool(b.interrupt_boundary)});
  require(!(allocate && b.csr_interrupt_take), "interrupt shares instruction allocation");
  if (allocate) {
    const bool fetch_fault = header.exception && (header.cause == 1 || header.cause == 12 || header.cause == 20);
    const Word bytes = fetch_fault ? 0 : (header.instruction & 3) == 3 ? 4 : 2;
    collector.instruction(instance,fresh,{header.pc,header.instruction,bytes,bytes,
      {b.privilege,bool(b.virtualized)}, Word((h.flen ? 29 : 5) + (h.vector ? 32 : 0))});
  }
  const bool vector_admitted = h.vector && vector.admitted(instance);
  if (h.vector && (allocate || vector_admitted))
    vector.capture(*frame.sample,instance,h.epoch,Allocate{fresh.order,Word(allocate),Word(vector_admitted),header.instruction});

  auto gpr = [&](Id owner, bool writes, Word rd, Word value) {
    const bool actual = writes && rd;
    if (actual) collector.effect(instance,owner,{0,0},RegisterWrite{Bank::Integer,rd,0,xmask,value});
    collector.seal(instance,owner,0,actual);
  };
  auto fpr = [&](Id owner, Word rd, Word value) {
    collector.effect(instance,owner,{3,0},RegisterWrite{Bank::FloatingPoint,rd,0,mask(h.flen),value});
    collector.seal(instance,owner,3,1);
  };
  const bool slow = c.wb_memory_accepted && !c.wb_split_accepted;
  const bool access = c.wb_attempt && offer.access != 0;
  std::optional<MemoryOwner> returned;
  if (c.response_fire) {
    require(!h.memory.empty(), "memory response has no accepted owner");
    returned = h.memory.front(); h.memory.pop_front();
    require(returned->request.writeback == response.writeback, "memory response writeback owner mismatch");
    memory(collector,instance,returned->id,h.xlen,returned->request,response.data,response.fault,
           returned->physical,returned->request.address);
    if (returned->request.integer && returned->request.rd)
      gpr(returned->id,true,returned->request.rd,response.data);
  }
  if (c.wb_split_done) {
    require(h.split_request.has_value(), "split response has no accepted request");
    memory(collector,instance,id,h.xlen,*h.split_request,response.split_data,response.split_fault,
           h.split_physical,response.split_fault_address);
  }
  if (allocate && access && !slow && !c.wb_split_accepted)
    memory(collector,instance,id,h.xlen,offer,c.commit_value,c.wb_data_fault,h.hit,c.fault_address);
  if (allocate && !access) collector.seal(instance,id,2,0);
  const bool load_result = slow && request.integer && c.commit_rd;
  if (((allocate && !c.wb_split_accepted) || c.wb_split_done) && !load_result &&
      !c.wb_commit_has_multiply && !c.wb_commit_has_divide && !fp.issued)
    gpr(id,c.write_valid && b.csr_retired,c.write_rd,c.write_data);
  for (unsigned index = 0; index < 2; ++index) {
    const auto& arithmetic = *frame.arithmetic[index];
    auto& owners = h.arithmetic[index];
    if (arithmetic.completed) {
      require(!owners.empty() && owners.front().rd == arithmetic.rd, "arithmetic completion owner mismatch");
      gpr(owners.front().id,true,arithmetic.rd,arithmetic.data); owners.pop_front();
    }
    if (arithmetic.accepted) {
      require(owners.size() < 64, "arithmetic owner capacity exceeded");
      owners.push_back({id,arithmetic.issue_rd});
    }
  }
  const Trap trap{b.cause & (xmask >> 1),b.epc,b.tval,b.target_pc,{b.target_privilege,bool(b.target_virtualized)},
                  bool(b.guest_valid),b.htval,b.htinst};
  if (b.csr_synchronous_trap) collector.exception(instance,id,trap);
  if (b.csr_interrupt_take) collector.interrupt(instance,id,{trap,0});
  if (b.csr_retired) collector.retire(instance,id,{b.next_pc,{b.next_privilege,bool(b.next_virtualized)}});

  const bool fp_load = c.offer_request_valid && offer.fp;
  if (h.flen) {
    const bool issued_fp = fp.issued && fp.issue_fp, issued_integer = fp.issued && fp.issue_integer;
    const bool completed_fp = fp.completed && fp.complete_fp;
    if (fp.completed) {
      require(fp.complete_fp + fp.complete_integer == 1 && fp.rd < 32, "invalid FP completion destination");
      Id owner{};
      if (completed_fp) {
        require(h.fp_owners[fp.rd].has_value(), "FP completion has no destination owner");
        owner = *h.fp_owners[fp.rd]; h.fp_owners[fp.rd].reset();
      } else {
        require(!h.fp_integer.empty() && h.fp_integer.front().rd == fp.rd, "FP integer completion owner mismatch");
        owner = h.fp_integer.front().id; h.fp_integer.pop_front();
      }
      gpr(owner,fp.complete_integer,fp.rd,fp.integer_value);
      if (completed_fp) fpr(owner,fp.rd,fp.fp_value);
      if (fp.flags_valid) collector.effect(instance,owner,{4,0},CsrUpdate{1,CsrOperation::SetBits,31,fp.exception_flags});
      collector.seal(instance,owner,4,fp.flags_valid);
    }
    if (fp.issued) {
      require(fp.issue_fp + fp.issue_integer == 1 && fp.issue_rd < 32, "invalid FP issue destination");
      if (issued_fp) {
        require(!h.fp_owners[fp.issue_rd], "FP destination still owned");
        h.fp_owners[fp.issue_rd] = id;
      } else if (issued_integer) {
        require(h.fp_integer.size() < 64, "FP integer owner capacity exceeded");
        h.fp_integer.push_back({id,fp.issue_rd});
      }
    }
    const bool load_response = c.response_fire && response.fp;
    if (fp.fp_load_valid) fpr(load_response ? returned->id : id,fp.fp_load_rd,fp.fp_load_data);
    const bool deferred_load = fp_load && (slow || c.wb_split_accepted);
    if (allocate && !issued_fp && !deferred_load && !(fp.fp_load_valid && !load_response))
      collector.seal(instance,id,3,0);
    if (c.wb_split_done && fp_load && response.split_fault) collector.seal(instance,id,3,0);
    if (allocate && !fp.issued) collector.seal(instance,id,4,0);

  }
  // Install edge-owned state only after all old completion lanes have resolved.
  if (slow) {
    require(h.memory.size() < 64, "memory owner capacity exceeded");
    h.memory.push_back({id,request,{bool(pa.valid),pa.address,false,0}});
  }
  if (c.wb_split_accepted) { h.split_request = request; h.split_physical = {}; }
  else if (h.split_request && pa.fragment_valid && c.wb_memory_pending) {
    if (pa.fragment_virtual == h.split_request->address) {
      h.split_physical.valid = true;
      h.split_physical.address = (pa.fragment_address + (h.split_request->address & (h.xlen/8-1))) & xmask;
      h.split_physical.split = (h.split_request->address & (h.xlen/8-1)) + (Word{1} << h.split_request->width) > h.xlen/8;
    } else h.split_physical.second = pa.fragment_address;
  }
  if (c.wb_split_done && !c.wb_split_accepted) h.split_request.reset();
  h.hit = {bool(pa.pipeline_valid),pa.pipeline_address,false,0};
  if (c.wb_current_exception_wait) h.exception = id;
  if (c.wb_vector_admission) h.vector_retained = fresh;
  if (c.wb_retained_memory_accepted || c.wb_reservation_wait_accept) h.retained = fresh;
  if (allocate || b.csr_interrupt_take) {
    require(h.next != UINT64_MAX, "instruction order wrapped"); ++h.next;
  }
}

extern "C" void rhodium_rv5stage_scalar_cycle(std::int64_t instance, std::int64_t xlen, std::int64_t flen, std::int64_t vector, std::int64_t wb_retained_memory_accepted, std::int64_t wb_reservation_wait_accept, std::int64_t wb_vector_admission, std::int64_t wb_vector_return, std::int64_t wb_commit_valid, std::int64_t wb_memory_pending, std::int64_t wb_reservation_wait_pending, std::int64_t wb_pending_exception_valid, std::int64_t wb_current_exception_wait, std::int64_t wb_memory_accepted, std::int64_t wb_attempt, std::int64_t wb_data_fault, std::int64_t wb_split_accepted, std::int64_t wb_split_done, std::int64_t wb_commit_has_multiply, std::int64_t wb_commit_has_divide, std::int64_t response_fire, std::int64_t write_valid, std::int64_t fault_address, std::int64_t commit_rd, std::int64_t commit_value, std::int64_t write_rd, std::int64_t write_data, std::int64_t offer_request_valid) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), ScalarCycle{Word(xlen), Word(flen), Word(vector), Word(wb_retained_memory_accepted), Word(wb_reservation_wait_accept), Word(wb_vector_admission), Word(wb_vector_return), Word(wb_commit_valid), Word(wb_memory_pending), Word(wb_reservation_wait_pending), Word(wb_pending_exception_valid), Word(wb_current_exception_wait), Word(wb_memory_accepted), Word(wb_attempt), Word(wb_data_fault), Word(wb_split_accepted), Word(wb_split_done), Word(wb_commit_has_multiply), Word(wb_commit_has_divide), Word(response_fire), Word(write_valid), Word(fault_address), Word(commit_rd), Word(commit_value), Word(write_rd), Word(write_data), Word(offer_request_valid)});
  });
}
extern "C" void rhodium_rv5stage_header(std::int64_t instance, std::int64_t pc, std::int64_t instruction, std::int64_t exception, std::int64_t cause) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), HeaderSample{Word(pc), Word(instruction), Word(exception), Word(cause)});
  });
}
extern "C" void rhodium_rv5stage_boundary(std::int64_t instance, std::int64_t csr_interrupt_take, std::int64_t csr_synchronous_trap, std::int64_t csr_commit, std::int64_t csr_retired, std::int64_t privilege, std::int64_t virtualized, std::int64_t cause, std::int64_t epc, std::int64_t tval, std::int64_t target_pc, std::int64_t target_privilege, std::int64_t target_virtualized, std::int64_t guest_valid, std::int64_t htval, std::int64_t htinst, std::int64_t next_pc, std::int64_t next_privilege, std::int64_t next_virtualized, std::int64_t interrupts, std::int64_t time, std::int64_t interrupt_boundary) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), BoundarySample{Word(csr_interrupt_take), Word(csr_synchronous_trap), Word(csr_commit), Word(csr_retired), Word(privilege), Word(virtualized), Word(cause), Word(epc), Word(tval), Word(target_pc), Word(target_privilege), Word(target_virtualized), Word(guest_valid), Word(htval), Word(htinst), Word(next_pc), Word(next_privilege), Word(next_virtualized), Word(interrupts), Word(time), Word(interrupt_boundary)});
  });
}
extern "C" void rhodium_rv5stage_request(std::int64_t instance, std::int64_t index, std::int64_t address, std::int64_t access, std::int64_t width, std::int64_t atomic, std::int64_t data, std::int64_t writeback, std::int64_t integer, std::int64_t fp, std::int64_t rd) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), RequestSample{Word(index), Word(address), Word(access), Word(width), Word(atomic), Word(data), Word(writeback), Word(integer), Word(fp), Word(rd)});
  });
}
extern "C" void rhodium_rv5stage_response(std::int64_t instance, std::int64_t data, std::int64_t fault, std::int64_t writeback, std::int64_t fp, std::int64_t split_data, std::int64_t split_fault, std::int64_t split_fault_address) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), ResponseSample{Word(data), Word(fault), Word(writeback), Word(fp), Word(split_data), Word(split_fault), Word(split_fault_address)});
  });
}
extern "C" void rhodium_rv5stage_physical(std::int64_t instance, std::int64_t valid, std::int64_t address, std::int64_t pipeline_valid, std::int64_t pipeline_address, std::int64_t fragment_valid, std::int64_t fragment_address, std::int64_t fragment_virtual) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), PhysicalSample{Word(valid), Word(address), Word(pipeline_valid), Word(pipeline_address), Word(fragment_valid), Word(fragment_address), Word(fragment_virtual)});
  });
}
extern "C" void rhodium_rv5stage_arithmetic(std::int64_t instance, std::int64_t index, std::int64_t accepted, std::int64_t issue_rd, std::int64_t completed, std::int64_t rd, std::int64_t data) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), ArithmeticSample{Word(index), Word(accepted), Word(issue_rd), Word(completed), Word(rd), Word(data)});
  });
}
extern "C" void rhodium_rv5stage_fp(std::int64_t instance, std::int64_t issued, std::int64_t completed, std::int64_t issue_fp, std::int64_t issue_integer, std::int64_t issue_rd, std::int64_t complete_fp, std::int64_t complete_integer, std::int64_t rd, std::int64_t integer_value, std::int64_t fp_value, std::int64_t flags_valid, std::int64_t exception_flags, std::int64_t fp_load_valid, std::int64_t fp_load_rd, std::int64_t fp_load_data) noexcept {
  observation::dpi_receive_adapter<HartAdapter>([&](HartAdapter& adapter, observation::Collector& collector) {
    adapter.capture(collector.sample(), Word(instance), collector.epoch(Word(instance)), FpSample{Word(issued), Word(completed), Word(issue_fp), Word(issue_integer), Word(issue_rd), Word(complete_fp), Word(complete_integer), Word(rd), Word(integer_value), Word(fp_value), Word(flags_valid), Word(exception_flags), Word(fp_load_valid), Word(fp_load_rd), Word(fp_load_data)});
  });
}
