// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <cstring>

enum IssueKind : unsigned { contiguous, stride_kind, inline_index, inline_ordered, resolved, resolved_ordered_index };
constexpr unsigned bytes=sizeof(memory_in.presponse.pbits);
constexpr unsigned row_credits=5;
unsigned byte_at(unsigned va) { return (va*13u+(va>>8))&255; }

void exercise(unsigned shift,unsigned first,unsigned limit,unsigned eew,unsigned vd=8,
              bool masked=false,std::uint64_t predicate=UINT64_MAX,bool checked_element=false) {
  const unsigned base=0x4000-bytes+shift, begin=base+(first<<eew), end=base+(limit<<eew);
  const unsigned beat0=begin & ~(bytes-1);
  const unsigned row0=(begin-base)/bytes, last_row=(end-1-base)/bytes;
  auto enabled=[&](unsigned va) { return va>=begin && va<end && ((predicate>>((va-base)>>eew))&1); };
  auto mask_at=[&](unsigned va) {
    unsigned mask=0; for (unsigned i=0;i<bytes;++i) if (enabled(va+i)) mask|=1u<<i;
    return mask;
  };
  std::vector<unsigned> beats,rows;
  for (unsigned va=beat0;va<end;va+=bytes) if (mask_at(va)) beats.push_back(va);
  for (unsigned r=row0;r<=last_row;++r) if (mask_at(base+r*bytes)) rows.push_back(r);
  const auto count=beats.size();
  launch_0_in={}; prepare_in={}; memory_in={}; write_in={}; chain_in={};
  auto &d=launch_0_in.pbits.pdescriptor;
  d.pmasked=masked;
  mask_0_in={}; std::memcpy(&mask_0_in.pdata,&predicate,sizeof(predicate));
  d.pstate.pvl=limit; d.pstate.pvstart=first; d.pstate.pvtype=eew<<3;
  d.pcontrol.pmemory_udata_ueew=eew; d.pscalar=base; d.pinstruction=vd<<7;
  launch_0_in.pbits.pkind = checked_element ? resolved_ordered_index : contiguous;
  if(checked_element) {
    CHECK(limit==first+1 && begin<0x4000 && end>0x4000);
    // An ordered singleton drains on its own. Unordered checked slices are
    // closed by the live checker; index-gather exercises that interface.
    d.pcontrol.pmemory=1; d.pcontrol.pmemory_uaddressing=3; d.pcontrol.pmemory_uindex_ueew=1;
    d.pscalar=begin;
  }
  launch_0_in.pbits.powner=2; prepare_in.pbits=launch_0_in.pbits;
  window_pages[0]=begin<0x4000 ? 0x60 : 0x90; window_pages[1]=0x90;
  launch_0_in.pvalid=prepare_in.pvalid=1; eval(); CHECK(prepare_out.pready && launch_0_out.pready);
  tick_model(); launch_0_in.pvalid=prepare_in.pvalid=0;
  unsigned sent=0,returned=0,grants=0,issued=0,delivered=0,finished=0,stall_cycles=0;
  std::uint64_t claims=0;
  for (auto r:rows) claims|=UINT64_C(1)<<(2*vd+r);
  const auto live=claims;
  for (unsigned t=0;t<160 && !finished;++t) {
    memory_in.prequest.pready=t>=3 && t%5!=0;
    memory_in.presponse.pvalid=returned<sent && t>=10 && t%4!=0;
    std::array<unsigned char,bytes> payload;
    for (unsigned i=0;i<bytes;++i) payload[i]=byte_at((returned<count ? beats[returned] : 0)+i);
    std::memcpy(&memory_in.presponse.pbits,payload.data(),bytes);
    // A previous reader can hold the first destination while VLSeq absorbs
    // the physical prefix. After WAR clears, stall the actual write port too.
    chain_in.polder_uaccesses=t<20 ? UINT64_C(1)<<(2*vd+row0) : 0;
    write_in.pready=t>=30 && t%3==0;
    eval(); CHECK((chain_out.punwritten_uvd & live)==claims);
    if (memory_out.prequest.pvalid) {
      CHECK(sent<count);
      const unsigned va=beats[sent], mask=mask_at(va);
      const auto &req=memory_out.prequest.pbits;
      CHECK(sent<count && !req.pstore && req.pbyte_umask==mask);
      CHECK(req.paddress==((va<0x4000 ? 0x60000u : 0x90000u)+(va&4095)));
      sent+=memory_in.prequest.pready;
    }
    returned+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    CHECK(sent-returned<=row_credits);
    if (issued_out.pvalid) { ++issued; CHECK(sent==count && issued_out.pbits==2); }
    if (delivered_out.pvalid) { ++delivered; CHECK(returned==count); }
    if (write_out.pvalid) {
      const auto &w=write_out.pbits;
      CHECK(grants<rows.size());
      const unsigned r=rows[grants];
      CHECK((t>=20 || r!=row0) && w.paddress==2*vd+r && chain_out.pwrite_uowner==2);
      std::array<unsigned char,bytes> data,mask;
      std::memcpy(data.data(),&w.pdata,bytes); std::memcpy(mask.data(),&w.pmask,bytes);
      for (unsigned i=0;i<bytes;++i) {
        const unsigned va=base+r*bytes+i;
        CHECK(mask[i]==(enabled(va) ? 255 : 0));
        if (mask[i]) CHECK(data[i]==byte_at(va));
      }
      if (write_in.pready) { claims &= ~(UINT64_C(1)<<w.paddress); ++grants; }
      else ++stall_cycles;
    }
    if (finished_out.pvalid) { ++finished; CHECK(!chain_out.punwritten_uvd && !claims && finished_out.pbits==2); }
    tick_model();
  }
  CHECK(sent==count && returned==count && grants==rows.size() && (rows.empty() || stall_cycles));
  CHECK(issued==1 && delivered==1 && finished==1);
}

