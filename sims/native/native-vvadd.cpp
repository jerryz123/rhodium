// Verifies and times shared all-hart workloads through the native simulator's cycle API.
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include "profile-window.h"
#include "vvadd-loader.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
static int callback(void *context, const uint64_t *in, size_t ni, uint64_t *out,
                    size_t no) {
  return static_cast<VvaddLoader *>(context)->tick_current(in, ni, out, no);
}
static double now() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}
int main(int argc, char **argv) {
  try {
    if (argc < 5)
      throw std::runtime_error("usage: native-vvadd model.rsim program.bin "
                               "harts workers [rounds] [cycle-limit]");
    VvaddLoader host;
    host.configure(argv[2], std::stoul(argv[3]),
                   argc > 5 ? std::stoul(argv[5]) : 64);
    uint64_t limit = argc > 6 ? std::stoull(argv[6])
                             : 2000000 + uint64_t(host.rounds) * (host.chasing ? 8388608 : 4096);
    rds_options options{
        uint32_t(std::stoul(argv[4])),
        uint32_t(std::getenv("RDS_FLAGS")
                     ? std::stoul(std::getenv("RDS_FLAGS"), nullptr, 0)
                     : 0)};
    char error[512];
    std::unique_ptr<rds_sim, decltype(&rds_free)> sim(
        rds_load_with_options(argv[1], &options, error, sizeof error),
        rds_free);
    if (!sim)
      throw std::runtime_error(error);
    auto *s = sim.get();
    auto check = [&](int result) {
      if (result)
        throw std::runtime_error(rds_error(s));
    };
    check(rds_bind_host(s, nullptr, callback, &host));
    if (const char *library = std::getenv("RDS_COMPILED"))
      check(rds_use_compiled(s, library));
    int reset = rds_find_port(s, "reset"), uart = rds_find_port(s, "uart_in"),
        exit_port = rds_find_port(s, "exit"),
        hart_count_port = rds_find_port(s, "hart_count");
    if (reset < 0 || uart < 0 || exit_port < 0 || hart_count_port < 0)
      throw std::runtime_error("missing banked harness ports");
    if (rds_port_width(s, hart_count_port) != 4)
      throw std::runtime_error(
          "requested hart count differs from the native model");
    ProfileWindow profile;
    double whole = now(), begin = 0, end = 0;
    for (uint64_t cycle = 0; cycle < limit; ++cycle) {
      check(rds_set_u64(s, reset, cycle < 8));
      check(rds_set_u64(s, uart, 1));
      check(rds_eval(s));
      uint64_t status = 0, model_harts = 0;
      check(rds_get_u64(s, exit_port, &status));
      check(rds_get_u64(s, hart_count_port, &model_harts));
      if (host.measuring && !begin) {
        profile.enable(true);
        begin = now();
      }
      if (host.finished_measurement && !end) {
        end = now();
        profile.enable(false);
      }
      if (model_harts != host.harts)
        throw std::runtime_error("unexpected hart count " + std::to_string(model_harts));
      if (status) {
        if (status != 1 || !host.error.empty())
          throw std::runtime_error(host.error.empty() ? "target failed"
                                                      : host.error);
        auto stats = rds_get_stats(s);
        auto execution = rds_get_execution_stats(s);
        std::printf(
            "{\"engine\":\"native\",\"workload\":\"%s\",\"workers\":%u,\"requested_workers\":%u,"
            "\"harts\":%u,\"rounds\":%u,\"cycles\":%llu,\"total_cycles\":%llu,"
            "\"seconds\":%.9f,\"whole_seconds\":%.9f,\"polls\":%llu,\"digest\":"
            "\"%016llx\",\"scheduled_operations\":%u,\"replicated_operations\":"
            "%u,\"harts_detail\":[",
            host.workload(), stats.workers, options.workers, host.harts, host.rounds,
            (unsigned long long)(host.stop - host.deadline),
            (unsigned long long)cycle, end - begin, now() - whole,
            (unsigned long long)host.polls, (unsigned long long)host.trace,
            execution.scheduled_operations, execution.replicated_operations);
        for (unsigned h = 0; h < host.harts; ++h) {
          const auto &a = host.hart[h];
          std::printf("%s{\"hart\":%u,\"progress\":%llu,\"retired\":%llu,"
                      "\"start\":%llu,\"finish\":%llu,\"heartbeat\":%s}",
                      h ? "," : "", h, (unsigned long long)a.progress,
                      (unsigned long long)a.retired,
                      (unsigned long long)a.start, (unsigned long long)a.finish,
                      a.heartbeat ? "true" : "false");
        }
        std::puts("]}");
        return 0;
      }
      check(rds_advance(s));
    }
    throw std::runtime_error("timeout stage=" + std::to_string(host.stage) +
                             " ready=" + std::to_string(host.ready_mask) +
                             " done=" + std::to_string(host.done_mask) +
                             " ticks=" + std::to_string(host.ticks));
  } catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
