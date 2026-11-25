// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <cstring>

using Data = decltype(result_in.pbits);
constexpr unsigned bytes = sizeof(Data);
enum Permutation { NoPermutation, SlideUp, SlideDown, SlideOneUp, SlideOneDown, GatherBroadcast, GatherVector, GatherVectorEI16, Compress };
enum Cursor { RowCursor, FoldCursor };
enum VpsUse { NoVps, PermuteVps, SeedVps };
enum DataResult { Alu, MaskBits, CopyVs2, Permuted };
enum ComputeFamily { IntegerData, IntProduct, IntDivide, FpArithmetic, FpDivSqrt, FpData };
enum class Kind { Up, Down, Broadcast, Gather, Ei16, OneUp, OneDown };

static void exercise(Kind kind, unsigned sew, unsigned distance, bool masked, unsigned first,
                     bool stall, bool inplace=false, bool immediate=false, bool fractional=false,
                     bool floating=false, bool singleton=false, bool bad_box=false, unsigned mask=0xdb) {
  reset=1; start_in={}; result_in={}; tick_model(); reset=0;
  const bool one=kind==Kind::OneUp || kind==Kind::OneDown;
  const bool up=kind==Kind::Up || kind==Kind::OneUp;
  const bool slide=one || up || kind==Kind::Down;
  const bool indexed=kind==Kind::Gather || kind==Kind::Ei16;
  const unsigned size=1u<<sew, index_size=kind==Kind::Ei16 ? 2u : size;
  const unsigned cap=(fractional ? bytes/2 : 8*bytes)/size, limit=singleton ? 1 : cap-1;
  const unsigned src=8, idx=16, dst=inplace ? src : 24;
  std::array<unsigned char,64*bytes> mem;
  for(unsigned i=0;i<mem.size();++i) mem[i]=(i*17+i/bytes+5)&255;
  std::vector<unsigned> indices;
  for(unsigned i=0;i<limit;++i) {
    unsigned v=(i%4==0 ? cap : i%4==1 ? cap-1 : i%4==2 ? 0 : cap/2);
    indices.push_back(v);
    for(unsigned j=0;j<index_size;++j) mem[2*idx*bytes+i*index_size+j]=j<4 ? v>>(8*j) : 0;
  }
  auto original=mem;
  std::memset(&predicate,mask,bytes); std::memset(&upper_predicate,mask,bytes);
  auto& d=start_in.pbits.pchecked.pdescriptor;
  d.pinstruction=(src<<20)|((immediate ? distance : idx)<<15)|(dst<<7);
  d.pstate.pvl=limit; d.pstate.pvstart=first; d.pstate.pvtype=(sew<<3)|(fractional ? 6 : 2);
  d.pscalar=distance; d.pmasked=masked;
  const std::uint64_t scalar=UINT64_C(0xf135c907ba42de13);
  const std::uint64_t low_mask=UINT64_MAX >> (64-size*8);
  if(one) d.pscalar=floating ? ~scalar : scalar;
  d.pfloating_uscalar=bad_box ? scalar & low_mask : scalar | ~low_mask;
  auto &c=d.pcontrol;
  c.pcompute_ufamily=floating ? FpData : IntegerData;
  c.pinteger=!floating; c.pfloating_upoint=floating; c.pvps=PermuteVps;
  c.ppermutation=one ? (up ? SlideOneUp : SlideOneDown) : slide ? (up ? SlideUp : SlideDown) :
                 indexed ? (kind==Kind::Ei16 ? GatherVectorEI16 : GatherVector) : GatherBroadcast;
  c.pdata_uresult=Permuted;
  c.preads.pvs2=1; c.preads.pvs1=indexed;
  c.poperand=indexed ? 0 : immediate ? 2 : floating ? 3 : 1;
  start_in.pvalid=1; eval(); CHECK(start_out.pready); tick_model(); start_in.pvalid=0;
  unsigned physical=0, writes=0, last=0, demand=0; bool returning=false,finished=false;
  const unsigned first_row=first*size/bytes, end_row=(limit*size-1)/bytes;
  unsigned next_index_row=2*idx+first*index_size/bytes;
  for(unsigned t=0;t<2000 && !finished;++t) {
    // Simulate scheduled store reads denying the shared port, including the
    // first request for a neighboring slide row. Results always return at +1.
    read_in.pready=!stall || (t>5 && t%5!=0);
    ahead_in.pready=!stall || t%3==1;
    write_available=!stall || (t%11>=4);
    older_writes=stall && t<9 ? UINT64_MAX : stall && t<17 ? UINT64_C(1)<<(2*src+1) : 0;
    result_in.pvalid=returning;
    if(returning) std::memcpy(&result_in.pbits,mem.data()+physical*bytes,bytes);
    eval();
    CHECK(!flags);
    bool rd=read_out.pvalid && read_in.pready, ahead=ahead_out.pvalid && ahead_in.pready;
    if(kind==Kind::Broadcast && distance>=cap) CHECK(!unread && !read_out.pvalid && !ahead_out.pvalid);
    if(indexed && rd && read_out.pbits>=2*idx) {
      CHECK(read_out.pbits==next_index_row);
      ++next_index_row;
    }
    CHECK(!(rd && ahead));
    if(rd || ahead) CHECK(!(older_writes & (UINT64_C(1)<<(rd ? read_out.pbits : ahead_out.pbits))));
    if(indexed && !finished && (read_out.pvalid || ahead_out.pvalid))
      CHECK((unread & (UINT64_C(1)<<(2*src)))!=0);
    // Model a younger writer immediately reclaiming every released source.
    // A repeated index must still see the original arbitrary VS2 row.
    if(stall && !inplace) for(unsigned r=2*src;r<2*dst;++r)
      if(!(unread & (UINT64_C(1)<<r))) std::memset(mem.data()+r*bytes,0xec,bytes);
    if(rd || ahead) { physical=rd ? read_out.pbits : ahead_out.pbits; demand+=rd; }
    returning=rd || ahead;
    if(write_out.pvalid) {
      const auto &w=write_out.pbits; unsigned row=first_row+writes;
      CHECK(w.paddress==2*dst+row);
      if(slide && !stall && writes) CHECK(t==last+1);
      for(unsigned b=0;b<bytes;++b) {
        unsigned pos=row*bytes+b, i=pos/size;
        bool enabled=i>=first && i<limit && (!masked || ((mask>>(i%8))&1)) &&
                     (kind!=Kind::Up || i>=distance);
        auto wm=reinterpret_cast<const unsigned char*>(&w.pmask)[b];
        CHECK(wm==(enabled?255:0));
        if(enabled) {
          const bool insert=one && i==(up ? 0 : limit-1);
          std::uint64_t source=indexed ? indices[i] : up ? i-distance :
                               slide ? std::uint64_t(i)+distance : distance;
          auto value=reinterpret_cast<const unsigned char*>(&w.pdata)[b];
          const auto inserted=bad_box ? (size==2 ? 0x7e00u : 0x7fc00000u) : scalar;
          CHECK(value==(insert ? ((inserted>>(8*(pos%size)))&255) : source<cap ? original[2*src*bytes+source*size+pos%size] : 0));
          mem[w.paddress*bytes+b]=value;
        }
      }
      ++writes; last=t;
    }
    finished=finish; CHECK(finished==(write_out.pvalid && writes==end_row-first_row+1));
    tick_model();
  }
  CHECK(finished && !unread);
  if(indexed) CHECK(next_index_row==2*idx+(limit*index_size+bytes-1)/bytes);
  if(one && singleton) CHECK(demand==0);
  if(kind==Kind::Broadcast) CHECK(demand==(distance<cap ? 1u : 0u));
}

