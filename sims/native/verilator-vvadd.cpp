// Verifies concurrent hart workloads and times their complete SoC execution on Verilator threads.
#include "VSoCHarness.h"
#include "VSoCHarness__Dpi.h"
#include "profile-window.h"
#include "verilated.h"
#include "vvadd-loader.h"
#include <chrono>
#include <cstdio>
#ifndef RDS_VERILATOR_THREADS
#define RDS_VERILATOR_THREADS 1
#endif
#ifndef RDS_VERILATOR_HARTS
#define RDS_VERILATOR_HARTS 8
#endif
static VvaddLoader host;
extern "C" int rhodium_htif_tick(unsigned char reset, char xlen, long long boot_address,
    unsigned char ready, unsigned char valid, long long data, char status,
    unsigned char *request_valid, unsigned char *write, long long *address,
    long long *write_data, char *length, unsigned char *response_ready) {
  uint64_t in[] = {reset, ready, valid, uint64_t(data), uint8_t(status)}, out[7];
  if (xlen != 64 || boot_address != 0x1000 || host.tick_current(in,5,out,7)) std::abort();
  *request_valid=out[0]; *write=out[1]; *address=out[2]; *write_data=out[3];
  *length=out[4]; *response_ready=out[5]; return int(out[6]);
}
static double now() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}
int main(int argc, char **argv) {
  try {
    if (argc < 3)
      throw std::runtime_error(
          "usage: verilator-vvadd program.bin harts [rounds] [cycle-limit]");
    host.configure(argv[1], std::stoul(argv[2]),
                   argc > 3 ? std::stoul(argv[3]) : 64);
    if (host.harts != RDS_VERILATOR_HARTS)
      throw std::runtime_error(
          "requested hart count differs from the Verilator model");
    uint64_t limit = argc > 4 ? std::stoull(argv[4])
                             : 2000000 + uint64_t(host.rounds) * (host.chasing ? 8388608 : 4096);
    VerilatedContext context;
    context.threads(RDS_VERILATOR_THREADS);
    context.commandArgs(argc, argv);
    VSoCHarness top{&context};
    ProfileWindow profile;
    double whole = now(), begin = 0, end = 0;
    for (uint64_t cycle = 0; cycle < limit; ++cycle) {
      top.clock = 0;
      top.reset = cycle < 8;
      top.uart_in = 1;
      top.eval();
      if (host.measuring && !begin) {
        profile.enable(true);
        begin = now();
      }
      if (host.finished_measurement && !end) {
        end = now();
        profile.enable(false);
      }
      if (top.hart_count != host.harts)
        throw std::runtime_error("unexpected hart count " +
                                 std::to_string(top.hart_count));
      if (top.exit) {
        if (top.exit != 1 || !host.error.empty())
          throw std::runtime_error(host.error.empty() ? "target failed"
                                                      : host.error);
        std::printf(
            "{\"engine\":\"verilator\",\"workload\":\"%s\",\"workers\":%u,\"context_threads\":%u,"
            "\"harts\":%u,\"rounds\":%u,\"cycles\":%llu,\"total_cycles\":%llu,"
            "\"seconds\":%.9f,\"whole_seconds\":%.9f,\"polls\":%llu,\"digest\":"
            "\"%016llx\",\"harts_detail\":[",
            host.workload(), top.threads(), context.threads(), host.harts, host.rounds,
            (unsigned long long)(host.stop - host.deadline),
            (unsigned long long)cycle, end - begin, now() - whole,
            (unsigned long long)host.polls, (unsigned long long)host.trace);
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
      top.clock = 1;
      top.eval();
      context.timeInc(1);
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
