// Checks sized host transactions, boot acknowledgement, reset, and response failures.
#include "../../sims/native/mini-loader.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
static void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() { try {
    unsetenv("RDS_LOOP_ITERATIONS");
    loader state{};
    require(loader_configure(&state) == 0, "loader configuration");
    std::array<uint64_t,5> in{1,0,0,0,0};
    std::array<uint64_t,7> out{};
    require(tick_current(&state,in.data(),5,out.data(),7) == 0, "reset");
    unsigned delay=0, boots=0, writes=0, polls=0;
    uint64_t response=0;
    bool boot_pending=false, running=false, complete=false;
    for (unsigned cycle=0;cycle<1000;++cycle) {
        in={0,delay==0,0,0,0};
        if (delay && --delay==0) {
            in[2]=1; in[3]=response;
            if (boot_pending) { running=true; boot_pending=false; }
        } else if (out[0] && in[1]) {
            if (out[2] == 0x1000) {
                require(out[1] && out[3]==0x80000000 && out[4]==8, "boot transaction");
                require(!running, "duplicate boot transaction");
                ++boots; boot_pending=true; response=0;
            } else {
                require(out[4]==4, "word transaction size");
                if (out[1]) {
                    require(out[3]==program[writes], "loaded instruction/data");
                    ++writes; response=0;
                } else {
                    require(running && out[2]==0x80000080, "poll before boot acknowledgement");
                    ++polls; response=1;
                }
            }
            delay=3;
        }
        require(tick_current(&state,in.data(),5,out.data(),7)==0, "host step");
        if (out[6]) { require(out[6]==1, "target status");complete=true;break; }
    }
    require(complete && boots==1 && writes==9 && polls==1, "complete coherent loader sequence");
    auto old_stage=state.stage;
    in={0,0,1,0,1};
    require(tick_current(&state,in.data(),5,out.data(),7)!=0 && state.stage==old_stage, "error response must fail before loader mutation");
    require(tick_current(&state,in.data(),4,out.data(),7)!=0, "input arity");
    require(tick_current(&state,in.data(),5,out.data(),8)!=0, "output arity");
    in={1,0,1,0,1};
    require(tick_current(&state,in.data(),5,out.data(),7)==0 && !state.bridge.boot_sent && !state.bridge.boot_request, "reset overrides stale response");
    std::puts("workload host: sized loads, acknowledged boot, polling, reset and errors passed");
} catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what());return 1; } }
