// Attaches instruction identity to all architectural comparison failures.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../events/record.h"
#include <sstream>
#include <stdexcept>
#include <string>

namespace rhodium::cosim {
// Copyable per-record diagnostic context, shared by the comparison helpers.
class Check {
 public:
  explicit Check(const observation::Record& record)
      : id_(record.id), pc_(std::holds_alternative<observation::Instruction>(record.event)
          ? std::get<observation::Instruction>(record.event).pc
          : std::get<observation::Interrupt>(record.event).trap.epc) {}
  void operator()(bool okay, const std::string& detail) const {
    if (okay) return;
    std::ostringstream message;
    message << "cosim mismatch: epoch " << id_.epoch << " order " << id_.order
            << " PC 0x" << std::hex << pc_ << ": " << detail;
    throw std::runtime_error(message.str());
  }
 private:
  observation::Id id_;
  std::uint64_t pc_;
};
}
