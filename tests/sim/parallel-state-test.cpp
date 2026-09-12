// Replays snapshot-prefix copies, FIFO/register regions, and broadcast epilogues
// across component and replicated schedules, resets, and repeated evaluation.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool yes, const char *why) {
  if (!yes)
    throw std::runtime_error(why);
}
int main(int argc, char **argv) {
  try {
    check(argc == 2, "usage: parallel-state-test output-directory");
    std::string dir = argv[1];
    std::filesystem::create_directories(dir);
    Model m;
    m.widths = {1, 64};
    m.metadata = {{"format", "rhodium-simulation-ir-v1"},
                  {"registers", Json::array()},
                  {"memories", Json::array()},
                  {"writes", Json::array()},
                  {"reads", Json::array()},
                  {"assertions", Json::array()},
                  {"objects", Json::array()},
                  {"origins", Json::array()},
                  {"inventory", Json::array()},
                  {"occurrences", Json::array()},
                  {"ports", Json::array({{0, 0, "reset"}, {0, 1, "data"}})}};
    auto emit = [&](unsigned op, unsigned width, std::vector<Id> a,
                    std::vector<uint64_t> im = std::vector<uint64_t>{}) {
      Id id = m.widths.size();
      m.widths.push_back(width);
      m.ops.push_back({op, id, a, im});
      return id;
    };
    Id zero = emit(0, 64, {}, {0});
    Id prefix = emit(5, 64, {1, emit(18, 64, {emit(2, 1, {0})})});
    std::vector<Id> valid, payload, acknowledgments;
    for (unsigned core = 0; core < 8; ++core) {
      valid.push_back(emit(29, 1, {}, {core, 2}));
      payload.push_back(emit(29, 64, {}, {core, 3}));
    }
    for (unsigned core = 0; core < 8; ++core) {
      Id q = m.widths.size();
      m.widths.push_back(64);
      Id x = emit(5, 64, {prefix, q});
      for (unsigned step = 0; step < 24; ++step)
        x = emit(step % 2 ? 5 : 6, 64,
                 {x, emit(0, 64, {}, {17 * core + step + 1})});
      Id en = emit(17, 1, {1}, {core});
      acknowledgments.push_back(emit(17, 1, {x}, {core}));
      Id ready = emit(2, 1, {valid[(core + 1) % 8]});
      m.metadata["objects"].push_back(
          {1, 64, 2, 0, std::vector<Id>{0, en, x, ready},
           "top/chip/core" + std::to_string(core) + "/queue"});
      Id next = emit(6, 64, {x, payload[(core + 7) % 8]});
      m.metadata["registers"].push_back({q, next, 0, zero});
      m.metadata["ports"].push_back({1, q, "q" + std::to_string(core)});
      m.metadata["ports"].push_back(
          {1, payload[core], "p" + std::to_string(core)});
    }
    std::vector<Id> broadcast_inputs{0, emit(17, 1, {1}, {15}), 1};
    broadcast_inputs.insert(broadcast_inputs.end(), acknowledgments.begin(), acknowledgments.end());
    m.metadata["objects"].push_back({9, 64, 8, 0, broadcast_inputs, "top/start/broadcast"});
    Id admitted = emit(29, 1, acknowledgments, {8, 0});
    Id admitted_q = m.widths.size();
    m.widths.push_back(1);
    m.metadata["registers"].push_back({admitted_q,
        emit(3, 1, {admitted, emit(2, 1, {admitted_q})}), 0, emit(0, 1, {}, {0})});
    m.metadata["ports"].push_back({1, admitted_q, "broadcast_admitted_register"});
    m.metadata["ports"].push_back({1, admitted, "broadcast_ready"});
    for (unsigned core = 0; core < 8; ++core)
      m.metadata["ports"].push_back({1, emit(29, 1, {}, {8, core + 2}),
                                    "broadcast_pending" + std::to_string(core)});
    m.metadata["ports"].push_back({1, emit(29, 64, {}, {8, 1}), "broadcast_payload"});
    // The late readiness value may enable an independent, earlier payload cone.
    // Its computation cannot read the previous cycle's readiness as a guard.
    Id late_payload = 1;
    for (unsigned step = 0; step < 32; ++step)
      late_payload = emit(step % 2 ? 5 : 6, 64,
                          {late_payload, emit(0, 64, {}, {step + 73})});
    m.metadata["objects"].push_back({1, 64, 1, 0,
        std::vector<Id>{0, admitted, late_payload, emit(0, 1, {}, {1})}, "top/late/queue"});
    m.metadata["ports"].push_back({1, emit(29, 64, {}, {9, 3}), "late_payload"});
    unsigned port_count = m.metadata["ports"].size();
    m.validate();
    std::string file = dir + "/model.rsim";
    m.write_binary(file);
    Model split = m;
    split_snapshot_prefixes(split, 16);
    check(split.metadata.at("prefix_split").at("accepted").get<bool>(),
          "fixture must exercise accepted snapshot-prefix splitting");
    check(split.metadata.at("prefix_split").at("cloned_values").get<unsigned>() > 0,
          "fixture must exercise cloned snapshot values");
    check(split.metadata["objects"].size() == m.metadata["objects"].size() &&
          split.metadata["registers"].size() == m.metadata["registers"].size(),
          "snapshot splitting must preserve unique state owners");
    std::string split_file = dir + "/split.rsim";
    split.write_binary(split_file);
    using Sim = std::unique_ptr<rds_sim, decltype(&rds_free)>;
    std::vector<Sim> sims;
    for (unsigned mode = 0; mode < 6; ++mode) {
      bool component = mode < 3 || mode == 5;
      rds_options options{mode ? 8u : 1u,
                          mode ? uint32_t(RDS_PARALLEL_STATE |
                                          RDS_PARALLEL_PUBLISH | RDS_SPIN |
                                          (component ? RDS_NO_PARTITION : 0))
                               : uint32_t(RDS_REFERENCE)};
      char error[512];
      const auto &input = mode && component ? split_file : file;
      Sim s(rds_load_with_options(input.c_str(), &options, error, sizeof error),
            rds_free);
      check(bool(s), error);
      if (mode)
        check(rds_get_stats(s.get()).workers == 8,
              "parallel state fixture must exercise eight workers");
      if (mode == 2 || mode == 4 || mode == 5) {
        if (mode == 2) {
          std::string plan = dir + "/component-plan.json";
          check(!rds_emit_plan(s.get(), plan.c_str()), rds_error(s.get()));
          Json report;
          std::ifstream(plan) >> report;
          check(!report.at("epilogue").empty(), "fixture must exercise deferred broadcast readiness");
        }
        std::string c = dir + "/model-" + std::to_string(mode) + ".c",
                    so = c + ".so";
        check(!rds_emit_c(s.get(), c.c_str(), mode == 5 ? 1 : 0), rds_error(s.get()));
        pid_t child = fork();
        check(child >= 0, "fork failed");
        if (!child) {
          execlp("clang", "clang", "-O3", "-march=native", "-DNDEBUG",
                 "-shared", "-fPIC", c.c_str(), "-o", so.c_str(), nullptr);
          _exit(127);
        }
        int status = 0;
        check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                  !WEXITSTATUS(status),
              "generated C compile failed");
        check(!rds_use_compiled(s.get(), so.c_str()), rds_error(s.get()));
      }
      sims.push_back(std::move(s));
    }
    std::mt19937_64 random(47);
    for (unsigned cycle = 0; cycle < 512; ++cycle) {
      uint64_t x = random();
      std::vector<uint64_t> expected;
      for (auto &s : sims) {
        check(!rds_set_u64(s.get(), 0, cycle < 3 || cycle == 251),
              rds_error(s.get()));
        check(!rds_set_u64(s.get(), 1, x), rds_error(s.get()));
        check(!rds_eval(s.get()) && !rds_eval(s.get()), rds_error(s.get()));
        std::vector<uint64_t> actual;
        for (unsigned p = 2; p < port_count; ++p) {
          uint64_t value;
          check(!rds_get_u64(s.get(), p, &value), rds_error(s.get()));
          actual.push_back(value);
        }
        if (expected.empty())
          expected = actual;
        else
          check(actual == expected,
                "cross-region FIFO/register replay mismatch");
        check(!rds_advance(s.get()), rds_error(s.get()));
      }
    }
    std::cout << "512 cycles: reference and eight-worker component/partition "
                 "schedules agree through resets and repeated eval\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
