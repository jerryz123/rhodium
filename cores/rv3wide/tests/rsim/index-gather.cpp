// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <cstring>

constexpr unsigned bytes = sizeof(memory_in.presponse.pbits);
unsigned probes, releases, requests, returns, writes, authorizations, completions;
bool fault_next, delay_proof, proof_pending, overlap;

void step() {
  CHECK(test_cycles < 600);
  check_in.presponse.pvalid = proof_pending && !delay_proof;
  check_in.presponse.pbits = !(fault_next && probes == (overlap ? 3u : 2u));
  memory_in.presponse.pvalid = returns < requests;
  std::memset(&memory_in.presponse.pbits, 0x11 * (returns + 1), bytes);
  eval();
  if (check_in.presponse.pvalid && check_out.presponse.pready) proof_pending = false;
  if (check_out.prequest.pvalid && check_in.prequest.pready) {
    const auto& r = check_out.prequest.pbits;
    CHECK(!proof_pending && probes < (overlap ? 3u : 2u) && !r.pstore);
    const unsigned consecutive[] = {0x2000,0x3000,0x5000};
    CHECK(r.pfirst == (overlap ? consecutive[probes] : probes ? 0x3fffu : 0x1fffu));
    CHECK(r.plast == r.pfirst + (overlap ? 4095 : 3));
    ++probes; proof_pending = true;
    pages[0] = probes == 1 ? 0x60 : 0x80;
    pages[1] = probes == 1 ? 0x90 : 0xb0;
  }
  releases += check_out.prelease.pvalid;
  if (memory_out.prequest.pvalid) {
    CHECK(requests < 4);
    const unsigned pa[] = {0x61000-bytes, 0x90000, 0x81000-bytes, 0xb0000};
    const auto& r = memory_out.prequest.pbits;
    if (overlap) {
      CHECK(requests < 2 && !writes && !r.pstore);
      CHECK(r.paddress == (requests ? 0x80000u : 0x60000u) && r.pbyte_umask == 15);
    } else {
      CHECK(!r.pstore && r.paddress == pa[requests]);
      CHECK(r.pbyte_umask == (requests % 2 ? 7u : 1u << (bytes-1)));
      if (requests >= 2) CHECK(writes == 1 && !fault_next);
    }
    requests += memory_in.prequest.pready;
  }
  returns += memory_in.presponse.pvalid && memory_out.presponse.pready;
  if (write_out.pvalid) {
    const unsigned offset = writes ? (overlap ? 4 : 8) : 0;
    const auto& w = write_out.pbits;
    CHECK(w.paddress == 16 + offset/bytes);
    for (unsigned b=0; b<bytes; ++b) {
      bool live = b >= offset%bytes && b < offset%bytes+4;
      CHECK(reinterpret_cast<const unsigned char*>(&w.pmask)[b] == (live ? 255 : 0));
      if (live) CHECK(reinterpret_cast<const unsigned char*>(&w.pdata)[b] == 0x11*(overlap ? writes+1 : 2*writes+(b==offset%bytes ? 1 : 2)));
    }
    writes += write_in.pready;
  }
  if (fault_out.pvalid) {
    CHECK(fault_next && writes == (overlap ? 2u : 1u) && requests == 2 && !authorizations);
    CHECK(restart == (overlap ? 3u : 2u));
  }
  authorizations += authorized;
  completions += finished;
  tick_model(); eval();
}