enum class Selection { None, Dense, Sparse, Exact };

// An immediate splat owns the stalled final row. Its successor inserts an FPR
// into element zero; neither the immediate field nor the FPR names a VRF read.
static void splat_to_fp_insert(bool boxed) {
  reset=1; start_in={}; result_in={}; tick_model(); reset=0;
  write_available=0; older_writes=UINT64_MAX;
  auto& d=start_in.pbits.pchecked.pdescriptor;
  d.pinstruction=(30u<<15)|(8u<<7); // vmv.v.i v8, -2
  d.pstate.pvtype=1u<<3; d.pstate.pvl=bytes/2;
  d.pscalar=0x1357; d.pfloating_uscalar=0x2468;
  d.pcontrol.pinteger=1; d.pcontrol.poperand=2; // Immediate, Signed5
  d.pcontrol.pinteger_urow.palu.presult_uselect=5; // Select
  start_in.pbits.powner=3; start_in.pvalid=1;
  eval(); CHECK(start_out.pready); tick_model();
  d={}; d.pinstruction=(4u<<15)|(20u<<7); // vfmv.s.f v20, f4
  d.pstate.pvtype=1u<<3; d.pstate.pvl=bytes/2;
  d.pscalar=0x9876;
  d.pfloating_uscalar=boxed ? UINT64_C(0xffffffffffff4211) : 0x4211;
  d.pcontrol.pcompute_ufamily=FpData;
  d.pcontrol.pfloating_upoint=1; d.pcontrol.poperand=3; // FloatingScalar
  d.pcontrol.psequence=4; d.pcontrol.pinteger_urow.palu.presult_uselect=5;
  start_in.pbits.powner=0;
  for(unsigned t=0;t<8;++t) {
    write_available=t>=3; eval();
    CHECK(!read_src && !read_rhs && !flags);
    CHECK(bool(reserving)==(t==3 || t==4));
    if(t<=3) CHECK(bool(start_out.pready)==(t==3));
    CHECK(bool(write_out.pvalid)==(t==5 || t==6) && bool(finish)==bool(write_out.pvalid));
    if(write_out.pvalid) {
      const bool splat=t==5;
      CHECK(write_owner==(splat?3:0) && write_out.pbits.paddress==(splat?16:40));
      const unsigned element=splat ? 0xfffe : boxed ? 0x4211 : 0x7e00;
      const auto *data=reinterpret_cast<const unsigned char*>(&write_out.pbits.pdata);
      const auto *mask=reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask);
      for(unsigned b=0;b<bytes;++b) {
        CHECK(mask[b]==(splat || b<2 ? 255 : 0));
        if(mask[b]) CHECK(data[b]==((element>>(8*(b%2)))&255));
      }
    }
    tick_model();
    if(t==3) { start_in.pvalid=0; d={}; }
  }
}

