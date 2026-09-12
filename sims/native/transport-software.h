/* Defines routed-token traces and independent copy/handle queue implementations. */
#ifndef RDS_TRANSPORT_SOFTWARE_H
#define RDS_TRANSPORT_SOFTWARE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#ifdef RDS_TRANSPORT_TYPED_INPUTS
typedef struct {bool reset;uint8_t pad0[7];bool push0;uint8_t pad1[7];bool push1;uint8_t pad2[7];bool pop0;uint8_t pad3[7];bool pop1;uint8_t pad4[7];uint64_t data0[16],data1[16];} transport_input;
#else
typedef struct {uint64_t reset,push0,push1,pop0,pop1,data0[16],data1[16];} transport_input;
#endif
typedef struct {uint64_t valid,ready,route,result0[16],result1[16];} transport_output;
#ifdef TRANSPORT_WIDTH
#define TRANSPORT_WORDS ((TRANSPORT_WIDTH+63)/64)
#define TRANSPORT_BODY (TRANSPORT_WIDTH-4)
#define TRANSPORT_SHIFT (TRANSPORT_BODY%64)
typedef struct {uint64_t payload[4][TRANSPORT_WORDS];unsigned char valid,handle[4],route[4],free_slots;} transport_state;
static inline void transport_initialize(transport_state*s){memset(s,0,sizeof(*s));for(unsigned q=0;q<4;++q)s->handle[q]=q;s->free_slots=15;}
static inline transport_output transport_step(transport_state*restrict s,const transport_input*restrict i){
    transport_output o={0};unsigned valid=s->valid;unsigned char route[4];
#pragma clang loop unroll(full)
    for(unsigned q=0;q<4;++q){
#ifdef TRANSPORT_POOL
        route[q]=s->route[q];
#else
        route[q]=s->payload[q][TRANSPORT_WORDS-1]>>TRANSPORT_SHIFT;
#endif
        o.route|=(uint64_t)route[q]<<(4*q);
    }
    o.valid=valid;o.ready=valid^15;
#pragma clang loop unroll(full)
    for(unsigned dst=0;dst<2;++dst)if(valid&(4u<<dst)){
        uint64_t*out=dst?o.result1:o.result0;
#ifdef TRANSPORT_POOL
        memcpy(out,s->payload[s->handle[dst+2]],8*TRANSPORT_WORDS);
        out[TRANSPORT_WORDS-1]|=(uint64_t)route[dst+2]<<TRANSPORT_SHIFT;
#else
        memcpy(out,s->payload[dst+2],8*TRANSPORT_WORDS);
#endif
    }
    unsigned consume=0,produce=0;
#ifdef TRANSPORT_BITMAP
    unsigned available=s->free_slots,retired=0;
    for(unsigned dst=0;dst<2;++dst)if((valid&(4u<<dst))&&(dst?i->pop1:i->pop0))retired|=1u<<s->handle[dst+2];
#endif
#pragma clang loop unroll(full)
    for(unsigned dst=0;dst<2;++dst){
        unsigned src=0;
        int offered=(valid&1)&&((route[0]&1)==dst);
        if(!offered){src=1;offered=(valid&2)&&((route[1]&1)==dst);}
        if(offered&&!(valid&(4u<<dst))){
            consume|=1u<<src;produce|=4u<<dst;
#ifdef TRANSPORT_POOL
#ifdef TRANSPORT_BITMAP
            s->handle[dst+2]=s->handle[src];s->route[dst+2]=route[src];
#else
            unsigned char free_slot=s->handle[dst+2];s->handle[dst+2]=s->handle[src];s->handle[src]=free_slot;s->route[dst+2]=route[src];
#endif
#else
            memcpy(s->payload[dst+2],s->payload[src],8*TRANSPORT_WORDS);
#endif
        }
    }
    consume|=(i->pop0?4u:0)|(i->pop1?8u:0);
#pragma clang loop unroll(full)
    for(unsigned src=0;src<2;++src)if((src?i->push1:i->push0)&&!(valid&(1u<<src))){
        const uint64_t*data=src?i->data1:i->data0;produce|=1u<<src;
#ifdef TRANSPORT_POOL
#ifdef TRANSPORT_BITMAP
        unsigned slot=__builtin_ctz(available);available&=available-1;s->handle[src]=slot;
        memcpy(s->payload[slot],data,8*TRANSPORT_WORDS);
#else
        unsigned slot=s->handle[src];memcpy(s->payload[slot],data,8*TRANSPORT_WORDS);
#endif
        s->payload[slot][TRANSPORT_WORDS-1]&=(UINT64_C(1)<<TRANSPORT_SHIFT)-1;
        s->route[src]=data[TRANSPORT_WORDS-1]>>TRANSPORT_SHIFT;
#else
        memcpy(s->payload[src],data,8*TRANSPORT_WORDS);
#endif
    }
    s->valid=(valid&~consume)|produce;
#ifdef TRANSPORT_BITMAP
    s->free_slots=available|retired;
#endif
    if(i->reset){s->valid=0;
#ifdef TRANSPORT_POOL
        for(unsigned q=0;q<4;++q)s->handle[q]=q;
#endif
#ifdef TRANSPORT_BITMAP
        s->free_slots=15;
#endif
    }
    return o;
}
#endif
#endif