static void segmented_fragments(unsigned fields, unsigned eew, bool skip_upper=false) {
  // One physical row per field. Buffer the last beat behind the penultimate
  // beat's stalled final write; only accepted fragments retire row claims.
  prepare_in={}; launch_0_in={}; launch_1_in={}; memory_in={}; chain_in={}; mask_0_in={};
  auto &d=launch_0_in.pbits.pdescriptor;
  d.pinstruction=8<<7; d.pcontrol.pmemory_unf=fields-1; d.pscalar=0x4000-bytes;
  const unsigned size=1u<<eew, lanes=bytes/size, visits=std::min(lanes,fields);
  d.pmasked=skip_upper; d.pstate.pvl=lanes; d.pcontrol.pmemory_udata_ueew=eew; d.pstate.pvtype=eew<<3;
  std::uint64_t predicate=(UINT64_C(1)<<(bytes/2))-1;
  std::memcpy(&mask_0_in.pdata,&predicate,sizeof(predicate));
  launch_0_in.pbits.powner=1; prepare_in.pbits=launch_0_in.pbits;
  window_pages[0]=0x60; window_pages[1]=0x90;
  launch_0_in.pvalid=prepare_in.pvalid=1; tick_model(); launch_0_in.pvalid=prepare_in.pvalid=0;
  unsigned sent=0,returned=0,grants=0,finished=0,stalls=0;
  auto rowbit=[](unsigned r){return UINT64_C(1)<<r;};
  const unsigned beats=skip_upper ? 1 : fields;
  const unsigned held_grant=std::max(1u,beats-1)*visits-1;
  bool successor_due=false;
  for(unsigned t=0;t<220 && !finished;++t) {
    memory_in.prequest.pready=1;
    const bool successor=beats>1 && returned==beats-1;
    memory_in.presponse.pvalid=returned<sent && t>=10 && (!successor || stalls>0);
    std::array<unsigned char,bytes> data;
    for(unsigned b=0;b<bytes;++b) data[b]=17+bytes*returned+b;
    std::memcpy(&memory_in.presponse.pbits,data.data(),bytes);
    write_in.pready=grants!=held_grant || (stalls>=2 && returned==beats);
    eval();
    if(stalls && grants==held_grant) CHECK(write_out.pvalid);
    if(successor_due) CHECK(write_out.pvalid && write_in.pready);
    std::uint64_t expected_claims=0;
    for(unsigned i=grants;i<beats*visits;++i)
      expected_claims |= rowbit(16+2*((i/visits*lanes+i%visits)%fields));
    CHECK(chain_out.punwritten_uvd==expected_claims);
    if(memory_out.prequest.pvalid) {
      CHECK(sent<beats && memory_out.prequest.pbits.paddress==(sent ? 0x90000u+(sent-1)*bytes : 0x61000u-bytes));
      CHECK(memory_out.prequest.pbits.pbyte_umask==((1u<<bytes)-1)); ++sent;
    }
    if(memory_in.presponse.pvalid && memory_out.presponse.pready) {
      if(successor) CHECK(grants==held_grant && write_out.pvalid && !write_in.pready);
      ++returned;
    }
    successor_due=false;
    if(write_out.pvalid) {
      const unsigned beat=grants/visits, field=(beat*lanes+grants%visits)%fields;
      const auto &w=write_out.pbits;
      CHECK(w.paddress==16+2*field);
      const auto *mask=reinterpret_cast<const unsigned char*>(&w.pmask);
      const auto *value=reinterpret_cast<const unsigned char*>(&w.pdata);
      for(unsigned b=0;b<bytes;++b) {
        const unsigned stream=((b/size)*fields+field)*size+b%size;
        CHECK(mask[b]==(stream/bytes==beat ? 255 : 0));
        if(mask[b]) CHECK(value[b]==17+stream);
      }
      if(write_in.pready) {
        successor_due=beats>1 && grants==held_grant;
        ++grants;
      } else ++stalls;
    }
    finished+=finished_out.pvalid; tick_model();
  }
  CHECK(finished==1 && stalls && grants==beats*visits && sent==beats && returned==beats);
}