// Restart a whole-register copy at its last row. A masked compare into v0
// replaces it on the read grant; each write must use its own source and mask.
static void copy_to_compare() {
  reset=1; start_in={}; result_in={}; tick_model(); reset=0;
  older_writes=0; write_available=0;
  auto &d=start_in.pbits.pchecked.pdescriptor;
  d.pinstruction=8u<<20 | 16u<<7;
  d.pcontrol.pinteger=d.pcontrol.preads.pvs2=d.pcontrol.pwhole_uregister_ucount=1;
  d.pcontrol.psequence=5; // WholeCopy
  d.pcontrol.pdata_uresult=CopyVs2;
  d.pstate.pvstart=bytes; d.pscalar=13; // Nonzero unused RHS distinguishes a raw copy from an add.
  start_in.pbits.powner=3; start_in.pvalid=1;
  eval(); CHECK(start_out.pready); tick_model();
  d={}; d.pinstruction=4u<<20 | 6u<<15; d.pstate.pvl=3; d.pmasked=1;
  d.pcontrol.pinteger=d.pcontrol.preads.pvs1=d.pcontrol.preads.pvs2=1;
  d.pcontrol.presult_ukind=1; d.pcontrol.prow_ulayout=2; // ElementsToMask
  d.pcontrol.pdata_uresult=MaskBits; d.pcontrol.pinteger_urow.palu.presult_uselect=3;
  start_in.pbits.powner=0;
  std::memset(&vs2,0x93,bytes);
  predicate={}; reinterpret_cast<unsigned char*>(&predicate)[0]=5;
  for(unsigned t=0;t<8;++t) {
    write_available=t>=3;
    if(t==4) reinterpret_cast<unsigned char*>(&vs2)[0]=0; // Only enabled element 0 equals VS1 (zero).
    eval();
    CHECK(bool(reserving)==(t==3 || t==4) && bool(read_src)==bool(reserving));
    if(t<=3) CHECK(bool(start_out.pready)==(t==3));
    CHECK(bool(write_out.pvalid)==(t==5 || t==6) && bool(finish)==bool(write_out.pvalid));
    if(write_out.pvalid) {
      CHECK(write_owner==(t==5?3:0) && write_out.pbits.paddress==(t==5?33:0));
      for(unsigned b=0;b<bytes;++b) {
        auto mask=reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask)[b];
        auto data=reinterpret_cast<const unsigned char*>(&write_out.pbits.pdata)[b];
        CHECK(mask==(t==5?255:b==0?5:0));
        CHECK((data&mask)==(t==5?0x93:b==0?1:0));
      }
    }
    tick_model(); if(t==3) start_in.pvalid=0;
  }
}

