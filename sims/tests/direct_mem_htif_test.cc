// Exercises exact FESVR transfers and software boot publication under stalls and failures.
// SPDX-License-Identifier: Apache-2.0
#include "direct_mem_htif.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#include <limits>
#include <stdexcept>
#include <vector>

using rhodium::fesvr::DirectMemoryHtif;
using rhodium::fesvr::DirectMemoryRequest;

class ImageHtif final : public DirectMemoryHtif {
 public:
  ImageHtif(int argc, char** argv, rhodium::fesvr::ImageMemoryMap map)
      : DirectMemoryHtif(argc, argv, 64, 0x3000, std::move(map)) {}
 protected:
  std::map<std::string, std::uint64_t> load_payload(const std::string&, reg_t* entry, reg_t) override {
    *entry = 0x80000000;
    std::vector<std::uint8_t> bytes(70000, 0xa5), read(bytes.size());
    memif().write(0x80000000, bytes.size(), bytes.data());
    memif().read(0x80000000, read.size(), read.data());
    assert(read == bytes);
    clear_chunk(0x80000003, 69990);
    memif().read(0x80000000, read.size(), read.data());
    for (std::size_t i = 0; i < read.size(); ++i)
      assert(read[i] == (i >= 3 && i < 69993 ? 0 : 0xa5));
    std::array<std::uint8_t, 8> edge = {1, 2, 3, 4, 5, 6, 7, 8};
    memif().write(0x7ffffffc, edge.size(), edge.data());
    memif().write(0x80000000 + 69996, edge.size(), edge.data());
    return {{"tohost", 0x80001000}, {"fromhost", 0x80001008}};
  }
  void reset() override {
    DirectMemoryHtif::reset();
    std::uint8_t byte = 0;
    memif().read(0x80000000, 1, &byte);
    assert(byte == 0x77);
    memif().write(0x80000000, 1, &byte);
    htif_exit(1);
  }
};