// A certified stride uses a physical envelope for ordering and the original
// signed stride for requests. The younger opposite-kind operation aliases
// only the far endpoint; hold the final memory response before admitting it.
static void strided_peer(bool store,int stride,bool masked) {
  using Data=std::array<unsigned char,bytes>;
  constexpr unsigned size=2, count=bytes-1, base=0x4842;
  const unsigned end=base+(count-1)*stride, ppn=0x60;
  const unsigned predicate=masked ? ((0x5555u|(1u<<(count-1))) & ~(1u<<1)) : 0xffffu;
  launch_0_in={}; launch_1_in={}; prepare_in={}; memory_in={}; read_result_in={};
  chain_in={}; store_reads_in={}; mask_0_in={}; mask_1_in={};
  auto& first=store ? launch_1_in : launch_0_in;
  auto& peer=store ? launch_0_in : launch_1_in;
  auto& d=first.pbits.pdescriptor;
  d.pcontrol.pmemory_uaddressing=1; d.pcontrol.pstore=store; d.pcontrol.pmemory_udata_ueew=1;
  d.pstate.pvtype=8; d.pstate.pvl=count; d.pstride=stride; d.pscalar=base;
  d.pinstruction=(8u<<7)|(1u<<20); d.pmasked=masked;
  d.pprecertified=1; d.pcertificate.pppn=ppn;
  // Match the fast proof's power-of-two envelope, not the exact stride.
  d.pcertificate.prange.pfirst=(stride<0 ? base-8*(count-1) : base)&~7u;
  d.pcertificate.prange.plast=(stride>0 ? base+8*(count-1)+1 : base+1)|7u;
  auto& p=peer.pbits.pdescriptor;
  p.pcontrol.pstore=!store; p.pstate.pvl=1; p.pinstruction=12u<<7;
  p.pprecertified=1; p.pcertificate.pppn=ppn; p.pscalar=0x9000+(end&4095);
  first.pbits.pkind=stride_kind; first.pbits.powner=1; peer.pbits.powner=2;
  auto& tap=store ? mask_1_in : mask_0_in;
  std::memcpy(&tap.pdata,&predicate,sizeof(predicate));
  prepare_in.pbits=first.pbits;
  first.pvalid=prepare_in.pvalid=1; tick_model(); first.pvalid=prepare_in.pvalid=0;
  peer.pvalid=1; prepare_in.pbits=peer.pbits;
  std::vector<unsigned> live;
  for(unsigned i=0;i<count;++i) if((predicate>>i)&1) live.push_back(i);
  const unsigned total=live.size();
  auto source_byte=[](unsigned off) { return (0xa5u+off*11)&255; };
  auto cache_byte=[](unsigned pa) { return (pa*13u+(pa>>4))&255; };
  auto pa_of=[&](unsigned j) { return ppn*4096+(j<total ? (base+live[j]*stride)&4095 : end&4095); };
  unsigned sent=0,replies=0,reads=0,writes=0,done=0,waits=0,ack_stalls=0,write_stalls=0,read_row=0;
  bool returning=false,admitted=false;
  for(unsigned t=0;t<400 && done!=2;++t) {
    memory_in.prequest.pready=t%4!=0;
    const bool hold=replies==total-1 && ack_stalls<10;
    memory_in.presponse.pvalid=replies<sent && !hold && t%3!=0;
    Data cache{},src{};
    for(unsigned b=0;b<bytes;++b) {
      cache[b]=cache_byte((pa_of(replies)&~(bytes-1))+b);
      src[b]=source_byte(read_row*bytes+b);
    }
    std::memcpy(&memory_in.presponse.pbits,cache.data(),bytes);
    std::memcpy(&read_result_in.pbits,src.data(),bytes);
    read_result_in.pvalid=returning; read_in.pready=1;
    write_in.pready=write_stalls>=8;
    // A younger memory head can already prepare its VRF side independently.
    prepare_in.pvalid=t==0;
    eval();
    if(t==0) CHECK(prepare_out.pready);
    const bool peer_ready=store ? launch_0_out.pready : launch_1_out.pready;
    if(replies<total && !admitted) { CHECK(!peer_ready); ++waits; }
    bool launch=peer.pvalid && peer_ready;
    if(hold && replies<sent) ++ack_stalls;
    if(!store && !writes) CHECK(chain_out.punwritten_uvd & (UINT64_C(1)<<16));
    if(write_out.pvalid) {
      if(!write_in.pready) ++write_stalls;
      const auto& w=write_out.pbits;
      CHECK(w.paddress==(store ? 24 : 16+writes));
      const auto *value=reinterpret_cast<const unsigned char*>(&w.pdata);
      const auto *mask=reinterpret_cast<const unsigned char*>(&w.pmask);
      for(unsigned b=0;b<bytes;++b) {
        unsigned off=writes*bytes+b, i=off/size;
        bool active=store ? b==0 : i<count && ((predicate>>i)&1);
        CHECK(mask[b]==(active ? 255 : 0));
        if(active) CHECK(value[b]==cache_byte(store ? pa_of(total) : ppn*4096+((base+i*stride)&4095)+off%size));
      }
      writes+=write_in.pready;
    }
    returning=read_out.pvalid && read_in.pready;
    if(returning) {
      CHECK(read_out.pbits==(store ? 16+reads : 24));
      read_row=reads++;
    }
    if(memory_out.prequest.pvalid) {
      CHECK(sent<=total);
      const auto& r=memory_out.prequest.pbits;
      const unsigned pa=pa_of(sent), lane=pa%bytes;
      const unsigned n=sent==total ? 1 : size;
      CHECK(r.paddress==(pa&~(bytes-1)) && r.pbyte_umask==(((1u<<n)-1)<<lane));
      CHECK(bool(r.pstore)==(sent<total ? store : !store));
      if(sent<total && store) for(unsigned b=0;b<size;++b)
        CHECK(reinterpret_cast<const unsigned char*>(&r.pdata)[lane+b]==source_byte(live[sent]*size+b));
      sent+=memory_in.prequest.pready;
    }
    replies+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    CHECK(sent-replies<=row_credits);
    done+=finished_out.pvalid; tick_model();
    if(launch) { peer.pvalid=0; admitted=true; }
  }
  CHECK(done==2 && admitted && waits && ack_stalls==10 && write_stalls>=8);
  CHECK(sent==total+1 && replies==sent && reads==(store ? 2u : 1u) && writes==(store ? 1u : 2u));
}

