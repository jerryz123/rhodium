// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <cstring>

enum ProductSelect : unsigned { ProductLow, ProductHigh, ProductWide, ProductFractional };
enum ProductVdRole : unsigned { VdUnused, VdAddend, VdFactor };
enum WidthShape : unsigned { Same, Widen, WidenWideVs2, Narrow, Extend2, Extend4, Extend8 };
enum RowLayout : unsigned { ElementRows, PackedMaskRows, ElementsToMask };
enum DataResult : unsigned { Alu, MaskBits, CopyVs2, Permuted };
enum ComputeFamily : unsigned { IntegerData, IntProduct, IntDivide, FpArithmetic, FpDivSqrt, FpData };
enum LocalFpResult : unsigned { RawThenRound, DirectWithFlags, DirectNoFlags };

using Row = decltype(write_out.pbits.pdata);
constexpr unsigned bytes = sizeof(Row);
static_assert(bytes == 8 || bytes == 16);

static std::uint64_t held(unsigned owner) {
  std::uint64_t mask = 0;
  for (unsigned i = 0; i < chain_out.pdestinations.size(); ++i) {
    const auto& d = chain_out.pdestinations[i];
    if ((chain_out.preserved >> i & 1) && d.powner == owner) mask |= UINT64_C(1) << d.prow;
  }
  return mask;
}

static void mask_claims(unsigned sew,unsigned lmul,unsigned first,unsigned end) {
  constexpr unsigned dlen=8*bytes, owner=2;
  reset=1; launch_in.pvalid=operands_in.pvalid=0; tick_model(); reset=0;
  auto &d=launch_in.pbits.pdescriptor; d={};
  d.pinstruction=(0u<<20)|(9u<<15)|(0u<<7); // vmxor.mm v0,v0,v9
  d.pstate.pvtype=sew*8+lmul; d.pstate.pvl=end; d.pstate.pvstart=first;
  d.pcontrol.preads.pvs1=d.pcontrol.preads.pvs2=d.pcontrol.pinteger=d.pcontrol.presult_ukind=1;
  d.pcontrol.prow_ulayout=PackedMaskRows;
  d.pcontrol.pinteger_urow.palu.presult_uselect=1; d.pcontrol.pinteger_urow.palu.plogic_uselect=2;
  read_credit=read_in.pready=fixed_write_credit=reserve_in.pready=1;
  launch_in.pbits.pcontext=owner; launch_in.pvalid=1; eval(); CHECK(launch_out.pready);
  tick_model(); launch_in.pvalid=0;
  const unsigned stop=(end-1)/dlen+1;
  unsigned next=first/dlen,written=next;
  bool returning=false;
  for(unsigned t=0;t<24 && written<stop;++t) {
    chain_in.polder_uaccesses=t<8 ? 2 : 0;
    operands_in.pvalid=returning;
    std::memset(&operands_in.pbits.pdata[0],0xaa,bytes);
    std::memset(&operands_in.pbits.pdata[1],0x3c,bytes);
    eval();
    const unsigned remaining=(1u<<stop)-(1u<<next);
    CHECK(chain_out.punissued.preads==(remaining | remaining<<18));
    CHECK(chain_out.punissued.pwrites==remaining && !chain_out.punissued.ppredicates);
    CHECK(held(owner)==(1u<<next)-(1u<<written));
    returning=read_out.pvalid;
    CHECK(returning==(next<stop && (next!=1 || t>=8)));
    if(returning) {
      const auto &r=read_out.pbits;
      CHECK(r.pleft==next && r.psecond==18+next && !r.preads_uv0 && !r.psources.paccumulator);
      ++next;
    }
    if(write_out.pvalid) {
      const auto &w=write_out.pbits;
      CHECK(w.paddress==written && held(owner) & (1u<<written));
      for(unsigned b=0;b<bytes;++b) {
        unsigned mask=0;
        for(unsigned bit=0;bit<8;++bit) {
          unsigned i=written*dlen+8*b+bit;
          if(first<=i && i<end) mask|=1u<<bit;
        }
        CHECK(reinterpret_cast<const unsigned char*>(&w.pmask)[b]==mask);
        CHECK(reinterpret_cast<const unsigned char*>(&w.pdata)[b]==0x96);
      }
      ++written;
    }
    CHECK(bool(finished_out.pvalid)==(written==stop));
    tick_model();
  }
  CHECK(next==stop && written==stop);
  chain_in.polder_uaccesses=0;
}

enum class Fragment { Compare, Carry, Borrow, FloatCompare };
// Track unissued and booked fragments separately. Pin an unrelated older v0
// writer for no-input arithmetic; only a real shadow read should conflict.
static void mask_fragments(unsigned sew,unsigned first,unsigned end,bool empty_final,
                           Fragment kind=Fragment::Compare,bool reads_v0=true) {
  constexpr unsigned dlen=8*bytes, owner=1;
  const bool arithmetic=kind==Fragment::Carry || kind==Fragment::Borrow, borrow=kind==Fragment::Borrow;
  const unsigned vd=reads_v0 ? 0 : 4;
  const unsigned size=1u<<sew, lanes=bytes/size, stop=(end+lanes-1)/lanes;
  std::array<unsigned char,2*bytes> v0, original;
  for(unsigned i=0;i<v0.size();++i) v0[i]=(i%2 ? 0x53 : 0xad);
  if(empty_final) for(unsigned i=(stop-1)*lanes;i<end;++i) v0[i/8]&=~(1u<<(i%8));
  original=v0;
  reset=1; launch_in.pvalid=operands_in.pvalid=0; tick_model(); reset=0;
  auto &d=launch_in.pbits.pdescriptor; d={};
  d.pinstruction=8u<<20 | 16u<<15 | vd<<7;
  d.pmasked=reads_v0;
  d.pcontrol.preads.pvs1=d.pcontrol.preads.pvs2=d.pcontrol.pinteger=d.pcontrol.presult_ukind=1;
  d.pcontrol.prow_ulayout=ElementsToMask;
  d.pcontrol.pdata_uresult=kind==Fragment::FloatCompare ? CopyVs2 : MaskBits;
  if(kind==Fragment::FloatCompare) {
    d.pcontrol.pcompute_ufamily=FpArithmetic;
    d.pcontrol.pinteger=0; d.pcontrol.pfloating_upoint=d.pcontrol.pfp_uservice=d.pcontrol.preads.pvs1=1;
    d.pcontrol.pfp.pexecution.punit=7; d.pcontrol.pfp.presult=2;
    d.pcontrol.pfp.plocal_uresult=DirectWithFlags;
  }
  d.pcontrol.preads.pv0=arithmetic && reads_v0 ? 2 : 0;
  d.pcontrol.pinteger_urow.palu.pmask_uresult_uselect=arithmetic;
  d.pcontrol.pinteger_urow.palu.psubtract=borrow;
  d.pcontrol.pinteger_urow.palu.presult_uselect=arithmetic ? 0 : 3; d.pstate.pvtype=8*sew+3;
  d.pstate.pvstart=first; d.pstate.pvl=end;
  read_credit=fixed_write_credit=reserve_in.pready=read_in.pready=1; launch_in.pbits.pcontext=owner;
  chain_in.polder_uaccesses=chain_in.polder_uwrites=reads_v0 ? 0 : 3;
  launch_in.pvalid=1; tick_model(); launch_in.pvalid=0;
  unsigned next=first/lanes,written=next,gap_cycles=0;
  std::array<unsigned,2> waits{};
  bool returning=false;
  auto saved_operands=operands_in.pbits;
  for(unsigned t=0; t<100 && written<stop; ++t) {
    const unsigned target=next*lanes/dlen;
    const bool closing=next<stop && (next+1==stop || (next+1)*lanes/dlen!=target);
    const bool stall=closing && waits[target]++<6;
    fixed_write_credit=reserve_in.pready=!stall;
    operands_in.pvalid=returning;
    operands_in.pbits=saved_operands;
    eval();
    std::uint64_t sources=0, unissued=0;
    for(unsigned r=next;r<stop;++r) {
      sources|=(UINT64_C(1)<<16 | UINT64_C(1)<<32)<<r;
      unissued|=UINT64_C(1)<<(r*lanes/dlen);
    }
    CHECK(chain_out.punissued.preads==sources && chain_out.punissued.pwrites==(unissued<<(2*vd)));
    CHECK(chain_out.punissued.ppredicates==(reads_v0 ? unissued : 0));
    std::array<unsigned,2> pending{},booked{};
    for(unsigned r=written;r<next;++r) ++pending[r*lanes/dlen];
    for(unsigned i=0;i<chain_out.pdestinations.size();++i) if(chain_out.preserved>>i&1) {
      auto &w=chain_out.pdestinations[i];
      CHECK(w.powner==owner && w.prow>=2*vd && w.prow<2*vd+2); ++booked[w.prow-2*vd];
    }
    CHECK(booked==pending);
    if(stall && written==next) { CHECK(!chain_out.preserved && (unissued>>target&1)); ++gap_cycles; }
    returning=read_out.pvalid;
    CHECK(returning==(next<stop && !stall));
    CHECK(bool(reserve_out.pvalid)==returning);
    CHECK(bool(issued_out.pvalid)==(returning && next+1==stop));
    if(returning) {
      CHECK(read_out.pbits.pleft==16+next && read_out.pbits.psecond==32+next);
      CHECK(bool(read_out.pbits.preads_uv0)==reads_v0 && read_out.pbits.pmask_urow==target);
      auto &ops=saved_operands;
      std::memcpy(&ops.ppredicate,v0.data()+target*bytes,bytes);
      std::memset(&ops.pdata[0],0x72,bytes); ops.pdata[1]=ops.pdata[0];
      for(unsigned j=0;j<lanes;++j) {
        const unsigned k=(next*lanes+j)%3;
        auto a=reinterpret_cast<unsigned char*>(&ops.pdata[0])+j*size;
        auto b=reinterpret_cast<unsigned char*>(&ops.pdata[1])+j*size;
        if(arithmetic) {
          std::memset(a,(borrow ? k==2 : k!=2) ? 0xff : 0,size);
          std::memset(b,0,size); b[0]=k==0;
        } else if(k) b[size-1]^=0x80;
      }
      ++next;
    }
    if(write_out.pvalid) {
      const auto &w=write_out.pbits;
      unsigned start=written*lanes,row=start/dlen;
      CHECK(w.paddress==2*vd+row);
      for(unsigned j=0;j<dlen;++j) {
        unsigned i=row*dlen+j, bit=1u<<(j%8);
        bool input=original[i/8]&bit;
        bool enabled=i>=start && i<start+lanes && i>=first && i<end && (arithmetic || input);
        CHECK(bool(reinterpret_cast<const unsigned char*>(&w.pmask)[j/8]&bit)==enabled);
        if(enabled) {
          bool data=reinterpret_cast<const unsigned char*>(&w.pdata)[j/8]&bit;
          CHECK(data==(i%3==0 || (arithmetic && i%3==1 && reads_v0 && input)));
          if(reads_v0) v0[i/8]=(v0[i/8]&~bit) | (data ? bit : 0);
        }
      }
      ++written;
    }
    CHECK(bool(finished_out.pvalid)==(written==stop));
    tick_model();
  }
  CHECK(written==stop && gap_cycles>=4);
  chain_in.polder_uaccesses=chain_in.polder_uwrites=0;
}