void check_image_loading(bool fail = false) {
  std::vector<std::uint8_t> storage(70016);
  std::size_t largest = 0;
  rhodium::fesvr::ImageMemoryMap map;
  rhodium::fesvr::ImageMemoryRegion region{
    "ram", 0x80000000, 70000, storage.size(), 8,
    [&](auto offset, auto bytes) { std::copy_n(storage.begin() + offset, bytes.size(), bytes.begin()); },
    [&](auto offset, auto bytes) {
      if (fail) throw std::runtime_error("image backend failure");
      largest = std::max(largest, bytes.size());
      std::copy(bytes.begin(), bytes.end(), storage.begin() + offset);
    },
    [&](auto offset, auto length) { std::fill_n(storage.begin() + offset, length, 0); }
  };
  for (int invalid = 0; invalid < 4; ++invalid) {
    auto bad = region;
    if (invalid == 0) bad.size = 0;
    if (invalid == 1) bad.base = UINT64_MAX;
    if (invalid == 2) bad.offset = bad.storage_size;
    if (invalid == 3) bad.write = {};
    bool rejected = false;
    try { map.add(bad); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
  }
  map.add(region);
  bool rejected = false;
  try { map.add(region); } catch (const std::invalid_argument&) { rejected = true; }
  assert(rejected);
  map.freeze();
  rejected = false;
  try { map.add(region); } catch (const std::logic_error&) { rejected = true; }
  assert(rejected);
  char executable[] = "image-test", program[] = "scripted";
  char* argv[] = {executable, program};
  ImageHtif transport(2, argv, map);
  const std::vector<DirectMemoryRequest> expected = {
    {true, 0x7ffffffc, 0x04030201, 4},
    {true, 0x80000000 + 70000, 0x08070605, 4},
    {true, 0x3000, 0x80000000, 8},
    {false, 0x80000000, 0, 1},
    {true, 0x80000000, 0x77, 1}
  };
  std::size_t index = 0;
  for (int cycle = 0; cycle < 1000 && transport.exit_word() == 0; ++cycle) {
    bool ready = transport.request_valid(), response = transport.response_ready();
    if (ready) {
      assert(index < expected.size());
      auto actual = transport.request(), want = expected[index];
      assert(actual.write == want.write && actual.address == want.address && actual.data == want.data && actual.length == want.length);
    }
    if (response) ++index;
    transport.tick(ready, response, 0x77, 0);
  }
  assert(transport.exit_word() == (fail ? 3U : 1U));
  assert(index == (fail ? 0 : expected.size()));
  if (!fail) assert(largest == 65536 && storage[8] == 5 && storage[7] == 0 && storage.back() == 0);
}

class ScriptedHtif final : public DirectMemoryHtif {
 public:
  ScriptedHtif(int argc, char** argv) : DirectMemoryHtif(argc, argv, 64, 0x3000) {}
 protected:
  void load_program() override {
    const std::uint8_t byte = 0xa5;
    const std::array<std::uint8_t, 2> half = {0x34, 0x12};
    const std::array<std::uint8_t, 8> word = {1, 2, 3, 4, 5, 6, 7, 0x88};
    memif().write(0x10000007, 1, &byte);
    memif().write(0x1002, half.size(), half.data());
    memif().write(0x1000, word.size(), word.data());
    std::array<std::uint8_t, 8> read = {};
    memif().read(0x1000, read.size(), read.data());
    assert(read == word);
    std::uint8_t read_byte = 0;
    memif().read(0x10000007, 1, &read_byte);
    assert(read_byte == 1);
    std::array<std::uint8_t, 2> read_half = {};
    memif().read(0x1002, read_half.size(), read_half.data());
    assert(read_half[0] == 1 && read_half[1] == 2);
    // The all-zero fast path must remain a write, never a read-modify-write.
    const std::uint8_t zero = 0;
    memif().write(0x10000007, 1, &zero);
    std::array<std::uint8_t, 11> bulk = {};
    for (std::size_t i = 0; i < bulk.size(); ++i) bulk[i] = i + 1;
    memif().write(0x80000003, bulk.size(), bulk.data());
    bulk.fill(0);
    memif().write(0x80000003, bulk.size(), bulk.data());
  }
  void reset() override { htif_exit(1); }
};

// Override only payload ingestion: the pinned FESVR still owns load_program,
// entry capture, and the startup reset callback used by real ELF execution.
class BootHtif final : public DirectMemoryHtif {
 public:
  BootHtif(int argc, char** argv, int xlen, std::uint64_t boot_register,
           std::uint64_t entry, int overlap = 99, bool clear = false,
           rhodium::fesvr::ImageMemoryMap map = {})
      : DirectMemoryHtif(argc, argv, xlen, boot_register, std::move(map)), boot_register_(boot_register),
        entry_(entry), overlap_(overlap), clear_(clear) {}
  bool boot_returned = false;
 protected:
  std::map<std::string, std::uint64_t> load_payload(const std::string&, reg_t* entry, reg_t) override {
    *entry = entry_;
    const std::array<std::uint8_t, 8> data = {1, 2, 3, 4, 5, 6, 7, 8};
    if (overlap_ != 99) {
      const auto address = overlap_ < 0 ? boot_register_ - 4 : boot_register_ + overlap_;
      if (clear_) clear_chunk(address, data.size());
      else memif().write(address, data.size(), data.data());
    } else {
      memif().write(0x80000100, data.size(), data.data());
    }
    return {{"tohost", 0x80001000}, {"fromhost", 0x80001008}};
  }
  void reset() override {
    DirectMemoryHtif::reset();
    boot_returned = true;
    std::array<std::uint8_t, 8> read = {};
    memif().read(boot_register_, read.size(), read.data());
    // Post-publication accesses to the register remain ordinary MMIO.
    const std::uint8_t value = 0xa5;
    memif().write(boot_register_, 1, &value);
    htif_exit(1);
  }
 private:
  std::uint64_t boot_register_, entry_;
  int overlap_;
  bool clear_;
};

void check_boot(int xlen, std::uint64_t boot_register, std::uint64_t entry,
                int fail_index = -1, int overlap = 99, bool clear = false) {
  char executable[] = "boot-test";
  char program[] = "scripted";
  char* argv[] = {executable, program};
  rhodium::fesvr::ImageMemoryMap map;
  if (overlap != 99) {
    map.add({"reserved", boot_register, 8, 8, 0,
      [](auto, auto) { assert(false); },
      [](auto, auto) { assert(false); },
      [](auto, auto) { assert(false); }});
  }
  BootHtif transport(2, argv, xlen, boot_register, entry, overlap, clear, std::move(map));
  const bool invalid_entry = entry == 0 || (xlen == 32 && entry > UINT32_MAX);
  std::vector<DirectMemoryRequest> expected;
  if (overlap == 99) {
    expected.push_back({true, 0x80000100, 0x0807060504030201ULL, 8});
    if (!invalid_entry) {
      expected.push_back({true, boot_register, entry, 8});
      expected.push_back({false, boot_register, 0, 8});
      expected.push_back({true, boot_register, 0xa5, 1});
    }
  }
  const bool failed = invalid_entry || overlap != 99 || fail_index >= 0;
  if (fail_index >= 0) expected.resize(fail_index + 1);
  std::size_t index = 0;
  unsigned delay = 0;
  bool accepted = false;
  for (int cycle = 0; cycle < 1000 && transport.exit_word() == 0; ++cycle) {
    if (index < 2) assert(!transport.boot_returned);
    bool ready = false, response = false;
    std::uint8_t status = 0;
    if (transport.request_valid()) {
      assert(index < expected.size());
      const auto& actual = transport.request();
      const auto& want = expected[index];
      assert(actual.write == want.write && actual.address == want.address);
      assert(actual.data == want.data && actual.length == want.length);
      ready = ++delay >= 3;
      if (ready) { accepted = true; delay = 0; }
    } else if (accepted && transport.response_ready()) {
      response = ++delay >= 5;
      if (response) {
        status = static_cast<int>(index) == fail_index ? 1 : 0;
        accepted = false; delay = 0; ++index;
      }
    }
    transport.tick(ready, response, entry, status);
  }
  if (transport.exit_word() != (failed ? 3U : 1U))
    std::fprintf(stderr, "boot xlen=%d register=%llx entry=%llx overlap=%d clear=%d fail=%d exit=%u transactions=%zu\n",
                 xlen, static_cast<unsigned long long>(boot_register), static_cast<unsigned long long>(entry),
                 overlap, clear, fail_index, transport.exit_word(), index);
  assert(transport.exit_word() == (failed ? 3U : 1U));
  assert(index == expected.size());
  assert(transport.boot_returned == !failed);
}

void check_transfers() {
  char executable[] = "transport-test";
  char program[] = "scripted";
  char* argv[] = {executable, program};
  const std::vector<DirectMemoryRequest> expected = {
    {true, 0x10000007, 0xa5, 1}, {true, 0x1002, 0x1234, 2},
    {true, 0x1000, 0x8807060504030201ULL, 8}, {false, 0x1000, 0, 8},
    {false, 0x10000007, 0, 1}, {false, 0x1002, 0, 2},
    {true, 0x10000007, 0, 1}, {true, 0x80000003, 0x0807060504030201ULL, 8},
    {true, 0x8000000b, 0x0b0a09, 3},
    {true, 0x80000003, 0, 8}, {true, 0x8000000b, 0, 3}
  };
  for (bool fail : {false, true}) {
    ScriptedHtif transport(2, argv);
    std::size_t index = 0;
    unsigned delay = 0;
    bool accepted = false;
    for (int cycle = 0; cycle < 1000 && transport.exit_word() == 0; ++cycle) {
      bool ready = false;
      bool response = false;
      if (transport.request_valid()) {
        assert(index < expected.size());
        const auto& actual = transport.request();
        const auto& want = expected[index];
        assert(actual.write == want.write && actual.address == want.address);
        assert(actual.data == want.data && actual.length == want.length);
        ready = ++delay >= 3;
        if (ready) { accepted = true; delay = 0; }
      } else if (accepted && transport.response_ready()) {
        response = ++delay >= 4;
        if (response) { accepted = false; delay = 0; ++index; }
      }
      transport.tick(ready, response, 0x8807060504030201ULL, fail ? 1 : 0);
    }
    if (transport.exit_word() != (fail ? 3U : 1U))
      std::fprintf(stderr, "mode=%d exit=%u transactions=%zu\n", fail, transport.exit_word(), index);
    assert(transport.exit_word() == (fail ? 3U : 1U));
    assert(index == (fail ? 1 : expected.size()));
  }
  std::puts("FESVR exact-width transport and error checks passed");
}

// The simulator owns one process-lifetime HTIF. Give each startup scenario its
// own process too, isolating FESVR global state and suspended host contexts.
template <typename Test>
void isolated(Test test) {
  const auto child = fork();
  assert(child >= 0);
  if (child == 0) { test(); std::fflush(nullptr); _exit(0); }
  int status = 0;
  pid_t result;
  do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
  assert(result == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main() {
  isolated([] { check_image_loading(); });
  isolated([] { check_image_loading(true); });
  const auto run_boot = [](int xlen, std::uint64_t address, std::uint64_t entry,
                           int fail = -1, int overlap = 99, bool clear = false) {
    isolated([=] { check_boot(xlen, address, entry, fail, overlap, clear); });
  };
  for (int xlen : {32, 64}) {
    run_boot(xlen, 0x3000, 0x80002000);
    run_boot(xlen, 0x5000, 0x80003000);
    run_boot(xlen, UINT64_MAX - 7, 0x80003000);
    run_boot(xlen, 0x3000, 0);
    run_boot(xlen, 0x3000, 0x80002000, 0);
    run_boot(xlen, 0x3000, 0x80002000, 1);
    for (int offset : {-4, 0, 4}) {
      run_boot(xlen, 0x3000, 0x80002000, -1, offset);
      run_boot(xlen, 0x3000, 0x80002000, -1, offset, true);
    }
  }
  run_boot(32, 0x3000, 0x180002000ULL);
  run_boot(64, 0x3000, 0x180002000ULL);
  isolated(check_transfers);
  char executable[] = "boot-config-test";
  char program[] = "scripted";
  char* argv[] = {executable, program};
  for (const auto address : {std::uint64_t(0x3004), UINT64_MAX}) {
    bool rejected = false;
    try { DirectMemoryHtif invalid(2, argv, 64, address); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
  }
  std::puts("FESVR boot publication, reserved writes/clears, RV32/RV64 entries, and completion ordering passed");
}