// Exercise both physical row sizes, including a stalled older VS2 producer,
// shared-port VS3 capture, a delayed cache ACK, and a denied VRF write.
static void indexed_bytes(bool store, bool ordered, bool mask_hole) {
  launch_0_in={}; launch_1_in={}; prepare_in={}; memory_in={}; read_result_in={};
  chain_in={}; store_reads_in={}; index_chain_in={}; mask_0_in={}; mask_1_in={};
  auto& start=store ? launch_1_in : launch_0_in;
  auto& d=start.pbits.pdescriptor;
  const unsigned base=0x4203, physical=0x60203, vl=bytes+3;
  const std::vector<unsigned> live{0,mask_hole?2u:1u,3,bytes,bytes+2};
  const std::array<unsigned,5> offsets{0x81,4,0xb8,0xff,0x81};
  unsigned predicate=0; for(auto i:live) predicate|=1u<<i;
  auto& tap=store ? mask_1_in : mask_0_in;
  std::memcpy(&tap.pdata,&predicate,sizeof(predicate));
  d.pcontrol.pmemory=1; d.pcontrol.pstore=store; d.pcontrol.pmemory_uaddressing=ordered?3:2;
  d.pcontrol.pmemory_udata_ueew=2; // Data width is SEW, not this unused column.
  d.pmasked=1; d.pstate.pvl=vl; d.pscalar=base; d.pinstruction=(4u<<20)|(8u<<7);
  d.pprecertified=1; d.pcertificate.pppn=0x60;
  d.pcertificate.prange.pfirst=base; d.pcertificate.prange.plast=base+255;
  start.pbits.pkind=ordered ? inline_ordered : inline_index; start.pbits.powner=1; prepare_in.pbits=start.pbits;
  prepare_in.pvalid=start.pvalid=1; eval(); CHECK(prepare_out.pready);
  tick_model(); prepare_in.pvalid=start.pvalid=0;
  unsigned sent=0,acks=0,writes=0,index_reads=0,data_reads=0,done=0,last_issue=0,write_stalls=0;
  unsigned prior_row=0; bool returning=false,blocked=false;
  std::uint64_t unread=3ull<<8;
  for(unsigned t=0;t<240 && !done;++t) {
    // Row 8 is demand; row 9 remains hazardous even after lookahead has
    // an idle read port and the consumer has started using row 8.
    index_chain_in.polder_uwrites=(t<8 ? (1ull<<8) : 0) | (t<18 ? (1ull<<9) : 0);
    read_in.pready=t%4!=0;
    read_result_in.pvalid=returning;
    auto *src=reinterpret_cast<unsigned char*>(&read_result_in.pbits);
    for(unsigned b=0;b<bytes;++b) {
      unsigned i=(prior_row%2)*bytes+b; src[b]=0x40+i;
      if(prior_row==8 || prior_row==9) {
        src[b]=0x33;
        for(unsigned j=0;j<live.size();++j) if(i==live[j]) src[b]=offsets[j];
      }
    }
    memory_in.prequest.pready=sent<2 || t%3!=0;
    // Delay unordered replies until all five active bytes have reserved credits.
    memory_in.presponse.pvalid=acks<sent && t>=last_issue+8 && (ordered || sent==live.size());
    const unsigned response_pa=physical+offsets[acks%offsets.size()];
    auto *rsp=reinterpret_cast<unsigned char*>(&memory_in.presponse.pbits);
    for(unsigned b=0;b<bytes;++b) rsp[b]=byte_at((response_pa&~(bytes-1))+b);
    write_in.pready=write_stalls>=5;
    eval(); CHECK(index_chain_out.punread==unread);
    if(returning && (prior_row==8 || prior_row==9)) unread&=~(1ull<<prior_row);
    returning=read_out.pvalid && read_in.pready;
    if(returning) {
      prior_row=read_out.pbits;
      if(prior_row<16) { CHECK(prior_row==8+index_reads && t>=(index_reads ? 18u : 8u)); ++index_reads; }
      else { CHECK(store && prior_row==16+data_reads); ++data_reads; }
    }
    const unsigned limit=ordered ? 1 : row_credits;
    if(sent-acks==limit && !memory_in.presponse.pvalid) { CHECK(!memory_out.prequest.pvalid); blocked=true; }
    if(memory_out.prequest.pvalid) {
      CHECK(sent<live.size() && sent-acks<limit);
      const auto& req=memory_out.prequest.pbits;
      unsigned addr=physical+offsets[sent], lane=addr%bytes;
      CHECK(bool(req.pstore)==store && req.paddress==(addr&~(bytes-1)) && req.pbyte_umask==(1u<<lane));
      if(store) CHECK(reinterpret_cast<const unsigned char*>(&req.pdata)[lane]==0x40+live[sent]);
      if(memory_in.prequest.pready) {
        // The second active element can issue before the first reply.
        // A masked lane costs a cursor cycle, but never takes a credit.
        if(!ordered && sent==1) CHECK(!acks && t==last_issue+live[1]);
        ++sent; last_issue=t;
      }
    }
    acks+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    if(write_out.pvalid) {
      const auto& w=write_out.pbits;
      CHECK(!store && writes<2 && w.paddress==16+writes && (chain_out.punwritten_uvd & (1ull<<w.paddress)));
      for(unsigned b=0;b<bytes;++b) {
        unsigned i=writes*bytes+b;
        CHECK(reinterpret_cast<const unsigned char*>(&w.pmask)[b]==(((predicate>>i)&1)?255:0));
        for(unsigned j=0;j<live.size();++j) if(i==live[j])
          CHECK(reinterpret_cast<const unsigned char*>(&w.pdata)[b]==byte_at(physical+offsets[j]));
      }
      if(write_in.pready) ++writes; else ++write_stalls;
    }
    done+=finished_out.pvalid; tick_model();
  }
  CHECK(done==1 && !unread && blocked && sent==live.size() && acks==sent && index_reads==2);
  CHECK(data_reads==(store?2u:0u) && writes==(store?0u:2u) && (store || write_stalls==5));
}

