// Exercises semantic ordering, delayed producers, lifecycle, and malformed hooks.
// SPDX-License-Identifier: Apache-2.0
#include "observation.h"
#include "hooks-dpi.h"
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace rhodium::cosim::observation;
static void require(bool v) { if (!v) throw std::runtime_error("test failed"); }
template<class F> static void rejects(F f) {
  try { f(); } catch (const std::runtime_error&) { return; }
  throw std::runtime_error("expected rejection");
}
static void reset(Collector& c, Word instance = 0, Word epoch = 0) {
  c.reset(instance, epoch, {instance + 10, 0x80000000, {3, false}, 64, 256});
}
static Instruction instruction(Word producers = 1) { return {0x80000000, 0x13, 4, 4, {3, false}, producers}; }
static Retirement retired() { return {0x80000004, {3, false}}; }
static RegisterWrite write(Word value = 42) { return {Bank::Integer, 1, 0, ~Word{0}, value}; }
static Trap trap() { return {13, 0x80000000, 0x2000, 0x100, {1, false}, true, 0x800, 0x3000}; }

static void permutations() {
  std::array<unsigned, 6> permutation{0, 1, 2, 3, 4, 5};
  do {
    Collector c; reset(c); c.begin_sample(0);
    const std::array<std::function<void()>, 6> calls{
      [&]{c.instruction(0, {0,0}, instruction(3));},
      [&]{c.retire(0, {0,0}, retired());},
      [&]{c.effect(0, {0,0}, {0,0}, write());},
      [&]{c.effect(0, {0,0}, {1,0}, CsrUpdate{1, CsrOperation::SetBits, 31, 16});},
      [&]{c.seal(0, {0,0}, 0, 1);},
      [&]{c.seal(0, {0,0}, 1, 1);}
    };
    for (auto i : permutation) calls[i]();
    auto records = c.end_sample();
    require(records.size() == 1 && records[0].effects.size() == 2);
    require(std::get<RegisterWrite>(records[0].effects.at({0,0})).value == 42);
    c.finish();
  } while (std::next_permutation(permutation.begin(), permutation.end()));
}
static void delayed() {
  Collector c; reset(c); reset(c, 1);
  c.environment(0, {0x80, 123, 4}); c.begin_sample(4);
  c.instruction(0, {0,0}, instruction()); c.retire(0, {0,0}, retired());
  c.seal(0, {0,0}, 0, 1); // Allowed before the delayed effect callback.
  c.instruction(0, {0,1}, instruction(0)); c.retire(0, {0,1}, retired());
  c.interrupt(1, {0,0}, {trap(), 0});
  auto r = c.end_sample(); require(r.size() == 1 && r[0].instance == 1);
  c.environment(0, {0, 200, 8}); c.begin_sample(8);
  c.effect(0, {0,0}, {0,0}, write());
  r = c.end_sample(); require(r.size() == 2 && r[0].id.order == 0 && r[1].id.order == 1);
  require(r[0].sample == 4 && r[0].environment.time == 123);
  require(r[0].hart == 10);
  c.finish();
}
static void partial_exception_and_reset() {
  Collector c; reset(c); c.begin_sample(0);
  c.instruction(0, {0,0}, instruction()); c.exception(0, {0,0}, trap());
  c.effect(0, {0,0}, {0,0}, RegisterWrite{Bank::Vector, 0, 128, 1, 1});
  c.effect(0, {0,0}, {0,1}, CsrUpdate{8, CsrOperation::AssignMasked, ~Word{0}, 4});
  c.effect(0, {0,0}, {0,2}, MemoryEffect{4, 3, AccessKind::Load, 0x2000, false, 0, 31, false, false, 0, 0, AccessResult::Fault});
  c.seal(0, {0,0}, 0, 3);
  auto r = c.end_sample(); require(r.size() == 1 && std::get<Trap>(r[0].outcome).tval == 0x2000);
  c.begin_sample(1); c.instruction(0, {0,1}, instruction()); require(c.end_sample().empty());
  reset(c, 0, 1); c.begin_sample(2);
  c.instruction(0, {1,0}, instruction(0)); c.retire(0, {1,0}, retired());
  require(c.end_sample().size() == 1); c.finish();
}
static void invalid() {
  const std::array<std::function<void(Collector&)>, 12> cases{
    [](Collector& c){c.instruction(0,{1,0},instruction());},
    [](Collector& c){c.instruction(0,{0,4096},instruction());},
    [](Collector& c){c.effect(0,{0,0},{0,0},write()); c.end_sample();},
    [](Collector& c){c.retire(0,{0,0},retired()); c.exception(0,{0,0},trap());},
    [](Collector& c){c.instruction(0,{0,0},instruction()); c.instruction(0,{0,0},instruction());},
    [](Collector& c){c.effect(0,{0,0},{0,0},write()); c.effect(0,{0,0},{0,0},write());},
    [](Collector& c){c.seal(0,{0,0},0,0); c.seal(0,{0,0},0,0);},
    [](Collector& c){c.instruction(0,{0,0},instruction(0)); c.seal(0,{0,0},0,0); c.end_sample();},
    [](Collector& c){c.instruction(0,{0,0},instruction()); c.seal(0,{0,0},0,0); c.effect(0,{0,0},{0,0},write()); c.end_sample();},
    [](Collector& c){c.effect(0,{0,0},{0,0},RegisterWrite{Bank::Integer,0,0,1,1});},
    [](Collector& c){c.effect(0,{0,0},{0,0},RegisterWrite{Bank::Vector,1,255,3,1});},
    [](Collector& c){c.environment(0,{});}
  };
  for (const auto& f : cases) {
    Collector c; reset(c); c.begin_sample(0); rejects([&]{f(c);}); rejects([&]{c.end_sample();});
  }
  for (unsigned kind = 0; kind < 4; ++kind) {
    Collector c; reset(c); c.begin_sample(0);
    c.instruction(0, {0, kind == 0 ? Word{1} : Word{0}}, instruction());
    if (kind != 1) c.retire(0, {0, kind == 0 ? Word{1} : Word{0}}, retired());
    if (kind != 2) c.seal(0, {0, kind == 0 ? Word{1} : Word{0}}, 0, 1);
    if (kind != 3) c.effect(0, {0, kind == 0 ? Word{1} : Word{0}}, {0,0}, write());
    c.end_sample(); rejects([&]{c.finish();});
  }
  Collector c; reset(c); c.begin_sample(0);
  c.instruction(0,{0,0},instruction(0)); c.retire(0,{0,0},retired()); c.end_sample();
  c.begin_sample(1); rejects([&]{c.retire(0,{0,0},retired());});
  Collector stale; reset(stale); reset(stale,0,1); stale.begin_sample(0);
  rejects([&]{stale.effect(0,{0,0},{0,0},write());});
}
static void dpi() {
  Collector c; reset(c); DpiBinding binding(c); c.begin_sample(0);
  rhodium_cosim_seal(0,0,0,0,1);
  rhodium_cosim_reg_write(0,0,0,0,0,0,1,0,-1,std::int64_t(0x8000000000000042ULL));
  rhodium_cosim_retire(0,0,0,0x80000004,3,0);
  rhodium_cosim_instruction(0,0,0,0x80000000,0x13,4,4,3,0,1);
  binding.check(); auto r = c.end_sample();
  require(std::get<RegisterWrite>(r[0].effects.at({0,0})).value == 0x8000000000000042ULL);
  c.begin_sample(1); rhodium_cosim_seal(0,0,0,0,0);
  rejects([&]{binding.check();}); // Failure retained rather than escaping C ABI.
}
static void widths_and_partial_fetch() {
  Collector c;
  c.reset(0, 0, {0, 0x1000, {3, false}, 32, 64});
  c.begin_sample(0);
  c.instruction(0, {0,0}, {0x1000, 1, 2, 2, {1, true}, 1});
  c.retire(0, {0,0}, {0x1002, {1, true}});
  c.effect(0, {0,0}, {0,0}, RegisterWrite{Bank::Integer, 31, 0, 0xffffffff, 0x80000000});
  c.effect(0, {0,0}, {0,1}, RegisterWrite{Bank::FloatingPoint, 2, 0, ~Word{0}, 0xffffffff3f800000ULL});
  c.effect(0, {0,0}, {0,2}, CsrUpdate{1, CsrOperation::ClearBits, 0xffffffff, 16});
  c.seal(0, {0,0}, 0, 3);
  c.instruction(0, {0,1}, {0x1ffe, 3, 2, 0, {1, true}, 0});
  c.exception(0, {0,1}, trap());
  auto r = c.end_sample(); require(r.size() == 2);
  require(std::get<Instruction>(r[0].event).instruction_bytes == 2);
  require(std::get<Instruction>(r[1].event).instruction_bytes == 0);
  c.finish();
  Collector invalid;
  invalid.reset(0, 0, {0, 0x1000, {3, false}, 32, 64}); invalid.begin_sample(0);
  rejects([&]{invalid.effect(0,{0,0},{0,0},write());});
}
int main() {
  permutations(); delayed(); partial_exception_and_reset(); invalid(); dpi(); widths_and_partial_fetch();
  std::cout << "cosim observation tests passed\n";
}
