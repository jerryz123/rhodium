// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <cstring>

constexpr unsigned bytes=sizeof(memory_in.presponse.pbits.pdata), vs3=8;
using Row=std::array<unsigned char, bytes>;

void exercise(unsigned displacement, unsigned first, unsigned limit, unsigned eew) {
  const unsigned base=0x4000-bytes+displacement, begin=base+(first<<eew), end=base+(limit<<eew);
  const unsigned beat0=begin & ~(bytes-1), beats=(end-1-beat0)/bytes+1;
  const unsigned first_row=(begin-base)/bytes, final_row=(end-base-1)/bytes;
  auto original=[](unsigned offset) { return (17*offset+7)&255; };
  std::array<Row,2> vrf{};
  for (unsigned i=0;i<2*bytes;++i) vrf[i/bytes][i%bytes]=original(i);
  std::array<unsigned char,3*bytes> ram;
  ram.fill(0xc5);
  prepare_in={}; launch_1_in={}; launch_0_in={}; memory_in={}; read_result_in={}; store_reads_in={};
  auto &d=launch_1_in.pbits.pissue.pdescriptor;
  d.pscalar=base; d.pinstruction=vs3<<7; d.pcontrol.pstore=1; d.pcontrol.pmemory_udata_ueew=eew;
  d.pstate.pvstart=first; d.pstate.pvl=limit; d.pstate.pvtype=eew<<3;
  launch_1_in.pbits.pissue.powner=3; prepare_in.pbits=launch_1_in.pbits.pissue;
  launch_1_in.pbits.ppages[0]=begin<0x4000 ? 0x60 : 0x90; launch_1_in.pbits.ppages[1]=0x90;
  launch_1_in.pvalid=prepare_in.pvalid=1; eval(); CHECK(launch_1_out.pready && prepare_out.pready);
  tick_model(); launch_1_in.pvalid=prepare_in.pvalid=0;
  launch_0_in.pbits=launch_1_in.pbits; launch_0_in.pbits.pissue.pdescriptor.pcontrol.pstore=0; // Probe readiness for a physically aliasing load.
  unsigned reads=0,accepted=0,acks=0,tails=0,done=0,stalls=0,read_row=0;
  bool returning=false,stalled=false;
  Row held{};
  std::uint64_t claims=0;
  for (unsigned r=first_row;r<=final_row;++r) claims|=UINT64_C(1)<<(2*vs3+r);
  for (unsigned t=0;t<160 && !done;++t) {
    read_in.pready=1;
    store_reads_in.polder_uwrites=t<18 ? UINT64_C(1)<<(2*vs3+1) : 0;
    read_result_in.pvalid=returning;
    std::memcpy(&read_result_in.pbits,vrf[read_row].data(),bytes);
    memory_in.prequest.pready=t>=30 && t%5==0;
    memory_in.presponse.pvalid=t>=60 && t%7==0 && acks<accepted;
    memory_in.presponse.pbits.pstore=1;
    eval(); CHECK(store_reads_out.punread_uvs3==claims && !store_reads_out.punread_uv0);
    CHECK(!launch_0_out.pready && !mask_1_out.psample.pvalid);
    if (returning) claims &= ~(UINT64_C(1)<<(2*vs3+read_row));
    if (t==10 && first_row==0) {
      // A younger writer can now overwrite the captured source. Row 1 is
      // still RAW-blocked and L1D has not accepted anything.
      CHECK(!accepted && reads==1 && !(claims & (UINT64_C(1)<<(2*vs3))));
      vrf[0].fill(0xee);
    }
    returning=read_out.pvalid && read_in.pready;
    if (returning) {
      read_row=read_out.pbits-2*vs3;
      CHECK(read_row==first_row+reads && read_row<=final_row);
      ++reads;
    }
    const auto &req=memory_out.prequest.pbits;
    if (stalled) CHECK(memory_out.prequest.pvalid && !std::memcmp(&req.pdata,held.data(),bytes));
    stalled=memory_out.prequest.pvalid && !memory_in.prequest.pready;
    if (memory_out.prequest.pvalid) {
      const unsigned va=beat0+accepted*bytes;
      CHECK(accepted<beats && req.pstore && req.paddress==((va<0x4000 ? 0x60000u : 0x90000u)+(va&4095)));
      unsigned mask=0; Row data;
      std::memcpy(data.data(),&req.pdata,bytes);
      for (unsigned i=0;i<bytes;++i) if (va+i>=begin && va+i<end) {
        mask|=1u<<i; CHECK(data[i]==original(va+i-base));
        if (memory_in.prequest.pready) ram[accepted*bytes+i]=data[i];
      }
      CHECK(req.pbyte_umask==mask && mask);
      if (stalled) { held=data; ++stalls; } else ++accepted;
    }
    acks+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    CHECK(accepted-acks<=5);
    if (issued_out.pvalid) { CHECK(accepted==beats && issued_out.pbits.powner==3); ++tails; }
    if (finished_out.pvalid) { CHECK(acks==beats && finished_out.pbits.powner==3); ++done; }
    tick_model();
  }
  eval(); CHECK(launch_0_out.pready);
  CHECK(stalls && tails==1 && done==1 && !claims && reads==final_row-first_row+1);
  for (unsigned i=0;i<ram.size();++i)
    CHECK(ram[i]==(beat0+i>=begin && beat0+i<end ? original(beat0+i-base) : 0xc5));
}