static void wide_indexed(bool store,bool ordered,unsigned eew) {
  const unsigned size=1u<<eew, base=0x4203, physical=0x60203, vl=bytes+2;
  // The first credit is occupied when the next element needs two fragments.
  // Its first byte ends a cache row. A later element repeats the split address.
  const std::vector<unsigned> live{1,2,3,bytes+1};
  const std::array<unsigned,4> offsets{0x80,bytes-4,0x50,bytes-4};
  struct Fragment { unsigned pa, logical, count; };
  std::vector<Fragment> expected;
  std::vector<unsigned> destinations;
  unsigned predicate=0;
  for(unsigned j=0;j<live.size();++j) {
    predicate|=1u<<live[j];
    unsigned logical=live[j]*size, addr=physical+offsets[j];
    for(unsigned b=0;b<size;) {
      unsigned n=std::min(size-b,bytes-(addr+b)%bytes);
      expected.push_back({addr+b,logical+b,n}); b+=n;
    }
    if(destinations.empty() || destinations.back()!=logical/bytes) destinations.push_back(logical/bytes);
  }
  launch_0_in={}; launch_1_in={}; prepare_in={}; memory_in={}; read_result_in={};
  chain_in={}; store_reads_in={}; index_chain_in={}; mask_0_in={}; mask_1_in={};
  auto& start=store ? launch_1_in : launch_0_in;
  auto& d=start.pbits.pdescriptor;
  auto& tap=store ? mask_1_in : mask_0_in;
  const auto mask_with_prestart=predicate|1u;
  std::memcpy(&tap.pdata,&mask_with_prestart,sizeof(mask_with_prestart));
  d.pcontrol.pmemory=1; d.pcontrol.pstore=store; d.pcontrol.pmemory_uaddressing=ordered?3:2;
  d.pmasked=1; d.pstate.pvl=vl; d.pstate.pvstart=1; d.pstate.pvtype=(eew<<3)|3; // LMUL=8
  d.pscalar=base; d.pinstruction=(4u<<20)|(8u<<7); d.pprecertified=1;
  d.pcertificate.pppn=0x60; d.pcertificate.prange.pfirst=base; d.pcertificate.prange.plast=base+254+size;
  start.pbits.pkind=ordered ? inline_ordered : inline_index; start.pbits.powner=1; prepare_in.pbits=start.pbits;
  start.pvalid=prepare_in.pvalid=1; eval(); CHECK(prepare_out.pready); tick_model(); start.pvalid=prepare_in.pvalid=0;
  unsigned sent=0,acks=0,grant=0,indices=0,prior=0,done=0,stalls=0,suffix_stalls=0,peak=0;
  std::uint64_t unread_indices=3ull<<8;
  bool returning=false;
  for(unsigned t=0;t<400 && !done;++t) {
    read_in.pready=t%5!=0; read_result_in.pvalid=returning;
    auto* source=reinterpret_cast<unsigned char*>(&read_result_in.pbits);
    for(unsigned b=0;b<bytes;++b) {
      if(prior>=16) source[b]=0x40+(prior-16)*bytes+b;
      else {
        source[b]=0xff;
        for(unsigned j=0;j<live.size();++j) if((prior-8)*bytes+b==live[j]) source[b]=offsets[j];
      }
    }
    memory_in.prequest.pready=sent!=2 || suffix_stalls>=5;
    memory_in.presponse.pvalid=acks<sent && (acks || t>=40) && t%4==0;
    const auto& response=expected[acks%expected.size()];
    auto* data=reinterpret_cast<unsigned char*>(&memory_in.presponse.pbits);
    for(unsigned b=0;b<bytes;++b) data[b]=byte_at(response.pa/bytes*bytes+b);
    write_in.pready=stalls>=5; eval();
    CHECK(index_chain_out.punread==unread_indices);
    if(returning && prior<16) unread_indices &= ~(1ull<<prior);
    returning=read_in.pready && read_out.pvalid;
    if(returning) {
      prior=read_out.pbits;
      if(prior<16) { CHECK(prior==8+indices); ++indices; }
    }
    if(memory_out.prequest.pvalid) {
      CHECK(sent<expected.size() && sent-acks<(ordered?1u:row_credits));
      const auto& f=expected[sent]; const auto& req=memory_out.prequest.pbits;
      unsigned lane=f.pa%bytes;
      CHECK(req.paddress==f.pa-lane && req.pbyte_umask==((1u<<f.count)-1)<<lane && bool(req.pstore)==store);
      if(store) for(unsigned b=0;b<f.count;++b)
        CHECK(reinterpret_cast<const unsigned char*>(&req.pdata)[lane+b]==0x40+f.logical+b);
      if(memory_in.prequest.pready) ++sent; else ++suffix_stalls;
    }
    if(t==39) CHECK(acks==0 && sent==(ordered?1u:row_credits));
    acks+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    peak=std::max(peak,sent-acks);
    if(write_out.pvalid) {
      const auto& w=write_out.pbits;
      CHECK(!store && grant<destinations.size() && w.paddress==16+destinations[grant]);
      CHECK(chain_out.punwritten_uvd & (1ull<<w.paddress));
      for(unsigned b=0;b<bytes;++b) {
        unsigned pos=destinations[grant]*bytes+b, i=pos/size;
        CHECK(reinterpret_cast<const unsigned char*>(&w.pmask)[b]==(((predicate>>i)&1)?255:0));
        for(unsigned j=0;j<live.size();++j) if(i==live[j])
          CHECK(reinterpret_cast<const unsigned char*>(&w.pdata)[b]==byte_at(physical+offsets[j]+pos%size));
      }
      if(write_in.pready) ++grant; else ++stalls;
    }
    if(finished_out.pvalid) { CHECK(acks==expected.size()); ++done; }
    tick_model();
  }
  CHECK(done && !unread_indices && indices==2 && sent==expected.size() && suffix_stalls==5 && peak==(ordered?1u:row_credits));
  CHECK(store ? grant==0 : grant==destinations.size() && stalls==5);
}

