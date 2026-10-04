// Defines passive architectural observations and ordered delayed-effect assembly.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <variant>
#include <vector>

namespace rhodium::cosim::observation {
using Word = std::uint64_t;
struct Id { Word epoch, order; auto operator<=>(const Id&) const = default; };
struct EffectId { Word producer, index; auto operator<=>(const EffectId&) const = default; };
struct Privilege { Word mode; bool virtualized; };
struct Instruction {
  Word pc, encoding, encoding_valid_bytes, instruction_bytes;
  Privilege privilege;
  Word producers; // Bitmap of up to 64 independently sealed contributors.
};
struct Retirement { Word next_pc; Privilege privilege; };
struct Trap {
  Word cause, epc, tval, target_pc;
  Privilege privilege;
  bool guest_valid;
  Word htval, htinst;
};
struct Interrupt { Trap trap; Word producers; };
enum class Bank : Word { Integer, FloatingPoint, Vector };
struct RegisterWrite { Bank bank; Word index, bit_offset, mask, value; };
enum class CsrOperation : Word { AssignMasked, SetBits, ClearBits };
struct CsrUpdate { Word address; CsrOperation operation; Word mask, value; };
enum class AccessKind : Word { Load, Store, Amo, Lr, Sc, CacheOperation };
enum class AccessResult : Word { Success, Fault, ScFailure };
struct MemoryEffect {
  Word access_id, fragment_offset;
  AccessKind kind;
  Word virtual_address;
  bool physical_valid;
  Word physical_address, byte_mask;
  bool read_valid, write_valid;
  Word read_data, write_data;
  AccessResult result;
};
using Effect = std::variant<RegisterWrite, CsrUpdate, MemoryEffect>;
using Outcome = std::variant<Retirement, Trap>;
struct Environment { Word interrupt_inputs = 0, time = 0, clock_ticks = 0; };
struct Record {
  Word instance, hart, sample;
  Id id;
  std::variant<Instruction, Interrupt> event;
  Outcome outcome;
  Environment environment;
  std::map<EffectId, Effect> effects;
};
struct ResetState { Word hart, pc; Privilege privilege; unsigned xlen, vlen; };

// Single simulation-thread collector. Protocol errors poison the session.
// Call reset outside a sample; callbacks are legal only inside a sample.
class Collector {
 public:
  explicit Collector(std::size_t max_pending = 4096, std::size_t max_effects = 65536);
  void reset(Word instance, Word epoch, ResetState state);
  Word epoch(Word instance);
  void begin_sample(Word sample);
  void environment(Word instance, Environment inputs);
  void instruction(Word instance, Id id, Instruction value);
  void retire(Word instance, Id id, Retirement value);
  void exception(Word instance, Id id, Trap value);
  void interrupt(Word instance, Id id, Interrupt value);
  void effect(Word instance, Id id, EffectId effect_id, Effect value);
  void seal(Word instance, Id id, Word producer, Word count);
  std::vector<Record> end_sample();
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
  };
  std::map<Word, Hart> harts_;
  std::optional<Word> sample_, previous_sample_;
  std::size_t max_pending_, max_effects_;
  bool failed_ = false, finished_ = false;
  void check(bool condition, const char* message);
  void usable();
  Hart& hart(Word instance);
  Pending& pending(Word instance, Id id);
  void privilege(Privilege value);
  void trap(Trap value);
  void header(Pending& pending, std::variant<Instruction, Interrupt> value);
  bool complete(const Pending& pending);
};
} // namespace rhodium::cosim::observation