enum class Boundary { Open, DenyPort, BlockWriter };

static void segmented_store(unsigned fields,unsigned eew,unsigned lmul=0,Boundary boundary=Boundary::Open,bool two_groups=false) {
  // Block the second field, then L1D. Each field has an independent last
  // capture, even when the first field in a beat rotates.
  prepare_in={}; launch_1_in={}; launch_0_in={}; memory_in={}; read_result_in={}; store_reads_in={};
  auto &d=launch_1_in.pbits.pissue.pdescriptor;
  const unsigned size=1u<<eew, lanes=bytes/size, visits=std::min(lanes,fields);
  const unsigned field_rows=2u<<lmul;
  const unsigned beats=fields*(two_groups ? 2 : 1);
  auto address=[&](unsigned visit) {
    const unsigned beat=visit/visits, field=(beat*lanes+visit%visits)%fields;
    return 2*vs3+field_rows*field+beat/fields;
  };
  d.pstate.pvl=(beats/fields)*lanes; d.pstate.pvtype=(eew<<3)|lmul; d.pcontrol.pmemory_udata_ueew=eew; d.pcontrol.pstore=1; d.pcontrol.pmemory_unf=fields-1;
  d.pscalar=0x4000-bytes; d.pinstruction=vs3<<7;
  launch_1_in.pbits.pissue.powner=2; prepare_in.pbits=launch_1_in.pbits.pissue;
  launch_1_in.pbits.ppages[0]=0x60; launch_1_in.pbits.ppages[1]=0x90;
  launch_1_in.pvalid=prepare_in.pvalid=1; tick_model(); launch_1_in.pvalid=prepare_in.pvalid=0;
  // Only the second beat aliases this following load via a different VA.
  auto &probe=launch_0_in.pbits.pissue.pdescriptor; probe={}; probe.pscalar=0x8000; probe.pstate.pvl=1;
  launch_0_in.pbits.ppages[0]=0x90;
  unsigned captures=0,sent=0,acks=0,finishes=0,stalls=0,read_row=0,last_grant=0;
  bool returning=false,saw_denial=false,saw_no_credit=false;
  for(unsigned t=0;t<300 && !finishes;++t) {
    const bool first_boundary=returning && captures+1==visits;
    read_in.pready=!(first_boundary && boundary==Boundary::DenyPort);
    store_reads_in.polder_uwrites=t<15 ? UINT64_C(1)<<(16+field_rows) : 0;
    if(first_boundary && boundary==Boundary::BlockWriter)
      store_reads_in.polder_uwrites |= UINT64_C(1)<<address(visits);
    read_result_in.pvalid=returning;
    Row data;
    for(unsigned b=0;b<bytes;++b) data[b]=(0x11+((read_row-16)/field_rows)*0x19+((read_row-16)%field_rows)*bytes+b)&255;
    if(captures>=beats*visits) data.fill(0xcc);
    std::memcpy(&read_result_in.pbits,data.data(),bytes);
    memory_in.prequest.pready=two_groups || (t>=45 && t%4==0);
    memory_in.presponse.pvalid=acks<sent && (two_groups || (t>=75 && t%3==0));
    memory_in.presponse.pbits.pstore=1;
    eval();
    std::uint64_t claims=0;
    for(unsigned i=captures;i<beats*visits;++i) claims |= UINT64_C(1)<<address(i);
    CHECK(store_reads_out.punread_uvs3==claims);
    CHECK(!launch_0_out.pready);
    if(t==12) CHECK(captures==1 && sent==0);
    if(t==35 && fields==2) CHECK(captures==4 && sent==0);
    if(first_boundary) {
      CHECK(bool(read_out.pvalid)==(boundary!=Boundary::BlockWriter));
      if(read_out.pvalid) CHECK(read_out.pbits==address(visits));
      saw_denial=!read_out.pvalid || !read_in.pready;
    }
    if(saw_denial && captures==visits && !returning) CHECK(read_out.pvalid && read_in.pready && read_out.pbits==address(visits));
    // The first two beats fill both FIFO slots while L1D is stopped. Do not
    // even offer the third beat's first read on the second beat's final return.
    if(!two_groups && fields>2 && t<45 && captures+unsigned(returning)==2*visits) {
      CHECK(!sent && !read_out.pvalid);
      saw_no_credit=true;
    }
    if(two_groups && returning && captures+1==fields*visits)
      CHECK(read_out.pvalid && read_in.pready && read_out.pbits==address(fields*visits));
    if(returning) ++captures;
    returning=read_out.pvalid && read_in.pready;
    if(returning) {
      read_row=read_out.pbits;
      CHECK(read_row==address(captures));
      if(captures%visits && captures!=1) CHECK(t==last_grant+1);
      last_grant=t;
    }
    if(memory_out.prequest.pvalid) {
      const auto &req=memory_out.prequest.pbits;
      CHECK(req.pstore && req.paddress==(sent ? 0x90000u+(sent-1)*bytes : 0x61000u-bytes));
      CHECK(req.pbyte_umask==((1u<<bytes)-1));
      Row actual; std::memcpy(actual.data(),&req.pdata,bytes);
      for(unsigned b=0;b<bytes;++b) {
        const unsigned slot=(sent*bytes+b)/size;
        CHECK(actual[b]==((0x11+(slot%fields)*0x19+(slot/fields)*size+b%size)&255));
      }
      if(memory_in.prequest.pready) ++sent; else ++stalls;
    }
    acks+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    finishes+=finished_out.pvalid; tick_model();
  }
  CHECK(finishes==1 && sent==beats && acks==beats && (two_groups || stalls) && captures==beats*visits);
  CHECK(saw_denial==(boundary!=Boundary::Open));
  CHECK(saw_no_credit==(!two_groups && fields>2));
}