// Check the read pipeline at the production DLEN, including a denied lookahead,
// two promised ingress slots, and immutable data after the source claim drops.
static void store_ingress(bool stalled) {
  constexpr unsigned rows = 6, first_row = 16, base = 0x60000;
  launch_0_in={}; launch_1_in={}; prepare_in={}; memory_in={}; read_result_in={};
  store_reads_in={}; index_chain_in={}; chain_in={};
  auto& d=launch_1_in.pbits.pdescriptor;
  d.pcontrol.pstore=1; d.pinstruction=8u<<7; d.pscalar=0x4000;
  d.pstate.pvtype=2; d.pstate.pvstart=2; d.pstate.pvl=rows*bytes-1; // e8,m4
  window_pages[0]=window_pages[1]=base>>12;
  launch_1_in.pbits.powner=2; prepare_in.pbits=launch_1_in.pbits;
  launch_1_in.pvalid=prepare_in.pvalid=1; eval(); CHECK(prepare_out.pready && launch_1_out.pready);
  tick_model(); launch_1_in.pvalid=prepare_in.pvalid=0;

  unsigned reads=0, captures=0, sent=0, acks=0, done=0;
  bool returning=false;
  for(unsigned t=0;t<100 && !done;++t) {
    read_in.pready=!stalled || t!=5;
    store_reads_in.polder_uwrites=stalled && t<5 ? 1ull<<(first_row+1) : 0;
    read_result_in.pvalid=returning;
    auto* src=reinterpret_cast<unsigned char*>(&read_result_in.pbits);
    for(unsigned b=0;b<bytes;++b) src[b]=returning ? byte_at(base+captures*bytes+b) : 0xdd;
    memory_in.prequest.pready=!stalled || t>=16;
    memory_in.presponse.pvalid=acks<sent && (!stalled || t>=24);
    eval();
    CHECK(store_reads_out.punread_uvs3 == (((1ull<<rows)-(1ull<<captures))<<first_row));
    const bool grant=read_out.pvalid && read_in.pready;
    if(!stalled && t<rows) CHECK(grant);
    if(stalled && t==1) CHECK(!read_out.pvalid); // row 1 still belongs to an older writer
    if(stalled && t==5) CHECK(read_out.pvalid && !grant);
    if(stalled && t>=8 && t<16) CHECK(reads==2 && captures==2 && !read_out.pvalid);
    if(grant) { CHECK(reads<rows && read_out.pbits==first_row+reads); ++reads; }
    if(returning) ++captures;
    if(memory_out.prequest.pvalid) {
      const auto& req=memory_out.prequest.pbits;
      const unsigned address=base+sent*bytes;
      unsigned mask=(1u<<bytes)-1;
      if(!sent) mask &= ~3u;
      if(sent+1==rows) mask >>= 1;
      CHECK(sent<rows && req.pstore && req.paddress==address && req.pbyte_umask==mask);
      for(unsigned b=0;b<bytes;++b) if(mask & (1u<<b))
        CHECK(reinterpret_cast<const unsigned char*>(&req.pdata)[b]==byte_at(address+b));
      sent+=memory_in.prequest.pready;
    }
    CHECK(reads-sent<=2);
    acks+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    if(finished_out.pvalid) { CHECK(acks==rows && finished_out.pbits==2); ++done; }
    tick_model(); returning=grant;
  }
  CHECK(done && reads==rows && captures==rows && sent==rows);
}