void gather(bool fail, bool masked_tail) {
  probes=releases=requests=returns=writes=authorizations=completions=0;
  proof_pending=delay_proof=false; fault_next=fail;
  start_in={}; memory_in={}; write_in={}; check_in={};
  check_in.prequest.pready=1;
  std::uint16_t offsets[] = {0xfff,0xdead,0x2fff,0xffff};
  if (overlap) for (unsigned i=0;i<4;++i) offsets[i]=(i+1)*0x1000;
  std::memcpy(&indices,offsets,sizeof(offsets));
  std::uint64_t mask=overlap?11:masked_tail?1:5;
  std::memcpy(&predicate,&mask,sizeof(mask));
  auto& entry=start_in.pbits;
  entry.powner=2;
  auto& d=entry.pchecked.pdescriptor;
  d.pstate.pvl=4; d.pstate.pvtype=16; d.pmasked=1; d.pscalar=0x1000;
  d.pcontrol.pmemory=1; d.pcontrol.pmemory_uaddressing=2; d.pcontrol.pmemory_uindex_ueew=1;
  d.pinstruction=(4u<<20)|(8u<<7);
  start_in.pvalid=1; eval(); CHECK(start_out.pready); step(); start_in.pvalid=0;
  if (overlap) {
    memory_in.prequest.pready=1;
    while (releases!=3 || returns!=2) step();
    for (unsigned i=0;i<5;++i) step();
    CHECK(probes==3 && requests==2 && !writes && !fault_out.pvalid && !completions);
    write_in.pready=1; step(); write_in.pready=0;
    for (unsigned i=0;i<5;++i) step();
    CHECK(writes==1 && write_out.pvalid && (claimed & (1ull<<16)) && !fault_out.pvalid);
    write_in.pready=1;
    while (!fault_out.pvalid) step();
    step();
    CHECK(start_out.pready && writes==2 && completions==1 && !authorizations);
    return;
  }
  // The VLSU snapshots both PPNs before the cache grants even the prefix.
  // The checker can replace that mapping or discover the fault immediately.
  while (!memory_out.prequest.pvalid) step();
  delay_proof=!fail && !masked_tail;
  for(unsigned i=0;i<10;++i) step();
  CHECK(probes==(masked_tail?1u:2u) && releases==(fail?2u:1u));
  CHECK(!requests && !writes && !authorizations && !completions && !fault_out.pvalid);
  memory_in.prequest.pready=1; step();
  memory_in.prequest.pready=0;
  for(unsigned i=0;i<4;++i) step();
  CHECK(requests==1 && !authorizations && !completions && !fault_out.pvalid);
  memory_in.prequest.pready=1;
  for(unsigned i=0;i<18;++i) step();
  // Both fragments have returned. The only backpressure is the actual VRF
  // write grant, not L1D ready or a delay on the response channel.
  CHECK(write_out.pvalid && !write_in.pready && returns==2 && !writes && !completions);
  CHECK(probes==(masked_tail?1u:2u) && releases==(fail?2u:1u));
  CHECK(authorizations==unsigned(masked_tail) && !fault_out.pvalid);
  write_in.pready=1;
  if (fail) {
    while (!fault_out.pvalid) step();
    step(); CHECK(completions==1 && !fault_out.pvalid);
  } else {
    if (delay_proof) {
      for(unsigned i=0;i<6;++i) step();
      CHECK(writes==1 && requests==2 && !authorizations && !completions);
      delay_proof=false;
    }
    while (!completions) step();
    CHECK(writes==(masked_tail?1u:2u) && authorizations==1 && completions==1);
  }
  CHECK(start_out.pready);
}