static void mac_stream(bool stall) {
  constexpr unsigned rows=16, base=32, owner=1;
  constexpr unsigned pause_at=4, resume=20;
  std::vector<unsigned> reads,writes;
  reset=1; launch_in.pvalid=operands_in.pvalid=0; tick_model(); reset=0;
  read_credit=read_in.pready=1;
  auto &d=launch_in.pbits.pdescriptor; d={};
  d.pstate.pvtype=3; d.pstate.pvl=rows*bytes; // e8,m8
  d.pinstruction=(8u<<20)|(16u<<7); d.pscalar=3; d.pmasked=1;
  d.pcontrol.preads.pvs2=d.pcontrol.preads.pold_uvd=1;
  d.pcontrol.pcompute_ufamily=IntProduct;
  d.pcontrol.poperand=1; d.pcontrol.pmultiply_udivide=1; d.pcontrol.pproduct.pvd_urole=VdAddend;
  launch_in.pbits.pcontext=owner; launch_in.pvalid=1; eval(); CHECK(launch_out.pready);
  tick_model(); launch_in.pvalid=0;
  bool returning=false;
  for (unsigned t=0;t<100 && writes.size()<rows;++t) {
    // Deny future bookings mid-stream. Already issued products must still write.
    fixed_write_credit=reserve_in.pready=!stall || t<pause_at || t>=resume;
    operands_in.pvalid=returning;
    if (returning) {
      unsigned r=reads.size()-1;
      std::memset(&operands_in.pbits.pdata[0],0x10+r,bytes);
      std::memset(&operands_in.pbits.pdata[1],0x30+r,bytes); // old vd
      std::memset(&operands_in.pbits.ppredicate,r%2 ? 0xaa : 0x55,bytes);
    }
    eval();
    std::uint64_t claims=0;
    for (unsigned r=writes.size();r<reads.size();++r) claims|=UINT64_C(1)<<(base+r);
    CHECK(held(owner)==claims);
    returning=read_out.pvalid && read_in.pready;
    if (returning) {
      CHECK(reads.size()<rows && read_out.pbits.psecond==base+reads.size());
      reads.push_back(t);
    }
    if (stall && t>=pause_at && t<resume) CHECK(!returning && reads.size()==pause_at);
    CHECK(bool(reserve_out.pvalid)==returning);
    if (returning) CHECK(reserve_out.pbits==6);
    const bool accepted=write_out.pvalid;
    CHECK(bool(finished_out.pvalid)==(accepted && writes.size()+1==rows));
    if (finished_out.pvalid) CHECK(finished_out.pbits==owner);
    if (write_out.pvalid) {
      const unsigned r=writes.size();
      CHECK(write_out.pbits.paddress==base+r);
      auto* data=reinterpret_cast<const unsigned char*>(&write_out.pbits.pdata);
      auto* mask=reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask);
      for (unsigned b=0;b<bytes;++b) {
        CHECK(mask[b]==((b%2==r%2) ? 255 : 0));
        if (mask[b]) CHECK(data[b]==3*(0x10+r)+0x30+r);
      }
      if (accepted) writes.push_back(t);
    }
    tick_model();
  }
  eval(); CHECK(reads.size()==rows && writes.size()==rows && !held(owner));
  for (unsigned r=0;r<rows;++r) CHECK(writes[r]==reads[r]+6);
  if (!stall) {
    std::cout<<"vmacc.vx DLEN="<<8*bytes<<" read:";
    for (auto t:reads) std::cout<<" "<<t;
    std::cout<<" write:"; for (auto t:writes) std::cout<<" "<<t;
    std::cout<<"\n";
    for (unsigned i=0;i<rows;++i) {
      CHECK(reads[i]==reads.front()+i);
      CHECK(writes[i]==writes.front()+i);
    }
  } else {
    CHECK(writes[pause_at-1]<resume && reads[pause_at]==resume);
  }
}

static void widening_last_use(unsigned start, bool overlapping_mac=false, bool wide_add=false) {
  constexpr unsigned vl=2*bytes+2, elems=bytes/2, owner=1;
  constexpr unsigned rhs=24, dest=32;
  const unsigned lhs=overlapping_mac ? 36 : 16; // v18 lies in the high half of v16m4.
  const unsigned first=start/elems, last=(vl-1)/elems;
  reset=1; launch_in.pvalid=operands_in.pvalid=0; tick_model(); reset=0;
  read_credit=read_in.pready=fixed_write_credit=reserve_in.pready=1;
  auto &d=launch_in.pbits.pdescriptor; d={};
  d.pstate.pvl=vl; d.pstate.pvstart=start; d.pstate.pvtype=1; // e8,m2
  d.pinstruction=((lhs/2)<<20)|(12u<<15)|(16u<<7); d.pmasked=1;
  d.pcontrol.pcompute_ufamily=wide_add ? IntegerData : IntProduct;
  d.pcontrol.pwidth_ushape=wide_add ? WidenWideVs2 : Widen;
  d.pcontrol.pinteger=wide_add;
  d.pcontrol.pproduct.pselect=ProductWide;
  d.pcontrol.pmultiply_udivide=!wide_add;
  d.pcontrol.preads.pvs1=d.pcontrol.preads.pvs2=1;
  d.pcontrol.preads.pold_uvd=overlapping_mac; d.pcontrol.pproduct.pvd_urole=overlapping_mac ? VdAddend : VdUnused;
  launch_in.pbits.pcontext=owner; launch_in.pvalid=1; tick_model(); launch_in.pvalid=0;
  unsigned next=first, written=first, returned_row=0, stalls=0;
  bool returning=false;
  for (unsigned t=0;t<40 && written<=last;++t) {
    // Deny the upper half of the second narrow row. The lower read must
    // neither release that source nor advance it through this stall.
    bool pause=next==3 && stalls<3;
    fixed_write_credit=reserve_in.pready=wide_add || overlapping_mac || !pause;
    read_in.pready=read_credit=!pause;
    if (pause) ++stalls;
    operands_in.pvalid=returning;
    if (returning) {
      auto &data=operands_in.pbits.pdata;
      std::memset(&data[2],0xff,bytes);
      for (unsigned b=0;b<bytes;++b) {
        reinterpret_cast<unsigned char*>(&data[0])[b]=1+returned_row*bytes+b;
        reinterpret_cast<unsigned char*>(&data[1])[b]=2+returned_row*bytes+b;
      }
      if (wide_add) for(unsigned b=0;b<elems;++b) {
        std::uint16_t value=1+(next-1)*elems+b;
        std::memcpy(reinterpret_cast<char*>(&data[0])+2*b,&value,2);
      }
      std::memset(&operands_in.pbits.ppredicate,0xad,bytes);
    }
    eval();
    std::uint64_t unread=0, unissued=0, inflight=0;
    for (unsigned r=next;r<=last;++r) {
      unread|=(UINT64_C(1)<<(lhs+(wide_add?r:r/2))) | (UINT64_C(1)<<(rhs+r/2));
      unissued|=UINT64_C(1)<<(dest+r);
    }
    for (unsigned r=written;r<next;++r) inflight|=UINT64_C(1)<<(dest+r);
    if (overlapping_mac) unread|=unissued;
    CHECK(chain_out.punissued.preads==unread && chain_out.punissued.pwrites==unissued);
    CHECK(held(owner)==inflight);
    if (wide_add && pause) {
      CHECK(read_out.pvalid && !reserve_out.pvalid);
      CHECK(read_out.pbits.pleft==lhs+next && read_out.pbits.psecond==rhs+next/2);
    }
    if (overlapping_mac && pause) {
      CHECK(read_out.pvalid && read_out.pbits.psources.paccumulator && read_out.pbits.pthird==dest+next);
      CHECK(!reserve_out.pvalid && (unread & (UINT64_C(1)<<(dest+next))));
    }
    returning=read_out.pvalid && read_in.pready;
    CHECK(returning==(!pause && next<=last));
    if (returning) {
      returned_row=next/2;
      CHECK(read_out.pbits.pleft==lhs+(wide_add?next:returned_row) && read_out.pbits.psecond==rhs+returned_row);
      if (overlapping_mac) CHECK(read_out.pbits.pthird==dest+next);
      ++next;
    }
    if (write_out.pvalid) {
      CHECK(write_out.pbits.paddress==dest+written);
      auto* mask=reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask);
      for (unsigned b=0;b<elems;++b) {
        unsigned i=written*elems+b;
        bool active=i>=start && i<vl && (0xad>>(i%8)&1);
        CHECK(mask[2*b]==(active?255:0) && mask[2*b+1]==mask[2*b]);
        std::uint16_t result;
        std::memcpy(&result,reinterpret_cast<const unsigned char*>(&write_out.pbits.pdata)+2*b,2);
        if (active) CHECK(result==(wide_add ? 3+2*i : (1+i)*(2+i)-unsigned(overlapping_mac)));
      }
      CHECK(bool(finished_out.pvalid)==(written==last));
      ++written;
    }
    tick_model();
  }
  eval(); CHECK(written==last+1 && stalls==3 && !held(owner));
}

static void saturating_reservations(bool active_overflow) {
  reset=1; launch_in.pvalid=operands_in.pvalid=0; tick_model(); reset=0;
  auto &d=launch_in.pbits.pdescriptor; d={};
  d.pinstruction=8u<<20 | 12u<<15 | 16u<<7;
  d.pcontrol.pinteger=d.pcontrol.preads.pvs1=d.pcontrol.preads.pvs2=1;
  d.pcontrol.pinteger_urow.pcan_usaturate=1;
  d.pcontrol.pinteger_urow.palu.parithmetic_umode=2; // vsadd.vv, signed
  d.pstate.pvstart=1; d.pstate.pvl=2*bytes-1; d.pmasked=1;
  chain_in.polder_uaccesses=chain_in.polder_uwrites=0;
  launch_in.pbits.pcontext=1; launch_in.pvalid=1; tick_model(); launch_in.pvalid=0;
  // Replace the older instruction at its last grant. Poison the younger XOR's
  // unused adder mode: even a genuine slice overflow must not raise vxsat.
  d.pinstruction=8u<<20 | 12u<<15 | 20u<<7;
  d.pcontrol.pinteger_urow.pcan_usaturate=0;
  d.pcontrol.pinteger_urow.palu.presult_uselect=1; // Logic
  d.pcontrol.pinteger_urow.palu.plogic_uselect=2; // XOR
  d.pmasked=d.pstate.pvstart=0; d.pstate.pvl=bytes;
  for(unsigned t=0;t<14;++t) {
    launch_in.pvalid=t==9; launch_in.pbits.pcontext=2;
    read_credit=fixed_write_credit=reserve_in.pready=t>=8; read_in.pready=1;
    operands_in.pvalid=t>=9 && t<=11;
    auto &r=operands_in.pbits;
    std::memset(&r.ppredicate,0xfd,bytes); // element 1 of each eight masked
    std::memset(&r.pdata[0],0,bytes); std::memset(&r.pdata[1],1,bytes);
    auto a=reinterpret_cast<unsigned char*>(&r.pdata[0]);
    a[1]=0x7f;
    if(t==9) a[0]=0x7f; // prestart
    if(t==10) { a[bytes-1]=0x7f; if(active_overflow) a[2]=0x7f; }
    if(t==11) std::memset(a,0x7f,bytes);
    eval();
    if(t==9) CHECK(launch_out.pready);
    CHECK(bool(read_out.pvalid)==(t>=8 && t<=10));
    CHECK(bool(reserve_out.pvalid)==bool(read_out.pvalid));
    if(t<8) CHECK(chain_out.punissued.pwrites==(UINT64_C(3)<<32) && !held(1));
    CHECK(bool(write_out.pvalid)==(t>=10 && t<=12));
    CHECK(bool(saturate_out.pvalid)==(active_overflow && t==11));
    if(write_out.pvalid) {
      CHECK(write_out.pbits.paddress==(t==12?40:32+t-10) && (held(t==12?2:1)>>write_out.pbits.paddress&1));
      auto mask=reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask);
      auto data=reinterpret_cast<const unsigned char*>(&write_out.pbits.pdata);
      for(unsigned j=0;j<bytes;++j) {
        bool enabled=t==12 || (j%8!=1 && !(t==10 && j==0) && !(t==11 && j==bytes-1));
        CHECK(mask[j]==(enabled ? 255 : 0));
        if(enabled) CHECK(data[j]==(t==12 ? 0x7e : t==11 && j==2 && active_overflow ? 127 : 1));
      }
    }
    tick_model();
  }
}

// Each wide source read deposits only one half of vd. Deny the second booking
// until the first write is gone: vd still has an unissued writer and VS1 is
// still unread. Direct fixture geometry is DLEN=2*XLEN on both configurations.
static void narrowing_claims(bool clip) {
  reset=1; launch_in.pvalid=operands_in.pvalid=0; tick_model(); reset=0;
  auto &d=launch_in.pbits.pdescriptor; d={};
  d.pinstruction=(8u<<20)|(12u<<15)|(16u<<7);
  d.pcontrol.pinteger=d.pcontrol.preads.pvs1=d.pcontrol.preads.pvs2=1;
  d.pcontrol.pwidth_ushape=Narrow;
  d.pcontrol.pinteger_urow.palu.presult_uselect=2; d.pcontrol.pinteger_urow.palu.pshift_uright=1;
  auto &row=d.pcontrol.pinteger_urow;
  row.palu.parithmetic_ushift=1; // vnsra or vnclip with a zero shift
  row.pclip=row.pround_ushift=row.pcan_usaturate=clip;
  d.pstate.pvstart=1; d.pstate.pvl=2*bytes-1;
  chain_in.polder_uwrites=chain_in.polder_uaccesses=0;
  launch_in.pbits.pcontext=1; launch_in.pvalid=1; tick_model(); launch_in.pvalid=0;
  unsigned grants=0,writes=0; bool returning=false;
  for(unsigned t=0;t<20;++t) {
    read_credit=fixed_write_credit=reserve_in.pready=t==0 || t>=8; read_in.pready=1;
    operands_in.pvalid=returning;
    for(unsigned i=0;i<bytes/2;++i) {
      std::uint16_t n=0xf000+(returning?grants-1:0)*bytes/2+i;
      std::memcpy(reinterpret_cast<char*>(&operands_in.pbits.pdata[0])+2*i,&n,2);
    }
    std::memset(&operands_in.pbits.pdata[1],0,bytes); eval();
    CHECK(bool(saturate_out.pvalid)==(clip && write_out.pvalid));
    CHECK(bool(write_out.pvalid)==(t==2 || (t>=10 && t<=12)));
    if(t>2 && t<8) {
      CHECK(!held(1) && !write_out.pvalid);
      CHECK(chain_out.punissued.pwrites==(UINT64_C(3)<<32));
      CHECK(chain_out.punissued.preads==((UINT64_C(14)<<16)|(UINT64_C(3)<<24)));
    }
    returning=read_out.pvalid;
    if(returning) {
      CHECK(read_out.pbits.pleft==16+grants && read_out.pbits.psecond==24+grants/2);
      ++grants;
    }
    if(write_out.pvalid) {
      CHECK(write_out.pbits.paddress==32+writes/2);
      auto data=reinterpret_cast<const unsigned char*>(&write_out.pbits.pdata);
      auto mask=reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask);
      for(unsigned j=0;j<bytes;++j) {
        unsigned element=(writes/2)*bytes+j;
        bool active=j/(bytes/2)==writes%2 && element>0 && element<2*bytes-1;
        CHECK(mask[j]==(active?255:0));
        if(active) CHECK(data[j]==(clip ? 0x80 : element));
      }
      ++writes;
    }
    tick_model();
  }
  CHECK(grants==4 && writes==4 && !held(1));
}

