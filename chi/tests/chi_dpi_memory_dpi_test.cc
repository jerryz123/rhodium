// Exercises reset registration, shared storage, CHI beat masking, and instance isolation.
// SPDX-License-Identifier: Apache-2.0
#include "chi_dpi_memory_dpi.h"
#include "chi_memory.h"
#include "chi_image_memory.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>

// Supply distinct simulator scopes without linking a generated Verilator model.
static std::string current_owner;
extern "C" svScope svGetScope() { return &current_owner; }
extern "C" const char* svGetNameFromScope(const svScope) { return current_owner.c_str(); }

template <typename Test>
void isolated_test(Test test) {
  const auto child = fork();
  assert(child >= 0);
  if (child == 0) { test(); _exit(0); }
  int status = 0;
  pid_t result;
  do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
  assert(result == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

void check_registration(int failure) {
  current_owner = "dut.ram";
  assert(rhodium_chi_memory_init(1, 4096, 0x80000000) == 0);
  assert(rhodium_chi_memory_init(1, 4096, 0x80000000) == 0);
  if (failure == 0) {
    current_owner = "dut.ram2";
    assert(rhodium_chi_memory_init(2, 4096, 0x80001000) == 0);
    auto map = rhodium::simulation::chi_image_memory_map();
    const auto segment = map.segment(0x80000001, 1);
    std::array<std::uint8_t, 1> byte{0xa5}, read{};
    segment.region->write(segment.offset, byte);
    rhodium::chi::initialized_memory(1, 4096).read(1, read);
    assert(byte == read);
    current_owner = "dut.ram";
    assert(rhodium_chi_memory_init(1, 4096, 0x80000000) == 0);
    rhodium::chi::initialized_memory(1, 4096).read(1, read);
    assert(byte == read);
    return;
  }
  if (failure == 1) current_owner = "dut.duplicate_id";
  if (failure == 7) current_owner.clear();
  if (failure == 2) {
    current_owner = "dut.overlap";
    assert(rhodium_chi_memory_init(2, 4096, 0x80000800) == 3);
  } else if (failure == 3) {
    rhodium::chi::freeze_memory_registrations();
    current_owner = "dut.late";
    assert(rhodium_chi_memory_init(2, 4096, 0x80001000) == 3);
  } else {
    assert(rhodium_chi_memory_init(failure == 8 ? 2 : 1, failure == 4 ? 8192 : 4096,
                                  failure == 6 ? -1LL : failure == 5 ? 0x80002000LL : 0x80000000LL) == 3);
  }
  bool rejected = false;
  try { rhodium::simulation::chi_image_memory_map(); }
  catch (const std::exception&) { rejected = true; }
  assert(rejected);  // A failed reset registration cannot become an empty-map fallback.
}

namespace {

constexpr std::size_t kWords = 512 / 32;

using PackedData = std::array<svBitVecVal, kWords>;

void set_byte(PackedData& data, std::size_t index, std::uint8_t value) {
  const std::size_t word = index / 4;
  const std::size_t shift = (index % 4) * 8;
  data[word] |= static_cast<svBitVecVal>(value) << shift;
}

std::uint8_t get_byte(const PackedData& data, std::size_t index) {
  const std::size_t word = index / 4;
  const std::size_t shift = (index % 4) * 8;
  return static_cast<std::uint8_t>(data[word] >> shift);
}

}  // namespace

int main() {
  PackedData uninitialized{};
  assert(rhodium_chi_memory_access(999, 4096, 16, 0, 0, uninitialized.data(), 0, uninitialized.data()) == 3);
  for (int failure = 0; failure <= 8; ++failure)
    isolated_test([=] { check_registration(failure); });
  for (const auto id : {16, 32, 64, 100, 101, 102, 103, 200}) {
    current_owner = "dut.ram" + std::to_string(id);
    assert(rhodium_chi_memory_init(id, id == 200 ? 8192 : 4096, 0x80000000LL + id * 65536) == 0);
  }
  auto& backing = rhodium::chi::memory(200, 8192);
  std::array<std::uint8_t, 32> host{};
  host.fill(0x5a);
  backing.write(4080, host);
  PackedData shared{};
  assert(rhodium_chi_memory_access(200, 8192, 16, 0, 4080, shared.data(), 0, shared.data()) == 0);
  for (std::size_t i = 0; i < 16; ++i) assert(get_byte(shared, i) == 0x5a);
  PackedData replacement{};
  set_byte(replacement, 0, 0xab);
  assert(rhodium_chi_memory_access(200, 8192, 16, 1, 4080, replacement.data(), 1, shared.data()) == 0);
  backing.read(4080, host);
  assert(host[0] == 0xab && host[1] == 0x5a);
  backing.zero(4081, 30);
  backing.read(4080, host);
  assert(host[0] == 0xab && host[31] == 0x5a);
  for (std::size_t i = 1; i < 31; ++i) assert(host[i] == 0);
  backing.zero(0, 8192);
  backing.read(4080, host);
  for (auto byte : host) assert(byte == 0);
  assert(rhodium_chi_memory_access(200, 8192, 32, 0, 8192, replacement.data(), 0, shared.data()) == 3);
  assert(rhodium_chi_memory_access(200, 4096, 32, 0, 0, replacement.data(), 0, shared.data()) == 3);
  for (const unsigned char beat_bytes : {16, 32, 64}) {
    PackedData written{};
    PackedData read{};
    for (std::size_t lane = 0; lane < beat_bytes; ++lane) {
      set_byte(written, lane, static_cast<std::uint8_t>(lane + beat_bytes));
    }
    const std::uint64_t mask = beat_bytes == 64
        ? ~std::uint64_t{0}
        : (std::uint64_t{1} << beat_bytes) - 1;
    assert(rhodium_chi_memory_access(beat_bytes, 4096,
                                     beat_bytes,
                                     1,
                                     0x123,
                                     written.data(),
                                     static_cast<long long>(mask),
                                     read.data()) == 0);
    read.fill(0);
    assert(rhodium_chi_memory_access(beat_bytes, 4096,
                                     beat_bytes,
                                     0,
                                     0x120,
                                     written.data(),
                                     0,
                                     read.data()) == 0);
    for (std::size_t lane = 0; lane < beat_bytes; ++lane) {
      assert(get_byte(read, lane) ==
             static_cast<std::uint8_t>(lane + beat_bytes));
    }
  }

  PackedData partial{};
  PackedData read{};
  set_byte(partial, 2, 0xa5);
  set_byte(partial, 7, 0x5a);
  assert(rhodium_chi_memory_access(100, 4096,
                                   16,
                                   1,
                                   0x200,
                                   partial.data(),
                                   (std::uint64_t{1} << 2) |
                                       (std::uint64_t{1} << 7),
                                   read.data()) == 0);
  read.fill(0xff);
  assert(rhodium_chi_memory_access(100, 4096,
                                   16,
                                   0,
                                   0x20f,
                                   partial.data(),
                                   0,
                                   read.data()) == 0);
  assert(get_byte(read, 2) == 0xa5);
  assert(get_byte(read, 7) == 0x5a);
  assert(get_byte(read, 3) == 0);

  PackedData first_beat{};
  PackedData second_beat{};
  PackedData block_read{};
  set_byte(first_beat, 0, 0x11);
  set_byte(second_beat, 0, 0x22);
  assert(rhodium_chi_memory_access(101, 4096,
                                   16,
                                   1,
                                   0x300,
                                   first_beat.data(),
                                   1,
                                   block_read.data()) == 0);
  assert(rhodium_chi_memory_access(101, 4096,
                                   16,
                                   1,
                                   0x310,
                                   second_beat.data(),
                                   1,
                                   block_read.data()) == 0);
  assert(rhodium_chi_memory_access(101, 4096,
                                   16,
                                   0,
                                   0x300,
                                   first_beat.data(),
                                   0,
                                   block_read.data()) == 0);
  assert(get_byte(block_read, 0) == 0x11);
  assert(rhodium_chi_memory_access(101, 4096,
                                   16,
                                   0,
                                   0x310,
                                   second_beat.data(),
                                   0,
                                   block_read.data()) == 0);
  assert(get_byte(block_read, 0) == 0x22);

  PackedData isolated{};
  assert(rhodium_chi_memory_access(102, 4096,
                                   16,
                                   0,
                                   0x200,
                                   partial.data(),
                                   0,
                                   isolated.data()) == 0);
  assert(get_byte(isolated, 2) == 0);
  assert(rhodium_chi_memory_access(103, 4096,
                                   8,
                                   0,
                                   0,
                                   partial.data(),
                                   0,
                                   isolated.data()) != 0);
}