// With three fields and two element lanes per beat, the middle beat wraps
// from field 2 of element 0 to field 0 of element 1. Only one lane is live.
static void masked_segment(bool second,bool late_mask=false) {
  constexpr unsigned size=bytes/2, eew=bytes==8 ? 2 : 3, first_row=2*vs3;
  constexpr unsigned lo=(1u<<size)-1, hi=lo<<size, full=lo|hi;
  const unsigned blocked=second ? 2 : 0;
  Row predicate{}; predicate[0]=1u<<second;
  std::memcpy(&mask_1_in.pdata,predicate.data(),bytes);
  prepare_in={}; launch_0_in={}; launch_1_in={}; memory_in={}; read_result_in={}; store_reads_in={};
  auto &d=launch_1_in.pbits.pissue.pdescriptor;
  d.pscalar=0x4000; d.pinstruction=vs3<<7; d.pcontrol.pstore=1; d.pmasked=1;
  d.pstate.pvl=2; d.pstate.pvtype=eew<<3; d.pcontrol.pmemory_udata_ueew=eew; d.pcontrol.pmemory_unf=2;
  launch_1_in.pbits.ppages[0]=0x60; prepare_in.pbits=launch_1_in.pbits.pissue;
  launch_1_in.pvalid=prepare_in.pvalid=1; tick_model(); launch_1_in.pvalid=prepare_in.pvalid=0;
  unsigned grants=0,returns=0,samples=0,sent=0,acks=0,done=0,field=0;
  bool returning=false, denied=false, saw_claims=false, boundary=false, overwritten=false;
  for(unsigned t=0;t<120 && !done;++t) {
    // In the middle beat the blocked row is masked off. In the last beat it
    // is either genuinely needed (second) or the entire beat is empty.
    store_reads_in.polder_uwrites=t<25 && (second || returns>=1) ? 1ull<<(first_row+2*blocked) : 0;
    const bool wait_v0=t<2 || (late_mask && samples==1 && t<12);
    if(wait_v0) store_reads_in.polder_uwrites|=1;
    read_in.pready=denied || grants!=1; // deny one live offer, including across another field's return
    read_result_in.pvalid=returning;
    Row src; for(unsigned b=0;b<bytes;++b) src[b]=0x10*field+b;
    std::memcpy(&read_result_in.pbits,src.data(),bytes);
    memory_in.prequest.pready=t>=40;
    memory_in.presponse.pvalid=t>=55 && acks<sent;
    memory_in.presponse.pbits.pstore=1;
    eval();
    const bool sample=mask_1_out.psample.pvalid;
    CHECK(store_reads_out.punread_uv0==(samples<3 ? 1u : 0u));
    if(wait_v0) CHECK(!sample);
    if(returning && grants==(second ? 1u : 2u)) {
      // Rotating successor starts with field 2 or 1. The port denies field 1
      // on this exact edge; a late predicate must not delay the outgoing beat.
      CHECK(!boundary && bool(read_out.pvalid)==!late_mask);
      if(!late_mask) CHECK(samples>=(second ? 3u : 2u) && read_out.pbits==first_row+2*grants);
      boundary=true;
    }
    if(t==20) {
      CHECK(!sent && overwritten && returns==(second ? 2u : 3u));
      // The final v0 sample is independent of FIFO space. The empty final
      // beat releases its VS3 obligations only when it takes its ordered slot.
      const auto tail_claims=(1ull<<(first_row+2)) | (1ull<<(first_row+4));
      CHECK(store_reads_out.punread_uvs3==(second ? 1ull<<(first_row+4) : tail_claims));
      saw_claims=true;
    }
    if(sample) { CHECK(samples<3 && mask_1_out.psample.pbits==0); ++samples; }
    if(returning) ++returns;
    returning=read_out.pvalid && read_in.pready;
    if(read_out.pvalid) {
      CHECK(grants<3 && read_out.pbits==first_row+2*grants);
      if(returning) field=grants++;
      else denied=true;
    }
    if(memory_out.prequest.pvalid) {
      const auto &req=memory_out.prequest.pbits;
      unsigned beat=sent+second, mask=beat==1 ? (second ? hi : lo) : full;
      CHECK(sent<2 && req.pstore && req.paddress==0x60000+beat*bytes && req.pbyte_umask==mask);
      Row data; std::memcpy(data.data(),&req.pdata,bytes);
      for(unsigned b=0;b<bytes;++b) if(mask & (1u<<b)) {
        const unsigned slot=2*beat+b/size;
        CHECK(data[b]==0x10*(slot%3)+size*second+b%size);
      }
      sent+=memory_in.prequest.pready;
    }
    acks+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    if(finished_out.pvalid) { CHECK(acks==2); ++done; }
    tick_model();
    if(sample && samples==3) {
      CHECK(!sent && !memory_in.prequest.pready);
      predicate[0]^=3; // A younger v0 write after last use must not change the retained byte enables.
      std::memcpy(&mask_1_in.pdata,predicate.data(),bytes);
      overwritten=true;
    }
  }
  CHECK(done && denied && boundary && overwritten && saw_claims && grants==3 && returns==3 && samples==3);
}