static void fractional_reservations() {
  reset=1; launch_in.pvalid=operands_in.pvalid=0; tick_model(); reset=0;
  auto &d=launch_in.pbits.pdescriptor; d={};
  d.pstate.pvl=2*bytes-1; d.pstate.pvstart=1; d.pmasked=1;
  d.pinstruction=(8u<<20)|(12u<<15)|(16u<<7);
  d.pcontrol.preads.pvs1=d.pcontrol.preads.pvs2=1;
  d.pcontrol.pcompute_ufamily=IntProduct;
  d.pcontrol.pmultiply_udivide=d.pcontrol.pproduct.pmode.pleft_usigned=d.pcontrol.pproduct.pmode.pright_usigned=1;
  d.pcontrol.pproduct.pselect=ProductFractional;
  launch_in.pbits.pcontext=0; launch_in.pvalid=1; tick_model(); launch_in.pvalid=0;
  for (unsigned t=0;t<20;++t) {
    // Deny bookings long enough that an incorrectly launched row would return.
    read_credit=fixed_write_credit=reserve_in.pready=t>=8; read_in.pready=1;
    operands_in.pvalid=t==9 || t==10;
    auto &r=operands_in.pbits;
    std::memset(&r.ppredicate,0xfd,bytes); // byte 1 of each eight is masked
    std::memset(&r.pdata[0],0x40,bytes); std::memset(&r.pdata[1],0x80,bytes);
    auto *a=reinterpret_cast<unsigned char*>(&r.pdata[0]);
    a[0]=a[1]=a[bytes-1]=0x80;
    if (t==9) a[bytes-1]=0x40; // row 0: saturation only before vstart or masked
    eval();
    CHECK(bool(reserve_out.pvalid)==(t==8 || t==9));
    CHECK(bool(write_out.pvalid)==(t==14 || t==15));
    CHECK(bool(saturate_out.pvalid)==(t==15)); // first enabled MIN x MIN
    tick_model();
  }
}

static void scalar_integer_claim(bool empty, bool fractional, bool divide=false) {
  reset=1; launch_in.pvalid=operands_in.pvalid=multiply_result_in.pvalid=divide_result_in.pvalid=0; tick_model(); reset=0;
  auto &d=launch_in.pbits.pdescriptor; d={};
  d.pinstruction=8u<<20 | 12u<<15 | 16u<<7;
  d.pcontrol.pmultiply_udivide=d.pcontrol.preads.pvs1=d.pcontrol.preads.pvs2=d.pmasked=1;
  d.pcontrol.pproduct.pselect=fractional ? ProductFractional : ProductLow;
  d.pcontrol.pcompute_ufamily=divide ? IntDivide : IntProduct;
  d.pcontrol.pdivision.psigned_umode=d.pcontrol.pdivision.premainder=divide;
  // Product controls are don't-cares on a divide. Even a subtracting vd-factor MAC
  // setting must leave vs2 and signed division untouched on the shared port.
  d.pcontrol.pproduct.pvd_urole=divide ? VdFactor : VdUnused; d.pcontrol.pproduct.psubtract=divide;
  d.pstate.pvtype=divide ? 0 : 8*(bytes==8 ? 2 : 3);
  d.pstate.pvl=divide ? bytes : 2; d.pstate.pvstart=d.pstate.pvl-1;
  launch_in.pbits.pcontext=2; launch_in.pvalid=1;
  read_in.pready=read_credit=fixed_write_credit=reserve_in.pready=shared_write_in.pready=0;
  chain_in.polder_uwrites=chain_in.polder_uaccesses=0;
  divide_request_in.pready=multiply_request_in.pready=0; tick_model(); launch_in.pvalid=0;
  for(unsigned i=0;i<2;++i) {
    eval(); CHECK(read_out.pvalid && !issued_out.pvalid && !launch_out.pready && !chain_out.pinteger_upending.pvalid);
    tick_model();
  }
  // The fixed calendar and its registered handoff credit stay blocked. A
  // shared multiply/divide can still release VXSeq on its last granted read.
  read_in.pready=read_credit=1; eval();
  CHECK(read_out.pvalid && launch_out.pready && !reserve_out.pvalid && issued_out.pvalid && issued_out.pbits==2);
  tick_model(); operands_in.pvalid=1; std::memset(&operands_in.pbits.ppredicate,empty?0:0xff,bytes);
  if (divide) {
    std::memset(&operands_in.pbits.pdata[0],0x81,bytes);
    std::memset(&operands_in.pbits.pdata[1],0xfe,bytes);
  }
  eval(); CHECK(chain_out.pinteger_upending.pvalid && launch_out.pready && !issued_out.pvalid);
  tick_model(); operands_in.pvalid=0;
  if(!empty) {
    for(unsigned i=0;i<3;++i) {
      eval(); CHECK(bool(divide_request_out.pvalid)==divide && bool(multiply_request_out.pvalid)==!divide);
      if (divide) {
        auto &args=divide_request_out.pbits.poperands;
        CHECK(args.psigned_umode && args.pdividend==decltype(args.pdividend)(-127) && args.pdivisor==decltype(args.pdivisor)(-2));
        CHECK(divide_request_out.pbits.ptag.pslot==2 && !divide_request_out.pbits.ptag.psew && divide_request_out.pbits.ptag.premainder);
      }
      CHECK(!issued_out.pvalid && launch_out.pready); tick_model();
    }
    divide_request_in.pready=multiply_request_in.pready=1; eval(); CHECK(!issued_out.pvalid);
    if(divide) divide_result_in.pbits.ptag=divide_request_out.pbits.ptag;
    tick_model();
    eval(); CHECK(launch_out.pready && !divide_request_out.pvalid && !multiply_request_out.pvalid && !shared_write_out.pvalid);
    if(divide) {
      divide_result_in.pbits.pvalue.pquotient=63;
      divide_result_in.pbits.pvalue.premainder=0xff;
      divide_result_in.pvalid=1; eval(); CHECK(divide_result_out.pready); tick_model(); divide_result_in.pvalid=0;
    } else {
      multiply_result_in.pbits.ptag.pslot=2;
      auto& product=multiply_result_in.pbits.pvalue;
      product={}; reinterpret_cast<unsigned char*>(&product)[sizeof(product)-1]=0x40; // MIN * MIN
      multiply_result_in.pvalid=1; eval(); CHECK(multiply_result_out.pready); tick_model(); multiply_result_in.pvalid=0;
    }
  }
  for(unsigned i=0;i<4;++i) {
    eval(); CHECK(!divide_request_out.pvalid && !multiply_request_out.pvalid && shared_write_out.pvalid && launch_out.pready && !finished_out.pvalid);
    CHECK(!reserve_out.pvalid && !chain_out.preserved && !write_out.pvalid);
    CHECK(!saturate_out.pvalid);
    CHECK(chain_out.pinteger_upending.pvalid && chain_out.pinteger_upending.pbits.powner==2 && chain_out.pinteger_upending.pbits.prow==32);
    for(unsigned b=0;b<bytes;++b)
      CHECK(reinterpret_cast<const unsigned char*>(&shared_write_out.pbits.pmask)[b]==(!empty && b>=(divide?bytes-1:bytes/2) ? 0xff : 0));
    if(divide && !empty) CHECK(reinterpret_cast<const unsigned char*>(&shared_write_out.pbits.pdata)[bytes-1]==0xff);
    tick_model();
  }
  shared_write_in.pready=1; eval(); CHECK(finished_out.pvalid && finished_out.pbits==2 && chain_out.pinteger_upending.pvalid);
  CHECK(bool(saturate_out.pvalid)==(fractional && !empty));
  tick_model(); eval(); CHECK(!chain_out.pinteger_upending.pvalid && !finished_out.pvalid && !saturate_out.pvalid);
}

static void delayed_mac(ProductVdRole role, bool scalar, bool subtract) {
  using Word=decltype(multiply_request_out.pbits.poperands.pleft);
  reset=1; launch_in={}; operands_in={}; multiply_result_in={}; tick_model(); reset=0;
  auto &d=launch_in.pbits.pdescriptor;
  d.pinstruction=(8u<<20)|(12u<<15)|(16u<<7);
  d.pstate.pvl=1; d.pstate.pvtype=8*(sizeof(Word)==4 ? 2 : 3); d.pscalar=5;
  d.pcontrol.pcompute_ufamily=IntProduct; d.pcontrol.pmultiply_udivide=1;
  d.pcontrol.poperand=scalar; d.pcontrol.preads.pvs1=!scalar;
  d.pcontrol.preads.pvs2=d.pcontrol.preads.pold_uvd=1;
  d.pcontrol.pproduct.pvd_urole=role; d.pcontrol.pproduct.psubtract=subtract;
  chain_in.polder_uwrites=chain_in.polder_uaccesses=0;
  read_credit=read_in.pready=shared_write_in.pready=0;
  fixed_write_credit=reserve_in.pready=multiply_request_in.pready=1;
  launch_in.pbits.pcontext=2; launch_in.pvalid=1; eval(); CHECK(launch_out.pready);
  tick_model(); launch_in.pvalid=0;
  for(unsigned t=0;t<3;++t) {
    eval(); const auto &r=read_out.pbits;
    CHECK(read_out.pvalid && r.psources.paccumulator && r.pleft==16);
    CHECK(r.psecond==(scalar ? 32 : 24) && (scalar || r.pthird==32));
    CHECK((chain_out.punissued.preads & (UINT64_C(1)<<32)) && !issued_out.pvalid && !multiply_request_out.pvalid);
    tick_model();
  }
  read_credit=read_in.pready=1; eval(); CHECK(issued_out.pvalid && issued_out.pbits==2);
  tick_model(); operands_in.pvalid=1;
  const Word inputs[]={3,Word(scalar ? 7 : 5),7}; // vs2, RHS or old vd, third read
  for(unsigned i=0;i<3;++i) std::memcpy(&operands_in.pbits.pdata[i],&inputs[i],sizeof(Word));
  tick_model(); operands_in={}; eval();
  const Word factor=role==VdFactor ? 7 : 3, addend=role==VdFactor ? 3 : 7;
  CHECK(multiply_request_out.pvalid && multiply_request_out.pbits.poperands.pleft==factor && multiply_request_out.pbits.poperands.pright==5);
  multiply_result_in.pbits.ptag=multiply_request_out.pbits.ptag;
  tick_model();

  // Issue and finish an independent add before returning the product.
  d={}; d.pstate.pvl=1; d.pinstruction=(2u<<20)|(4u<<15)|(6u<<7);
  d.pcontrol.preads.pvs1=d.pcontrol.preads.pvs2=1;
  launch_in.pbits.pcontext=1; launch_in.pvalid=1; eval(); CHECK(launch_out.pready);
  tick_model(); launch_in.pvalid=0; eval(); CHECK(read_out.pvalid);
  tick_model(); operands_in.pvalid=1; tick_model(); operands_in.pvalid=0;
  eval(); CHECK(write_out.pvalid && finished_out.pvalid && finished_out.pbits==1);
  CHECK(chain_out.pinteger_upending.pvalid && !shared_write_out.pvalid);
  tick_model();
  const Word product=factor*5;
  std::memcpy(&multiply_result_in.pbits.pvalue,&product,sizeof(Word));
  multiply_result_in.pvalid=1; eval(); CHECK(multiply_result_out.pready);
  tick_model(); multiply_result_in.pvalid=0;
  for(unsigned t=0;t<4;++t) {
    shared_write_in.pready=t==3; eval();
    CHECK(shared_write_out.pvalid && shared_write_out.pbits.paddress==32);
    Word result; std::memcpy(&result,&shared_write_out.pbits.pdata,sizeof result);
    CHECK(result==(subtract ? addend-product : addend+product));
    for(unsigned i=0;i<bytes;++i)
      CHECK(reinterpret_cast<const unsigned char*>(&shared_write_out.pbits.pmask)[i]==(i<sizeof(Word) ? 0xff : 0));
    CHECK(chain_out.pinteger_upending.pvalid && chain_out.pinteger_upending.pbits.powner==2);
    CHECK(bool(finished_out.pvalid)==(t==3) && (t!=3 || finished_out.pbits==2));
    tick_model();
  }
  eval(); CHECK(!chain_out.pinteger_upending.pvalid && !shared_write_out.pvalid);
}

