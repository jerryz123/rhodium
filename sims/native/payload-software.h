/* Defines copied and pooled software references for queues and fixed valid pipelines. */
#ifndef RDS_PAYLOAD_SOFTWARE_H
#define RDS_PAYLOAD_SOFTWARE_H
#include <stdint.h>
#include <string.h>
#ifdef RDS_PAYLOAD_TYPED_INPUTS
#include <stdbool.h>
#include <stddef.h>
/* Keep the trace ABI offsets while exposing verified one-bit control types. */
typedef struct {bool reset;unsigned char reset_padding[7];bool push;unsigned char push_padding[7];bool pop;unsigned char pop_padding[7];uint64_t data[16];} payload_input;
typedef char payload_input_layout_check[(sizeof(payload_input)==152&&offsetof(payload_input,push)==8&&offsetof(payload_input,pop)==16&&offsetof(payload_input,data)==24)?1:-1];
#else
typedef struct {uint64_t reset,push,pop,data[16];} payload_input;
#endif
typedef struct {uint64_t ready,valid,data[16];} payload_output;
#if defined(PAYLOAD_STAGES)
#define PAYLOAD_WORDS ((PAYLOAD_WIDTH+63)/64)
typedef struct {uint32_t full;uint8_t handle[PAYLOAD_STAGES];uint64_t data[PAYLOAD_STAGES+1][PAYLOAD_WORDS];} payload_software;
static inline payload_output payload_step(payload_software*s,const payload_input*i,int pooled){
 uint32_t old=s->full,mask=(UINT32_C(1)<<PAYLOAD_STAGES)-1;
#ifdef PAYLOAD_FIXED_PIPELINE
 uint32_t en=((old<<1)|(i->push!=0))&mask;
 payload_output out={0};out.ready=1;out.valid=(old>>(PAYLOAD_STAGES-1))&1;
#else
 uint32_t en=((old<<1)|(i->push!=0))&~old&mask;
 uint32_t de=(old&~(old>>1)&(mask>>1))|(old&(mask^(mask>>1))&(0u-(i->pop!=0)));
 payload_output out={0};out.ready=!(old&1);out.valid=(old>>(PAYLOAD_STAGES-1))&1;
#endif
 unsigned last=pooled?s->handle[PAYLOAD_STAGES-1]:PAYLOAD_STAGES-1;
 if(
#ifdef PAYLOAD_VALID_ONLY
 out.valid
#else
 1
#endif
 )memcpy(out.data,s->data[last],PAYLOAD_WORDS*8);
 unsigned slot=0;
 if(pooled&&(en&1)){uint32_t live=0;
  for(unsigned n=0;n<PAYLOAD_STAGES;++n)live|=UINT32_C(1)<<s->handle[n];
  slot=(unsigned)__builtin_ctz(~live&((UINT32_C(1)<<(PAYLOAD_STAGES+1))-1));
  memcpy(s->data[slot],i->data,PAYLOAD_WORDS*8);
 }
 for(unsigned n=PAYLOAD_STAGES;n-->1;)if(en&(UINT32_C(1)<<n)){
  if(pooled)s->handle[n]=s->handle[n-1];else memcpy(s->data[n],s->data[n-1],PAYLOAD_WORDS*8);
 }
 if(en&1){if(pooled)s->handle[0]=slot;else memcpy(s->data[0],i->data,PAYLOAD_WORDS*8);}
 s->full=i->reset?0:
#ifdef PAYLOAD_FIXED_PIPELINE
 en;
#else
 (old|en)&~de;
#endif
 return out;
}
#ifdef PAYLOAD_VALID_ONLY
/* Ordered lossless transport retains only timing bits and ingress/egress positions.
 * No intermediate stage reads or copies data; one live token owns one pool row. */
typedef struct {uint32_t full,head,tail;uint64_t data[PAYLOAD_STAGES][PAYLOAD_WORDS];} payload_ring;
static inline payload_output payload_ring_step(payload_ring*s,const payload_input*i){
 uint32_t old=s->full,mask=(UINT32_C(1)<<PAYLOAD_STAGES)-1;
#ifdef PAYLOAD_FIXED_PIPELINE
 /* Fixed latency identifies the retiring row by time: one cyclic position
  * replaces separate live-token head/tail counters. Invalid slots are unread. */
 payload_output out={0};out.ready=1;out.valid=!!(old&(UINT32_C(1)<<(PAYLOAD_STAGES-1)));
 if(out.valid)memcpy(out.data,s->data[s->head],PAYLOAD_WORDS*8);
 if(i->push)memcpy(s->data[s->head],i->data,PAYLOAD_WORDS*8);
 s->head=s->head+1==PAYLOAD_STAGES?0:s->head+1;
 s->full=i->reset?0:((old<<1)|(i->push!=0))&mask;
 if(i->reset)s->head=0;
#else
 uint32_t en=((old<<1)|(i->push!=0))&~old&mask;
 uint32_t de=(old&~(old>>1)&(mask>>1))|(old&(mask^(mask>>1))&(0u-(i->pop!=0)));
 payload_output out={0};out.ready=!(old&1);out.valid=!!(old&(UINT32_C(1)<<(PAYLOAD_STAGES-1)));
 if(out.valid)memcpy(out.data,s->data[s->head],PAYLOAD_WORDS*8);
 if(en&1){memcpy(s->data[s->tail],i->data,PAYLOAD_WORDS*8);s->tail=s->tail+1==PAYLOAD_STAGES?0:s->tail+1;}
 if(out.valid&&i->pop)s->head=s->head+1==PAYLOAD_STAGES?0:s->head+1;
 s->full=i->reset?0:(old|en)&~de;
 if(i->reset)s->head=s->tail=0;
#endif
 return out;
}
#endif
#endif
#endif
