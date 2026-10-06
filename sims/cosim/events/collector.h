// Assembles delayed architectural effects into an ordered, epoch-scoped record stream.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "record.h"
#include <cstddef>
#include <optional>

namespace rhodium::cosim::observation {
// Single simulation-thread collector. Protocol errors poison the session.
// Call reset outside a sample; callbacks are legal only inside a sample.
class Collector {
 public:
  explicit Collector(std::size_t max_pending = 4096, std::size_t max_effects = 65536);
  void reset(Word instance, Word epoch, ResetState state);
  Word epoch(Word instance);
  Word sample();
  void begin_sample(Word sample);
  void environment(Word instance, Environment inputs);
  void sampled_environment(Word instance, Environment inputs);
  void instruction(Word instance, Id id, Instruction value);
  void retire(Word instance, Id id, Retirement value);
  void exception(Word instance, Id id, Trap value);
  void interrupt(Word instance, Id id, Interrupt value);
  void effect(Word instance, Id id, EffectId effect_id, Effect value);
  void seal(Word instance, Id id, Word producer, Word count);
  std::vector<Record> end_sample();
  // Freeze the observed prefix at a settled boundary; retain every admitted record.
  // Repeated calls report whether all records in that prefix have been published.
  bool drain();
  void finish();

 private:
  struct Pending {
    std::optional<std::variant<Instruction, Interrupt>> event;
    std::optional<Outcome> outcome;
    Word sample = 0;
    Environment environment;
    std::map<EffectId, Effect> effects;
    std::map<Word, Word> seals;
  };
  struct Hart {
    Word epoch, next = 0;
    ResetState state;
    Environment environment;
    std::map<Word, Pending> pending;
    bool exhausted = false;
    std::optional<Environment> sampled_environment;
    std::optional<Word> drain_through;
  };
  std::map<Word, Hart> harts_;
  std::optional<Word> sample_, previous_sample_;
  std::size_t max_pending_, max_effects_;
  bool failed_ = false, finished_ = false, draining_ = false;
  void check(bool condition, const char* message);
  void usable();
  Hart& hart(Word instance);
  Pending& pending(Word instance, Id id);
  bool accepts(Word instance, Id id);
  void privilege(Privilege value);
  void trap(Trap value);
  void header(Pending& pending, std::variant<Instruction, Interrupt> value);
  bool complete(const Pending& pending);
};
} // namespace rhodium::cosim::observation