void page_run(bool enable_bad, bool masked_tail=false, bool stall_vrf=false) {
  start_in={}; memory_in={}; write_in={}; check_in={};
  check_in.prequest.pready=memory_in.prequest.pready=1;
  write_in.pready=!enable_bad && !stall_vrf;
  const std::uint16_t offsets[]={0x40,0x11,0x5043,0x09};
  std::uint16_t row[bytes/2];
  for (unsigned i=0;i<bytes/2;++i) row[i]=offsets[i%4];
  std::memcpy(&indices,row,bytes);
  const std::uint64_t mask=enable_bad?0x3bf:0x3bb; // Skip element 6; optionally enable element 2.
  std::memcpy(&predicate,&mask,sizeof(mask));
  auto& d=start_in.pbits.pchecked.pdescriptor;
  d.pstate.pvl=masked_tail?11:10; d.pmasked=1; d.pscalar=0x1000; // Optional masked tail on the bad page.
  d.pcontrol.pmemory=1; d.pcontrol.pmemory_uaddressing=2; d.pcontrol.pmemory_uindex_ueew=1;
  d.pinstruction=(4u<<20)|(8u<<7);
  unsigned checks=0,released=0,issued=0,returned=0,written=0,first_issue=0,last_issue=0,reply_at=0,authorized_count=0;
  unsigned installs=0,streams=0,stalled=0,lookaheads=0;
  bool consecutive_streams=false;
  const unsigned element[]={0,1,3,4,5,7,8,9};
  const unsigned ppn=enable_bad?0x90:0x80; // The next macro must not inherit the previous mapping.
  bool owned=false, pending=false, good=false, done=false;
  start_in.pvalid=1;
  for(unsigned t=0;t<300 && !done;++t) {
    check_in.prequest.pready=!owned && !pending;
    check_in.presponse.pvalid=pending && t>=reply_at;
    check_in.presponse.pbits=good;
    memory_in.presponse.pvalid=returned<issued;
    std::memset(&memory_in.presponse.pbits,0x40+returned,bytes);
    if(stall_vrf && stalled==8) write_in.pready=1;
    if(enable_bad && checks==2 && t>=reply_at+8) write_in.pready=1;
    eval();
    installs+=install;
    streams+=streamed;
    lookaheads+=prefetched;
    if (check_out.prequest.pvalid && check_in.prequest.pready) {
      auto r=check_out.prequest.pbits;
      good=r.pfirst/4096==1;
      CHECK(!r.pstore && r.pfirst==(checks==0 ? 0x1000u : 0x6000u) && r.plast==r.pfirst+4095);
      if (!good) CHECK(enable_bad && !written);
      pages[0]=ppn; pending=true; reply_at=t+3; ++checks;
    }
    if (check_in.presponse.pvalid && check_out.presponse.pready) { pending=false; owned=good; }
    if (memory_out.prequest.pvalid) {
      CHECK(issued<(enable_bad?2u:8u));
      if(issued==1) CHECK(t==first_issue+1);
      consecutive_streams|=issued>=2 && t==last_issue+1;
      unsigned offset=offsets[element[issued]%4];
      CHECK(memory_out.prequest.pbits.paddress==ppn*4096+(offset&~(bytes-1)));
      CHECK(memory_out.prequest.pbits.pbyte_umask==(1u<<(offset%bytes)));
      if (!issued) first_issue=t;
      last_issue=t; ++issued;
    }
    if(write_out.pvalid) {
      CHECK(written<issued);
      const auto& w=write_out.pbits;
      const unsigned e=element[written];
      CHECK(w.paddress==16+e/bytes);
      for(unsigned i=0;i<bytes;++i)
        CHECK(reinterpret_cast<const unsigned char*>(&w.pmask)[i]==(i==e%bytes ? 255 : 0));
      CHECK(reinterpret_cast<const unsigned char*>(&w.pdata)[e%bytes]==0x40+written);
      if(stall_vrf && !write_in.pready) {
        CHECK(issued>=2 && issued<=5 && installs==1 && !finished && !authorized);
        CHECK(claimed & (1ull<<16));
        ++stalled;
      }
    }
    const bool release=check_out.prelease.pvalid;
    if (release) { ++released; owned=false; }
    if (authorized) { CHECK(!enable_bad && issued==8); ++authorized_count; }
    returned+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    written+=write_out.pvalid && write_in.pready;
    if(enable_bad) {
      CHECK(bool(finished)==bool(fault_out.pvalid) && !authorized);
      if(fault_out.pvalid) {
        CHECK(!owned && !pending && written==2 && restart==2);
        eval(); done=true;
      }
    } else done=finished;
    tick_model(); start_in.pvalid=0;
    if (release) pages[0]=0;
  }
  CHECK(done && !owned && !pending && issued==(enable_bad?2u:8u) && issued==written && checks==(enable_bad?2u:1u) && released==checks);
  CHECK(authorized_count==unsigned(!enable_bad));
  CHECK(installs==1 && streams==issued-1 && stalled==(stall_vrf?8u:0u));
  if(!enable_bad && !stall_vrf && !masked_tail) {
    CHECK(consecutive_streams);
    CHECK(lookaheads==(block_lookahead?0u:9/(bytes/2)));
    if(!block_lookahead) std::cout<<"same-page gather: checks="<<checks<<" first-to-last issue="<<last_issue-first_issue<<" cycles\n";
  }
}