// Drive one decoded same-width operation through VXSeq; choose exact lane inputs so the
// numerical oracle is independent of HardFloat. Double replies emulate the shared service.
static void fp_family(unsigned sew) {
  const unsigned size=1u<<sew, lanes=bytes/size;
  auto encode=[&](int n)->std::uint64_t {
    if (!n) return 0;
    const unsigned m=n<0 ? -n:n, e=31-__builtin_clz(m);
    const unsigned frac=size==2?10:size==4?23:52, bias=size==2?15:size==4?127:1023;
    return (std::uint64_t(n<0)<<(8*size-1)) | (std::uint64_t(e+bias)<<frac) |
           (std::uint64_t(m-(1u<<e))<<(frac-e));
  };
  auto run=[&](unsigned unit,unsigned mode,bool scalar,bool old_factor,bool swap,
               bool from_int,bool int_result,std::uint64_t expected,bool rtz=false,bool active_snan=false) {
    reset=1; operands_in={}; fp_result_in={}; launch_in={}; tick_model(); reset=0;
    auto &d=launch_in.pbits.pdescriptor; auto &c=d.pcontrol; auto &f=c.pfp.pexecution;
    d.pstate.pvtype=sew*8; d.pstate.pvl=lanes; d.pmasked=1;
    d.pinstruction=(8u<<20)|(16u<<15)|(24u<<7);
    auto rhs=encode(3);
    d.pfloating_uscalar=rhs | (size==8 ? 0 : (~UINT64_C(0)<<(size*8)));
    c.pcompute_ufamily=FpArithmetic;
    c.pfloating_upoint=c.pfp_uservice=1;
    const bool unary=unit==8 || unit==10 || unit==11;
    c.preads.pvs2=1; c.preads.pold_uvd=unit==1;
    c.preads.pvs1=!scalar && !unary; c.poperand=scalar?3:0;
    c.pfp.porder=old_factor ? 2 : swap ? 1 : 0;
    c.prow_ulayout=unit==7 ? ElementsToMask : ElementRows; c.presult_ukind=unit==7; c.pfp.presult=unit==7?2:int_result?1:0;
    c.pfp.psource=from_int; c.pfp.prounding=rtz;
    c.pfp.plocal_uresult=unit<=3 ? RawThenRound : unit==5 || unit==10 ? DirectNoFlags : DirectWithFlags;
    f.punit=unit; f.pfused_uoperation=mode; f.padd_uoperation=mode;
    f.pminmax_uoperation=mode; f.psign_uoperation=mode; f.pcomparison=mode;
    f.pinteger_uunsigned=mode; f.pestimate_uoperation=mode;
    launch_in.pbits.pcontext=2; launch_in.pvalid=1; fixed_write_credit=reserve_in.pready=0; read_credit=0;
    read_in.pready=1; shared_write_in.pready=0; fp_request_in.pready=0;
    tick_model(); launch_in.pvalid=0;
    bool returning=false,received=false,issued=false; unsigned reads=0,writes=0,booked_edge=0;
    const unsigned flags=active_snan?16:unit==11?8:0;
    for(unsigned t=0;t<28;++t) {
      // Only lane zero is active. A masked-off sNaN must not raise NV.
      const auto snan=size==2?0x7c01ULL:size==4?0x7f800001ULL:0x7ff0000000000001ULL;
      operands_in.pvalid=returning;
      operands_in.pbits.ppredicate={}; reinterpret_cast<unsigned char*>(&operands_in.pbits.ppredicate)[0]=1;
      for(unsigned i=0;i<lanes;++i) {
        std::uint64_t a=i || active_snan?snan:from_int?2:unit==11?0:encode(2), b=rhs, v=encode(5);
        std::memcpy(reinterpret_cast<char*>(&operands_in.pbits.pdata[0])+i*size,&a,size);
        std::memcpy(reinterpret_cast<char*>(&operands_in.pbits.pdata[1])+i*size,scalar?&v:&b,size);
        std::memcpy(reinterpret_cast<char*>(&operands_in.pbits.pdata[2])+i*size,&v,size);
      }
      fixed_write_credit=reserve_in.pready=read_credit=t>=3; fp_request_in.pready=t>=7;
      fp_result_in.pvalid=received; fp_result_in.pbits.ptag=2; fp_result_in.pbits.pvalue=expected;
      fp_result_in.pbits.pexception_uflags_uvalid=unit!=5 && unit!=10;
      fp_result_in.pbits.pexception_uflags=flags;
      shared_write_in.pready=t>=17;
      eval(); received=false;
      returning=read_out.pvalid && read_in.pready; reads+=returning;
      CHECK(bool(reserve_out.pvalid)==(returning && size!=8));
      if(reserve_out.pvalid) { CHECK(reserve_out.pbits==2); booked_edge=t+2; }
      if(size!=8) CHECK(bool(write_out.pvalid)==(booked_edge && t==booked_edge));
      if(returning) CHECK(bool(read_out.pbits.psources.pvector_urhs)==(!unary&&!scalar));
      if(fp_request_out.pvalid) {
        CHECK(size==8);
        auto &r=fp_request_out.pbits;
        auto a=from_int?2:unit==11?0:encode(2);
        CHECK(r.pleft==(old_factor?encode(5):swap?rhs:a) && r.ptag==2);
        if (!unary) CHECK(r.pright==(swap?a:rhs));
        if(unit==1) CHECK(r.pthird==(old_factor?a:encode(5)));
        CHECK(r.pcontrol.pexecution.punit==unit && r.prounding_umode==unsigned(rtz));
        if(fp_request_in.pready) { CHECK(!issued); issued=received=true; }
      }
      if(size==8 && shared_write_out.pvalid && !shared_write_in.pready)
        CHECK(chain_out.pfp_upending.pvalid && !finished_out.pvalid && !fp_update_out.pvalid);
      if(write_out.pvalid || (shared_write_out.pvalid && shared_write_in.pready)) {
        CHECK(bool(write_out.pvalid)==(size!=8));
        const auto &w=write_out.pvalid ? write_out.pbits:shared_write_out.pbits;
        std::uint64_t data=0,mask=0; std::memcpy(&data,&w.pdata,size); std::memcpy(&mask,&w.pmask,size);
        CHECK(mask==(unit==7?1:size==8?UINT64_MAX:(UINT64_C(1)<<(size*8))-1));
        CHECK((data&mask)==(expected&mask));
        CHECK(bool(fp_update_out.pvalid)==(unit!=5 && unit!=10));
        if (fp_update_out.pvalid) CHECK(fp_update_out.pbits==flags);
        CHECK(finished_out.pvalid && finished_out.pbits==2); ++writes;
      }
      tick_model();
    }
    CHECK(reads==1 && writes==1 && issued==(size==8));
  };
  for(bool scalar:{false,true}) {
    run(2,0,scalar,false,false,false,false,encode(5));
    run(2,1,scalar,false,false,false,false,encode(-1));
    run(3,0,scalar,false,false,false,false,encode(6));
    for(unsigned mode=0;mode<4;++mode) for(bool old:{false,true}) {
      int product=old?15:6, addend=old?2:5;
      run(1,mode,scalar,old,false,false,false,encode((mode&2?-product:product)+(mode&1?-addend:addend)));
    }
    for(unsigned mode=0;mode<3;++mode) run(5,mode,scalar,false,false,false,false,encode(mode==1?-2:2));
    for(unsigned mode:{2,3}) run(6,mode,scalar,false,false,false,false,encode(mode==2?2:3));
    for(unsigned mode=0;mode<4;++mode) run(7,mode,scalar,false,false,false,true,mode!=0);
  }
  run(2,1,true,false,true,false,false,encode(1));
  for(unsigned mode:{2,3}) run(7,mode,true,false,true,false,true,0);
  run(10,0,false,false,false,false,true,64);
  if(size!=8) run(7,2,false,false,false,false,true,0,false,true); // active sNaN: false, NV
  const auto inf=size==2?0x7c00ULL:size==4?0x7f800000ULL:0x7ff0000000000000ULL;
  for(unsigned mode:{0,1}) {
    run(11,mode,false,false,false,false,false,inf);
    run(8,mode,false,false,false,true,false,encode(2));
    run(8,mode,false,false,false,false,true,2);
    run(8,mode,false,false,false,false,true,2,true);
  }
  // Neither an empty range nor a vstart beyond vl consumes VRF or FP credits.
  for(unsigned first:{0u,5u}) {
    auto &d=launch_in.pbits.pdescriptor; d.pstate.pvl=0; d.pstate.pvstart=first;
    launch_in.pvalid=1; tick_model(); launch_in.pvalid=0; eval();
    CHECK(!read_out.pvalid && !fp_request_out.pvalid && !write_out.pvalid && !reserve_out.pvalid);
    CHECK(issued_out.pvalid && finished_out.pvalid && !fp_update_out.pvalid); tick_model();
  }
}

// Delay the second fragment until the first has written. A younger writer
// must still see the unread narrow source; a reader must see the unfinished
// narrow destination. Test different operands in the two physical halves.
static void mixed_rows(bool narrow) {
  constexpr unsigned lanes=bytes/4, stop=3, first=1, end=stop*lanes-1;
  reset=1; operands_in={}; launch_in={}; chain_in={}; tick_model(); reset=0;
  auto &d=launch_in.pbits.pdescriptor; auto &c=d.pcontrol;
  d.pinstruction=(8u<<20)|(16u<<15)|(24u<<7);
  d.pstate.pvtype=9; d.pstate.pvstart=first; d.pstate.pvl=end; d.pmasked=1;
  c.pcompute_ufamily=FpArithmetic;
  c.pfloating_upoint=c.pfp_uservice=1;
  c.pwidth_ushape=narrow?Narrow:Widen;
  c.preads.pvs2=1; c.preads.pold_uvd=c.preads.pvs1=!narrow;
  c.pfp.pexecution.punit=narrow?8:1; c.pfp.prounding=narrow?2:0;
  c.pfp.plocal_uresult=narrow?DirectWithFlags:RawThenRound;
  launch_in.pbits.pcontext=2; launch_in.pvalid=1; read_in.pready=1;
  tick_model(); launch_in.pvalid=0;
  unsigned reads=0,writes=0; bool returning=false;
  const auto active=[](unsigned i){ return i>=first && i<end && i%4!=2; };
  for(unsigned t=0;t<24 && writes<stop;++t) {
    read_credit=fixed_write_credit=reserve_in.pready=reads!=1 || t>=8;
    operands_in.pvalid=returning; auto &p=operands_in.pbits; p={};
    const unsigned r=reads?reads-1:0;
    unsigned predicate=0;
    for(unsigned i=0;i<stop*lanes;++i) if(i%4!=2) predicate|=1u<<i;
    std::memcpy(&p.ppredicate,&predicate,sizeof(predicate));
    for(unsigned i=0;i<lanes;++i) {
      unsigned value=active(r*lanes+i)?0x3f800001:0x7f800001;
      std::memcpy(reinterpret_cast<char*>(&p.pdata[narrow?0:2])+4*i,&value,4);
      if(!narrow) {
        value=0x3f800000;
        std::memcpy(reinterpret_cast<char*>(&p.pdata[2])+4*i,&value,4);
      }
    }
    if(!narrow) for(unsigned i=0;i<2*lanes;++i) {
      unsigned elem=(r/2)*2*lanes+i;
      std::uint16_t a=active(elem)?(i<lanes?0x4000:0x4400):0x7c01,b=0x4200;
      std::memcpy(reinterpret_cast<char*>(&p.pdata[0])+2*i,&a,2);
      std::memcpy(reinterpret_cast<char*>(&p.pdata[1])+2*i,&b,2);
    }
    eval();
    if(reads==1 && t<8 && writes==1) {
      CHECK(!held(2) && !read_out.pvalid);
      if(narrow) CHECK(chain_out.punissued.pwrites & (UINT64_C(1)<<48));
      else CHECK(chain_out.punissued.preads & (UINT64_C(1)<<16));
    }
    returning=read_out.pvalid;
    if(returning) {
      CHECK(read_out.pbits.pleft==16+(narrow?reads:reads/2));
      CHECK(!narrow || !read_out.pbits.psources.pvector_urhs); ++reads;
    }
    if(write_out.pvalid) {
      auto &w=write_out.pbits;
      CHECK(w.paddress==48+(narrow?writes/2:writes));
      CHECK(held(2)&(UINT64_C(1)<<w.paddress));
      bool any=false;
      for(unsigned i=0;i<(narrow?2*lanes:lanes);++i) {
        unsigned elem=(narrow?(writes/2)*2:writes)*lanes+i, size=narrow?2:4;
        bool enabled=active(elem) && (!narrow || i/lanes==writes%2);
        unsigned value=0,mask=0; any|=enabled;
        std::memcpy(&value,reinterpret_cast<const char*>(&w.pdata)+size*i,size);
        std::memcpy(&mask,reinterpret_cast<const char*>(&w.pmask)+size*i,size);
        CHECK(mask==(enabled?(narrow?0xffffu:UINT32_MAX):0));
        if(enabled) CHECK(value==(narrow?0x3c01u:writes%2?0x41500000u:0x40e00000u));
      }
      CHECK(bool(fp_update_out.pvalid)==any);
      if(any) CHECK(fp_update_out.pbits==unsigned(narrow));
      ++writes; CHECK(bool(finished_out.pvalid)==(writes==stop));
    }
    tick_model();
  }
  CHECK(reads==stop && writes==stop);
}