// Deny the outgoing final VS2 read, then forward its return or stall the write reservation.
// Slide-one and gathers return different data even when handing off on the same edge.
enum class Successor { Add, Slide, Gather };
static void tail_handoff(Permutation outgoing, bool stall_write, Successor next) {
  const bool gather=outgoing==GatherVector || outgoing==GatherVectorEI16;
  const bool one=outgoing==SlideOneUp || outgoing==SlideOneDown;
  const unsigned tail_source=one ? 16 : 17;
  reset=1; start_in={}; result_in={}; tick_model(); reset=0;
  ahead_in.pready=0; older_writes=0; write_available=1;
  auto &d=start_in.pbits.pchecked.pdescriptor;
  d.pinstruction=(8u<<20)|(12u<<15)|(16u<<7); d.pstate.pvl=2;
  d.pcontrol.pinteger=1; d.pcontrol.pvps=PermuteVps; d.pcontrol.ppermutation=outgoing;
  d.pcontrol.pdata_uresult=Permuted;
  d.pcontrol.preads.pvs2=1; d.pcontrol.preads.pvs1=gather;
  d.pcontrol.poperand=gather ? 0 : 1; d.pscalar=one ? 0xab : bytes-1;
  start_in.pbits.powner=3; start_in.pvalid=1; eval(); CHECK(start_out.pready); tick_model();
  d={}; d.pinstruction=(8u<<20)|(12u<<15)|(20u<<7); d.pstate.pvl=2;
  d.pcontrol.pinteger=1; d.pcontrol.preads.pvs2=1; d.pcontrol.preads.pvs1=next!=Successor::Slide;
  d.pcontrol.pvps=next==Successor::Add ? NoVps : PermuteVps;
  d.pcontrol.pdata_uresult=next==Successor::Add ? Alu : Permuted;
  std::memset(&vs2,0xc3,bytes); // The fixture ties the add's second operand to zero.
  d.pcontrol.ppermutation=next==Successor::Slide ? SlideDown : next==Successor::Gather ? GatherVector : NoPermutation;
  d.pcontrol.poperand=next==Successor::Slide ? 1 : 0; d.pscalar=bytes;
  start_in.pbits.powner=0;
  unsigned denied=0, held=0, row=0; bool returning=false, accepted=false;
  for(unsigned t=0;t<30 && !accepted;++t) {
    result_in.pvalid=returning; result_in.pbits={};
    auto *data=reinterpret_cast<unsigned char*>(&result_in.pbits);
    if(row==24) { data[0]=bytes-1; data[outgoing==GatherVectorEI16 ? 2 : 1]=bytes; }
    else std::memset(data,row,bytes);
    write_available=!stall_write || held==3;
    read_in.pready=0; eval();
    bool last_demand=read_out.pvalid && read_out.pbits==tail_source;
    if(last_demand && denied<3) ++denied;
    else read_in.pready=1;
    eval();
    accepted=start_out.pready;
    CHECK(accepted==bool(reserving) && accepted==bool(offering_row && write_available));
    if(offering_row) {
      CHECK(denied==3);
      if(!held) CHECK(returning && row==tail_source);
      if(!write_available) ++held;
    }
    returning=read_out.pvalid && read_in.pready;
    if(returning) row=read_out.pbits;
    tick_model();
  }
  CHECK(accepted && held==(stall_write ? 3u : 0u));
  start_in.pvalid=0;
  unsigned requests=0, grants=0, writes=0;
  for(unsigned t=1;t<15 && writes<2;++t) {
    result_in.pvalid=returning; result_in.pbits={};
    auto *source=reinterpret_cast<unsigned char*>(&result_in.pbits);
    if(row==24) { source[0]=bytes-1; source[1]=bytes; }
    else std::memset(source,row,bytes);
    eval();
    returning=read_out.pvalid && read_in.pready;
    if(t==1) CHECK(next==Successor::Add ? (reserving && read_src) : returning);
    if(returning) {
      row=read_out.pbits;
      const unsigned gather_reads[]={24,16,17};
      if(next==Successor::Slide) CHECK(!requests && row==17);
      else CHECK(next==Successor::Gather && requests<3 && row==gather_reads[requests]);
      ++requests;
    }
    grants+=reserving;
    CHECK(bool(write_out.pvalid)==bool(finish));
    if(finish) {
      CHECK(write_owner==(writes?0:3) && write_out.pbits.paddress==(writes?40:32));
      const auto *data=reinterpret_cast<const unsigned char*>(&write_out.pbits.pdata);
      if(!writes && one) CHECK(data[0]==(outgoing==SlideOneUp?0xab:16) && data[1]==(outgoing==SlideOneDown?0xab:16));
      else if(!writes || next!=Successor::Add) CHECK(data[0]==(writes && next==Successor::Slide ? 17 : 16) && data[1]==17);
      else CHECK(data[0]==0xc3 && data[1]==0xc3);
      for(unsigned b=0;b<bytes;++b) CHECK(reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask)[b]==(b<2?255:0));
      if(!writes || next==Successor::Add) CHECK(t==2+writes);
      ++writes;
    }
    tick_model();
  }
  CHECK(grants==1 && writes==2 && requests==(next==Successor::Add ? 0u : next==Successor::Slide ? 1u : 3u));
}

