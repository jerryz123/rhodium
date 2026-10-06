// Reconstructs per-hart architectural order without relying on DPI callback order.
// SPDX-License-Identifier: Apache-2.0
#include "collector.h"
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace rhodium::cosim::observation {
Collector::Collector(std::size_t max_pending, std::size_t max_effects)
    : max_pending_(max_pending), max_effects_(max_effects) {
  check(max_pending && max_effects, "observation limits must be nonzero");
}
void Collector::check(bool condition, const char* message) {
  if (!condition) { failed_ = true; throw std::runtime_error(message); }
}
void Collector::usable() { check(!failed_ && !finished_, "closed observation session"); }
void Collector::privilege(Privilege v) {
  check((v.mode == 0 || v.mode == 1 || v.mode == 3) && !(v.mode == 3 && v.virtualized), "invalid privilege");
}
void Collector::trap(Trap v) { privilege(v.privilege); }
Collector::Hart& Collector::hart(Word instance) {
  usable();
  check(harts_.contains(instance), "unregistered hart instance");
  return harts_.at(instance);
}
void Collector::reset(Word instance, Word epoch, ResetState state) {
  usable();
  check(!sample_, "reset during sample");
  check(!draining_, "reset during observation drain");
  privilege(state.privilege);
  check(state.xlen == 32 || state.xlen == 64, "invalid XLEN");
  check(state.vlen == 0 || (state.vlen >= 64 && state.vlen <= 65536 && !(state.vlen & (state.vlen - 1))), "invalid VLEN");
  if (harts_.contains(instance)) {
    const auto& old = harts_.at(instance);
    check(epoch > old.epoch && state.hart == old.state.hart, "reset must advance epoch and preserve hart identity");
  }
  // Reset explicitly abandons all incomplete old-epoch instructions.
  harts_.insert_or_assign(instance, Hart{epoch, 0, state, {}, {}, false, {}, {}});
}
void Collector::begin_sample(Word sample) {
  usable();
  check(!sample_ && (!previous_sample_ || sample > *previous_sample_), "nonmonotonic or nested sample");
  sample_ = sample;
  for (auto& [instance, h] : harts_) { (void)instance; h.sampled_environment.reset(); }
}
Word Collector::epoch(Word instance) { return hart(instance).epoch; }
Word Collector::sample() { usable(); check(sample_.has_value(), "callback outside sample"); return *sample_; }
void Collector::environment(Word instance, Environment inputs) {
  auto& h = hart(instance);
  check(!sample_, "set environment before beginning the sample");
  h.environment = inputs;
}
void Collector::sampled_environment(Word instance, Environment inputs) {
  auto& h = hart(instance);
  check(sample_.has_value() && !h.sampled_environment, "duplicate or out-of-sample environment");
  h.sampled_environment = inputs;
}
Collector::Pending& Collector::pending(Word instance, Id id) {
  auto& h = hart(instance);
  check(sample_.has_value(), "observation outside sample");
  check(id.epoch == h.epoch && !h.exhausted && id.order >= h.next, "stale epoch or already published order");
  check(id.order - h.next < max_pending_, "instruction order exceeds observation window");
  return h.pending[id.order];
}
bool Collector::accepts(Word instance, Id id) {
  const auto& h = hart(instance);
  check(sample_.has_value() && id.epoch == h.epoch, "out-of-sample or stale-epoch observation");
  return !draining_ || (h.drain_through && id.order <= *h.drain_through);
}
void Collector::header(Pending& p, std::variant<Instruction, Interrupt> value) {
  check(!p.event, "duplicate architectural header");
  p.event = value;
  p.sample = *sample_;
}
void Collector::instruction(Word instance, Id id, Instruction value) {
  if (!accepts(instance, id)) return;
  privilege(value.privilege);
  check(value.encoding_valid_bytes <= 4 && (value.instruction_bytes == 0 || value.instruction_bytes == 2 || value.instruction_bytes == 4), "invalid instruction length");
  check(!value.instruction_bytes || value.encoding_valid_bytes <= value.instruction_bytes, "invalid encoding coverage");
  check(value.encoding <= 0xffffffffULL, "instruction encoding exceeds 32 bits");
  auto& p = pending(instance, id);
  header(p, value);
  p.environment = hart(instance).environment;
}
void Collector::retire(Word instance, Id id, Retirement value) {
  if (!accepts(instance, id)) return;
  privilege(value.privilege);
  auto& p = pending(instance, id);
  check(!p.outcome, "duplicate instruction outcome");
  p.outcome = value;
}
void Collector::exception(Word instance, Id id, Trap value) {
  if (!accepts(instance, id)) return;
  trap(value);
  auto& p = pending(instance, id);
  check(!p.outcome, "duplicate instruction outcome");
  p.outcome = value;
}
void Collector::interrupt(Word instance, Id id, Interrupt value) {
  if (!accepts(instance, id)) return;
  trap(value.trap);
  auto& p = pending(instance, id);
  check(!p.outcome, "duplicate interrupt outcome");
  header(p, value);
  p.outcome = value.trap;
  p.environment = hart(instance).environment;
}
void Collector::effect(Word instance, Id id, EffectId eid, Effect value) {
  if (!accepts(instance, id)) return;
  auto& p = pending(instance, id);
  check(eid.producer < 64 && eid.index < max_effects_, "effect identity out of range");
  check(p.effects.size() < max_effects_ && !p.effects.contains(eid), "duplicate effect or effect limit exceeded");
  const auto& state = hart(instance).state;
  if (auto* r = std::get_if<RegisterWrite>(&value)) {
    check(r->bank <= Bank::Vector && r->index < 32 && r->mask, "invalid register effect");
    const Word width = r->bank == Bank::Vector ? state.vlen : r->bank == Bank::Integer ? state.xlen : 64;
    check(r->bit_offset < width, "register bit offset out of range");
    const Word remaining = width - r->bit_offset;
    check(remaining >= 64 || (r->mask >> remaining) == 0, "register mask out of range");
    check(r->bank != Bank::Integer || r->index != 0, "x0 has no architectural write");
  } else if (auto* c = std::get_if<CsrUpdate>(&value)) {
    check(c->address < 4096 && c->operation <= CsrOperation::ClearBits && c->mask, "invalid CSR effect");
    check(state.xlen == 64 || (c->mask >> 32) == 0, "CSR mask exceeds XLEN");
  } else {
    const auto& m = std::get<MemoryEffect>(value);
    check(m.kind <= AccessKind::CacheOperation && m.result <= AccessResult::ScFailure && m.byte_mask > 0 && m.byte_mask <= 255, "invalid memory effect");
    check(m.result != AccessResult::ScFailure || m.kind == AccessKind::Sc, "SC failure on non-SC access");
  }
  p.effects.emplace(eid, value);
}
void Collector::seal(Word instance, Id id, Word producer, Word count) {
  if (!accepts(instance, id)) return;
  auto& p = pending(instance, id);
  check(producer < 64 && count <= max_effects_ && !p.seals.contains(producer), "invalid or duplicate seal");
  p.seals.emplace(producer, count);
}
bool Collector::complete(const Pending& p) {
  check(p.event.has_value(), "effect or outcome without architectural header at sample end");
  const Word producers = std::visit([](const auto& h) { return h.producers; }, *p.event);
  for (const auto& [eid, value] : p.effects) {
    (void)value;
    check(producers & (Word{1} << eid.producer), "undeclared effect producer");
    const auto seal = p.seals.find(eid.producer);
    check(seal == p.seals.end() || eid.index < seal->second, "effect exceeds sealed count");
  }
  std::size_t total = 0;
  for (const auto& [producer, count] : p.seals) {
    check(producers & (Word{1} << producer), "undeclared sealing producer");
    check(count <= max_effects_ - total, "sealed effects exceed record limit");
    total += count;
  }
  if (!p.outcome) return false;
  if (const auto* i = std::get_if<Instruction>(&*p.event); i && std::holds_alternative<Retirement>(*p.outcome))
    check(i->instruction_bytes && i->encoding_valid_bytes == i->instruction_bytes, "retired instruction lacks complete encoding");
  for (Word producer = 0; producer < 64; ++producer) {
    if (!(producers & (Word{1} << producer))) continue;
    const auto seal = p.seals.find(producer);
    if (seal == p.seals.end()) return false;
    for (Word index = 0; index < seal->second; ++index)
      if (!p.effects.contains({producer, index})) return false;
  }
  return true;
}
std::vector<Record> Collector::end_sample() {
  usable();
  check(sample_.has_value(), "no open sample");
  std::vector<Record> result;
  for (auto& [instance, h] : harts_) {
    if (h.state.sampled_inputs)
      for (const auto& [order, p] : h.pending) {
        (void)order;
        check(!p.event || p.sample != *sample_ || h.sampled_environment.has_value(), "missing header environment");
      }
    // Edge callbacks are unordered. Freeze the environment onto new headers only
    // after all callbacks arrive; delayed completion must never replace it.
    if (h.sampled_environment)
      for (auto& [order, p] : h.pending) {
        (void)order;
        if (p.event && p.sample == *sample_) p.environment = *h.sampled_environment;
      }
    // Validate younger records even while an older record blocks publication.
    for (const auto& [order, p] : h.pending) { (void)order; complete(p); }
    while (!h.exhausted && h.pending.contains(h.next) && complete(h.pending.at(h.next))) {
      auto& p = h.pending.at(h.next);
      result.push_back({instance, h.state.hart, p.sample, {h.epoch, h.next}, *p.event, *p.outcome, p.environment, std::move(p.effects)});
      h.pending.erase(h.next);
      if (h.next == std::numeric_limits<Word>::max()) h.exhausted = true;
      else ++h.next;
    }
  }
  previous_sample_ = sample_;
  sample_.reset();
  return result;
}
bool Collector::drain() {
  usable();
  check(!sample_, "drain during observation sample");
  bool empty = true;
  for (auto& [instance, h] : harts_) {
    (void)instance;
    if (!draining_) {
      if (!h.pending.empty()) h.drain_through = h.pending.rbegin()->first;
      else if (h.exhausted) h.drain_through = std::numeric_limits<Word>::max();
      else if (h.next) h.drain_through = h.next - 1;
    }
    empty &= h.pending.empty();
  }
  draining_ = true;
  return empty;
}
void Collector::finish() {
  usable();
  check(!sample_, "unfinished observation sample");
  for (const auto& [instance, h] : harts_) {
    if (h.pending.empty()) continue;
    std::ostringstream message;
    message << "unfinished architectural records or order gap: instance " << instance
            << " next order " << h.next << " pending " << h.pending.size();
    const auto& [order, p] = *h.pending.begin();
    message << "; first order " << order;
    if (p.event) {
      if (const auto* instruction = std::get_if<Instruction>(&*p.event))
        message << " pc=0x" << std::hex << instruction->pc << std::dec;
      message << " producers=" << std::visit([](const auto& event) { return event.producers; }, *p.event);
    }
    message << " outcome=" << p.outcome.has_value() << " effects=" << p.effects.size() << " seals=";
    for (const auto& [producer, count] : p.seals) message << producer << ':' << count << ',';
    check(false, message.str().c_str());
  }
  finished_ = true;
}
} // namespace rhodium::cosim::observation