unsigned slice_latency(bool store, bool ordered, unsigned sew, unsigned lane, bool byte_indices=false) {
  const unsigned size=1u<<sew, element=(bytes+lane)/size; // Second register row.
  const unsigned memory_lane=bytes-size;
  start_in={}; check_in={}; memory_in={}; write_in={};
  std::uint16_t offsets[bytes/2];
  for(auto& offset:offsets) offset=memory_lane;
  if(byte_indices) std::memset(&indices,memory_lane,bytes);
  else std::memcpy(&indices,offsets,bytes);
  for(unsigned i=0;i<bytes;++i) {
    reinterpret_cast<unsigned char*>(&ports::store_data)[i]=0x40+i;
    reinterpret_cast<unsigned char*>(&memory_in.presponse.pbits)[i]=0x80+i;
  }
  auto& d=start_in.pbits.pchecked.pdescriptor;
  d.pstate.pvstart=element; d.pstate.pvl=element+1; d.pstate.pvtype=sew<<3;
  d.pscalar=0x1000; d.pcontrol.pmemory=1; d.pcontrol.pstore=store;
  d.pcontrol.pmemory_uaddressing=ordered?3:2; d.pcontrol.pmemory_uindex_ueew=byte_indices?0:1;
  d.pinstruction=(4u<<20)|(8u<<7);
  start_in.pvalid=check_in.prequest.pready=write_in.pready=1;
  bool proved=false,issued=false,returned=false,wrote=false;
  unsigned proof_at=0,offer_at=0;
  for(unsigned t=1;t<100;++t) {
    check_in.presponse.pvalid=proved && !proof_at;
    check_in.presponse.pbits=1; pages[0]=0x80;
    memory_in.prequest.pready=offer_at && t>=offer_at+3;
    memory_in.presponse.pvalid=issued && !returned;
    eval();
    if(start_in.pvalid) CHECK(start_out.pready);
    if(check_out.prequest.pvalid) {
      CHECK(!proved && check_out.prequest.pbits.pfirst==0x1000 && bool(check_out.prequest.pbits.pstore)==store);
      proved=true;
    }
    if(check_in.presponse.pvalid && check_out.presponse.pready) proof_at=t;
    if(memory_out.prequest.pvalid) {
      CHECK(proof_at && !issued);
      if(!offer_at) offer_at=t;
      const auto& r=memory_out.prequest.pbits;
      CHECK(bool(r.pstore)==store && r.paddress==0x80000 && r.pbyte_umask==(low_mask(size)<<memory_lane));
      if(store) for(unsigned i=0;i<size;++i)
        CHECK(reinterpret_cast<const unsigned char*>(&r.pdata)[memory_lane+i]==0x40+lane+i);
      issued=memory_in.prequest.pready;
    } else CHECK(!offer_at || issued);
    if(memory_in.presponse.pvalid && memory_out.presponse.pready) returned=true;
    if(write_out.pvalid) {
      const auto& w=write_out.pbits;
      CHECK(!wrote && !store && w.paddress==17);
      for(unsigned i=0;i<bytes;++i) {
        const bool live=i>=lane && i<lane+size;
        CHECK(reinterpret_cast<const unsigned char*>(&w.pmask)[i]==(live?255:0));
        if(live) CHECK(reinterpret_cast<const unsigned char*>(&w.pdata)[i]==0x80+memory_lane+i-lane);
      }
      wrote=true;
    }
    const bool done=finished;
    tick_model(); start_in.pvalid=0;
    if(done) {
      CHECK(returned && wrote==!store && offer_at>proof_at);
      return offer_at-proof_at;
    }
  }
  throw std::runtime_error("checked slice did not drain");
}

