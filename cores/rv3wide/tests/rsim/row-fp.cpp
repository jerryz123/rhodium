// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <vector>

using Word = std::uint64_t;
unsigned cycle;
bool measuring, checking_double;
unsigned double_grants;
std::vector<unsigned> shared_lane_cycles, scalar_div_cycles;
extern "C" void observe_scalar_variable(Word resetting, Word valid, Word ready) {
  if (!resetting && measuring && valid && ready) scalar_div_cycles.push_back(cycle);
}
Word pc = 0x8000, retired, replay;
Word pair_pc;
unsigned pair_retire[2], pair_replays, pair_products;
bool pair_in_mem;
std::array<Word,32> regs{};
std::vector<unsigned> launches, reads, writes;
extern "C" void observe_fp(Word resetting, Word scalar, Word shared_vector, Word ready) {
  if (!resetting && measuring) {
    if (checking_double) {
      if (shared_vector && ready) { ++double_grants; shared_lane_cycles.push_back(cycle); }
    } else CHECK(!shared_vector);
    if (scalar) launches.push_back(cycle);
  }
}
extern "C" void observe_row(Word resetting, Word rv, Word rr, Word wv) {
  if (!resetting && measuring) {
    if (rv && rr) reads.push_back(cycle);
    if (wv) writes.push_back(cycle);
  }
}
void step() {
  CHECK(++cycle < 8000);
  memory_in.pdrained = 1;
  eval();
  if (!reset) {
    if (pair_pc && memory_stage_0_out.pvalid && memory_stage_0_out.pbits.ppc==pair_pc &&
        memory_stage_1_out.pvalid && memory_stage_1_out.pbits.ppc==pair_pc+4) pair_in_mem=true;
    if (redirect_out.pvalid) {
      const auto& r = redirect_out.pbits;
      if (r.presolution.pdisposition == 1)
        throw std::runtime_error("trap at PC " + std::to_string(r.ppc) + " cause " + std::to_string(r.presolution.pcause));
      if (r.presolution.pdisposition == 2) {
        replay = r.ptarget;
        if (pair_pc && replay==pair_pc) ++pair_replays;
      }
    }
    for (const auto* port : {&retired_0_out,&retired_1_out,&retired_2_out}) if (port->pvalid) {
      const auto& r = port->pbits;
      retired = r.pfetched.ppc;
      if (pair_pc && (retired==pair_pc || retired==pair_pc+4)) ++pair_retire[(retired-pair_pc)/4];
      if (r.pwrite && !r.pdeferred) regs[r.prd] = r.pdata;
    }
    if (completed_out.pvalid && completed_out.pbits.pwrite) {
      regs[completed_out.pbits.prd] = completed_out.pbits.pdata;
      if (pair_pc && completed_out.pbits.prd==6) ++pair_products;
    }
  }
  tick_model();
}
void send(unsigned insn) {
  const auto here = pc; pc += 4;
  instructions_in = {};
  instructions_in.pvalid = instructions_in.pbits.pcount = 1;
  auto& i = instructions_in.pbits.pentries[0];
  i.ppc = here; i.psequential_upc = pc; i.pinstruction = i.praw_uinstruction = insn;
  while (retired != here) {
    eval(); bool accepted = instructions_in.pvalid && instructions_out.pready;
    step();
    if (accepted) instructions_in.pvalid = 0;
    if (replay == here) { instructions_in.pvalid = 1; replay = 0; }
  }
}
unsigned op(unsigned fn, unsigned vm, unsigned vs2, unsigned s1, unsigned form, unsigned vd) {
  return fn<<26 | vm<<25 | vs2<<20 | s1<<15 | form<<12 | vd<<7 | 0x57;
}
// A vector-to-GPR move retires before its architectural writeback.
void extract(unsigned vs, Word expected) {
  send(op(16,1,vs,0,2,10));
  for(unsigned i=0;i<100 && regs[10]!=expected;++i) step();
  CHECK(regs[10]==expected);
}
// One active element makes every retired opcode require exactly one VXSeq read.
void check_routes(unsigned sew, bool mixed=false) {
  send(0xc0007057 | (sew*8)<<20 | 1u<<15);
  for(unsigned i=0;i<8;++i) step();
  std::vector<unsigned> forms;
  if (mixed) {
    for(unsigned form:{1u,5u})
      for(unsigned fn:{0x30,0x32,0x34,0x36,0x38,0x3c,0x3d,0x3e,0x3f})
        forms.push_back(op(fn,1,8,2,form,12));
    for(unsigned sel:{8,9,10,11,12,14,15,16,17,18,19,20,21,22,23})
      forms.push_back(op(0x12,1,8,sel,1,12));
  } else {
    for(unsigned form:{1u,5u})
      for(unsigned fn:{0,2,0x24,8,9,10,4,6,0x18,0x1c,0x1b,0x19,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f})
        forms.push_back(op(fn,1,8,1,form,12));
    for(unsigned fn:{0x27,0x1d,0x1f}) forms.push_back(op(fn,1,8,1,5,12));
    for(unsigned sel:{4,5,16}) forms.push_back(op(0x13,1,8,sel,1,12));
    for(unsigned sel:{0,1,2,3,6,7}) forms.push_back(op(0x12,1,8,sel,1,12));
  }
  CHECK(forms.size()==(mixed?33:52));
  checking_double=(sew+mixed)==3;
  for(unsigned insn:forms) {
    reads.clear(); writes.clear(); double_grants=0; measuring=true;
    send(insn); for(unsigned i=0;i<14;++i) step();
    measuring=false;
    CHECK(reads.size()==1 && writes.size()==unsigned(!checking_double));
    CHECK(double_grants==unsigned(checking_double));
  }
}
int main() {
  return run_test([] {
    reset = 1; step(); step(); reset = 0;
    send(0x60b7); send(0x60008093); send(0x30009073); // FS/VS
    send(0xc0007057 | 19u<<20 | 31u<<15); // e32,m8; 31 elements span eight rows
    send(0x7f800137); send(0x00110113); // x2 = signaling NaN
    send(0x3f8001b7); send(0xf00180d3); // f1 = 1.0
    send(op(23,1,0,0,3,8));  // clear source, then install sNaN in element zero
    send(op(16,1,0,2,6,8));  // vmv.s.x v8,x2
    send(op(23,1,0,30,3,0)); // v0 bit zero clear, bits 1..30 set
    for (unsigned n=0;n<80;++n) step();
    measuring = true;
    send(op(0,0,8,8,1,24)); // vfadd.vv v24,v8,v8,v0.t
    while (reads.empty()) step();
    send(0x00108153); // independent fadd.s f2,f1,f1
    while (writes.size()<8) step();
    CHECK(reads.size()==8 && launches.size()==1);
    CHECK(reads.front() <= launches.front() && launches.front() < writes.back());
    measuring = false;
    send(0xe0010253); // fmv.x.w x4,f2
    send(0x001022f3); // csrr x5,fflags
    CHECK(regs[4]==0x40000000 && regs[5]==0);
    send(op(0,1,8,8,1,24)); // positive control: unmask the same sNaN
    for (unsigned n=0;n<40;++n) step();
    send(0x001022f3); CHECK(regs[5]==16);

    // A scalar multiply can follow a vector admitted on real credit. Repeat
    // with the sole credit occupied: neither member may retire twice on replay.
    send(0x00300393); // addi x7,x0,3
    send(0x04000413); // x8 = 64 elements, long enough to fill the credit before the pair
    send((19u<<20)|(8u<<15)|0x7057); // vsetvli x0,x8,e32,m8
    for (bool full : {false,true}) {
      send(op(0,1,8,8,0,24)); // vadd.vv occupies execute
      if (full) send(op(0,1,8,8,0,24)); // second descriptor waits in admission
      pair_pc=pc; pc+=8;
      pair_in_mem=false;
      pair_retire[0]=pair_retire[1]=pair_products=pair_replays=0;
      instructions_in={};
      instructions_in.pvalid=1; instructions_in.pbits.pcount=2;
      for (unsigned n=0;n<2;++n) {
        auto& i=instructions_in.pbits.pentries[n];
        i.ppc=pair_pc+4*n; i.psequential_upc=i.ppc+4;
        i.pinstruction=i.praw_uinstruction=n ? 0x02738333 : op(0,1,8,8,1,16);
      }
      while (!pair_retire[1] || !pair_products) {
        eval(); bool accepted=instructions_in.pvalid && instructions_out.pready;
        step();
        if (accepted) instructions_in.pvalid=0;
        if (replay==pair_pc) { instructions_in.pvalid=1; replay=0; }
      }
      for (unsigned n=0;n<48;++n) step();
      CHECK(pair_in_mem && bool(pair_replays)==full);
      CHECK(pair_retire[0]==1 && pair_retire[1]==1 && pair_products==1 && regs[6]==9);
      pair_pc=0;
    }
    send(0xc0007057 | 19u<<20 | 31u<<15); // restore e32,m8, VL=31

    // Compare writes v0 in fragments; a dependent predicated add must see the
    // finished mask. Only element zero (the sNaN) compares false.
    send(op(24,1,8,8,1,0));    // vmfeq.vv v0,v8,v8
    send(op(23,1,0,3,3,24));   // initialize all destination elements to 3
    send(op(0,0,8,8,1,24));    // masked vfadd preserves v24[0]
    extract(24,3);
    send(op(12,1,24,1,3,16));  // vrgather.vi v16,v24,1
    extract(16,0);

    // Both requesters use the physical double pipe. The vector operation owns
    // one request per active element but must not consume a fixed row booking.
    send(0xc0007057 | 24u<<20 | 4u<<15);  // e64,m1, vl=4
    send(0x00300113);                      // x2=3
    send((0x69u<<25)|(2u<<15)|(1u<<7)|0x53); // fcvt.d.w f1,x2
    send(op(23,1,0,1,5,8));                // vfmv.v.f v8,f1
    for(unsigned i=0;i<16;++i) step();
    checking_double=measuring=true;
    reads.clear(); writes.clear(); launches.clear();
    send(op(0,1,8,1,5,9));                 // vfadd.vf v9,v8,f1
    send((1u<<25)|(1u<<20)|(1u<<15)|(2u<<7)|0x53); // fadd.d f2,f1,f1
    for(unsigned i=0;i<32;++i) step();
    measuring=false;
    CHECK(reads.size()==2 && writes.empty() && double_grants==4 && launches.size()==1);
    extract(9,UINT64_C(0x4018000000000000));
    send(0xe20105d3); for(unsigned i=0;i<12;++i) step();
    CHECK(regs[11]==UINT64_C(0x4018000000000000)); // fmv.x.d x11,f2
    // The same back-to-back conversion pair runs through the real shared FP64 pipe.
    send(0x3ff80137); send(0x02011113); // double 1.5 in x2
    send(op(23,1,0,2,4,8));             // vmv.v.x v8,x2
    send(0x0021d073);                    // csrwi frm,3 (RUP)
    send(0x00101073);                    // clear fflags
    send(op(0x12,1,8,1,1,12));           // vfcvt.x.f.v: 1.5 -> 2
    send(op(0x12,1,8,7,1,16));           // vfcvt.rtz.x.f.v: 1.5 -> 1
    extract(12,2); extract(16,1);
    send(0x001022f3); CHECK(regs[5]==1); // NX
    send(0x00205073);                    // restore RNE
    // Distinct vs2=1.5, scalar rhs=3.0, old vd=2.0 distinguish the fused forms.
    send(0x40000137); send(0x02011113);
    send(op(23,1,0,2,4,16)); send(op(23,1,0,2,4,20));
    send(op(0x28,1,8,1,5,16)); // vfmadd.vf: vd * rhs + vs2
    send(op(0x2c,1,8,1,5,20)); // vfmacc.vf: vs2 * rhs + vd
    extract(16,UINT64_C(0x401e000000000000)); // 7.5
    extract(20,UINT64_C(0x401a000000000000)); // 6.5
    // Four singles span both halves of the source row. Chain through shared
    // widening conversion, wide-VS2 addition, and narrowing without fences.
    send(0xc0007057 | 16u<<20 | 4u<<15); // e32,m1
    send(0x40000137); // x2 = 2.0f
    send(op(23,1,0,2,4,8));               // vmv.v.x v8,x2
    send(op(0x12,1,8,12,1,12));           // vfwcvt.f.f.v v12,v8
    send(op(0x34,1,12,8,1,16));           // vfwadd.wv v16,v12,v8
    send(op(0x12,1,16,21,1,20));           // vfncvt.rod.f.f.w v20,v16
    send(op(12,1,20,3,3,24));             // get the upper-half result
    extract(24,0x40800000);
    // These byte↔half conversions are legal with Zvfh. The final
    // conversion must saturate to a byte, not truncate a halfword.
    send(0xc0007057 | 1u<<15);            // e8,m1, vl=1
    send(op(23,1,0,31,3,8));              // v8[0] = 0xff
    send(op(0x12,1,8,10,1,12));           // uint8 255 -> FP16
    send(0x00101073);                     // clear fflags
    send(op(0x12,1,12,17,1,16));           // FP16 -> int8, clips to 127
    extract(16,127);
    send(0x001022f3); CHECK(regs[5]==16);  // invalid, not inexact
    // Check real decoder, seed read, and tagged double service. v0 enables
    // elements 0 and 2. Keep the seed in vd to exercise legal aliasing.
    for(unsigned fn:{1,3,5,7,0x31,0x33}) for(unsigned dest_sew:{2,3}) {
      const bool wide=fn>=0x30, dbl=dest_sew==3;
      const unsigned source_sew=dest_sew-wide;
      auto config=[](unsigned sew){send(0xc0007057|(sew*8)<<20|4u<<15);};
      config(dest_sew); send(op(23,1,0,0,3,12)); // +0 seed
      config(source_sew);
      if(source_sew==1) { send(0x00004137); send(0xc0010113); } // x2=half 1.0 (0x3c00)
      else if(source_sew==2) send(0x3f800137); // single 1.0
      else { send(0x3ff00137); send(0x02011113); } // double 1.0
      send(op(23,1,0,2,4,8)); send(op(23,1,0,5,3,0));
      for(unsigned i=0;i<24;++i) step();
      checking_double=dbl; measuring=true; reads.clear(); writes.clear(); double_grants=0;
      send(op(fn,0,8,12,1,12));
      for(unsigned i=0;i<48;++i) step();
      measuring=false;
      CHECK(double_grants==(dbl?2:0) && writes.size()==1);
      config(dest_sew);
      Word expected=fn==5?0:fn==7?(dbl?UINT64_C(0x3ff0000000000000):0x3f800000):(dbl?UINT64_C(0x4000000000000000):0x40000000);
      extract(12,expected);
    }
    // Exercise the real iterative unit and scalar/vector arbiter. Five of
    // six FP32 lanes are active; the masked lane must generate no request.
    send(0xc0007057 | 16u<<20 | 6u<<15);
    send(0x40800137); send(op(23,1,0,2,4,8)); // v8 = 4.0
    send(0x40000137); send(0xf00100d3);       // f1 = 2.0
    send(0x03d00113); send(op(16,1,0,2,6,0)); // v0 = 0b111101
    for(unsigned i=0;i<12;++i) step(); // drain native mask insertion before counting divide rows
    for(unsigned fn:{0u,1u,2u,3u}) {
      checking_double=measuring=true; reads.clear(); writes.clear(); shared_lane_cycles.clear(); scalar_div_cycles.clear();
      send(fn==0?op(0x20,0,8,8,1,9):fn==1?op(0x20,0,8,1,5,9):
           fn==2?op(0x21,0,8,1,5,9):op(0x13,0,8,0,1,9));
      if (!fn) {
        while(shared_lane_cycles.empty()) step();
        send((0x0cu<<25)|(1u<<20)|(1u<<15)|(2u<<7)|0x53); // fdiv.s f2,f1,f1
      }
      while(shared_lane_cycles.size()<5) step();
      measuring=false;
      CHECK(reads.size()==2 && writes.empty() && shared_lane_cycles.size()==5);
      if (!fn) CHECK(scalar_div_cycles.size()==1 && shared_lane_cycles.front()<scalar_div_cycles[0] &&
                     scalar_div_cycles[0]<shared_lane_cycles.back());
      extract(9,fn==0?0x3f800000:fn==2?0x3f000000:0x40000000);
    }
    for(unsigned sew:{1,2,3}) check_routes(sew);
    for(unsigned sew:{1,2}) check_routes(sew,true);
    // Use the real scalar FP64 service: compare [sNaN,0,0,1] against 1
    // under mask 1110; only lane 3 may enable the dependent classification.
    send(0xc0007057 | 24u<<20 | 4u<<15); // e64,m1
    send(0x3ff00137); send(0x02011113);  // x2 = double 1.0
    send(op(23,1,0,2,4,16)); send(op(23,1,0,0,3,12));
    send(op(15,1,12,2,6,8));            // vslide1down.vx v8,v12,x2
    send(0x7ff00137); send(0x02011113); send(0x00110113);
    send(op(16,1,0,2,6,8));             // v8[0] = signaling NaN
    send(op(23,1,0,14,3,0)); send(op(23,1,0,7,3,24));
    send(0x00101073);                    // clear fflags
    send(op(0x18,0,8,16,1,0));           // vmfeq.vv v0,v8,v16,v0.t
    send(op(0x13,0,8,16,1,24));          // vfclass.v v24,v8,v0.t
    extract(0,8); extract(24,7);
    send(op(12,1,24,3,3,12)); extract(12,64); // class of positive normal
    send(0x001022f3); CHECK(regs[5]==0);
  });
}
