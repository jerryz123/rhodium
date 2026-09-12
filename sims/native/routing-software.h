/* Defines packed routing traces and an exact word-mask software implementation. */
#ifndef RDS_ROUTING_SOFTWARE_H
#define RDS_ROUTING_SOFTWARE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {uint64_t requests[2],valids,payloads[61],ready,reset;} routing_input;
typedef struct {uint64_t selected[31],offered,accepted;} routing_output;
#if defined(RDS_ROUTING_ROWS)
static inline uint64_t routing_slice(const uint64_t *p,unsigned bit,unsigned width){
    uint64_t x=p[bit/64]>>(bit%64);
    if(bit%64 && width>64-bit%64)x|=p[bit/64+1]<<(64-bit%64);
    return width==64?x:x&((UINT64_C(1)<<width)-1);
}
static inline void routing_insert(uint64_t *p,unsigned bit,unsigned width,uint64_t x){
    p[bit/64]|=x<<(bit%64);
    if(bit%64 && width>64-bit%64)p[bit/64+1]|=x>>(64-bit%64);
}
typedef struct {unsigned char priority[RDS_ROUTING_COLS];} routing_software_state;
static inline routing_output routing_software_step(routing_software_state *s,const routing_input *in){
    routing_output out={0};uint64_t used=0;
    if(!in->valids){
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
        for(unsigned col=0;col<RDS_ROUTING_COLS;++col){
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
            for(unsigned bit=0;bit<RDS_ROUTING_WIDTH;bit+=64){
                unsigned width=RDS_ROUTING_WIDTH-bit<64?RDS_ROUTING_WIDTH-bit:64;
                routing_insert(out.selected,col*RDS_ROUTING_WIDTH+bit,width,routing_slice(in->payloads,bit,width));
            }
        }
        if(in->reset)memset(s->priority,0,sizeof s->priority);
        return out;
    }
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
    for(unsigned col=0;col<RDS_ROUTING_COLS;++col){
        uint64_t requests=0;
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
        for(unsigned row=0;row<RDS_ROUTING_ROWS;++row)
            requests|=routing_slice(in->requests,row*RDS_ROUTING_COLS+col,1)<<row;
        uint64_t eligible=requests&in->valids&~used;
        unsigned winner=0;
        if(eligible){
            uint64_t preferred=eligible&(UINT64_MAX<<s->priority[col]);
            winner=(unsigned)__builtin_ctzll(preferred?preferred:eligible);
            used|=UINT64_C(1)<<winner;out.offered|=UINT64_C(1)<<col;
            if((in->ready>>col)&1){out.accepted|=UINT64_C(1)<<winner;
                s->priority[col]=winner+1==RDS_ROUTING_ROWS?0:winner+1;}
        }
        // GrantMerge previews input zero when no valid grant exists.
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
        for(unsigned bit=0;bit<RDS_ROUTING_WIDTH;bit+=64){
            unsigned width=RDS_ROUTING_WIDTH-bit<64?RDS_ROUTING_WIDTH-bit:64;
            routing_insert(out.selected,col*RDS_ROUTING_WIDTH+bit,width,
                routing_slice(in->payloads,winner*RDS_ROUTING_WIDTH+bit,width));
        }
    }
    if(in->reset)memset(s->priority,0,sizeof s->priority);
    return out;
}
#endif
#endif
