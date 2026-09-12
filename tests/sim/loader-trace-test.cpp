// Checks exact eight-byte-slot trace hashing across loader handshakes, resets and wide callback inputs.
#include "../../sims/native/mini-loader.h"
#include <cstdio>
#include <initializer_list>

static void check(bool condition) { if(!condition) std::abort(); }
static uint64_t next_random(uint64_t &state) {
    state^=state<<13; state^=state>>7; state^=state<<17; return state;
}
static uint64_t reference_word(uint64_t trace,uint64_t value) {
    for(unsigned i=0;i<8;++i) { trace^=value&255; trace*=UINT64_C(1099511628211); value>>=8; }
    return trace;
}
int main() {
    uint64_t random=UINT64_C(0x7209338ebfc971d5);
    for(unsigned loops:{0u,1u,256u,1024u,2047u}) {
        loader host={}; host.loop_iterations=loops;
        check(!setenv("RDS_LOOP_ITERATIONS","256",1)); loader configured={};
        check(!loader_configure(&configured)&&configured.loop_iterations==256);
        for(unsigned i=0;i<50000;++i) {
            uint64_t in[5]={i%137==0, next_random(random)&1, next_random(random)&1,
                static_cast<uint32_t>(next_random(random)), next_random(random)&1}, out[8];
            if(i%3==0)in[3]=1;
            if(i%5==0)in[3]=0;
            if(i%11==0) { in[1]=next_random(random);in[2]=next_random(random);in[3]=next_random(random);in[4]=next_random(random); }
            uint64_t expected=host.trace;
            check(!tick(&host,in,5,out,8));
            if(in[0])expected=UINT64_C(14695981039346656037);
            else {
                for(uint64_t value:in)expected=reference_word(expected,value);
                for(uint64_t value:out)expected=reference_word(expected,value);
            }
            check(host.trace==expected);
        }
        uint64_t in[5]={},out[8]={}; loader before=host;
        check(tick(&host,in,4,out,8)==-1);check(!memcmp(&host,&before,sizeof host));
        check(tick(&host,in,5,out,7)==-1);check(!memcmp(&host,&before,sizeof host));
    }
    std::puts("loader trace: 250000 reset/handshake/wide-input comparisons passed");
}
