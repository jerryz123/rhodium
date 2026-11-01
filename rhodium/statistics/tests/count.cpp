// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "statistics.h"
#include "collector.h"

static rhodium::statistics::Collector collector(rhodium::statistics::descriptors);
extern "C" void rhodium_stat_count(int id) { collector.count(static_cast<std::uint32_t>(id)); }

int main() {
  return run_test([] {
    auto tick = [] {
      collector.begin(reset);
      tick_model();
      collector.end();
    };
    reset = 1; left_event = 1; right_event = 1;
    tick(); tick();
    reset = 0;
    for (unsigned i = 0; i < 10; ++i) {
      left_event = i % 2 == 0;
      right_event = i % 3 == 0;
      tick();
    }
    CHECK(left_value == 5 && right_value == 4);
    collector.freeze();
    const auto report = collector.json();
    CHECK(report == "[{\"path\":[\"left\"],\"name\":\"events\",\"count\":5},"
                    "{\"path\":[\"left\"],\"name\":\"never\",\"count\":0},"
                    "{\"path\":[\"right\"],\"name\":\"events\",\"count\":4},"
                    "{\"path\":[\"right\"],\"name\":\"never\",\"count\":0},"
                    "{\"path\":[],\"name\":\"cycles\",\"count\":10}]");
    left_event = 1; right_event = 1;
    tick(); tick();
    CHECK(collector.json() == report);
    CHECK(left_value == 7 && right_value == 6);
    bool rejected = false;
    try { collector.count(0); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    collector.begin(false);
    rejected = false;
    try { collector.count(99); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    collector.count(0);
    rejected = false;
    try { collector.count(0); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    collector.end();
  });
}