static void compress(unsigned sew, Selection selection) {
  reset=1; start_in={}; result_in={}; tick_model(); reset=0;
  // LMUL=8 at SEW=8 crosses from the first selection-mask row into the second.
  const unsigned size=1u<<sew, lanes=bytes/size, limit=16*lanes-1;
  const unsigned src=8, maskreg=16, dst=24;
  std::array<unsigned char,64*bytes> mem{};
  std::vector<unsigned char> expected;
  for(unsigned i=0;i<limit;++i) {
    const bool take=selection==Selection::Dense || (selection==Selection::Exact && i<lanes) ||
                    (selection==Selection::Sparse && (i%4==0 || i==limit-1));
    if(take) mem[2*maskreg*bytes+i/8]|=1u<<(i%8);
    for(unsigned j=0;j<size;++j) {
      auto value=static_cast<unsigned char>(i*23+j*19+7);
      mem[2*src*bytes+i*size+j]=value;
      if(take) expected.push_back(value);
    }
  }
  // Set tail bits: these must never select a source element past vl.
  mem[2*maskreg*bytes+limit/8]|=static_cast<unsigned char>(0xffu<<(limit%8));
  auto &d=start_in.pbits.pchecked.pdescriptor;
  d.pinstruction=src<<20 | maskreg<<15 | dst<<7;
  d.pcontrol.pinteger=1; d.pcontrol.preads.pvs2=d.pcontrol.preads.pvs1=1; d.pcontrol.pvps=PermuteVps; d.pcontrol.ppermutation=Compress;
  d.pcontrol.pdata_uresult=Permuted;
  d.pstate.pvl=limit; d.pstate.pvtype=(sew<<3)|3;
  start_in.pbits.powner=3; start_in.pvalid=1; eval(); CHECK(start_out.pready); tick_model();
  // Keep an independent VPS client waiting through every data row. Its
  // sources are distinct so a stale slot or stale owner is observable.
  d={}; d.pinstruction=(4u<<20)|(6u<<15)|(20u<<7); d.pstate.pvl=1;
  d.pcontrol.pinteger=1;
  d.pcontrol.pvps=PermuteVps;
  d.pcontrol.pdata_uresult=Permuted;
  d.pcontrol.ppermutation=selection==Selection::Sparse ? SlideDown : selection==Selection::None ? GatherVector : Compress;
  d.pcontrol.preads.pvs1=d.pcontrol.ppermutation!=SlideDown;
  d.pcontrol.poperand=d.pcontrol.ppermutation==SlideDown ? 1 : 0;
  start_in.pbits.powner=0;
  bool returning=false, done=false; unsigned physical=0, written=0, reservations=0, held=0;
  const unsigned rows=(expected.size()+bytes-1)/bytes;
  for(unsigned t=0;t<500 && !done;++t) {
    result_in.pvalid=returning;
    if(returning) std::memcpy(&result_in.pbits,mem.data()+physical*bytes,bytes);
    older_writes=t<4 ? ~UINT64_C(0) : 0;
    read_in.pready=t%4!=0; ahead_in.pready=1;
    // Deny every grant for an empty result. Otherwise hold the final
    // row at the actual output handshake for eight consecutive cycles.
    write_available=rows && (reservations+1!=rows || held>=8);
    eval();
    CHECK(!flags && !ahead_out.pvalid);
    if(held && held<8) CHECK(offering_row && !write_available);
    if(offering_row && !write_available) ++held;
    if(reserving) { CHECK(write_available); ++reservations; }
    returning=read_out.pvalid && read_in.pready;
    if(returning) physical=read_out.pbits;
    // A younger writer immediately reclaims any released source row.
    for(unsigned r=2*src;r<2*dst;++r)
      if(!(unread & (UINT64_C(1)<<r))) std::memset(mem.data()+r*bytes,0xbc,bytes);
    if(write_out.pvalid) {
      auto &w=write_out.pbits;
      CHECK(written<rows && w.paddress==2*dst+written);
      for(unsigned b=0;b<bytes;++b) {
        unsigned pos=written*bytes+b;
        CHECK(reinterpret_cast<const unsigned char*>(&w.pmask)[b]==(pos<expected.size()?255:0));
        if(pos<expected.size()) CHECK(reinterpret_cast<const unsigned char*>(&w.pdata)[b]==expected[pos]);
      }
      ++written;
    }
    done=finish;
    CHECK(bool(start_out.pready)==done && bool(accepting_end)==done);
    if(done) CHECK(write_owner==3 && !unread && !reserving && !write_out.pvalid && !returning);
    tick_model();
  }
  CHECK(done && written==rows && reservations==rows && (!rows || held==8));
  start_in.pvalid=0; write_available=read_in.pready=1; older_writes=0;
  bool finished=false; unsigned writes=0;
  for(unsigned t=0;t<16 && !finished;++t) {
    result_in.pvalid=returning; result_in.pbits={};
    auto *input=reinterpret_cast<unsigned char*>(&result_in.pbits);
    if(physical==8) input[0]=0x5a;
    else if(physical==12 && (d.pcontrol.ppermutation==Compress)) input[0]=1;
    eval(); returning=read_out.pvalid && read_in.pready;
    if(!t) CHECK(returning); // No empty cycle between the two owners.
    if(returning) { physical=read_out.pbits; CHECK(physical==8 || physical==12); }
    if(write_out.pvalid) {
      CHECK(write_out.pbits.paddress==40 && reinterpret_cast<const unsigned char*>(&write_out.pbits.pdata)[0]==0x5a);
      for(unsigned b=0;b<bytes;++b) CHECK(reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask)[b]==(b?0:255));
      ++writes;
    }
    if((finished=finish)) CHECK(write_owner==0 && bool(accepting_end)==bool((d.pcontrol.ppermutation==Compress)));
    tick_model();
  }
  CHECK(finished && writes==1);
  for(unsigned i=0;i<5;++i) { result_in.pvalid=0; eval(); CHECK(!finish && !write_out.pvalid && !reserving); tick_model(); }
}