// Emulate VPS seed/feedback and independently withhold the final VRF booking.
// The ordered FP32 sum is (2^24 + 1 - 2^24) + 1 = 1, raising NX once.
static void fp_fold(unsigned mode, bool empty=false) {
  constexpr unsigned n=bytes/4, rows=2;
  const bool all_masked=mode==1, invalid=mode==2, wide=mode==3, minmax=mode>=4;
  reset=1; launch_in={}; operands_in={}; chain_in={}; reduction_in={}; fp_result_in={}; tick_model(); reset=0;
  auto &d=launch_in.pbits.pdescriptor; auto &c=d.pcontrol;
  d.pinstruction=(8u<<20)|(12u<<15)|(16u<<7); d.pstate.pvtype=wide?8:16;
  const unsigned per=wide?bytes/2:n, end=2*per;
  d.pstate.pvl=empty?0:end; d.pmasked=1;
  c.pcursor=1; c.pvps=2; c.psequence=1; // Fold cursor, VPS seed, reduction.
  c.preads.pvs1=c.preads.pvs2=1;
  c.pfloating_upoint=c.pfp_uservice=1;
  c.pcompute_ufamily=FpArithmetic; c.pwidth_ushape=wide?Widen:Same;
  c.pfp.pexecution.punit=minmax?6:2; c.pfp.pexecution.pminmax_uoperation=mode==5?3:2;
  c.pfp.plocal_uresult=minmax?DirectWithFlags:RawThenRound;
  std::uint32_t seed=all_masked?0x7f800123:mode==0?0x4b800000:0;
  auto &acc=reduction_in.pvalue; acc.pvalid=!empty;
  std::memcpy(&acc.pbits,&seed,4);
  read_credit=0; read_in.pready=1; fixed_write_credit=reserve_in.pready=0;
  launch_in.pbits.pcontext=3; launch_in.pvalid=1; eval(); CHECK(launch_out.pready); tick_model(); launch_in.pvalid=0;
  c.pfp.plocal_uresult=minmax?RawThenRound:DirectNoFlags; // feedback belongs to the accepted fold
  unsigned reads=0,writes=0,steps=0,bookings=0; bool returning=false,done=false;
  for(unsigned t=0;t<150 && !done;++t) {
    fixed_write_credit=reserve_in.pready=t>=100;
    operands_in.pvalid=returning; auto &p=operands_in.pbits; p={};
    unsigned pred=0;
    const unsigned part=reads?reads-1:0;
    for(unsigned j=0;j<end;++j) if(!all_masked && (j==0 || j==per || j==end-1)) pred|=1u<<j;
    std::memcpy(&p.ppredicate,&pred,sizeof(pred));
    for(unsigned i=0;i<per;++i) {
      unsigned j=part*per+i;
      if(wide) {
        std::uint16_t v=(pred>>j&1)?0x3c00:0x7c01;
        std::memcpy(reinterpret_cast<char*>(&p.pdata[0])+i*2,&v,2);
      } else {
        unsigned v=!(pred>>j&1) || (invalid&&j==0)?0x7f800011:j==per?(minmax?0xbf800000:0xcb800000):0x3f800000;
        std::memcpy(reinterpret_cast<char*>(&p.pdata[0])+i*4,&v,4);
      }
    }
    eval(); CHECK(!fp_request_out.pvalid);
    if(t==90 && !empty) {
      CHECK(reads==rows && !bookings && !writes && chain_out.punissued.pwrites==(UINT64_C(1)<<32));
      CHECK(!chain_out.punissued.preads && !chain_out.punissued.ppredicates);
    }
    returning=read_out.pvalid;
    if(returning && read_out.pbits.psources.pvector_ulhs) {
      CHECK(reads<rows && read_out.pbits.pleft==16+reads && read_out.pbits.preads_uv0); ++reads;
    }
    bookings+=reserve_out.pvalid && reserve_in.pready;
    const auto next=reduction_out.pupdate;
    if(next.pvalid) ++steps;
    if(write_out.pvalid) {
      unsigned value=0; std::memcpy(&value,&write_out.pbits.pdata,4);
      CHECK(value==(all_masked?seed:invalid?0x7fc00000:wide?0x40400000:mode==4?0xbf800000:0x3f800000));
      for(unsigned i=0;i<bytes;++i) CHECK(reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask)[i]==(i<4?255:0));
      CHECK(held(3)==(UINT64_C(1)<<32)); ++writes;
    }
    CHECK(bool(fp_update_out.pvalid)==(write_out.pvalid && !all_masked));
    if(fp_update_out.pvalid) CHECK(fp_update_out.pbits==(invalid?16:mode==0?1:0));
    done=finished_out.pvalid || fold_finished_out.pvalid;
    if(done) CHECK(empty?fold_finished_out.pvalid:write_out.pvalid);
    tick_model(); if(next.pvalid) acc.pbits=next.pbits;
  }
  CHECK(done && reads==(empty?0:rows) && bookings==!empty && writes==!empty && steps==(empty||all_masked?0:3));
  reduction_in={};
}

// Delay both ends of the scalar service and the final VRF reservation.
static void double_fold() {
  reset=1; launch_in={}; operands_in={}; fp_result_in={}; reduction_in={}; chain_in={}; tick_model(); reset=0;
  auto &d=launch_in.pbits.pdescriptor; auto &c=d.pcontrol;
  d.pinstruction=(8u<<20)|(12u<<15)|(16u<<7); d.pstate.pvtype=16; d.pstate.pvl=3; d.pmasked=1;
  c.pcursor=1; c.pvps=2; c.psequence=1; // Fold cursor, VPS seed, reduction.
  c.preads.pvs1=c.preads.pvs2=1;
  c.pfloating_upoint=c.pfp_uservice=1; c.pfp.pexecution.punit=2;
  c.pcompute_ufamily=FpArithmetic; c.pwidth_ushape=Widen;
  launch_in.pbits.pcontext=2; launch_in.pvalid=1; read_in.pready=1; read_credit=fixed_write_credit=reserve_in.pready=0;
  tick_model(); launch_in.pvalid=0;
  auto &acc=reduction_in.pvalue; acc.pvalid=1;
  unsigned reads=0,sent=0,due=0,replies=0,updates=0,stalls=0; bool returned=false,done=false;
  for(unsigned t=0;t<110 && !done;++t) {
    operands_in.pvalid=returned; reinterpret_cast<unsigned char*>(&operands_in.pbits.ppredicate)[0]=5;
    const unsigned src[4]={0x3f800000,0x7f800001,0x40000000,0x7f800001};
    std::memcpy(&operands_in.pbits.pdata[0],src,bytes);
    fp_request_in.pready=t%9==0; fp_result_in.pvalid=due && t==due;
    fp_result_in.pbits.ptag=2; fp_result_in.pbits.pexception_uflags_uvalid=1;
    // Deliberately distinct flags demonstrate that replies accrue until publication.
    fp_result_in.pbits.pexception_uflags=sent==1?1:16;
    fp_result_in.pbits.pvalue=sent==1?UINT64_C(0x3ff0000000000000):UINT64_C(0x4008000000000000);
    fixed_write_credit=reserve_in.pready=t>=80;
    eval();
    returned=read_out.pvalid;
    if(returned && read_out.pbits.psources.pvector_ulhs) ++reads;
    if(fp_request_out.pvalid) {
      CHECK(sent==replies && sent<2);
      auto &r=fp_request_out.pbits;
      CHECK(r.ptag==2 && r.pleft_uprecision==2 && r.pright_uprecision==1);
      CHECK(r.pleft==(sent?UINT64_C(0x3ff0000000000000):0));
      CHECK(r.pright==(UINT64_C(0xffffffff00000000)|src[2*sent]));
      if(fp_request_in.pready) {++sent; due=t+5;} else ++stalls;
    }
    if(fp_result_in.pvalid) {CHECK(fp_result_out.pready); ++replies; due=0;}
    const auto next=reduction_out.pupdate; updates+=next.pvalid;
    if(t==70) CHECK(updates==2 && chain_out.punissued.pwrites==(UINT64_C(1)<<32) && !held(2));
    CHECK(bool(fp_update_out.pvalid)==bool(write_out.pvalid));
    if(write_out.pvalid) {
      std::uint64_t value=0; std::memcpy(&value,&write_out.pbits.pdata,8);
      CHECK(value==UINT64_C(0x4008000000000000) && fp_update_out.pbits==17 && held(2));
      done=finished_out.pvalid && finished_out.pbits==2;
    }
    tick_model(); if(next.pvalid) acc.pbits=next.pbits;
  }
  CHECK(done && reads==1 && stalls && sent==2 && replies==2 && updates==2);
  reduction_in={}; fp_result_in={};
}