// Each beat visits four of five fields. The 8th-to-9th visit crosses a
// boundary on two skips: neither an older writer nor VRF denial may stall it.
static void masked_boundary_skip() {
  constexpr unsigned size=bytes/4, eew=bytes==8 ? 1 : 2;
  prepare_in={}; launch_0_in={}; launch_1_in={}; memory_in={}; read_result_in={}; store_reads_in={};
  auto &d=launch_1_in.pbits.pissue.pdescriptor;
  d.pscalar=0x4000; d.pinstruction=vs3<<7; d.pcontrol.pstore=1; d.pmasked=1; d.pcontrol.pmemory_unf=4;
  d.pstate.pvl=3; d.pstate.pvtype=eew<<3; d.pcontrol.pmemory_udata_ueew=eew;
  launch_1_in.pbits.ppages[0]=0x60; prepare_in.pbits=launch_1_in.pbits.pissue;
  Row pred{}; pred[0]=5; std::memcpy(&mask_1_in.pdata,pred.data(),bytes);
  launch_1_in.pvalid=prepare_in.pvalid=1; tick_model(); launch_1_in.pvalid=prepare_in.pvalid=0;
  unsigned samples=0,slot=0,field=0,sent=0,acks=0; bool returning=false,done=false;
  for(unsigned t=0;t<60 && !done;++t) {
    const bool live=slot<15 && slot/5!=1;
    read_in.pready=slot!=8;
    store_reads_in.polder_uwrites=slot==8 ? 1ull<<(2*(vs3+3)) : 0;
    read_result_in.pvalid=returning;
    Row src; for(unsigned b=0;b<bytes;++b) src[b]=field*16+b;
    std::memcpy(&read_result_in.pbits,src.data(),bytes);
    memory_in.prequest.pready=1; memory_in.presponse.pvalid=acks<sent;
    memory_in.presponse.pbits.pstore=1;
    eval();
    if(t) {
      CHECK(bool(read_out.pvalid)==live);
      if(live) { field=slot%5; CHECK(read_out.pbits==2*(vs3+field) && read_in.pready); }
      ++slot;
    } else CHECK(!read_out.pvalid);
    returning=read_out.pvalid && read_in.pready;
    samples+=mask_1_out.psample.pvalid;
    if(memory_out.prequest.pvalid) {
      const auto &req=memory_out.prequest.pbits;
      CHECK(sent<4 && req.paddress==0x60000+sent*bytes);
      unsigned mask=0;
      for(unsigned b=0;b<bytes;++b) {
        unsigned i=sent*4+b/size;
        if(i<15 && i/5!=1) {
          mask|=1u<<b;
          CHECK(reinterpret_cast<const unsigned char*>(&req.pdata)[b]==(i%5)*16+(i/5)*size+b%size);
        }
      }
      CHECK(req.pbyte_umask==mask); ++sent;
    }
    acks+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    done=finished_out.pvalid;
    tick_model();
  }
  CHECK(done && samples==4 && sent==4 && acks==4);
}