static void fold_with_stalls(bool scalar, bool masked_off, bool widening=false) {
  reset=1; start_in={}; result_in={}; tick_model(); reset=0;
  auto& d=start_in.pbits.pchecked.pdescriptor;
  auto& c=d.pcontrol;
  const unsigned src=16, seed=9, dst=24;
  d.pinstruction=src<<20 | seed<<15 | dst<<7;
  d.pstate.pvl=scalar ? bytes*8+3 : bytes*2-1;
  d.pstate.pvtype=scalar ? 3 : widening ? 2 : 0;
  d.pmasked=1;
  c.pcursor=FoldCursor; c.pvps=scalar ? NoVps : SeedVps;
  c.psequence=scalar ? 2 : 1; // Scan or Reduction
  c.pscan_ucontrol.ppacked_urows=scalar;
  c.pinteger=1; c.presult_ukind=scalar ? 2 : 0;
  c.preads.pvs2=1; c.preads.pvs1=!scalar; c.pwidth_ushape=widening ? 1 : 0; // Widen or Same
  write_available=gpr_ready=0;
  std::memset(&predicate,masked_off ? 0 : 255,bytes);
  upper_predicate=predicate;
  std::memset(&vs2,scalar ? 255 : 1,bytes);
  start_in.pvalid=1; eval(); CHECK(start_out.pready); tick_model(); start_in.pvalid=0;
  unsigned seed_reads=0, source_reads=0, bookings=0, writes=0, gprs=0, booked_at=0;
  bool returning=false, stalled=false;
  read_in.pready=1; ahead_in.pready=0;
  for(unsigned t=0;t<45;++t) {
    older_writes=t<4 ? (scalar ? UINT64_MAX : UINT64_C(1)<<(2*seed)) : 0;
    result_in.pvalid=returning; std::memset(&result_in.pbits,0,bytes);
    reinterpret_cast<unsigned char*>(&result_in.pbits)[0]=7;
    // Poison the seed bus outside its one synchronous return; the wide accumulator
    // must retain both seed bytes through the stalled final write.
    reinterpret_cast<unsigned char*>(&result_in.pbits)[1]=returning ? 0xa5 : 0x3c;
    write_available=gpr_ready=t>=28;
    eval();
    // VS1 is element zero of exactly v9, not the m4 group containing it.
    // It stays unread across the blocked request and through its return.
    CHECK(unread==(!scalar && (!seed_reads || returning) ? UINT64_C(1)<<(2*seed) : 0));
    if(t<4) CHECK(bool(vps_busy)==!scalar && !read_src && !reserving);
    returning=read_out.pvalid;
    if(returning) { CHECK(!scalar && t>=4 && read_out.pbits==2*seed); ++seed_reads; }
    if(read_src) { CHECK(t>=4 && t<28); ++source_reads; }
    if(t==25) {
      CHECK(source_reads==(widening?4u:2u) && !bookings && !gprs && !writes);
      CHECK(claimed==(scalar ? 0 : UINT64_C(1)<<(2*dst)));
      if(scalar) CHECK(gpr_offer);
      stalled=true;
    }
    if(gpr_offer && gpr_ready) { CHECK(scalar && !bookings); booked_at=t; ++bookings; }
    if(reserving) { CHECK(!scalar && !bookings && t>=28); booked_at=t; ++bookings; }
    if(gpr_done) { CHECK(scalar && t==booked_at+1 && gpr_data==(masked_off ? 0 : bytes*8+3)); ++gprs; }
    if(write_out.pvalid) {
      CHECK(!scalar && t==booked_at+2 && write_out.pbits.paddress==2*dst);
      for(unsigned i=0;i<bytes;++i) CHECK(reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask)[i]==(i<1u+widening?255:0));
      CHECK(reinterpret_cast<const unsigned char*>(&write_out.pbits.pdata)[0]==7+(masked_off?0:2*bytes-1));
      if(widening) CHECK(reinterpret_cast<const unsigned char*>(&write_out.pbits.pdata)[1]==0xa5);
      ++writes;
    }
    tick_model();
  }
  CHECK(stalled && seed_reads==!scalar && bookings==1 && writes==!scalar && gprs==scalar);
}

