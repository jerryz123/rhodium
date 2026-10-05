// SPDX-License-Identifier: Apache-2.0
#include "chi_memory.h"
#include <svdpi.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

extern "C" int rsim_chi_test_registry(int phase) {
  try {
    const auto& entries = rhodium::chi::freeze_memory_registrations();
    const std::string scope = svGetNameFromScope(svGetScope());
    if (entries.size() != 1) throw std::runtime_error("expected one registered memory");
    const auto& entry = entries.front();
    if (entry.model_id != 0 || entry.base != 0x80000000 || entry.storage->size() != 4096 ||
        (entry.owner != scope + ".dut" && entry.owner != scope + ".dut.memory"))
      throw std::runtime_error("incorrect registered identity, window, or DPI scope");
    std::array<std::uint8_t, 64> bytes{};
    if (phase == 0) {
      for (unsigned i = 0; i < 64; ++i) bytes[i] = i ^ 0x5a;
      entry.storage->write(0, bytes);
    } else {
      entry.storage->read(256, bytes);
      constexpr std::uint64_t mask = UINT64_C(0x8000000180000081);
      for (unsigned i = 0; i < 64; ++i)
        if (bytes[i] != (((mask >> i) & 1) ? 0xa0 + i : i + 1))
          throw std::runtime_error("masked write or reset retention mismatch");
    }
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "CHI registry test: %s\n", error.what());
    return 1;
  }
}