void stream_split_beat(bool store) {
  start_in={}; check_in={}; memory_in={}; write_in={};
  std::uint16_t offsets[bytes/2]={0,std::uint16_t(bytes-1),0};
  std::memcpy(&indices,offsets,bytes);
  auto& d=start_in.pbits.pchecked.pdescriptor;
  d.pstate.pvl=3; d.pstate.pvtype=2u<<3; d.pscalar=0x1000;
  d.pcontrol.pstore=store;
  d.pcontrol.pmemory=1; d.pcontrol.pmemory_uaddressing=2; d.pcontrol.pmemory_uindex_ueew=1;
  d.pinstruction=(4u<<20)|(8u<<7);
  start_in.pvalid=check_in.prequest.pready=write_in.pready=1;
  unsigned probes=0,issued=0,returned=0,written=0,stalls=0,installs=0,streams=0;
  bool reply=false;
  for(unsigned t=0;t<100;++t) {
    check_in.presponse.pvalid=reply; check_in.presponse.pbits=1; pages[0]=0x80;
    memory_in.prequest.pready=issued!=2 || stalls==4;
    memory_in.presponse.pvalid=returned<issued;
    std::memset(&memory_in.presponse.pbits,0x50+returned,bytes);
    eval();
    if(vs3_read) {
      for(unsigned i=0;i<bytes;++i) reinterpret_cast<unsigned char*>(&ports::store_data)[i]=0x30+(vs3_row-16)*bytes+i;
    } else std::memset(&ports::store_data,0xde,bytes);
    eval();
    if(check_out.prequest.pvalid) { ++probes; reply=true; }
    if(check_in.presponse.pvalid && check_out.presponse.pready) reply=false;
    installs+=install; streams+=streamed;
    if(memory_out.prequest.pvalid) {
      CHECK(issued<4);
      const auto& r=memory_out.prequest.pbits;
      const unsigned masks[]={15u,1u<<(bytes-1),7u,15u};
      CHECK(r.paddress==0x80000+(issued==2?bytes:0) && bool(r.pstore)==store && r.pbyte_umask==masks[issued]);
      if(store) {
        const unsigned first[]={0,4,5,8};
        unsigned src=first[issued];
        for(unsigned b=0;b<bytes;++b) if(masks[issued] & (1u<<b))
          CHECK(reinterpret_cast<const unsigned char*>(&r.pdata)[b]==0x30+src++);
      }
      if(memory_in.prequest.pready) ++issued;
      else { CHECK(issued==2 && streams==1 && !authorized); ++stalls; }
    }
    returned+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    if(write_out.pvalid) {
      CHECK(written<3);
      const unsigned offset=4*written;
      const auto& w=write_out.pbits;
      CHECK(w.paddress==16+offset/bytes);
      for(unsigned b=0;b<bytes;++b) {
        const unsigned lane=offset%bytes;
        const bool active=b>=lane && b<lane+4;
        CHECK(reinterpret_cast<const unsigned char*>(&w.pmask)[b]==(active?255:0));
        if(active) CHECK(reinterpret_cast<const unsigned char*>(&w.pdata)[b]==0x50+(written==0?0:written==2?3:b==lane?1:2));
      }
      ++written;
    }
    const bool done=finished;
    tick_model(); start_in.pvalid=0;
    if(done) {
      CHECK(probes==1 && installs==1 && streams==2 && stalls==4 && issued==4 && returned==4 && written==(store?0u:3u));
      return;
    }
  }
  throw std::runtime_error("streamed split beat did not drain");
}

void page_scatter(bool delay_ack, bool deny=false) {
  start_in={}; check_in={}; memory_in={}; write_in={};
  auto& d=start_in.pbits.pchecked.pdescriptor;
  d.pstate.pvl=10; d.pmasked=1; d.pscalar=0x1000;
  d.pcontrol.pmemory=1; d.pcontrol.pstore=1; d.pcontrol.pmemory_uaddressing=2; d.pcontrol.pmemory_uindex_ueew=1;
  d.pinstruction=(4u<<20)|(8u<<7);
  const std::uint16_t offsets[]={0x11,0x11,0x5043,0x09};
  for(unsigned i=0;i<bytes/2;++i)
    std::memcpy(reinterpret_cast<unsigned char*>(&indices)+2*i,&offsets[i%4],2);
  const std::uint64_t mask=deny?0x3bf:0x3bb;
  std::memcpy(&predicate,&mask,sizeof(mask));
  const unsigned elements[]={0,1,3,4,5,7,8,9};
  unsigned probes=0,reads=0,issued=0,acks=0,installs=0,streams=0,first=0,last=0,auth=0;
  bool reply=false;
  start_in.pvalid=check_in.prequest.pready=1;
  for(unsigned t=0;t<200;++t) {
    pages[0]=0x80;
    check_in.presponse.pvalid=reply; check_in.presponse.pbits=probes==1;
    memory_in.prequest.pready=!delay_ack || issued<2 || t%5!=0;
    memory_in.presponse.pvalid=acks<issued && (!delay_ack || t>=first+15);
    eval();
    if(check_out.prequest.pvalid) {
      CHECK(check_out.prequest.pbits.pstore && probes<(deny?2u:1u));
      CHECK(check_out.prequest.pbits.pfirst==(probes?0x6000u:0x1000u));
      if(probes) CHECK(!acks);
      ++probes; reply=true;
    }
    if(check_in.presponse.pvalid && check_out.presponse.pready) reply=false;
    if(vs3_read) {
      CHECK(vs3_row==16+reads);
      for(unsigned i=0;i<bytes;++i) reinterpret_cast<unsigned char*>(&ports::store_data)[i]=0x50+(vs3_row-16)*bytes+i;
      ++reads;
    } else std::memset(&ports::store_data,0xde,bytes);
    eval(); installs+=install; streams+=streamed;
    if(memory_out.prequest.pvalid) {
      CHECK(issued<(deny?2u:8u));
      const unsigned e=elements[issued],offset=offsets[e%4];
      const auto& r=memory_out.prequest.pbits;
      CHECK(r.pstore && r.paddress==0x80000+(offset&~(bytes-1)) && r.pbyte_umask==(1u<<(offset%bytes)));
      CHECK(reinterpret_cast<const unsigned char*>(&r.pdata)[offset%bytes]==0x50+e);
      if(memory_in.prequest.pready) {
        if(!issued) first=t;
        if(issued==1) CHECK(t==first+1); // Duplicate address, distinct VS3 lanes.
        last=t; ++issued;
      }
    }
    if(delay_ack && issued && t<first+15) CHECK(issued<=5 && !authorized && !finished && !acks);
    acks+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    auth+=authorized;
    if(deny) {
      CHECK(!auth && bool(finished)==bool(fault_out.pvalid));
      if(fault_out.pvalid) {
        CHECK(probes==2 && reads==1 && issued==2 && acks==2 && restart==2);
        eval(); tick_model(); return;
      }
    }
    const bool done=finished;
    tick_model(); start_in.pvalid=0;
    if(done) {
      CHECK(issued==8 && acks==8 && auth==1 && probes==1);
      CHECK(installs==1 && streams==7 && reads==1+elements[7]/bytes);
      if(!delay_ack) std::cout<<"same-page scatter: installs="<<installs<<" streamed="<<streams<<" VS3 reads="<<reads<<" first-to-last issue="<<last-first<<" cycles\n";
      return;
    }
  }
  throw std::runtime_error("same-page scatter did not drain");
}

