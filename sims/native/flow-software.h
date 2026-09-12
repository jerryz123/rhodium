/* Defines cycle-exact software examples and the common flow benchmark ABI. */
#ifndef RDS_FLOW_SOFTWARE_H
#define RDS_FLOW_SOFTWARE_H
#include <stdint.h>
typedef struct { uint64_t x[8]; } flow_input;
typedef struct { uint64_t x[4]; } flow_output;
typedef struct { uint64_t busy, data, data2; unsigned full, full2; } flow_state;

/* Four different pure map functions. Selection is observable; unselected
 * computations have no diagnostics or effects and need not execute. */
static inline uint64_t flow_map(unsigned select, uint64_t a, uint64_t b) {
    uint64_t x = a;
    switch (select) {
    case 0: return a + b;
    case 1:
        for (unsigned j=0;j<8;++j) x = (x ^ (x >> 13)) * (UINT64_C(0x9e3779b185ebca87) + 2*j) + b;
        return x;
    case 2:
        for (unsigned j=0;j<8;++j) x = ((x << 7) | (x >> 57)) + b + j;
        return x;
    default:
        for (unsigned j=0;j<8;++j) x = (x + b) ^ (x >> (j+1));
        return x;
    }
}

static inline flow_output flow_scoreboard(flow_state *s, const flow_input *i) {
    uint64_t busy=s->busy;
    flow_output o={{busy,(busy>>i->x[3])&1,(busy>>i->x[4])&1,busy==0}};
    /* Rhodium Scoreboard exposes old state and gives clear priority. */
    uint64_t set=i->x[1]?(UINT64_C(1)<<i->x[3]):0;
    uint64_t clear=i->x[2]?(UINT64_C(1)<<i->x[4]):0;
    s->busy=i->x[0]?0:(busy|set)&~clear;
    return o;
}
static inline flow_output flow_mux(flow_state *s, const flow_input *i) {
    (void)s;
    flow_output o={{flow_map((unsigned)i->x[3],i->x[4],i->x[5]),0,0,0}};
    return o;
}
static inline flow_output flow_queue(flow_state *s, const flow_input *i) {
    /* Ordinary depth-one Queue: no same-cycle refill when initially full,
     * no empty bypass, and an invalid payload preview remains observable. */
    flow_output o={{s->data,s->full,!s->full,s->full&&i->x[2]}};
    unsigned enqueue=!s->full&&i->x[1], dequeue=s->full&&i->x[2];
    if(enqueue) s->data=flow_map((unsigned)i->x[3],i->x[4],i->x[5]);
    s->full=i->x[0]?0:(s->full+enqueue-dequeue);
    return o;
}
static inline flow_output flow_pipeline(flow_state *s, const flow_input *i) {
    unsigned en0=!s->full&&i->x[1], transfer=s->full&&!s->full2;
    unsigned de1=s->full2&&i->x[2];
    flow_output o={{s->data2,s->full2,!s->full,transfer}};
    uint64_t next0=en0?flow_map((unsigned)i->x[3],i->x[4],i->x[5]):s->data;
    uint64_t next1=transfer?s->data+i->x[5]:s->data2;
    s->data=next0;s->data2=next1;
    s->full=i->x[0]?0:s->full+en0-transfer;
    s->full2=i->x[0]?0:s->full2+transfer-de1;
    return o;
}
#endif
