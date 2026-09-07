// Exercises the pinned FESVR memif against exact-width transport handshakes and failures.
#include "direct_mem_htif.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

using rhodium::fesvr::DirectMemoryHtif;
using rhodium::fesvr::DirectMemoryRequest;

class ScriptedHtif final : public DirectMemoryHtif {
 public:
  ScriptedHtif(int argc, char** argv) : DirectMemoryHtif(argc, argv, 64) {}
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

int main() {
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
      transport.tick(ready, response, 0x8807060504030201ULL, fail ? 1 : 0, true);
    }
    if (transport.exit_word() != (fail ? 3U : 1U))
      std::fprintf(stderr, "mode=%d exit=%u transactions=%zu\n", fail, transport.exit_word(), index);
    assert(transport.exit_word() == (fail ? 3U : 1U));
    assert(index == (fail ? 1 : expected.size()));
  }
  std::puts("FESVR exact-width transport and error checks passed");
}
