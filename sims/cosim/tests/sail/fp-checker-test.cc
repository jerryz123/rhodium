// SPDX-License-Identifier: Apache-2.0
#include "../../sail/checker.h"
#include "config_utils.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace rhodium::cosim;
namespace o = rhodium::cosim::observation;
constexpr std::uint64_t base = 0x80000000;

void run(unsigned mutation, unsigned xlen = 64) {
  auto config = jsoncons::json::parse(xlen == 32 ? get_default_rv32_config() : get_default_config());
  if (xlen == 32) {
    config["extensions"]["D"]["supported"] = false;
    config["extensions"]["Zcd"]["supported"] = false;
    config["extensions"]["V"]["support_level"] = "Float_single";
  }
  config["platform"]["clint"]["supported"] = false;
  config["platform"]["simple_interrupt_generator"]["supported"] = false;
  SailChecker checker(config.to_string(), base, {{base, 4096}});
  // Enable FS, write f0, produce NV twice, classify/convert a NaN, then use an
  // improperly boxed operand, then explicitly write/clear FFLAGS and FCSR.
  // Expected values are authored, never DUT repairs.
  const std::array<unsigned, 16> code{0x000040b7, 0x3000a073, 0xf0000053,
    0x180000d3, 0x18000153, 0xe00091d3, 0xc0008053, 0xf20001d3,
    0x00018253, 0xe2020253, 0x00105073, 0x001022f3, 0x00115073,
    0x001022f3, 0x00301073, 0x003022f3};
  std::vector<std::uint8_t> bytes;
  for (auto word : code) for (unsigned i = 0; i != 4; ++i) bytes.push_back(word >> (8 * i));
  checker.load(base, bytes);
  const auto count = xlen == 32 ? 7 : code.size();
  for (unsigned n = 0; n != count; ++n) {
    o::Record r{0, 0, n, {0, n}, o::Instruction{base+4*n,code[n],4,4,{3,false},0},
      o::Retirement{base+4*(n+1),{3,false}}, {}, {}};
    auto reg = [&](o::Bank bank, unsigned index, std::uint64_t value) {
      const std::uint64_t mask = xlen == 32 ? UINT32_MAX : UINT64_MAX;
      r.effects[{0,0}] = o::RegisterWrite{bank, static_cast<std::uint16_t>(index), 0, mask, value & mask};
    };
    if (n == 0) reg(o::Bank::Integer,1,0x4000);
    if (n == 2) reg(o::Bank::FloatingPoint,0,0xffffffff00000000);
    if (n == 3 || n == 4) reg(o::Bank::FloatingPoint,n-2,0xffffffff7fc00000);
    if (n == 5) reg(o::Bank::Integer,3,512);
    if (n == 7) reg(o::Bank::FloatingPoint,3,0);
    if (n == 8) reg(o::Bank::FloatingPoint,4,0xffffffff7fc00000);
    if (n == 9) reg(o::Bank::Integer,4,0xffffffff7fc00000);
    if (n == 3 || n == 4 || n == 6 || n == 8)
      r.effects[{1,0}] = o::CsrUpdate{1,o::CsrOperation::SetBits,31,n == 8 ? 0ULL : 16ULL};
    if (n == 11 || n == 13 || n == 15) reg(o::Bank::Integer,5,n == 13 ? 2 : 0);
    if (n == 2 && mutation == 1) std::get<o::RegisterWrite>(r.effects.at({0,0})).value = 0;
    if (n == 3 && mutation == 2) std::get<o::RegisterWrite>(r.effects.at({0,0})).bank = o::Bank::Integer;
    if (n == 3 && mutation == 3) std::get<o::RegisterWrite>(r.effects.at({0,0})).index = 2;
    if (n == 3 && mutation == 4) std::get<o::CsrUpdate>(r.effects.at({1,0})).value = 0;
    if (n == 8 && mutation == 6) std::get<o::RegisterWrite>(r.effects.at({0,0})).value = 0xffffffff00000000;
    if (n == 9 && mutation == 7) std::get<o::RegisterWrite>(r.effects.at({0,0})).value ^= 1;
    if (n == 2 && mutation == 8) r.effects.clear();
    if (n == 6 && mutation == 9) reg(o::Bank::Integer,0,0x7fffffff);
    if (n == 5 && mutation == 10) r.effects[{1,0}] = o::CsrUpdate{1,o::CsrOperation::SetBits,31,1};
    if (n == 11 && mutation == 11) std::get<o::RegisterWrite>(r.effects.at({0,0})).value = 16;
    if (n == 15 && mutation == 12) std::get<o::RegisterWrite>(r.effects.at({0,0})).value = 2;
    checker.check(r);
  }
  if (checker.checked() != count) throw std::runtime_error("FP stream did not drain");
}
int main() {
  run(0,32);
  for (unsigned mutation : {2,3,4,8,9,10}) {
    bool rejected = false;
    try { run(mutation,32); } catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) throw std::runtime_error("RV32F corruption was not rejected");
  }
  run(0);
  for (unsigned mutation : {1,2,3,4,6,7,8,9,10,11,12}) {
    bool rejected = false;
    try { run(mutation); } catch (const std::runtime_error& e) {
      rejected = std::string(e.what()).find("cosim mismatch:") != std::string::npos;
    }
    if (!rejected) throw std::runtime_error("FP corruption was not rejected");
  }
  std::cout << "FP checker: boxing, f0, x0, flag effects, CSR readback and eleven corruptions passed\n";
}