// Deny every fixed booking. Delay each scalar reply independently, then
// hold the completed row and its bit enables behind the VRF write arbiter.
static void iterative_fp_row(unsigned sew, bool empty, bool sqrt) {
  const unsigned size=1u<<sew, lanes=bytes/size;
  reset=1; launch_in={}; operands_in={}; fp_result_in={}; chain_in={}; tick_model(); reset=0;
  auto &d=launch_in.pbits.pdescriptor; auto &c=d.pcontrol;
  d.pinstruction=(8u<<20)|(12u<<15)|(16u<<7); d.pstate.pvtype=sew*8;
  d.pstate.pvstart=lanes>2; d.pstate.pvl=lanes-(lanes>3); d.pmasked=1;
  c.pcompute_ufamily=FpDivSqrt;
  c.pfp_uservice=c.pfloating_upoint=c.preads.pvs2=1; c.preads.pvs1=!sqrt;
  c.pfp.pexecution.pdivide_uoperation=sqrt;
  c.pfp.pexecution.punit=4; launch_in.pbits.pcontext=2; launch_in.pvalid=1;
  read_in.pready=1; fixed_write_credit=reserve_in.pready=read_credit=shared_write_in.pready=0;
  tick_model(); launch_in.pvalid=0;
  unsigned live=0;
  for(unsigned i=d.pstate.pvstart;i<d.pstate.pvl;++i) if(!empty && i!=2) live|=1u<<i;
  const unsigned predicate=empty?0:0xfb;
  std::memcpy(&operands_in.pbits.ppredicate,&predicate,sizeof(predicate));
  for(unsigned i=0;i<lanes;++i) {
    std::uint64_t v=100+i;
    std::memcpy(reinterpret_cast<char*>(&operands_in.pbits.pdata[0])+size*i,&v,size);
  }
  unsigned unsent=live, replies=0, due=0, lane=0, issued=0, reads=0, held_cycles=0, bits=0;
  bool returning=false, done=false;
  for(unsigned t=0;t<78 && !done;++t) {
    operands_in.pvalid=returning; fp_request_in.pready=t%4==0;
    fp_result_in.pvalid=t==due && due; fp_result_in.pbits.ptag=2;
    fp_result_in.pbits.pvalue=0x3120+lane; fp_result_in.pbits.pexception_uflags_uvalid=1;
    fp_result_in.pbits.pexception_uflags=1u<<(lane%5); shared_write_in.pready=t>=68;
    eval(); returning=read_out.pvalid; reads+=returning;
    CHECK(!reserve_out.pvalid && !write_out.pvalid && !held(2));
    if(fp_request_out.pvalid) {
      CHECK(!due && unsent);
      auto &r=fp_request_out.pbits; lane=__builtin_ctz(unsent);
      const std::uint64_t box=size==8?0:(~UINT64_C(0)<<(8*size));
      using Transport=decltype(r.pleft);
      CHECK(r.ptag==2 && r.pcontrol.pexecution.punit==4 && r.pleft_uprecision==sew-1);
      CHECK(r.pcontrol.pexecution.pdivide_uoperation==unsigned(sqrt));
      CHECK(r.pleft==Transport(box|(100+lane)));
      if(fp_request_in.pready) { unsent&=unsent-1; due=t+3+lane%3; }
    }
    if(fp_result_in.pvalid) { CHECK(fp_result_out.pready); due=0; ++replies; bits|=1u<<(lane%5); }
    CHECK(bool(issued_out.pvalid)==returning);
    if(returning) { CHECK(issued_out.pbits==2); ++issued; }
    if(shared_write_out.pvalid) {
      CHECK(chain_out.pfp_upending.pvalid && chain_out.pfp_upending.pbits.prow==32 && !due && !unsent);
      for(unsigned i=0;i<bytes;++i) {
        const bool active=live>>(i/size)&1;
        CHECK(reinterpret_cast<const unsigned char*>(&shared_write_out.pbits.pmask)[i]==(active?255:0));
        if(active) CHECK(reinterpret_cast<const unsigned char*>(&shared_write_out.pbits.pdata)[i]==
                         (((UINT64_C(0x3120)+i/size)>>(8*(i%size)))&255));
      }
      if(!shared_write_in.pready) ++held_cycles;
      else done=true;
    }
    CHECK(bool(finished_out.pvalid)==done && bool(fp_update_out.pvalid)==bool(done && live));
    if(fp_update_out.pvalid) CHECK(fp_update_out.pbits==bits);
    tick_model();
  }
  CHECK(done && reads==1 && issued==1 && held_cycles>2 && replies==unsigned(__builtin_popcount(live)));
  eval(); CHECK(!chain_out.pfp_upending.pvalid);
}

// Offer a second divide on the first one's tail, then an add. The second
// divide owns the cursor but cannot release it using the other adapter's slot.
static void shared_tail_credit(bool fp) {
  reset=1; launch_in={}; operands_in={}; chain_in={}; divide_result_in={}; fp_result_in={};
  tick_model(); reset=0;
  divide_request_in.pready=fp_request_in.pready=shared_write_in.pready=0;
  fixed_write_credit=reserve_in.pready=0; read_credit=read_in.pready=0;
  auto offer=[&](unsigned owner, bool divide) {
    auto &d=launch_in.pbits.pdescriptor; d={}; d.pinstruction=(8u<<20)|(12u<<15)|((12+2*owner)<<7);
    d.pstate.pvtype=16; d.pstate.pvl=1;
    auto &c=d.pcontrol; c.preads.pvs1=c.preads.pvs2=1;
    c.pcompute_ufamily=divide ? (fp ? FpDivSqrt : IntDivide) : IntegerData;
    if(divide && fp) { c.pfloating_upoint=c.pfp_uservice=c.preads.pvs1=1; c.pfp.pexecution.punit=4; }
    else { c.pinteger=1; c.pmultiply_udivide=divide; }
    launch_in.pbits.pcontext=owner; launch_in.pvalid=1;
  };
  offer(1,true); eval(); CHECK(launch_out.pready); tick_model();
  offer(2,true); eval(); CHECK(read_out.pvalid && !launch_out.pready && !issued_out.pvalid); tick_model();
  read_credit=read_in.pready=1;
  eval(); CHECK(launch_out.pready && issued_out.pvalid && issued_out.pbits==1 && !reserve_out.pvalid); tick_model();
  operands_in.pvalid=1; offer(3,false);
  fixed_write_credit=reserve_in.pready=1;
  for(unsigned i=0;i<3;++i) {
    eval(); CHECK(!read_out.pvalid && !launch_out.pready && !issued_out.pvalid);
    const auto &pending = fp ? chain_out.pfp_upending : chain_out.pinteger_upending;
    CHECK(pending.pvalid && pending.pbits.powner==1);
    tick_model(); operands_in.pvalid=0;
  }
  divide_request_in.pready=fp_request_in.pready=1;
  eval(); CHECK(fp ? fp_request_out.pvalid : divide_request_out.pvalid); tick_model();
  if(fp) { fp_result_in.pbits.ptag=1; fp_result_in.pvalid=1; }
  else { divide_result_in.pbits.ptag.pslot=1; divide_result_in.pvalid=1; }
  eval(); CHECK(fp ? fp_result_out.pready : divide_result_out.pready); tick_model();
  divide_result_in.pvalid=fp_result_in.pvalid=0;
  eval(); CHECK(shared_write_out.pvalid && !launch_out.pready && !read_out.pvalid); tick_model();
  shared_write_in.pready=1; eval(); CHECK(finished_out.pvalid && finished_out.pbits==1 && !launch_out.pready); tick_model();
  fixed_write_credit=reserve_in.pready=0;
  eval(); CHECK(read_out.pvalid && launch_out.pready && issued_out.pbits==2 && issued_out.pvalid); tick_model();
  operands_in.pvalid=1; launch_in.pvalid=0;
  eval(); CHECK(!read_out.pvalid && !launch_out.pready); // Add still needs the fixed write port.
  fixed_write_credit=reserve_in.pready=1;
  eval(); CHECK(read_out.pvalid && issued_out.pvalid && issued_out.pbits==3 && reserve_out.pvalid); tick_model();
  tick_model(); // Synchronous operand return for the add.
  operands_in.pvalid=0;
}

// Issue three independent rows through one cursor: integer divide, FP divide,
// then a short ALU row. Both divides stay live across the booked ALU write.
static void overlap_divides(bool both_ready) {
  reset=1; launch_in={}; operands_in={}; fp_result_in={}; divide_result_in={}; chain_in={};
  tick_model(); reset=0;
  read_in.pready=fixed_write_credit=reserve_in.pready=divide_request_in.pready=fp_request_in.pready=1;
  read_credit=shared_write_in.pready=0;
  auto launch=[&](unsigned owner, unsigned vd) {
    auto &d=launch_in.pbits.pdescriptor; d={};
    d.pinstruction=8u<<20 | 12u<<15 | vd<<7;
    d.pstate.pvtype=2*8; d.pstate.pvl=1;
    auto &c=d.pcontrol; c.preads.pvs1=c.preads.pvs2=1;
    c.pcompute_ufamily=owner==1 ? IntDivide : owner==2 ? FpDivSqrt : IntegerData;
    if(owner==1) { c.pmultiply_udivide=c.pinteger=1; }
    else if(owner==2) { c.pfloating_upoint=c.pfp_uservice=c.preads.pvs1=1; c.pfp.pexecution.punit=4; }
    else c.pinteger=1;
    launch_in.pbits.pcontext=owner; launch_in.pvalid=1; eval(); CHECK(launch_out.pready);
    tick_model(); launch_in.pvalid=0;
    eval(); CHECK(read_out.pvalid && bool(reserve_out.pvalid)==(owner==3));
    CHECK(issued_out.pvalid && issued_out.pbits==owner);
    tick_model(); operands_in.pvalid=1; eval(); CHECK(!finished_out.pvalid);
    tick_model(); operands_in.pvalid=0;
  };
  launch(1,16);
  eval(); CHECK(divide_request_out.pvalid && !issued_out.pvalid);
  tick_model();
  launch(2,18);
  eval(); CHECK(fp_request_out.pvalid && !issued_out.pvalid);
  tick_model();
  fp_result_in.pvalid=1; fp_result_in.pbits.ptag=2;
  fp_result_in.pbits.pvalue=0x12345678; fp_result_in.pbits.pexception_uflags_uvalid=1;
  fp_result_in.pbits.pexception_uflags=8; eval(); CHECK(fp_result_out.pready);
  tick_model(); fp_result_in.pvalid=0;
  launch(3,20);
  eval(); CHECK(write_out.pvalid && write_out.pbits.paddress==40 && finished_out.pvalid && finished_out.pbits==3);
  for(unsigned t=0;t<4;++t) {
    eval(); CHECK(chain_out.pinteger_upending.pvalid && chain_out.pinteger_upending.pbits.prow==32);
    CHECK(chain_out.pfp_upending.pvalid && chain_out.pfp_upending.pbits.prow==36);
    CHECK(shared_write_out.pvalid && shared_write_out.pbits.paddress==36 && !fp_update_out.pvalid);
    if(t) CHECK(!finished_out.pvalid);
    tick_model();
  }
  auto integer_reply=[&] {
    divide_result_in.pvalid=1; divide_result_in.pbits.ptag.pslot=1; divide_result_in.pbits.pvalue.pquotient=17;
    eval(); CHECK(divide_result_out.pready); tick_model(); divide_result_in.pvalid=0;
  };
  if(both_ready) integer_reply();
  bool int_live=true, fp_live=true;
  for(unsigned i=0;i<2;++i) {
    if(i==1 && !both_ready) integer_reply();
    shared_write_in.pready=1; eval();
    CHECK(shared_write_out.pvalid && finished_out.pvalid && !write_out.pvalid);
    const bool fp=shared_write_out.pbits.paddress==36;
    CHECK(fp ? fp_live : (int_live && shared_write_out.pbits.paddress==32));
    if(!both_ready) CHECK(fp==(i==0));
    CHECK(finished_out.pbits==(fp?2:1) && bool(fp_update_out.pvalid)==fp);
    if(fp) CHECK(fp_update_out.pbits==8);
    std::uint32_t value; std::memcpy(&value,&shared_write_out.pbits.pdata,sizeof value);
    CHECK(value==(fp?0x12345678u:17u));
    if(fp) fp_live=false; else int_live=false;
    tick_model(); shared_write_in.pready=0; eval();
    CHECK(bool(chain_out.pinteger_upending.pvalid)==int_live && bool(chain_out.pfp_upending.pvalid)==fp_live);
    CHECK(!finished_out.pvalid && !fp_update_out.pvalid);
  }
}

// Contend for the one completion port with either an irrevocably booked ALU
// write or a divider result held at the FU. The scan has no destination write.
static void scan_completion_contest(bool divide, bool port_ready) {
  reset=1; launch_in={}; operands_in={}; chain_in={}; permutation_in={}; divide_result_in={}; fp_result_in={};
  tick_model(); reset=0;
  read_credit=read_in.pready=fixed_write_credit=reserve_in.pready=divide_request_in.pready=1;
  shared_write_in.pready=0;
  auto offer=[&](unsigned owner) {
    auto &d=launch_in.pbits.pdescriptor; d={}; d.pinstruction=(8u<<20)|(12u<<15)|((12u+2*owner)<<7);
    d.pstate.pvtype=16; d.pstate.pvl=1;
    auto &c=d.pcontrol; c.pinteger=1;
    c.pvps=owner==2; c.ppermutation=owner==2 ? 8 : 0; // Compress or None.
    c.pdata_uresult=owner==2 ? Permuted : Alu;
    c.preads.pvs1=c.preads.pvs2=owner!=2;
    c.pcompute_ufamily=divide && owner==1 ? IntDivide : IntegerData;
    c.pmultiply_udivide=divide && owner==1;
    launch_in.pbits.pcontext=owner; launch_in.pvalid=1;
  };
  offer(1); eval(); CHECK(launch_out.pready); tick_model();
  offer(2); eval(); CHECK(launch_out.pready && issued_out.pvalid && issued_out.pbits==1); tick_model();
  offer(3); operands_in.pvalid=1; eval(); CHECK(!launch_out.pready); tick_model();
  operands_in.pvalid=0;
  if(divide) {
    eval(); CHECK(divide_request_out.pvalid); tick_model();
    divide_result_in.pvalid=1; divide_result_in.pbits.ptag.pslot=1;
    eval(); CHECK(divide_result_out.pready); tick_model(); divide_result_in.pvalid=0;
    eval(); CHECK(shared_write_out.pvalid && !finished_out.pvalid);
  }
  fixed_write_credit=reserve_in.pready=0; shared_write_in.pready=port_ready;
  permutation_in.pvalid=1; permutation_in.pbits.pend_uof_uscan=1;
  if(!divide) {
    eval(); CHECK(write_out.pvalid && finished_out.pvalid && finished_out.pbits==1);
    CHECK(!permutation_out.pready && !launch_out.pready && !issued_out.pvalid); tick_model();
  }
  eval(); CHECK(permutation_out.pready && launch_out.pready && !read_out.pvalid && !reserve_out.pvalid);
  CHECK(finished_out.pvalid && finished_out.pbits==2 && issued_out.pvalid && issued_out.pbits==2);
  CHECK(!write_out.pvalid && !shared_write_out.pvalid); tick_model();
  launch_in.pvalid=permutation_in.pvalid=0;
  if(divide) {
    eval(); CHECK(shared_write_out.pvalid && shared_write_out.pbits.paddress==28);
    CHECK(bool(finished_out.pvalid)==port_ready);
    shared_write_in.pready=1; eval(); CHECK(finished_out.pvalid && finished_out.pbits==1); tick_model();
  }
  fixed_write_credit=reserve_in.pready=1;
  eval(); CHECK(issued_out.pvalid && issued_out.pbits==3 && !finished_out.pvalid); tick_model();
  operands_in.pvalid=1; tick_model(); operands_in.pvalid=0;
  eval(); CHECK(write_out.pvalid && finished_out.pvalid && finished_out.pbits==3); tick_model();
  eval(); CHECK(!finished_out.pvalid && !shared_write_out.pvalid);
}

