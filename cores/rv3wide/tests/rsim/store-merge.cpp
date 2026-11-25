// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <cstring>

constexpr unsigned bytes=sizeof(memory_in.presponse.pbits), vs3=8;
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
    CHECK(accepted-acks<=2);
    if (issued_out.pvalid) { CHECK(accepted==beats && issued_out.pbits.powner==3); ++tails; }
    if (finished_out.pvalid) { CHECK(acks==beats && finished_out.pbits.powner==3); ++done; }
    tick_model();
  }
  eval(); CHECK(launch_0_out.pready);
  CHECK(stalls && tails==1 && done==1 && !claims && reads==final_row-first_row+1);
  for (unsigned i=0;i<ram.size();++i)
    CHECK(ram[i]==(beat0+i>=begin && beat0+i<end ? original(beat0+i-base) : 0xc5));
}

static void segmented_store(unsigned fields,unsigned eew,unsigned lmul=0) {
  // Block the second field, then L1D. Each field has an independent last
  // capture, even when the first field in a beat rotates.
  prepare_in={}; launch_1_in={}; launch_0_in={}; memory_in={}; read_result_in={}; store_reads_in={};
  auto &d=launch_1_in.pbits.pissue.pdescriptor;
  const unsigned size=1u<<eew, lanes=bytes/size, visits=std::min(lanes,fields);
  const unsigned field_rows=2u<<lmul;
  d.pstate.pvl=lanes; d.pstate.pvtype=(eew<<3)|lmul; d.pcontrol.pmemory_udata_ueew=eew; d.pcontrol.pstore=1; d.pcontrol.pmemory_unf=fields-1;
  d.pscalar=0x4000-bytes; d.pinstruction=vs3<<7;
  launch_1_in.pbits.pissue.powner=2; prepare_in.pbits=launch_1_in.pbits.pissue;
  launch_1_in.pbits.ppages[0]=0x60; launch_1_in.pbits.ppages[1]=0x90;
  launch_1_in.pvalid=prepare_in.pvalid=1; tick_model(); launch_1_in.pvalid=prepare_in.pvalid=0;
  // Only the second beat aliases this following load via a different VA.
  auto &probe=launch_0_in.pbits.pissue.pdescriptor; probe={}; probe.pscalar=0x8000; probe.pstate.pvl=1;
  launch_0_in.pbits.ppages[0]=0x90;
  unsigned captures=0,sent=0,acks=0,finishes=0,stalls=0,read_row=0;
  bool returning=false;
  for(unsigned t=0;t<300 && !finishes;++t) {
    read_in.pready=1; store_reads_in.polder_uwrites=t<15 ? UINT64_C(1)<<(16+field_rows) : 0;
    read_result_in.pvalid=returning;
    Row data;
    for(unsigned b=0;b<bytes;++b) data[b]=(0x11+((read_row-16)/field_rows)*0x19+b)&255;
    if(captures>=fields*visits) data.fill(0xcc);
    std::memcpy(&read_result_in.pbits,data.data(),bytes);
    memory_in.prequest.pready=t>=45 && t%4==0;
    memory_in.presponse.pvalid=t>=75 && t%3==0 && acks<sent;
    eval();
    std::uint64_t claims=0;
    for(unsigned i=captures;i<fields*visits;++i)
      claims |= UINT64_C(1)<<(16+field_rows*((i/visits*lanes+i%visits)%fields));
    CHECK(store_reads_out.punread_uvs3==claims);
    CHECK(!launch_0_out.pready);
    if(t==12) CHECK(captures==1 && sent==0);
    if(t==35 && fields==2) CHECK(captures==4 && sent==0);
    if(returning) ++captures;
    returning=read_out.pvalid && read_in.pready;
    if(returning) { read_row=read_out.pbits; CHECK(read_row==16+field_rows*((captures/visits*lanes+captures%visits)%fields)); }
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
  CHECK(finishes==1 && sent==fields && acks==fields && stalls && captures==fields*visits);
}

int main() {
  return run_test([] {
    reset=1; tick_model(); reset=0;
    segmented_store(2,1,1); // Short m2 fields: read v8 and v10, never v9 or v11.
    for(unsigned n=2;n<=8;++n)
      for(unsigned e=0;(1u<<e)<=bytes/2;++e) segmented_store(n,e);
    for (unsigned d=1;d<bytes;++d) {
      exercise(d,0,2*bytes,0);
      exercise(d,bytes-1,bytes+1,0);
      exercise(d,bytes,bytes+1,0);
      exercise(d,0,bytes-d,0);
    }
    for (unsigned eew=1;(1u<<eew)<bytes;++eew) exercise(1u<<eew,0,2*bytes>>eew,eew);
  });
}