// A halfword segment straddles two pages between fields. The final successful
// field is already out of memory but not yet in the VRF when the next probe faults.
void fault_first_write_stall(unsigned segment, unsigned fault_field=1, unsigned lmul=0) {
  start_in={}; check_in={}; memory_in={}; write_in={};
  auto& d=start_in.pbits.pchecked.pdescriptor;
  d.pstate.pvl=segment+2; d.pstate.pvstart=segment; d.pstate.pvtype=(1u<<3)|lmul;
  d.pscalar=0x3000-6*segment-2*fault_field;
  d.pcontrol.pmemory=1; d.pcontrol.pmemory_umode=1; // FaultOnlyFirst
  d.pcontrol.pmemory_unf=2; d.pcontrol.pmemory_udata_ueew=1;
  d.pinstruction=(2u<<29)|(1u<<25)|(16u<<20)|(5u<<12)|(8u<<7)|7u;
  start_in.pvalid=check_in.prequest.pready=memory_in.prequest.pready=1;
  unsigned checks=0, issued=0, delivered=0, stalls=0, committed=0;
  bool reply=false;
  for(unsigned t=0;t<100;++t) {
    check_in.presponse.pvalid=reply; check_in.presponse.pbits=checks==1;
    pages[0]=0x80; memory_in.presponse.pvalid=delivered<issued;
    write_in.pready=committed+1<fault_field || stalls==10;
    eval();
    if(check_out.prequest.pvalid) {
      CHECK(checks<2 && check_out.prequest.pbits.pfirst==(checks?0x3000u:0x2000u));
      ++checks; reply=true;
    }
    if(check_in.presponse.pvalid && check_out.presponse.pready) reply=false;
    if(memory_out.prequest.pvalid) {
      const unsigned lane=bytes-2*(fault_field-issued);
      CHECK(issued<fault_field && memory_out.prequest.pbits.paddress==0x81000-bytes);
      CHECK(memory_out.prequest.pbits.pbyte_umask==(3u<<lane));
      ++issued;
    }
    delivered+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    if(write_out.pvalid) {
      const unsigned byte=2*segment;
      CHECK(delivered==committed+1 && write_out.pbits.paddress==16+2*(committed<<lmul)+byte/bytes);
      for(unsigned b=0;b<bytes;++b)
        CHECK(reinterpret_cast<const unsigned char*>(&write_out.pbits.pmask)[b]==(b>=byte%bytes && b<byte%bytes+2 ? 255 : 0));
      if(write_in.pready) ++committed;
      else if(checks==2 && !reply) ++stalls;
    }
    CHECK(!authorized && !(fault_out.pvalid && truncate_out.pvalid));
    CHECK(bool(finished)==bool(fault_out.pvalid || truncate_out.pvalid));
    const bool done=finished;
    if(done) {
      CHECK(stalls==10 && committed==fault_field && issued==fault_field && bool(fault_out.pvalid)==(segment==0));
      CHECK(segment?truncate_out.pbits==segment:restart==0);
    }
    tick_model(); start_in.pvalid=0;
    if(done) return;
  }
  throw std::runtime_error("fault-first field did not drain");
}