// VFADD and widening VFWADD use the same pipelined FP64 row. On its last
// read hand VXSeq to a divide; the divide cannot change the older issue cadence.
template<class Request, class Reply>
static void pipelined_double_row(const Request &request, Reply &reply, bool widen) {
  reset=1; launch_in={}; operands_in={}; reply={}; chain_in={}; tick_model(); reset=0;
  auto &d=launch_in.pbits.pdescriptor; auto &c=d.pcontrol;
  c.pwidth_ushape=widen?Widen:Same; c.pcompute_ufamily=FpArithmetic; c.pfloating_upoint=c.pfp_uservice=1;
  c.pfp.pexecution.punit=2; c.preads.pvs1=c.preads.pvs2=1;
  d.pstate.pvtype=widen?16:24; d.pstate.pvl=2; d.pinstruction=8u<<20 | 12u<<15 | 16u<<7;
  fixed_write_credit=reserve_in.pready=shared_write_in.pready=0; read_credit=read_in.pready=1;
  fp_request_in.pready=0; launch_in.pbits.pcontext=2; launch_in.pvalid=1; tick_model();
  c.pcompute_ufamily=FpDivSqrt; c.pwidth_ushape=Same; c.pfp.pexecution.punit=4;
  d.pinstruction=8u<<20 | 12u<<15 | 24u<<7; launch_in.pbits.pcontext=1;
  eval(); CHECK(read_out.pvalid && issued_out.pvalid && launch_out.pready && !reserve_out.pvalid);
  tick_model(); launch_in.pvalid=0;
  operands_in.pvalid=1;
  const unsigned size=widen?4:8;
  const std::uint64_t lhs=widen?0x3fc00000:UINT64_C(0x3ff8000000000000);
  const std::uint64_t rhs=widen?0x3f000000:UINT64_C(0x3fe0000000000000);
  for(unsigned i=0;i<2;++i) {
    const auto a=lhs+i, b=rhs+i;
    std::memcpy(reinterpret_cast<char*>(&operands_in.pbits.pdata[0])+i*size,&a,size);
    std::memcpy(reinterpret_cast<char*>(&operands_in.pbits.pdata[1])+i*size,&b,size);
  }
  tick_model(); operands_in.pvalid=0;
  for(unsigned t=0;t<5;++t) {
    fp_request_in.pready=t>=3;
    unsigned i=t==4;
    eval(); const auto &r=request.pbits;
    CHECK(request.pvalid && r.ptag==2 && r.pcontrol.pexecution.punit==2 && !read_out.pvalid && !reserve_out.pvalid);
    const auto box=widen?UINT64_C(0xffffffff00000000):0;
    CHECK(r.pleft==(box|(lhs+i)) && r.pright==(box|(rhs+i)));
    CHECK(r.pleft_uprecision==(widen?1:2) && r.pright_uprecision==(widen?1:2) && r.pcontrol.pexecution.pdestination_uprecision==2);
    tick_model();
  }
  const std::uint64_t results[]={UINT64_C(0x4000000000000000),UINT64_C(0x4000000000000001)};
  for(auto value:results) {
    reply.pvalid=1; reply.pbits.ptag=2; reply.pbits.pvalue=value;
    eval(); CHECK(fp_result_out.pready && !request.pvalid && !shared_write_out.pvalid); tick_model();
  }
  reply.pvalid=0;
  for(unsigned t=0;t<5;++t) {
    shared_write_in.pready=t==4; eval();
    CHECK(shared_write_out.pvalid && shared_write_out.pbits.paddress==32 && !read_out.pvalid && !reserve_out.pvalid);
    CHECK(chain_out.pfp_upending.pvalid && chain_out.pfp_upending.pbits.powner==2 && chain_out.pfp_upending.pbits.prow==32);
    CHECK(!std::memcmp(&shared_write_out.pbits.pdata,results,sizeof(results)));
    CHECK(bool(finished_out.pvalid)==(t==4) && (!finished_out.pvalid || finished_out.pbits==2));
    tick_model();
  }
  eval(); CHECK(!chain_out.pfp_upending.pvalid && read_out.pvalid && issued_out.pbits==1);
}

// While the completed FP64 row waits for the VRF, a local FP32 class result
// replaces the live execute context. The adapter must retain its own layout.
static void held_mask_fragment() {
  reset=1; launch_in={}; operands_in={}; fp_result_in={}; chain_in={}; tick_model(); reset=0;
  shared_write_in.pready=0; fp_request_in.pready=read_credit=read_in.pready=fixed_write_credit=reserve_in.pready=1;
  auto &d=launch_in.pbits.pdescriptor; auto &c=d.pcontrol;
  d.pinstruction=8u<<20 | 12u<<15; d.pstate.pvtype=24; d.pstate.pvl=4; d.pstate.pvstart=2; d.pmasked=1;
  c.pfloating_upoint=c.pfp_uservice=c.preads.pvs1=c.preads.pvs2=1;
  c.pcompute_ufamily=FpArithmetic; c.prow_ulayout=ElementsToMask; c.presult_ukind=1;
  c.pfp.presult=2; c.pfp.pexecution.punit=7;
  launch_in.pbits.pcontext=1; launch_in.pvalid=1; eval(); CHECK(launch_out.pready); tick_model(); launch_in.pvalid=0;
  eval(); CHECK(read_out.pvalid && issued_out.pvalid && !reserve_out.pvalid); tick_model();
  operands_in.pvalid=1;
  const std::uint64_t src[]={UINT64_C(0x7ff0000000000001),UINT64_C(0x3ff0000000000000)};
  std::memcpy(&operands_in.pbits.pdata[0],src,bytes);
  operands_in.pbits.ppredicate={}; reinterpret_cast<unsigned char*>(&operands_in.pbits.ppredicate)[0]=8;
  tick_model(); operands_in.pvalid=0;
  eval(); CHECK(fp_request_out.pvalid && fp_request_out.pbits.pleft==src[1]); tick_model();
  fp_result_in.pvalid=1; fp_result_in.pbits.ptag=1; fp_result_in.pbits.pvalue=1;
  fp_result_in.pbits.pexception_uflags_uvalid=1; eval(); CHECK(fp_result_out.pready && !fp_request_out.pvalid);
  tick_model(); fp_result_in.pvalid=0;
  d={}; d.pinstruction=8u<<20 | 24u<<7; d.pstate.pvtype=16; d.pstate.pvl=1;
  c.pfloating_upoint=c.pfp_uservice=c.preads.pvs2=1; c.pcompute_ufamily=FpArithmetic;
  c.prow_ulayout=ElementRows; c.pfp.pexecution.punit=10; c.pfp.presult=1;
  c.pfp.plocal_uresult=DirectNoFlags;
  launch_in.pbits.pcontext=2; launch_in.pvalid=1;
  for(unsigned t=0;t<7;++t) {
    shared_write_in.pready=t==6; operands_in.pvalid=t==2;
    operands_in.pbits.pdata[0]={}; // +0 classifies as bit 4, not a mask bit.
    eval(); CHECK(!fp_request_out.pvalid);
    if(!t) CHECK(launch_out.pready);
    CHECK(bool(read_out.pvalid)==(t==1) && bool(write_out.pvalid)==(t==3));
    if(t==3) {
      unsigned data,mask; std::memcpy(&data,&write_out.pbits.pdata,4); std::memcpy(&mask,&write_out.pbits.pmask,4);
      CHECK(data==16 && mask==UINT32_MAX && write_out.pbits.paddress==48);
    }
    CHECK(shared_write_out.pvalid && shared_write_out.pbits.paddress==0);
    for(unsigned i=0;i<bytes;++i) {
      const unsigned m=reinterpret_cast<const unsigned char*>(&shared_write_out.pbits.pmask)[i];
      CHECK(m==(i?0:8) && (reinterpret_cast<const unsigned char*>(&shared_write_out.pbits.pdata)[i]&m)==m);
    }
    CHECK(chain_out.pfp_upending.pvalid && chain_out.pfp_upending.pbits.powner==1 && chain_out.pfp_upending.pbits.prow==0);
    CHECK(bool(finished_out.pvalid)==(t==3 || t==6));
    if(finished_out.pvalid) CHECK(finished_out.pbits==(t==3?2:1));
    CHECK(bool(fp_update_out.pvalid)==(t==6) && (!fp_update_out.pvalid || !fp_update_out.pbits));
    tick_model(); launch_in.pvalid=0;
  }
  eval(); CHECK(!chain_out.pfp_upending.pvalid && !shared_write_out.pvalid);
}

