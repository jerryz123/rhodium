/* Boots actual MiniSoC through coherent host writes and checks a target-written mailbox. */
#define _POSIX_C_SOURCE 200809L
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mini-loader.h"

/* Measures repeated coherent boots while excluding model construction and warmup. */
static double now_seconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
static int benchmark(int argc,char **argv) {
    if(argc<2) return 2;
    unsigned rounds=argc>2?(unsigned)strtoul(argv[2],NULL,10):200;
    char error[512]; loader host={0};
    if(loader_configure(&host)) { fprintf(stderr,"invalid RDS_LOOP_ITERATIONS\n"); return 2; }
    rds_options options={getenv("RDS_WORKERS")?(uint32_t)strtoul(getenv("RDS_WORKERS"),NULL,10):1,getenv("RDS_FLAGS")?(uint32_t)strtoul(getenv("RDS_FLAGS"),NULL,0):0};
    double begin=now_seconds();
    rds_sim *s=rds_load_with_options(argv[1],&options,error,sizeof error);
    if(!s) { fprintf(stderr,"%s\n",error); return 1; }
    if(rds_bind_host(s,NULL,tick_current,&host)) goto fail;
    if(getenv("RDS_COMPILED") && rds_use_compiled(s,getenv("RDS_COMPILED"))) goto fail;
    int reset=rds_find_port(s,"reset"), uart=rds_find_port(s,"uart_in"), exit_port=rds_find_port(s,"exit");
    double startup=now_seconds()-begin, elapsed=0;
    uint64_t cycles=0, digest=0, total_polls=0;
    for(unsigned run=0;run<rounds+5;++run) {
        if(run==5) begin=now_seconds();
        uint64_t cycle;
        for(cycle=0;cycle<20000;++cycle) {
            uint64_t status=0;
            if(rds_set_u64(s,reset,cycle<8)||rds_set_u64(s,uart,1)||rds_eval(s)||rds_get_u64(s,exit_port,&status)) goto fail;
            if(status && cycle>=8) {
                if(status!=1) goto fail;
                if(run>=5) { cycles+=cycle; total_polls+=host.polls; digest=(digest^host.trace^cycle)*UINT64_C(1099511628211); }
                break;
            }
            if(rds_advance(s)) goto fail;
        }
        if(cycle==20000) goto fail;
    }
    elapsed=now_seconds()-begin;
    printf("{\"seconds\":%.9f,\"startup_seconds\":%.9f,\"cycles\":%llu,\"polls\":%llu,\"digest\":\"%016llx\",\"workers\":%u}\n",elapsed,startup,(unsigned long long)cycles,(unsigned long long)total_polls,(unsigned long long)digest,rds_get_stats(s).workers);
    rds_free(s); return 0;
fail: fprintf(stderr,"native failure: %s\n",rds_error(s)); rds_free(s); return 1;
}

int main(int argc,char **argv) {
    if (argc > 3 && !strcmp(argv[3], "bench")) return benchmark(argc, argv);
    if(argc<2) return 2;
    char error[512]; loader host={0};
    if(loader_configure(&host)) { fprintf(stderr,"invalid RDS_LOOP_ITERATIONS\n"); return 2; }
    rds_options options={getenv("RDS_WORKERS")?(uint32_t)strtoul(getenv("RDS_WORKERS"),NULL,10):1,getenv("RDS_FLAGS")?(uint32_t)strtoul(getenv("RDS_FLAGS"),NULL,0):0};
    rds_sim *s=rds_load_with_options(argv[1],&options,error,sizeof error);
    if(!s) { fprintf(stderr,"%s\n",error); return 1; }
    if(rds_bind_host(s,NULL,tick_current,&host)) goto fail;
    if(getenv("RDS_COMPILED") && rds_use_compiled(s,getenv("RDS_COMPILED"))) goto fail;
    int reset=rds_find_port(s,"reset"), uart=rds_find_port(s,"uart_in"), exit_port=rds_find_port(s,"exit");
    uint64_t limit=argc>2?strtoull(argv[2],NULL,10):200000;
    rds_stats st=rds_get_stats(s);
    if (argc > 3 && !strcmp(argv[3], "stats")) {
        rds_execution_stats ex = rds_get_execution_stats(s);
        printf("{\"model_bytes\":%zu,\"descriptor_bytes\":%zu,\"schedule_bytes\":%zu,\"value_bytes\":%zu,"
               "\"memory_bytes\":%zu,\"update_bytes\":%zu,\"object_bytes\":%zu,\"payload_bytes\":%zu,"
               "\"workers\":%u,\"batches\":%u,\"scheduled_operations\":%u,\"replicated_operations\":%u,"
               "\"compiled_blocks\":%u,\"compiled_shapes\":%u,\"estimated_work\":%llu,\"largest_worker_work\":%llu,"
               "\"compiled_file_bytes\":%zu}\n",
               st.model_bytes,st.descriptor_bytes,st.schedule_bytes,st.value_bytes,st.memory_bytes,st.update_bytes,
               st.object_bytes,st.payload_bytes,st.workers,st.batches,ex.scheduled_operations,ex.replicated_operations,
               ex.compiled_blocks,ex.compiled_shapes,(unsigned long long)ex.estimated_work,
               (unsigned long long)ex.largest_worker_work,ex.compiled_file_bytes);
        rds_free(s); return 0;
    }
    printf("MiniSoC: %u native objects, %u residual operations, %zu payload bytes, %u workers\n",st.objects,st.operations,st.payload_bytes,st.workers);
    for(uint64_t cycle=0;cycle<limit;++cycle) {
        uint64_t status=0;
        if(rds_set_u64(s,reset,cycle<8)||rds_set_u64(s,uart,1)||rds_eval(s)||rds_get_u64(s,exit_port,&status)) goto fail;
        if(status) { printf("PASS: MiniSoC executed RV64I and coherent mailbox read returned 1 at cycle %llu (%llu polls), host trace %016llx\n",(unsigned long long)cycle,(unsigned long long)host.polls,(unsigned long long)host.trace); rds_free(s); return status==1?0:1; }
        if(rds_advance(s)) goto fail;
    }
    fprintf(stderr,"MiniSoC timeout: loader stage %u, word %u, polls %llu\n",host.stage,host.index,(unsigned long long)host.polls);
    rds_free(s); return 1;
fail:
    fprintf(stderr,"%s\n",rds_error(s)); rds_free(s); return 1;
}