// With no active body, extract still reads element zero and returns once.
static void empty_extract() {
  reset=1; start_in={}; result_in={}; tick_model(); reset=0;
  auto& d=start_in.pbits.pchecked.pdescriptor;
  d.pinstruction=16u<<20 | 7u<<7;
  d.pcontrol.pinteger=1; d.pcontrol.presult_ukind=2;
  d.pcontrol.pcursor=FoldCursor; d.pcontrol.preads.pvs2=1; d.pcontrol.psequence=3; // Extract
  older_writes=0; gpr_ready=0; write_available=1;
  std::memset(&vs2,0,bytes); reinterpret_cast<unsigned char*>(&vs2)[0]=0x37;
  start_in.pvalid=1; eval(); CHECK(start_out.pready); tick_model(); start_in.pvalid=0;
  unsigned reads=0, offers=0, results=0;
  for(unsigned t=0;t<20;++t) {
    gpr_ready=t>=8; eval();
    CHECK(!read_out.pvalid && !ahead_out.pvalid && !unread && !write_out.pvalid);
    reads+=read_src;
    if(t==7) CHECK(gpr_offer && !gpr_done);
    offers+=gpr_offer && gpr_ready;
    if(gpr_done) { CHECK(gpr_data==0x37); ++results; }
    tick_model();
  }
  CHECK(reads==1 && offers==1 && results==1);
}