int main() {
  return run_test([] {
    if constexpr(bytes==16) held_mask_fragment();
    if constexpr(bytes==16) for(bool widen:{false,true}) pipelined_double_row(fp_request_out,fp_result_in,widen);
    scan_completion_contest(false,false);
    for(bool ready:{false,true}) scan_completion_contest(true,ready);
    shared_tail_credit(false); shared_tail_credit(true);
    overlap_divides(false);
    overlap_divides(true);
    for(unsigned sew=1;sew<=(bytes==16?3u:2u);++sew)
      for(bool empty:{false,true}) for(bool sqrt:{false,true}) iterative_fp_row(sew,empty,sqrt);
    for(unsigned m=0;m<=5;++m) fp_fold(m);
    fp_fold(0,true); if constexpr(bytes==16) double_fold();
    mixed_rows(true); mixed_rows(false);
    fp_family(1); fp_family(2); if constexpr(bytes==16) fp_family(3);
    for(auto role:{VdAddend,VdFactor}) for(bool scalar:{false,true})
      for(bool subtract:{false,true}) delayed_mac(role,scalar,subtract);
    for(bool empty:{false,true}) {
      for(bool fractional:{false,true}) scalar_integer_claim(empty,fractional);
      scalar_integer_claim(empty,false,true);
    }
    reset = 1; tick_model(); reset = 0;
    read_credit = read_in.pready = 1;
    fixed_write_credit=reserve_in.pready=1;
    unsigned reads = 0, writes = 0;
    std::uint64_t first_read = 0;
    bool returning = false;

    // A synchronous VRF returns two distinct sources and a changing v0.
    // Owner 3 adds vv with a masked partial range. Owner 0 shifts by a scalar.
    auto step = [&] {
      operands_in.pvalid = returning;
      if (returning) {
        unsigned r = reads - 1;
        auto& data = operands_in.pbits.pdata;
        std::memset(&data[0], 0x20+r, bytes);
        std::memset(&data[1], 0x93, bytes);
        std::memset(&operands_in.pbits.ppredicate, r == 0 ? 0x55 : 0xaa, bytes);
      }
      eval();
      // Check before every edge, including the read-return cycle before a
      // result becomes writable, the handoff edge, and all stalled cycles.
      for (unsigned group = 0; group < 2; ++group) {
        const unsigned owner = group ? 0 : 3, base = group ? 32 : 24;
        std::uint64_t expected = 0;
        for (unsigned i = writes; i < reads; ++i)
          if (i/2 == group) expected |= UINT64_C(1) << (base + i%2);
        CHECK(held(owner) == expected);
      }
      if (first_read && reads < 4) {
        auto remaining = reads % 2 ? 2u : 3u;
        const auto& c = chain_out.punissued;
        CHECK(c.preads == ((std::uint64_t(remaining) << (reads < 2 ? 8 : 16)) |
                           (reads < 2 ? std::uint64_t(remaining) << 12 : 0)));
        CHECK(c.ppredicates == (reads < 2 ? 1u : 0u));
        CHECK(c.pwrites == (std::uint64_t(remaining) << (reads < 2 ? 24 : 32)));
      }
      if (reads == 4)
        CHECK(!(chain_out.punissued.preads | chain_out.punissued.ppredicates | chain_out.punissued.pwrites));
      const bool take_read = read_out.pvalid && read_in.pready;
      const bool take_write = write_out.pvalid;
      CHECK(bool(issued_out.pvalid) == (take_read && reads % 2 == 1));
      if (issued_out.pvalid) CHECK(issued_out.pbits == (reads < 2 ? 3 : 0));
      if (take_read) {
        CHECK(reads < 4 && read_out.pbits.pleft == (reads < 2 ? 8 : 16) + reads % 2);
        if (!reads) first_read = test_cycles;
        CHECK(bool(read_out.pbits.psources.pvector_urhs) == (reads < 2));
        CHECK(bool(read_out.pbits.preads_uv0) == (reads < 2));
      }
      CHECK(bool(finished_out.pvalid) == (take_write && writes % 2 == 1));
      if (first_read && !writes) CHECK(bool(write_out.pvalid) == (test_cycles >= first_read+2));
      if (write_out.pvalid) {
        unsigned row = (writes < 2 ? 24 : 32) + writes % 2;
        unsigned owner = writes < 2 ? 3 : 0;
        CHECK(write_out.pbits.paddress == row && (held(owner) >> row & 1));
        if (finished_out.pvalid) CHECK(finished_out.pbits == owner);
        auto* data = reinterpret_cast<const unsigned char*>(&write_out.pbits.pdata);
        auto* mask = reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask);
        for (unsigned b = 0; b < bytes; ++b) {
          bool enabled = writes >= 2 || ((b % 2 == writes) && (writes || b > 0) && (!writes || b+1 < bytes));
          CHECK(mask[b] == (enabled ? 255 : 0));
          if (enabled) CHECK(data[b] == (writes < 2 ? (0x20+writes)+0x93 : (0x20+writes)>>2));
        }
      }
      reads += take_read; writes += take_write; returning = take_read;
      tick_model();
    };
    auto launch = [&](bool second) {
      auto& d = launch_in.pbits.pdescriptor; d = {};
      launch_in.pbits.pcontext = second ? 0 : 3;
      d.pstate.pvl = 2*bytes - !second; d.pstate.pvstart = !second;
      d.pmasked = !second; d.pscalar = 0x7a;
      d.pcontrol.preads.pvs2=1; d.pcontrol.preads.pvs1=!second;
      d.pcontrol.poperand = second ? 1 : 0; // scalar / vector
      d.pcontrol.pinteger_urow.palu.presult_uselect = second ? 2 : 0; // shift / adder
      d.pcontrol.pinteger_urow.palu.pshift_uright = second;
      d.pinstruction = ((second ? 8u : 4u)<<20) | (6u<<15) | ((second ? 16u : 12u)<<7);
      launch_in.pvalid = 1;
      if (second) {
        read_credit = read_in.pready = 0;
        for (unsigned i=0; i<3; ++i) {
          eval(); CHECK(read_out.pvalid && !issued_out.pvalid && !launch_out.pready);
          step();
        }
        read_credit = read_in.pready = 1;
      }
      eval(); CHECK(launch_out.pready);
      if (second) CHECK(issued_out.pvalid && issued_out.pbits==3);
      step(); launch_in.pvalid = 0;
    };
    launch(false);
    step(); CHECK(reads == 1 && writes == 0);
    launch(true); // The old owner's writes continue during a read-port stall.
    eval(); CHECK(read_out.pvalid && chain_out.powner==0);
    for (unsigned i=0; i<6; ++i) step();
    eval(); CHECK(reads == 4 && writes == 4 && !write_out.pvalid && !finished_out.pvalid);

    // FP flags and masked data mature together on the promised write edge.
    constexpr unsigned lanes = bytes / sizeof(std::uint32_t);
    auto &d = launch_in.pbits.pdescriptor;
    constexpr unsigned fp_rows=16;
    d = {}; d.pstate.pvl = fp_rows*lanes; d.pstate.pvtype = 19; // e32,m8
    d.pcontrol.pcompute_ufamily=FpArithmetic;
    d.pmasked = 1; d.pcontrol.pfloating_upoint = d.pcontrol.pfp_uservice = d.pcontrol.preads.pvs2 = d.pcontrol.preads.pvs1 = 1;
    d.pinstruction = (8u<<20) | (16u<<15) | (24u<<7);
    auto fp_step = [&] {
      operands_in.pvalid = returning;
      std::array<std::uint32_t,lanes> a{}, b{}, c{};
      for (unsigned i=0;i<lanes;++i) {
        a[i] = i%2 ? 0x7f800001 : 0x3f800000; b[i]=0x33800000;
        c[i]=0x3f800000;
      }
      std::memcpy(&operands_in.pbits.pdata[0],a.data(),bytes);
      std::memcpy(&operands_in.pbits.pdata[1],b.data(),bytes);
      std::memcpy(&operands_in.pbits.pdata[2],c.data(),bytes);
      std::memset(&operands_in.pbits.ppredicate,0x55,bytes);
      eval();
      returning = read_out.pvalid && read_in.pready;
      if (returning && d.pcontrol.pfp.pexecution.punit==1) {
        CHECK(read_out.pbits.psources.paccumulator && read_out.pbits.pthird==48+reads);
        CHECK(chain_out.punissued.preads & (UINT64_C(1)<<(48+reads)));
      }
      const bool accepted = write_out.pvalid;
      CHECK(bool(fp_update_out.pvalid)==accepted);
      if (write_out.pvalid) {
        CHECK(write_out.pbits.paddress==48+writes && (held(2) >> (48+writes) & 1));
        std::array<std::uint32_t,lanes> data,mask;
        std::memcpy(data.data(),&write_out.pbits.pdata,bytes);
        std::memcpy(mask.data(),&write_out.pbits.pmask,bytes);
        for (unsigned i=0;i<lanes;++i) {
          CHECK(mask[i]==(i%2 ? 0 : UINT32_MAX));
          if (mask[i]) CHECK(data[i]==0x3f800001);
        }
        CHECK(fp_update_out.pbits==1); // NX, never NV
      }
      CHECK(bool(finished_out.pvalid)==(accepted && writes+1==fp_rows));
      reads+=returning; writes+=accepted; tick_model();
    };
    for (unsigned unit : {2u,1u}) { // add and fused multiply-accumulate, RUP
      d.pcontrol.preads.pold_uvd=unit==1; d.pcontrol.pfp.pexecution.punit=unit; d.prounding_umode=3;
      launch_in.pbits.pcontext=2; fixed_write_credit=reserve_in.pready=0; launch_in.pvalid=1;
      eval(); CHECK(launch_out.pready); tick_model(); launch_in.pvalid=0;
      d.prounding_umode=0; launch_in.pbits.pcontext=1; // input no longer owns these rows
      reads=writes=0; returning=false;
      for (unsigned i=0;i<12;++i) fp_step();
      CHECK(!reads && !writes);
      fixed_write_credit=reserve_in.pready=1;
      for (unsigned i=0;i<fp_rows+4;++i) fp_step();
      CHECK(reads==fp_rows && writes==fp_rows);
    }

    // Two products book t=6,7. The first add finishes at t=5; its sibling
    // cannot book either product edge, and finishes at t=8 instead.
    auto mixed_writeback = [&](bool mac) {
      d={}; d.pstate.pvl=2*bytes; d.pmasked=1;
      d.pcontrol.pcompute_ufamily=IntProduct;
      d.pcontrol.pmultiply_udivide=1;
      d.pcontrol.preads.pvs1=d.pcontrol.preads.pvs2=1;
      d.pcontrol.preads.pold_uvd=mac; d.pcontrol.pproduct.pvd_urole=mac ? VdAddend : VdUnused;
      d.pcontrol.pproduct.pselect=ProductLow;
      d.pinstruction=(4u<<20)|(6u<<15)|(12u<<7);
      launch_in.pbits.pcontext=1; fixed_write_credit=reserve_in.pready=1; launch_in.pvalid=1;
      eval(); CHECK(launch_out.pready); tick_model(); launch_in.pvalid=0;
      std::array<bool,16> promised{};
      unsigned count=0;
      for (unsigned t=0;t<10;++t) {
        if (t==1) {
          d={}; d.pcontrol.preads.pvs1=d.pcontrol.preads.pvs2=1; d.pstate.pvl=2*bytes; d.pinstruction=(8u<<20)|(10u<<15)|(16u<<7);
          launch_in.pbits.pcontext=2; launch_in.pvalid=1;
        } else launch_in.pvalid=0;
        read_credit=read_in.pready=t!=2;
        // Like the calendar, answer the *requested* future edge from older
        // accepted bookings. Do not manufacture stalls on predetermined cycles.
        eval();
        const unsigned due=t+reserve_out.pbits;
        CHECK(due<promised.size());
        fixed_write_credit=reserve_in.pready=!promised[due];
        if(t==1) {
          fixed_write_credit=0; eval(); CHECK(read_out.pvalid && !launch_out.pready);
          fixed_write_credit=1;
        }
        operands_in.pvalid=t==1 || t==2 || t==4 || t==7;
        std::memset(&operands_in.pbits.pdata[0],0x21+t,bytes);
        std::memset(&operands_in.pbits.pdata[1],3,bytes);
        std::memset(&operands_in.pbits.pdata[2],0x51+t,bytes);
        std::memset(&operands_in.pbits.ppredicate,t==1 ? 0x55 : 0xaa,bytes);
        eval();
        if (t==1) CHECK(launch_out.pready);
        CHECK(bool(reserve_out.pvalid)==(t<=1 || t==3 || t==6));
        if(t==4 || t==5) CHECK(promised[due] && !read_out.pvalid);
        if(reserve_out.pvalid) {
          CHECK(!promised[due] && reserve_out.pbits==(t<=1 ? 6u : 2u));
          promised[due]=true;
        }
        CHECK(!fp_update_out.pvalid);
        CHECK(bool(write_out.pvalid)==promised[t]);
        if (write_out.pvalid) {
          const unsigned row[]={32,24,25,33}, owner[]={2,1,1,2};
          CHECK(write_out.pbits.paddress==row[count] && (held(owner[count])>>row[count] & 1));
          auto* data=reinterpret_cast<const unsigned char*>(&write_out.pbits.pdata);
          auto* mask=reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask);
          for (unsigned b=0;b<bytes;++b) {
            const bool product=count==1 || count==2;
            bool enabled=!product || b%2==count-1;
            CHECK(mask[b]==(enabled ? 255 : 0));
            if (enabled) CHECK(data[b]==(product ? 3*(0x21+count)+(mac ? 0x51+count : 0) : count ? 0x2b : 0x28));
          }
          CHECK(bool(finished_out.pvalid)==(count>=2));
          if (finished_out.pvalid) CHECK(finished_out.pbits==owner[count]);
          ++count;
        }
        tick_model();
      }
      CHECK(count==4);
    };
    mixed_writeback(false);
    mixed_writeback(true);
    mac_stream(false);
    mac_stream(true);
    widening_last_use(0);
    widening_last_use(bytes/2+1);
    widening_last_use(bytes/2+1,false,true); // vwadd.wv: stall the final reuse of narrow VS1.
    widening_last_use(0,true);
    widening_last_use(bytes/2+1,true);
    fractional_reservations();
    saturating_reservations(false);
    saturating_reservations(true);
    for(bool clip:{false,true}) narrowing_claims(clip);
    // Probe the isolated row cursor with identical cross-row bounds at both SEWs.
    // The e16 crossing is a geometry probe, not an architecturally legal VL.
    for(unsigned sew : {0u,1u}) mask_claims(sew,3,5,16*bytes-3);
    mask_claims(0,3,8*bytes+3,8*bytes+9);
    // At e16,m8 the largest legal VL ends exactly at the DLEN mask boundary.
    for(unsigned sew : {0u,1u}) mask_claims(sew,3,5,8*bytes);
    mask_claims(1,3,5,8*bytes-3);
    mask_claims(2,0,1,bytes/2);
    for(unsigned sew:{1u,2u}) mask_fragments(sew,1,(16*bytes>>sew)-1,true,Fragment::FloatCompare);
    mask_fragments(0,3,16*bytes-3,false);
    mask_fragments(0,8*bytes+2,12*bytes-1,true);
    for(unsigned sew=1;(1u<<sew)<=bytes/2;++sew)
      mask_fragments(sew,1,(16*bytes>>sew)-1,true);
    for(auto kind : {Fragment::Carry,Fragment::Borrow}) for(bool input : {false,true})
      for(unsigned sew=0;(1u<<sew)<=bytes/2;++sew)
        mask_fragments(sew,1,(16*bytes>>sew)-1,true,kind,input);
  });
}
