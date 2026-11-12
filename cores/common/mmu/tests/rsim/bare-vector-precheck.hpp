// Checks identity translation before a vector window can authorize packed memory service.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <initializer_list>

template <typename Probe> void check_bare_vector_precheck(Probe probe) {
  constexpr std::uint64_t ram = 0x10008;
  constexpr std::uint64_t alias = ram | (UINT64_C(1) << 56);
  for (bool store : {false, true}) {
    probe(ram, ram + 7, store, true);
    probe(alias, alias + 7, store, false);
  }
}