int main() {
  return run_test([] {
    empty_extract();
    for(bool boxed:{false,true}) splat_to_fp_insert(boxed);
    copy_to_compare();
    for(auto outgoing:{SlideDown,SlideOneUp,SlideOneDown,GatherVector,GatherVectorEI16}) for(bool stalled:{false,true})
      for(auto next:{Successor::Add,Successor::Slide,Successor::Gather}) tail_handoff(outgoing,stalled,next);
    exercise(Kind::Broadcast,0,~0u,true,1,true);
    exercise(Kind::Broadcast,0,31,false,0,true,false,true,true);
    for(bool scalar:{false,true}) for(bool masked_off:{false,true}) fold_with_stalls(scalar,masked_off);
    for(bool masked_off:{false,true}) fold_with_stalls(false,masked_off,true);
    for(bool stall:{false,true}) for(Kind k:{Kind::Up,Kind::Down}) {
      exercise(k,0,3,true,2,stall,k==Kind::Down,true);
      exercise(k,1,bytes/2+1,false,3,stall,k==Kind::Down);
      for(unsigned offset:{8*bytes-1,8*bytes,8*bytes+1,~0u}) exercise(k,0,offset,false,0,stall);
      exercise(k,0,2,false,0,stall,false,false,true);
    }
    for(Kind k:{Kind::Broadcast,Kind::Gather,Kind::Ei16}) for(unsigned sew=0;sew<=(bytes==16 ? 3u : 2u);++sew) {
      exercise(k,sew,3,true,1,true);
      if(k==Kind::Broadcast) exercise(k,sew,31,false,0,false,false,true);
    }
    for(unsigned sew=0;sew<=(bytes==16 ? 3u : 2u);++sew) {
      for(auto mode:{Selection::None,Selection::Sparse,Selection::Exact,Selection::Dense}) compress(sew,mode);
      for(Kind k:{Kind::OneUp,Kind::OneDown}) {
        exercise(k,sew,1,false,0,true,k==Kind::OneDown);
        exercise(k,sew,1,true,1,true);
        exercise(k,sew,1,false,0,true,false,false,false,false,true);
        if(sew==1) for(bool fp:{false,true}) for(unsigned mask:{0xfeu,0xffu})
          exercise(k,sew,1,true,0,true,false,false,false,fp,true,fp,mask);
        if(sew) {
          exercise(k,sew,1,false,0,true,false,false,false,true);
          if((1u<<sew)<bytes/2) exercise(k,sew,1,false,0,true,false,false,false,true,false,true);
        }
      }
    }
  });
}