// Six e8 rows start in the last slice of v0 row 0 and finish in row 1.
// Overwrite each shadow row immediately after last use, even with L1D blocked.
static void masked_pipeline(bool stalls) {
  constexpr unsigned first=7, count=6, base=0x60000, full=(1u<<bytes)-1;
  std::array<Row,2> shadow{};
  std::array<unsigned,count> enables{};
  std::vector<unsigned> live;
  for(unsigned i=0;i<count;++i) {
    unsigned bits=(0x5555u << (i%2)) & full;
    if(stalls && (i==2 || i>=4)) bits=0;
    const unsigned r=first+i;
    for(unsigned b=0;b<bytes;++b) if(bits & (1u<<b)) {
      const auto bit=(r%8)*bytes+b;
      shadow[r/8][bit/8] |= 1u<<(bit%8);
    }
    enables[i]=bits & (i ? full : full-1) & (i+1==count ? full>>1 : full);
    if(enables[i]) live.push_back(i);
  }
  prepare_in={}; launch_0_in={}; launch_1_in={}; memory_in={}; read_result_in={}; store_reads_in={};
  auto& d=launch_1_in.pbits.pissue.pdescriptor;
  d.pscalar=0x4000; d.pinstruction=vs3<<7; d.pcontrol.pstore=1; d.pmasked=1;
  d.pstate.pvtype=3; d.pstate.pvstart=first*bytes+1; d.pstate.pvl=(first+count)*bytes-1;
  launch_1_in.pbits.pissue.powner=2; prepare_in.pbits=launch_1_in.pbits.pissue;
  launch_1_in.pbits.ppages[0]=launch_1_in.pbits.ppages[1]=base>>12;
  launch_1_in.pvalid=prepare_in.pvalid=1; eval(); CHECK(prepare_out.pready && launch_1_out.pready);
  tick_model(); launch_1_in.pvalid=prepare_in.pvalid=0;

  unsigned samples=0, reads=0, sent=0, acks=0, done=0, rdata=0;
  bool returning=false;
  for(unsigned t=0;t<100 && !done;++t) {
    store_reads_in.polder_uwrites=stalls ? ((t<3 ? 1u : 0u) | (1ull<<(2*vs3+first+2))) : 0;
    read_in.pready=!stalls || t!=4;
    read_result_in.pvalid=returning;
    Row src; src.fill(0x80+rdata);
    std::memcpy(&read_result_in.pbits,src.data(),bytes);
    memory_in.prequest.pready=!stalls || t>=16;
    memory_in.presponse.pvalid=acks<sent && (!stalls || t>=22);
    memory_in.presponse.pbits.pstore=1;
    eval();
    const unsigned tap=mask_1_out.psample.pbits;
    std::memcpy(&mask_1_in.pdata,shadow[tap].data(),bytes); eval();
    const bool sample=mask_1_out.psample.pvalid, grant=read_out.pvalid && read_in.pready;
    CHECK(store_reads_out.punread_uv0 == (samples==0 ? 3u : samples<count ? 2u : 0u));
    if(!stalls && t>0 && t<=count) CHECK(grant);
    if(stalls && t<3) CHECK(!sample && !grant);
    if(stalls && t==4) CHECK(!sample && read_out.pvalid && !grant);
    if(stalls && t>=8 && t<16) CHECK(reads==2 && !read_out.pvalid && !sent);
    if(sample) {
      CHECK(samples<count && tap==(first+samples)/8);
      ++samples;
    }
    if(read_out.pvalid) CHECK(reads<live.size() && read_out.pbits==2*vs3+first+live[reads]);
    if(grant) rdata=live[reads++];
    if(memory_out.prequest.pvalid) {
      CHECK(sent<live.size());
      const auto i=live[sent]; const auto& req=memory_out.prequest.pbits;
      CHECK(req.pstore && req.paddress==base+(first+i)*bytes && req.pbyte_umask==enables[i]);
      for(unsigned b=0;b<bytes;++b) if(enables[i] & (1u<<b))
        CHECK(reinterpret_cast<const unsigned char*>(&req.pdata)[b]==0x80+i);
      sent+=memory_in.prequest.pready;
    }
    acks+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    if(finished_out.pvalid) { CHECK(acks==live.size() && finished_out.pbits.powner==2); ++done; }
    tick_model(); returning=grant;
    if(sample && (samples==1 || samples==count)) shadow[tap].fill(0xff);
  }
  CHECK(done && samples==count && reads==live.size() && sent==reads);
}

