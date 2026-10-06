// Defines model-independent architectural events and completed instruction records.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <compare>
#include <cstdint>
#include <map>
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
// Pre-edge pins/time and elapsed active cycles, not a CSR or retirement snapshot.
struct Environment { Word interrupt_inputs = 0, time = 0, cycle = 0; bool interrupt_boundary = false; };
struct Record {
  Word instance, hart, sample;
  Id id;
  std::variant<Instruction, Interrupt> event;
  Outcome outcome;
  Environment environment;
  std::map<EffectId, Effect> effects;
};
struct ResetState { Word hart, pc; Privilege privilege; unsigned xlen, vlen; bool sampled_inputs = false; };

} // namespace rhodium::cosim::observation
