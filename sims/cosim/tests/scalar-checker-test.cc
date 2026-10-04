// SPDX-License-Identifier: Apache-2.0
#include "scalar-checker.h"
#include "config_utils.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace rhodium::cosim;
namespace o = rhodium::cosim::observation;
constexpr std::uint64_t base = 0x80000000;

std::string configuration() {
  auto config = jsoncons::json::parse(get_default_config());
  config["platform"]["clint"]["supported"] = false;
  config["platform"]["simple_interrupt_generator"]["supported"] = false;
  return config.to_string();
}
o::Record record(unsigned order, unsigned encoding) {
  return {0, 0, order, {0, order},
          o::Instruction{base + order * 4, encoding, 4, 4, {3, false}, 0},
          o::Retirement{base + (order + 1) * 4, {3, false}}, {}, {}};
}
void run(int mutation) {
  ScalarChecker checker(configuration(), base, {{base, 4096}});
  const std::array<unsigned, 5> words{0x00000117, 0x00700093, 0x02113023, 0x02013183, 0x00000073};
  std::vector<std::uint8_t> code;
  for (auto word : words) for (unsigned i = 0; i != 4; ++i) code.push_back(word >> (8 * i));
  checker.load(base, code);
  auto r = record(0, words[0]);
  r.effects[{0, 0}] = o::RegisterWrite{o::Bank::Integer, 2, 0, UINT64_MAX, base};
  if (mutation == 1) std::get<o::RegisterWrite>(r.effects.at({0, 0})).value ^= 1;
  if (mutation == 2) std::get<o::Retirement>(r.outcome).next_pc += 4;
  if (mutation == 3) r.effects.clear();
  checker.check(r);
  r = record(1, words[1]);
  r.effects[{0, 0}] = o::RegisterWrite{o::Bank::Integer, 1, 0, UINT64_MAX, 7};
  checker.check(r);
  r = record(2, words[2]);
  r.effects[{2, 0}] = o::MemoryEffect{0, 0, o::AccessKind::Store, base + 32, false, 0, 255, false, true, 0, 7, o::AccessResult::Success};
  if (mutation == 4) std::get<o::MemoryEffect>(r.effects.at({2, 0})).virtual_address += 8;
  if (mutation == 5) std::get<o::MemoryEffect>(r.effects.at({2, 0})).write_data = 8;
  if (mutation == 7) std::get<o::MemoryEffect>(r.effects.at({2, 0})).result = o::AccessResult::Fault;
  if (mutation == 8) std::get<o::MemoryEffect>(r.effects.at({2, 0})).physical_valid = true;
  checker.check(r);
  // A completed external write belongs before the later load, not before the older store.
  const std::array<std::uint8_t, 8> nine{9, 0, 0, 0, 0, 0, 0, 0};
  checker.host_write(3, base + 32, nine);
  r = record(3, words[3]);
  r.effects[{0, 0}] = o::RegisterWrite{o::Bank::Integer, 3, 0, UINT64_MAX, 9};
  r.effects[{2, 0}] = o::MemoryEffect{0, 0, o::AccessKind::Load, base + 32, false, 0, 255, true, false, 9, 0, o::AccessResult::Success};
  checker.check(r);
  r = record(4, words[4]);
  r.outcome = o::Trap{11, base + 16, 0, 0, {3, false}, false, 0, 0};
  if (mutation == 6) std::get<o::Trap>(r.outcome).tval = 1;
  checker.check(r);
  if (checker.checked() != 5) throw std::runtime_error("wrong checked count");
}
int main() {
  run(0);
  {
    ScalarChecker checker(configuration(), base, {{base, 4096}});
    const std::array<std::uint8_t, 8> code{0x73, 0x00, 0x50, 0x10, 0x93, 0x00, 0x70, 0x00};
    checker.load(base, code);
    checker.check(record(0, 0x10500073));
    auto next = record(1, 0x00700093);
    next.effects[{0, 0}] = o::RegisterWrite{o::Bank::Integer, 1, 0, UINT64_MAX, 7};
    checker.check(next);
    if (checker.checked() != 2) throw std::runtime_error("WFI must occupy exactly one record");
  }
  for (int mutation = 1; mutation <= 8; ++mutation) {
    bool rejected = false;
    try { run(mutation); }
    catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("cosim mismatch:") != std::string::npos;
    }
    if (!rejected) throw std::runtime_error("mutation was not detected");
  }
  std::cout << "Scalar checker: independent effects, host writes, and eight corruptions passed\n";
}