// Model the overlapping registers as one byte image on both the VPS read port
// and VLSeq write port. Disable lookahead so correctness cannot depend on it.
void overlapping_restart(unsigned shape) {
  constexpr unsigned count=2*bytes, bad=count-3;
  const unsigned size=shape==2?2:1, idx=shape==1?2:1;
  const unsigned vd=8, vs2=vd+(shape==2), first_row=vd*2;
  std::uint8_t regs[2*count]={}, expected[sizeof regs];
  unsigned offsets[count];
  for(unsigned i=0;i<count;++i) {
    offsets[i]=i==bad?0xd0:2*((7*i)%53);
    for(unsigned b=0;b<idx;++b) regs[(vs2-vd)*count+i*idx+b]=offsets[i]>>(8*b);
  }
  std::memcpy(expected,regs,sizeof regs);
  block_lookahead=1;
  for(bool retry : {false,true}) {
    start_in={}; check_in={}; memory_in={}; write_in={};
    auto& d=start_in.pbits.pchecked.pdescriptor;
    d.pscalar=0x2f80; d.pstate.pvl=count; d.pstate.pvstart=retry?bad:0;
    d.pstate.pvtype=shape==2?9:0; // e16,m2 for the high-end overlap
    d.pinstruction=(vs2<<20)|(vd<<7); d.pcontrol.pmemory=1;
    d.pcontrol.pmemory_uaddressing=2; d.pcontrol.pmemory_uindex_ueew=idx==2;
    start_in.pvalid=check_in.prequest.pready=memory_in.prequest.pready=1;
    unsigned issue=d.pstate.pvstart,ret=issue,write=issue,stalls=0;
    bool pending=false,safe=false,done=false,denied=false;
    for(unsigned t=0;t<400 && !done;++t) {
      check_in.presponse.pvalid=pending; check_in.presponse.pbits=safe;
      memory_in.presponse.pvalid=ret<issue;
      std::memset(&memory_in.presponse.pbits,0x90+ret,bytes);
      write_in.pready=retry || write!=bad-1 || stalls==10;
      eval();
      if(index_row>=first_row && index_row<first_row+4)
        std::memcpy(&indices,regs+(index_row-first_row)*bytes,bytes);
      eval();
      if(check_in.presponse.pvalid && check_out.presponse.pready) pending=false;
      if(check_out.prequest.pvalid) {
        const unsigned vpn=check_out.prequest.pbits.pfirst>>12;
        CHECK(vpn==2 || vpn==3);
        pending=true; safe=retry || vpn!=3; denied|=!safe; pages[0]=vpn;
      }
      if(memory_out.prequest.pvalid) {
        CHECK(issue<(retry?count:bad));
        const unsigned a=0x2f80+offsets[issue++];
        CHECK(memory_out.prequest.pbits.paddress==(a&~(bytes-1)));
        CHECK(memory_out.prequest.pbits.pbyte_umask==(low_mask(size)<<(a%bytes)));
      }
      ret+=memory_in.presponse.pvalid && memory_out.presponse.pready;
      if(write_out.pvalid) {
        CHECK(write<ret);
        const auto& w=write_out.pbits;
        CHECK(w.paddress==first_row+write*size/bytes);
        for(unsigned b=0;b<bytes;++b) {
          const unsigned n=(w.paddress-first_row)*bytes+b;
          const bool live=n/size==write;
          CHECK(reinterpret_cast<const unsigned char*>(&w.pmask)[b]==(live?255:0));
          if(live && write_in.pready) {
            regs[n]=reinterpret_cast<const unsigned char*>(&w.pdata)[b];
            expected[n]=0x90+write;
          }
        }
        if(write_in.pready) ++write; else if(denied) ++stalls;
      }
      CHECK(!truncate_out.pvalid && (!fault_out.pvalid || (!retry && write==bad && restart==bad)));
      done=finished;
      if(done) CHECK(bool(fault_out.pvalid)==!retry);
      tick_model(); start_in.pvalid=0;
    }
    CHECK(done && write==issue && issue==(retry?count:bad) && stalls==(retry?0u:10u));
    CHECK(!std::memcmp(regs,expected,sizeof regs));
  }
  block_lookahead=0;
}

