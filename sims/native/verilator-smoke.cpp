// Runs the same coherent MiniSoC loader against generated RTL at the C++ boundary.
#include "VSoCHarness.h"
#include "VSoCHarness__Dpi.h"
#include "verilated.h"
#include "mini-loader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifndef RDS_VERILATOR_THREADS
#define RDS_VERILATOR_THREADS 1
#endif
static loader host;
extern "C" int rhodium_htif_tick(unsigned char reset, char xlen, long long boot_address,
    unsigned char ready, unsigned char valid, long long data, char status,
    unsigned char *request_valid, unsigned char *write, long long *address,
    long long *write_data, char *length, unsigned char *response_ready) {
  uint64_t in[] = {reset, ready, valid, uint64_t(data), uint8_t(status)}, out[7];
  if (xlen != 64 || boot_address != 0x1000 || tick_current(&host,in,5,out,7)) std::abort();
  *request_valid=out[0]; *write=out[1]; *address=out[2]; *write_data=out[3];
  *length=out[4]; *response_ready=out[5]; return int(out[6]);
}
/* Measures repeated coherent boots while excluding model construction and warmup. */
#include <time.h>
static double now_seconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
static int benchmark(int argc,char **argv) {
    if(loader_configure(&host)) { std::fprintf(stderr,"invalid RDS_LOOP_ITERATIONS\n"); return 2; }
    unsigned rounds=argc>1?(unsigned)std::strtoul(argv[1],nullptr,10):200;
    double begin=now_seconds();
    VerilatedContext context; context.threads(RDS_VERILATOR_THREADS); context.commandArgs(argc,argv);
    VSoCHarness top{&context};
    double startup=now_seconds()-begin;
    uint64_t cycles=0,digest=0,total_polls=0;
    for(unsigned run=0;run<rounds+5;++run) {
        if(run==5) begin=now_seconds();
        uint64_t cycle;
        for(cycle=0;cycle<20000;++cycle) {
            top.clock=0; top.reset=cycle<8; top.uart_in=1; top.eval();
            if(top.exit && cycle>=8) {
                if(top.exit!=1) return 1;
                if(run>=5) { cycles+=cycle; total_polls+=host.polls; digest=(digest^host.trace^cycle)*UINT64_C(1099511628211); }
                break;
            }
            top.clock=1; top.eval(); context.timeInc(1);
        }
        if(cycle==20000) { std::fprintf(stderr,"timeout\n"); return 1; }
    }
    double elapsed=now_seconds()-begin;
    std::printf("{\"seconds\":%.9f,\"startup_seconds\":%.9f,\"cycles\":%llu,\"polls\":%llu,\"digest\":\"%016llx\",\"workers\":%u,\"context_threads\":%u}\n",elapsed,startup,(unsigned long long)cycles,(unsigned long long)total_polls,(unsigned long long)digest,top.threads(),context.threads());
    return 0;
}

int main(int argc,char **argv) {
    if (argc > 2 && !std::strcmp(argv[2], "bench")) return benchmark(argc, argv);
    if(loader_configure(&host)) { std::fprintf(stderr,"invalid RDS_LOOP_ITERATIONS\n"); return 2; }
    VerilatedContext context; context.threads(RDS_VERILATOR_THREADS); context.commandArgs(argc,argv);
    VSoCHarness top{&context};
    uint64_t limit=argc>1?std::strtoull(argv[1],nullptr,10):200000;
    for(uint64_t cycle=0;cycle<limit;++cycle) {
        top.clock=0; top.reset=cycle<8; top.uart_in=1; top.eval();
        if(top.exit) {
            std::printf("PASS: MiniSoC executed RV64I and coherent mailbox read returned 1 at cycle %llu (%llu polls), host trace %016llx\n",
                (unsigned long long)cycle,(unsigned long long)host.polls,(unsigned long long)host.trace);
            return top.exit==1?0:1;
        }
        top.clock=1; top.eval(); context.timeInc(1);
    }
    std::fprintf(stderr,"MiniSoC timeout: stage %u, word %u, polls %llu\n",host.stage,host.index,(unsigned long long)host.polls);
    return 1;
}
