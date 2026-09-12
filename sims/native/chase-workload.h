// Defines independent bank-striped pointer-chase state and its host result oracle.
#ifndef RHODIUM_CHASE_WORKLOAD_H
#define RHODIUM_CHASE_WORKLOAD_H
#include <array>
#include <cstdint>
struct ChaseWorkload {
  static constexpr unsigned nodes = 1024, samples = 64;
  std::array<std::array<uint64_t, nodes>, 8> data{};
  std::array<uint64_t, 8> checksum{};
  static unsigned next(unsigned i) { return (13 * i + 17) & (nodes - 1); }
  static unsigned sample(unsigned i) { return (17 * i) & (nodes - 1); }
  static uint64_t address(unsigned hart, unsigned i) {
    return UINT64_C(0x80000800) + (uint64_t(hart) << 10) +
           (uint64_t(i >> 7) << 14) + ((i & 127) << 3);
  }
  void initialize(unsigned harts, unsigned rounds) {
    for (unsigned h = 0; h < harts; ++h) {
      for (unsigned i = 0; i < nodes; ++i)
        data[h][i] = (uint64_t((h << 16) + 17 * i + 1) << 32) | next(i);
      uint32_t acc = 0;
      unsigned i = (h * 97) & (nodes - 1);
      for (unsigned step = 0; step < rounds * nodes; ++step) {
        uint64_t old = data[h][i];
        uint32_t mix = uint32_t(old >> 32) + acc;
        uint32_t value = mix & 1 ? ((mix << 5) ^ (mix >> 3) ^ 0x9e3779b9u)
                                 : ((mix << 7) + (mix >> 9) + 0x7f4a7c15u);
        data[h][i] = (uint64_t(value) << 32) | uint32_t(old);
        acc = (acc ^ value) + 1;
        i = uint32_t(old);
      }
      checksum[h] = acc;
    }
  }
};
#endif
