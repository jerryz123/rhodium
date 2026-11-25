// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <cstdio>

int main() {
  return run_test([] {
    eval();
    unsigned checked = 0, footprints = 0, special = 0, memory = 0;
    unsigned compute_examples=0, scalar_move_examples=0;
    const unsigned reg_bytes=register_bytes;
    for (unsigned i=0; i<count; ++i) {
      vs1=8; vs2=16; vd=24; scalar=0;
      ports::index=i;
      for(unsigned sew=0;sew<4;++sew) for(unsigned mask=0;mask<2;++mask)
      for(unsigned cert=0;cert<2;++cert) for(unsigned state=0;state<3;++state) {
        vtype=sew<<3; vl=state==0?0:4; start=state==2?4:0;
        masked=mask; fast_proof=certified=cert; eval();
        if (!legal) continue;
        ++checked;
        if (!routed) std::fprintf(stderr,"unrouted 0x%08x sew=%u vl=%u vstart=%u\n",
                                   unsigned(instruction),8u<<sew,unsigned(vl),unsigned(start));
        CHECK(routed);
        if(state==1) CHECK(!empty);
      }
      // VS2=v16, VS1=v8, VD=v24. Check whole-register interlocks before
      // row cursors exist; never round both sources out to the enclosing m8.
      const unsigned fn=instruction>>26, form=(instruction>>12)&7, operand=(instruction>>15)&31;
      const unsigned opcode=instruction&127;
      if(opcode==0x07 || opcode==0x27) {
        const auto ins=unsigned(instruction);
        auto expect=[&](unsigned reads,unsigned writes) {
          eval(); CHECK(legal && read_claims==reads && write_claims==writes); ++memory;
        };
        // Inline ei8 requires disjoint groups and a WB proof, not merely a retained window.
        if(((ins>>26)==1 || (ins>>26)==3) && form==0) {
          vtype=0; start=0; vl=4; vs2=8; vd=16; masked=1;
          for(unsigned window:{0u,1u}) for(unsigned proof:{0u,1u}) {
            certified=window; fast_proof=proof; eval(); CHECK(legal && direct==(window && proof));
          }
          vs2=vd; eval(); CHECK(!direct);
        }
        // vluxei16.v: restarted e32,m4 suffix. Index EEW is 16, not SEW.
        if(opcode==0x07 && (ins>>26)==1 && form==5) {
          vs2=8; vd=16; vtype=18; start=reg_bytes/2; vl=reg_bytes; masked=1;
          expect(1u|(1u<<9),3u<<18);
          start=vl; expect(0,0);
        }
        // vsseg2e16.v: one element in each m2 field, not four adjacent registers.
        if(opcode==0x27 && (ins>>26)==8 && form==5 && ((ins>>20)&31)==0) {
          vd=16; vtype=9; start=0; vl=1; masked=1;
          expect(1u|(1u<<16)|(1u<<18),0);
        }
        // Packed mask and whole-register transfers use their encoded effective length.
        if(opcode==0x07 && (ins>>26)==0 && form==0 && ((ins>>20)&31)==11) {
          vd=16; vtype=3; vl=reg_bytes*8; start=reg_bytes-1; masked=0;
          expect(0,1u<<16);
        }
        if(opcode==0x07 && (ins>>26)==8 && form==5 && ((ins>>20)&31)==8) {
          vd=16; vtype=0; vl=0; start=reg_bytes/2; masked=0;
          expect(0,1u<<17);
        }
        continue;
      }
      if(opcode!=0x57) continue;
      const bool rv64=reg_bytes==32;
      auto live=[&](unsigned sew) {
        vtype=sew<<3; masked=0; start=0; vl=4; eval(); CHECK(legal && !empty && routed);
      };
      if(fn==0x25 && form==2) { // vmul.vv
        live(1); CHECK(!fold && product);
        live(rv64?3:2); CHECK(!fold && shared_product); ++compute_examples;
      }
      if((fn==0 || fn==3) && form==1) { // vfadd.vv / vfredosum.vs
        live(2); CHECK(local_fp && fold==(fn==3) && reduction_seed==(fn==3));
        start=1; eval(); CHECK(legal==(fn==0));
        if(rv64) {live(3); CHECK(pipelined_fp && fold==(fn==3) && reduction_seed==(fn==3));}
        ++compute_examples;
      }
      if(form==1 && (fn==0x20 || (fn==0x13 && operand==0))) { // vfdiv.vv / vfsqrt.v
        for(unsigned sew=1;sew<=(rv64?3u:2u);++sew) {live(sew); CHECK(!fold && iterative_fp);}
        ++compute_examples;
      }
      if(fn==0x33 && form==1) { // vfwredosum.vs routes by accumulator width, not source SEW.
        live(1); CHECK(fold && reduction_seed && local_fp);
        if(rv64) {live(2); CHECK(fold && reduction_seed && pipelined_fp);}
        ++compute_examples;
      }
      if(fn==0x0c && form==0) { // vrgather.vv
        live(0); CHECK(!fold && permutation && data_unit); ++compute_examples;
      }
      if(fn==0 && form==0) { // Empty vadd: dispatch closes the owner without launching.
        live(0); CHECK(!fold && !permutation && !reduction_seed && data_unit);
        start=vl; eval(); CHECK(legal && empty);
        start=vl=0; eval(); CHECK(legal && empty); ++compute_examples;
      }
      if(fn==16 && form==2 && operand==16) { // Empty vcpop.m still owes a scalar zero.
        live(0); vl=0; eval(); CHECK(legal && !empty && routed && fold && data_unit && !reduction_seed);
        ++compute_examples;
      }
      auto claim = [&](unsigned reads, unsigned writes) {
        eval(); CHECK(legal);
        if(read_claims!=reads || write_claims!=writes)
          std::fprintf(stderr,"0x%08x vl=%u claims=%08x/%08x expected=%08x/%08x\n",
                       unsigned(instruction),unsigned(vl),unsigned(read_claims),unsigned(write_claims),reads,writes);
        CHECK(read_claims==reads && write_claims==writes); ++special;
      };
      // e32,m4: data occupies v16..19, but EI16 indices occupy only v8..9.
      // One active index may still reference the last data register.
      start=0; vl=reg_bytes; vtype=18; masked=0;
      const bool extract=fn==16 && (form==1 || form==2) && operand==0;
      const bool insert=fn==16 && (form==5 || form==6) && ((instruction>>20)&31)==0;
      if(extract || insert) {
        vs2=extract ? 9 : 0; vd=extract ? 5 : 9; vs1=insert ? 7 : 0;
        for(unsigned lmul:{2u,3u}) for(unsigned length:{0u,4u}) {
          vtype=16+lmul; vl=length; start=0; eval();
          CHECK(legal && read_claims==(extract ? 1u<<9 : 0));
          CHECK(write_claims==(insert && length ? 1u<<9 : 0) && empty==(insert && !length));
          start=length+1; eval();
          CHECK(legal && empty==insert && read_claims==(extract ? 1u<<9 : 0) && !write_claims);
        }
        ++scalar_move_examples;
        continue;
      }
      if(fn==39 && form==3) {
        const unsigned n=operand+1;
        vs2=8; vd=16; vl=0; vtype=10; // e16,m4; effective length ignores VL and LMUL.
        unsigned group=(1u<<n)-1;
        claim(group<<8,group<<16);
        start=(n-1)*reg_bytes/2+1; claim(1u<<(8+n-1),1u<<(16+n-1));
        if(n==4) { start=reg_bytes; claim(3u<<10,3u<<18); }
        continue;
      }
      bool scalar_scan=fn==16 && form==2 && (operand==16 || operand==17);
      if(scalar_scan || (fn==20 && form==2 && (operand==1 || operand==2 || operand==3 || operand==16 || operand==17))) {
        bool element=!scalar_scan && operand>=16, vid=element && operand==17;
        vs2=9; vd=16; vtype=3; masked=1; vl=reg_bytes+1;
        claim(1u|(vid?0u:1u<<9),scalar_scan?0u:(element?3u:1u)<<16);
        vl=0; claim(0,0);
        start=1; vl=4; eval(); CHECK(legal==vid);
        if(vid) { masked=0; start=reg_bytes+1; vl=2*reg_bytes+1; claim(0,3u<<17); }
        continue;
      }
      if(fn==0x0e && form==0) {
        claim((15u<<16)|(3u<<8),15u<<24);
        masked=1; vl=1; claim((15u<<16)|(1u<<8)|1,15u<<24);
        vtype=2; vl=reg_bytes*4; claim((15u<<16)|(255u<<8)|1,15u<<24); // e8: indices are m8
      }
      if((fn==0x31 && form==0) || (fn==1 && form==1)) {
        vs1=9; vtype=2+(form==1?16:0); vl=reg_bytes*4/(form==1?4:1);
        claim((15u<<16)|(1u<<9),1u<<24);
        masked=1; vl=1; claim((1u<<16)|(1u<<9)|1,1u<<24);
      }
      if(fn==0x17 && form==2) {
        vs1=9; claim((15u<<16)|(1u<<9),15u<<24);
      }
      if(fn==0x0c && form==4) {
        scalar=reg_bytes-1; claim(1u<<19,15u<<24);
        scalar=reg_bytes; claim(0,15u<<24);
      }
      if(fn==0x0f && form==4) {
        vl=2; scalar=reg_bytes/4-1; claim(3u<<16,15u<<24); // slide across v16/v17
        scalar=reg_bytes; claim(0,15u<<24);
      }
      bool wide=fn==0x35 && form==2, mac=fn==61 && form==2, narrow=fn==0x2c && form==0;
      bool extend=fn==0x12 && form==2 && operand==4;
      if(!(wide || mac || narrow || extend)) continue;
      for(unsigned group:{1u,7u}) for(unsigned pred:{0u,1u}) {
        // m2 or mf2. VF4 at e32,m2 uses just one register for its mf2 VS2.
        if(extend && group==7) continue;
        vtype=group+(extend?16:0); start=0; vl=1; masked=pred; eval(); CHECK(legal);
        unsigned normal=group==1?3:1, twice=group==1?15:1;
        unsigned left=extend?1:(wide || narrow)?twice:normal;
        unsigned dest=wide || mac?twice:normal;
        unsigned expected=(left<<16) | (extend?0:normal<<8) | (mac?dest<<24:0) | pred;
        CHECK(read_claims==expected && write_claims==(dest<<24)); ++footprints;
      }
    }
    CHECK(checked>count && compute_examples==9 && scalar_move_examples==4 && footprints==14 && special==36 && memory==5);
  });
}