int main() {
  return run_test([] {
    reset=1; tick_model(); reset=0;
    store_ingress(false);
    store_ingress(true);
    exercise(bytes-5,1,2,2,8,false,UINT64_MAX,true); // checked e32 lane 1, physically disjoint pages
    for(bool store:{false,true}) for(bool ordered:{false,true}) for(bool hole:{false,true}) indexed_bytes(store,ordered,hole);
    for(unsigned e=1;e<=(bytes==8 ? 2u : 3u);++e)
      for(bool store:{false,true}) for(bool ordered:{false,true}) wide_indexed(store,ordered,e);
    for(bool store:{false,true}) for(int stride:{6,-6}) for(bool masked:{false,true})
      strided_peer(store,stride,masked);
    segmented_fragments(2,0,true);
    for(unsigned n=2;n<=8;++n)
      for(unsigned e=0;(1u<<e)<=bytes/2;++e) segmented_fragments(n,e);
    for (unsigned shift=1;shift<bytes;++shift) {
      exercise(shift,0,2*bytes,0);       // first beat cannot write yet; middle beat contributes twice
      exercise(shift,bytes-1,bytes+1,0); // vstart in the previous row's suffix, short final carry
      exercise(shift,0,1,0);             // partial load contained in a single beat
    }
    for (unsigned eew=1;(1u<<eew)<bytes;++eew)
      exercise(1u<<eew,0,2*bytes>>eew,eew,31); // final memory beat extends past the last physical VRF row
    for (unsigned eew=0;(1u<<eew)<bytes;++eew)
      for (auto pred : {UINT64_C(0),UINT64_C(1),UINT64_C(0x69699669),UINT64_MAX,
                        UINT64_C(1)|(UINT64_C(1)<<((2*bytes)>>eew))}) {
        exercise(1u<<eew,0,(4*bytes)>>eew,eew,8,true,pred);
        exercise(bytes-(1u<<eew),1,((4*bytes)>>eew)-1,eew,8,true,pred);
      }
  });
}
