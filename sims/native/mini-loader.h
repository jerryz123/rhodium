/* Supplies the same coherent RV64I smoke loader and transaction digest to both simulators. */
#ifndef RHODIUM_MINI_LOADER_H
#define RHODIUM_MINI_LOADER_H
#include <stdint.h>
#include "workload-host.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
/* RV64I: build unsigned 0x80000000, store 1 to +0x80, then loop.
 * No simulator backdoor writes or substituted CPU are involved. */
static const uint32_t program[]={0x80000337,0x02031313,0x02035313,0x08030313,0x00100293,0x00533023,0x0000006f,0,0};
/* The optional loop exercises sustained target execution before the mailbox
 * store. x7 counts down; the bne at PC+24 returns to the addi at PC+20. */
static const uint32_t loop_program[]={0x80000337,0x02031313,0x02035313,0x08030313,
                                     0,0xfff38393,0xfe039ee3,0x00100293,0x00533023,0x0000006f,0,0};
typedef struct { unsigned stage, index, loop_iterations; int request, start; uint64_t polls, trace; rds_workload_host bridge; } loader;
static int loader_configure(loader *l) {
    const char *text=getenv("RDS_LOOP_ITERATIONS");
    if(!text) return 0;
    char *end; unsigned long count=strtoul(text,&end,10);
    if(!*text||*end||count>2047) return -1;
    l->loop_iterations=(unsigned)count; return 0;
}
enum { LOAD, DRAIN, START, POLL, RESPONSE, DONE };
/* Hash the original little-endian eight-byte slots, including zero extension.
 * Consecutive zero bytes multiply by a power of the FNV prime exactly modulo
 * 2^64; the trace format and its serial ordering are unchanged. */
static uint64_t loader_trace_u64(uint64_t trace,uint64_t value) {
    for(unsigned byte=0;byte<8;++byte) { trace^=(value>>(8*byte))&255; trace*=UINT64_C(1099511628211); }
    return trace;
}
static uint64_t loader_trace_u8(uint64_t trace,uint8_t value) {
    return (trace^value)*UINT64_C(0x1efac7090aef4a21);
}
static uint64_t loader_trace_u32(uint64_t trace,uint32_t value) {
    for(unsigned byte=0;byte<3;++byte) { trace^=(value>>(8*byte))&255; trace*=UINT64_C(1099511628211); }
    return (trace^(value>>24))*UINT64_C(0x0caee32a7d4f6a63);
}
static int tick(void *context,const uint64_t *in,size_t ni,uint64_t *out,size_t no) {
    loader *l=(loader *)context;
    if(ni!=5||no!=8) return -1;
    memset(out,0,no*sizeof *out);
    if(in[0]) {
        unsigned loops=l->loop_iterations; memset(l,0,sizeof *l); l->loop_iterations=loops;
        l->trace=UINT64_C(14695981039346656037); return 0;
    }
    unsigned code_words=l->loop_iterations?10:7;
    unsigned prior_stage=l->stage;
    if(l->request&&in[1]) {
        if(l->stage==LOAD) { if(++l->index==code_words+2) l->stage=DRAIN; }
        else if(l->stage==POLL) l->stage=RESPONSE;
    }
    if(prior_stage==DRAIN&&in[1]) l->stage=START;
    if(l->stage==START&&l->start&&in[4]) l->stage=POLL;
    if(l->stage==RESPONSE&&in[2]) { ++l->polls; l->stage=in[3]==1?DONE:POLL; }
    out[2]=UINT64_C(0x80000080); /* Keep address decoding valid while draining. */
    out[4]=1;
    if(l->stage==LOAD) {
        out[0]=out[1]=1;
        out[2]=UINT64_C(0x80000000)+(l->index<code_words?4*l->index:0x80+4*(l->index-code_words));
        out[3]=l->loop_iterations?loop_program[l->index]:program[l->index];
        if(l->loop_iterations&&l->index==4) out[3]=(l->loop_iterations<<20)|0x393;
    } else if(l->stage==START) { out[5]=1; out[6]=UINT64_C(0x80000000); }
    else if(l->stage==POLL) { out[0]=1; out[2]=UINT64_C(0x80000080); }
    else if(l->stage==DONE) out[7]=1;
    l->request=(int)out[0]; l->start=(int)out[5];
    uint64_t trace=l->trace;
    /* DirectMemoryHTIF supplies three Boolean inputs and a 32-bit response.
     * Preserve full-slot hashing for wider callback inputs as well. */
    if((in[1]|in[2]|in[4])<=1 && in[3]<=UINT32_MAX) {
        trace=loader_trace_u8(trace,0); /* Non-reset ticks have in[0] == 0. */
        trace=loader_trace_u8(trace,(uint8_t)in[1]);
        trace=loader_trace_u8(trace,(uint8_t)in[2]);
        trace=loader_trace_u32(trace,(uint32_t)in[3]);
        trace=loader_trace_u8(trace,(uint8_t)in[4]);
    } else {
        for(size_t i=0;i<ni;++i) trace=loader_trace_u64(trace,in[i]);
    }
    /* These output widths follow the assignments above, not input promises. */
    trace=loader_trace_u8(trace,(uint8_t)out[0]);
    trace=loader_trace_u8(trace,(uint8_t)out[1]);
    trace=loader_trace_u64(trace,out[2]);
    trace=loader_trace_u32(trace,(uint32_t)out[3]);
    trace=loader_trace_u8(trace,(uint8_t)out[4]);
    trace=loader_trace_u8(trace,(uint8_t)out[5]);
    trace=loader_trace_u32(trace,(uint32_t)out[6]);
    trace=loader_trace_u8(trace,(uint8_t)out[7]);
    l->trace=trace;
    return 0;
}
static inline int workload_tick(void *p,const uint64_t *in,size_t ni,uint64_t *out,size_t no) { return tick((loader *)p,in,ni,out,no); }
static inline int tick_current(void *p,const uint64_t *in,size_t ni,uint64_t *out,size_t no) { loader *l=(loader *)p; return rds_workload_host_tick(&l->bridge,workload_tick,p,in,ni,out,no); }
#endif