// An inherited window belongs to the whole checked macro. Neither its first
// slice nor a stalled final VRF write releases it, even after final issue.
void owned_window(bool ordered) {
  start_in={}; memory_in={}; write_in={}; check_in={};
  pages[0]=0x60; pages[1]=0x90;
  const std::uint16_t offsets[]={0,1};
  std::memset(&indices,0,sizeof(indices)); std::memcpy(&indices,offsets,sizeof(offsets));
  auto& entry=start_in.pbits;
  entry.powner=3; entry.pchecked.pcertified=entry.pchecked.powns_uwindow=1;
  auto& d=entry.pchecked.pdescriptor;
  d.pstate.pvl=2; d.pscalar=0x1000; d.pinstruction=(4u<<20)|(8u<<7);
  d.pcontrol.pmemory=1; d.pcontrol.pmemory_uaddressing=ordered?3:2; d.pcontrol.pmemory_uindex_ueew=1;
  start_in.pvalid=1;
  unsigned sent=0,replies=0,grants=0,released_window=0,auth=0,held=0,done=0;
  for(unsigned t=0;t<130 && !done;++t) {
    memory_in.prequest.pready=t>=4;
    memory_in.presponse.pvalid=replies<sent;
    std::memset(&memory_in.presponse.pbits,0x73,bytes);
    // Allow the first element to finish, but hold the final element after
    // its reply. The macro's window must survive this physical write stall.
    write_in.pready=grants==0 || held>=9;
    eval();
    CHECK(!check_out.prequest.pvalid && !check_out.prelease.pvalid && !fault_out.pvalid);
    if(memory_out.prequest.pvalid && memory_in.prequest.pready) {
      CHECK(sent<2 && memory_out.prequest.pbits.paddress==0x60000 &&
            memory_out.prequest.pbits.pbyte_umask==(1u<<sent));
      ++sent;
    }
    replies+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    if(write_out.pvalid) {
      CHECK(write_out.pbits.paddress==16);
      if(!write_in.pready) { ++held; CHECK(!finished && !window_released); }
      else ++grants;
    }
    if(window_released) { ++released_window; CHECK(finished && replies==2 && grants==2); }
    if(finished) ++done;
    auth+=authorized;
    const bool accepted=start_in.pvalid && start_out.pready;
    tick_model(); if(accepted) start_in.pvalid=0;
  }
  CHECK(done==1 && released_window==1 && grants==2 && held==9 && auth==1);
  eval(); CHECK(start_out.pready && !window_released);
}

int main() {
  return run_test([] {
    reset=1; tick_model(); reset=0;
    owned_window(false); owned_window(true);
    fault_first_write_stall(0); fault_first_write_stall(3);
    fault_first_write_stall(0,2,1); fault_first_write_stall(bytes/2+1,2,1);
    gather(true,false);
    gather(false,true);
    gather(false,false);
    overlap=true; gather(true,false);
    page_run(false); page_run(false,true); page_run(false,false,true); page_run(true);
    block_lookahead=1; page_run(false); block_lookahead=0;
    stream_split_beat(false); stream_split_beat(true);
    page_scatter(false); page_scatter(true); page_scatter(true,true);
    for(unsigned shape=0;shape<3;++shape) overlapping_restart(shape);
    for(bool store:{false,true}) for(bool ordered:{false,true}) for(unsigned sew=0;(1u<<sew)<=bytes/2;++sew) {
      const unsigned latency=slice_latency(store,ordered,sew,0);
      CHECK(slice_latency(store,ordered,sew,bytes-(1u<<sew),true)==latency);
      for(unsigned lane=1u<<sew;lane<bytes;lane+=1u<<sew)
        CHECK(slice_latency(store,ordered,sew,lane)==latency);
      if(!sew) std::cout<<(ordered?"ordered ":"unordered ")<<(store?"scatter":"gather")<<" proof-to-offer: "<<latency<<" cycles at all "<<bytes<<" byte lanes\n";
    }
  });
}