int main() {
  return run_test([] {
    reset=1; tick_model(); reset=0;
    segmented_store(2,1,1); // Short m2 fields: read v8 and v10, never v9 or v11.
    for(unsigned n=2;n<=8;++n)
      for(unsigned e=0;(1u<<e)<=bytes/2;++e) segmented_store(n,e);
    // Two lanes and three fields rotate the first VS3 field 0 -> 2 -> 1.
    constexpr unsigned two_lanes_eew=bytes==8 ? 2 : 3;
    segmented_store(3,two_lanes_eew,0,Boundary::DenyPort);
    segmented_store(3,two_lanes_eew,0,Boundary::BlockWriter);
    segmented_store(3,two_lanes_eew,0,Boundary::Open,true);
    masked_segment(false);
    masked_segment(true);
    masked_segment(false,true);
    masked_boundary_skip();
    for (unsigned d=1;d<bytes;++d) {
      exercise(d,0,2*bytes,0);
      exercise(d,bytes-1,bytes+1,0);
      exercise(d,bytes,bytes+1,0);
      exercise(d,0,bytes-d,0);
    }
    for (unsigned eew=1;(1u<<eew)<bytes;++eew) exercise(1u<<eew,0,2*bytes>>eew,eew);
    masked_pipeline(false);
    masked_pipeline(true);
  });
}
