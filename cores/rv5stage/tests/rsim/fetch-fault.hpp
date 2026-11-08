// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

template <class Instruction>
bool fetch_page_fault(const Instruction &entry) {
  return entry.pfault.pvalid &&
         (entry.pfault.pcause == 12 || entry.pfault.pcause == 20);
}

template <class Instruction>
bool fetch_access_fault(const Instruction &entry) {
  return entry.pfault.pvalid && entry.pfault.pcause == 1;
}

template <class Instruction>
auto fetch_fault_address(const Instruction &entry) {
  return static_cast<decltype(entry.ppc)>(
      entry.ppc + (entry.pfault.pcontinuation ? 2u : 0u));
}
