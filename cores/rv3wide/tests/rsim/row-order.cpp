// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <algorithm>
#include <deque>
#include <cstring>

unsigned cycle, ack_at, acknowledgements, requests, retire_cycle;
constexpr unsigned row_credits = 5;
std::uint64_t pc = 0x8000, retired_pc, replay_pc, data, returned, staged;
bool scalar_lookup, scalar_store, load_run, hold_loads;
unsigned loads, replies;
bool mixed, stride_order, indexed_order, indexed_scatter, index_proof;
unsigned first_load, index_base=0x400, checked_address, checked_bytes;
bool checked_gather;
bool wide_scatter;
bool iterative_scatter, proof_response, unordered_slices;
std::deque<unsigned> scatter_bytes;
unsigned element_checks, first_scatter_grant;
bool masked_store, masked_load, displaced_store, sparse_store, hold_requests, watch_writes;
unsigned first_store;
std::vector<unsigned> vrf_rows;
extern "C" void observe_vrf_write(std::uint64_t resetting, std::uint64_t valid, std::uint64_t row) {
  if (!resetting && watch_writes && valid) vrf_rows.push_back(row);
}
std::deque<bool> ordered;
unsigned release_at, last_reply;

void step() {
  CHECK(++cycle < 3000);
  // An accepted row holds the physical drain low until its real acknowledgement.
  memory_in.pdrained = loads == replies && requests == acknowledgements;
  memory_in.prequest.pready = pipeline_in.pcommit_uready = 1;
  pipeline_in.presponse.pvalid = scalar_lookup;
  pipeline_in.presponse.pbits.poutcome = scalar_store ? 2 : 1;
  pipeline_in.presponse.pbits.pdata = data;
  row_memory_in.prequest.pready = !hold_requests;
  std::memset(&row_memory_in.presponse.pbits,0x33,sizeof(row_memory_in.presponse.pbits));
  row_memory_in.presponse.pvalid = mixed ? !ordered.empty() && cycle >= release_at
                               : load_run ? loads != replies && !hold_loads
                                            : requests != acknowledgements && cycle >= ack_at;
  vector_fast_in.pcandidate_ucurrent = 1;
  eval();
  if(iterative_scatter || checked_gather) {
    vector_precheck_in.prequest.pready=(cycle%4)!=0;
    vector_precheck_in.presponse.pvalid=proof_response;
    vector_precheck_in.presponse.pbits=1;
    eval();
    if(proof_response && vector_precheck_out.presponse.pready) proof_response=false;
    if(vector_precheck_out.prequest.pvalid && vector_precheck_in.prequest.pready) {
      const auto& span=vector_precheck_out.prequest.pbits;
      CHECK(!proof_response && element_checks==0);
      if (checked_gather) CHECK(!span.pstore && span.pfirst==0 && span.plast==0xfff);
      else CHECK(span.pstore && span.pfirst==0x1000 && span.plast==0x1fff);
      ++element_checks; proof_response=true;
    }
  }
  if(indexed_order && vector_fast_out.prequest.pvalid) {
    const auto& span=vector_fast_out.prequest.pbits;
    const unsigned word=sizeof(row_memory_in.presponse.pbits)/2;
    const unsigned last=index_base+255+(wide_scatter?3:0);
    CHECK(last<=0xfff && span.pfirst==(index_base&~(word-1)) && span.plast==(last|(word-1)));
    index_proof=true;
  }
  vector_fast_in.presponse.pvalid = vector_fast_out.prequest.pvalid;
  vector_fast_in.presponse.pbits.prange = vector_fast_out.prequest.pbits;
  vector_fast_in.presponse.pbits.pppn = 0;
  eval();
  if (!reset) {
    for (const auto* r : {&retired_0_out, &retired_1_out, &retired_2_out})
      if (r->pvalid) { retired_pc = r->pbits.pfetched.ppc; returned = r->pbits.pdata; retire_cycle = cycle; }
    if (redirect_out.pvalid) {
      CHECK(redirect_out.pbits.presolution.pdisposition != 1);
      if (redirect_out.pbits.presolution.pdisposition == 2) replay_pc = redirect_out.pbits.ptarget;
    }
    if (row_memory_out.prequest.pvalid && row_memory_in.prequest.pready) {
      const auto& req = row_memory_out.prequest.pbits;
      const unsigned row_bytes=sizeof(req.pdata);
      if (iterative_scatter) {
        CHECK(req.pstore && req.paddress==0x400 && req.pbyte_umask==4);
        CHECK(ordered.size()==(unordered_slices ? requests-first_store : 0));
        if (!unordered_slices && requests==first_store) release_at=cycle+32;
        if (unordered_slices) {
          if (requests==first_store) first_scatter_grant=cycle;
          else { CHECK(cycle==first_scatter_grant+1); release_at=cycle+24; }
        }
      } else if (checked_gather) {
        CHECK(!req.pstore && ordered.empty());
        CHECK(req.paddress==(checked_address & ~(row_bytes-1)));
        CHECK(req.pbyte_umask==(low_mask(checked_bytes)<<(checked_address%row_bytes)));
      } else if (wide_scatter) {
        const unsigned part=(requests-first_store)%2, first=index_base+2;
        CHECK(req.pstore && ordered.empty());
        CHECK(req.paddress==(first&~(row_bytes-1))+part*row_bytes);
        CHECK(req.pbyte_umask==(part?7u:1u<<(row_bytes-1)));
      } else if (indexed_order) {
        // v8 contains 0x5a; independent element identities all name byte 0x45a.
        CHECK(ordered.size()<(indexed_scatter ? row_credits : 1u) && req.paddress==((index_base+0x5a) & ~(row_bytes-1)));
        CHECK(req.pbyte_umask==(1u<<(0x5a%row_bytes)) && bool(req.pstore)==indexed_scatter);
      } else if (stride_order) {
        unsigned lane=6-3*(loads-first_load);
        CHECK(req.paddress==0x400 && req.pbyte_umask==(req.pstore ? 7u : 1u<<lane));
        if(!req.pstore) CHECK(acknowledgements==first_store+1);
      } else if (sparse_store) {
        CHECK(req.pstore && requests-first_store<2);
        CHECK(req.paddress==0x400u+(requests-first_store)*2*row_bytes && req.pbyte_umask==(1u<<4));
      } else if (displaced_store) {
        CHECK(req.paddress==(req.pstore ? 0x400u+(requests-first_store)*row_bytes : 0x400u));
        if (req.pstore) CHECK(req.pbyte_umask==(requests==first_store ? low_mask(row_bytes)-15 : 15));
        else CHECK(acknowledgements==first_store+2); // overlaps the first beat; cannot pass the spill ACK
      } else CHECK(req.paddress == (masked_store ? 0x400u+(requests-first_store)*row_bytes : mixed && !masked_load && !req.pstore ? 0x440u : 0x400u));
      if (mixed) ordered.push_back(req.pstore);
      else CHECK(bool(req.pstore) != load_run);
      if (!req.pstore) {
        if (masked_load) CHECK(req.pbyte_umask==(0x3333u & low_mask(row_bytes)));
        ++loads;
      } else {
        CHECK(requests - acknowledgements < row_credits);
        if (!masked_store) CHECK(!row_memory_in.presponse.pvalid);
        const auto* bytes=reinterpret_cast<const unsigned char*>(&req.pdata);
        if (iterative_scatter) {
          CHECK(bytes[2]==0x33+requests-first_store);
          scatter_bytes.push_back(bytes[2]);
        }
        else if (wide_scatter) {
          for(unsigned b=0;b<row_bytes;++b) if(req.pbyte_umask>>b & 1) CHECK(bytes[b]==(b==row_bytes-1 ? 0x33 : 0));
        } else if (stride_order) CHECK(bytes[0]==0x5a && bytes[1]==0x5a && bytes[2]==0x5a);
        else if (sparse_store) CHECK(bytes[4]==0x5a);
        else if (masked_store) {
          CHECK(requests-first_store<4 && req.pbyte_umask==(0x3333u & low_mask(row_bytes)));
          for (unsigned i=0;i<row_bytes;++i) if (req.pbyte_umask>>i & 1) CHECK(bytes[i]==0x5a);
        } else for (unsigned i=0;i<row_bytes;++i) if (req.pbyte_umask>>i & 1) CHECK(bytes[i]==0x33);
        staged = iterative_scatter ? bytes[2] : 0x33333333; ++requests; ack_at = cycle + 24;
      }
    }
    if (requests != acknowledgements) CHECK(!pipeline_out.prequest.pvalid && !pipeline_out.pcommit && !memory_out.prequest.pvalid);
    if (row_memory_in.presponse.pvalid && row_memory_out.presponse.pready) {
      const bool load = mixed ? !ordered.front() : load_run;
      if (mixed) { ordered.pop_front(); last_reply = cycle; release_at = cycle+24; }
      if (load) ++replies;
      else {
        data = iterative_scatter ? scatter_bytes.front() : staged;
        if (iterative_scatter) scatter_bytes.pop_front();
        ++acknowledgements; if (acknowledgements != requests) ack_at = cycle + 24;
      }
    }
  }
  if (checked_gather) CHECK(!pipeline_out.prequest.pvalid);
  scalar_lookup = pipeline_out.prequest.pvalid;
  scalar_store = pipeline_out.prequest.pbits.pmemory.paccess == 2;
  tick_model(); eval();
}
void send(unsigned insn) {
  const auto here = pc; pc += 4;
  instructions_in = {};
  instructions_in.pvalid = instructions_in.pbits.pcount = 1;
  auto& i = instructions_in.pbits.pentries[0];
  i.ppc = here; i.psequential_upc = here + 4; i.pinstruction = i.praw_uinstruction = insn;
  while (retired_pc != here) {
    eval();
    bool accepted = instructions_in.pvalid && instructions_out.pready;
    step();
    if (accepted) instructions_in.pvalid = 0;
    if (replay_pc == here) { instructions_in.pvalid = 1; replay_pc = 0; }
  }
  instructions_in.pvalid = 0;
}

// Give WB a faultable vector load and a younger scalar store in the same group.
// A reserved descriptor is not authorization for the store, even with L1D ready.
void faultable_pair() {
  const auto base = (pc + 15) & ~UINT64_C(15);
  instructions_in = {};
  instructions_in.pbits.pcount = 2;
  instructions_in.pvalid = 1;
  for (unsigned lane=0; lane<2; ++lane) {
    auto &i = instructions_in.pbits.pentries[lane];
    i.ppc = base + 4*lane;
    i.psequential_upc = i.ppc + 4;
    // Lane 0: vle8.v v12,(x1). Lane 1: sw x2,0(x1).
    i.pinstruction = i.praw_uinstruction = lane ? 0x0020a023u : 0x02008607u;
  }
  vector_fast_in = {};
  vector_precheck_in = {};
  element_translation_in = {};
  memory_in.pdrained = memory_in.prequest.pready = pipeline_in.pcommit_uready = 1;
  row_memory_in.presponse.pvalid = 0;
  row_memory_in.prequest.pready = 1;
  bool lookup = false, pair = false, outstanding = false, trapped = false;
  unsigned exact = 0, due = 0, proofs = 0, after = 0;
  for (unsigned t=0; t<150; ++t) {
    pipeline_in.presponse.pvalid = lookup;
    pipeline_in.presponse.pbits.poutcome = 2; // even a potential scalar store hit cannot commit
    vector_precheck_in.prequest.pready = !outstanding;
    vector_precheck_in.presponse.pvalid = outstanding;
    vector_precheck_in.presponse.pbits = 0; // decline the envelope; check the actual bytes
    element_translation_in.prequest.pready = 1;
    element_translation_in.presponse.pvalid = exact && t==due;
    element_translation_in.presponse.pbits.pservice = 3; // Fault
    element_translation_in.presponse.pbits.pcause = 13; // load page fault
    eval();
    const bool accepted = instructions_in.pvalid && instructions_out.pready;
    pair |= memory_stage_0_out.pvalid && memory_stage_0_out.pbits.ppc==base &&
            memory_stage_1_out.pvalid && memory_stage_1_out.pbits.ppc==base+4;
    CHECK(!pipeline_out.pcommit && !memory_out.prequest.pvalid && !row_memory_out.prequest.pvalid);
    for (const auto* r : {&retired_0_out, &retired_1_out, &retired_2_out})
      CHECK(!r->pvalid || (r->pbits.pfetched.ppc!=base && r->pbits.pfetched.ppc!=base+4));
    if (vector_precheck_out.presponse.pready && outstanding) outstanding=false;
    if (vector_precheck_out.prequest.pvalid && vector_precheck_in.prequest.pready) { outstanding=true; ++proofs; }
    if (element_translation_out.prequest.pvalid) {
      CHECK(!exact && element_translation_out.prequest.pbits.pfirst==0x400);
      ++exact; due=t+8;
    }
    if (redirect_out.pvalid && redirect_out.pbits.presolution.pdisposition==1) {
      CHECK(!trapped && exact==1 && t>due && redirect_out.pbits.presolution.pvalue==0x400);
      trapped=true;
    }
    lookup = pipeline_out.prequest.pvalid;
    tick_model();
    if (accepted) instructions_in.pvalid=0;
    if (trapped && ++after==5) break;
  }
  CHECK(pair && proofs && exact==1 && trapped);
}
int main() {
  return run_test([] {
    reset = 1; step(); step(); reset = 0;
    send(0x60000093); send(0x30009073); // mstatus.VS
    send(0x40000093); send(0x03300113); // x1=address, x2=0x33
    send(0xc00072d7 | (sizeof(row_memory_in.presponse.pbits)<<15)); // vsetivli: one full DLEN row, e8,m1
    send((23u<<26)|(1u<<25)|(2u<<15)|(4u<<12)|(8u<<7)|0x57);
    for (unsigned younger : {0x0000a503u, 0x0020a023u, 0x0ff0000fu}) { // lw, sw, fence rw,rw
      data = 0;
      const auto before = acknowledgements;
      send(0x02008427); // vse8.v v8,(x1)
      while (requests == before) step();
      send(0x02008427);
      while (requests != before + 2) step();
      CHECK(acknowledgements == before+1); // VSIQ was not replaced before A's ACK.
      send(younger);
      CHECK(acknowledgements == before + 2 && requests == acknowledgements && retire_cycle > ack_at);
      if (younger == 0x0000a503u) CHECK(returned == 0x33333333);
    }
    // The first load, not an unrelated scalar operation, holds L1D non-drained.
    load_run = hold_loads = true;
    send(0x02008407); // vle8.v v8,(x1)
    while (loads != 1) step();
    const auto before = requests;
    send(0x02008427); // The same-address store can reserve, but cannot issue to L1D.
    CHECK(!replies && requests == before && !memory_in.pdrained);
    hold_loads = false;
    while (!replies) step();
    load_run = false;
    send(0x0ff0000f);
    CHECK(requests == before+1 && requests == acknowledgements);
    send(0x44000193); // x3: disjoint physical range in the same page
    mixed = true;
    for (bool store_first : {false, true}) {
      const auto old_loads = loads, old_stores = requests;
      const unsigned store = 0x02008427, load = 0x02018607; // v8,(x1); v12,(x3)
      release_at = cycle + 300; // allow both requests, but no response
      send(store_first ? store : load);
      while (ordered.empty()) step();
      send(store_first ? load : store);
      while (ordered.size() != 2) { CHECK(cycle+1 < release_at); step(); }
      CHECK(ordered.front() == store_first && ordered.back() != store_first);
      CHECK(replies == old_loads && acknowledgements == old_stores && !memory_in.pdrained);
      release_at = cycle+16;
      send(0x0ff0000f);
      CHECK(ordered.empty() && retire_cycle > last_reply);
      CHECK(replies == old_loads+1 && acknowledgements == old_stores+1);
    }
    // One DLEN row on each production geometry. Keep the store outstanding
    // while VLSeq captures the 0x33 predicate and VXSeq overwrites v0.
    send((23u<<26)|(1u<<25)|(2u<<15)|(4u<<12)|0x57); // vmv.v.x v0,x2
    send(0x0ff0000f);
    masked_load=true; watch_writes=true; vrf_rows.clear();
    const auto old_acks=acknowledgements, old_loads=loads;
    release_at=cycle+300;
    send(0x02008427); // vse8.v v8,(x1)
    while (ordered.empty()) step();
    send(0x00008607); // vle8.v v12,(x1),v0.t
    send((11u<<26)|(1u<<25)|(8u<<20)|(8u<<15)|0x57); // vxor.vv v0,v8,v8
    for (unsigned n=0;n<16;++n) step();
    CHECK(acknowledgements==old_acks && loads==old_loads && ordered.size()==1 && ordered.front());
    CHECK((vrf_rows==std::vector<unsigned>{0}));
    release_at=cycle+1; send(0x0ff0000f);
    CHECK(acknowledgements==old_acks+1 && loads==old_loads+1 && ordered.empty());
    watch_writes=masked_load=false;
    // x3 = x1 + 4. One e8 register row now needs two physical store beats.
    // Check a fence directly behind the store, then an aliasing vector load.
    send(0x40400193);
    displaced_store=true;
    for (bool alias : {false,true}) {
      first_store=requests; release_at=cycle+64;
      send(0x02018427); // vse8.v v8,(x3)
      while (requests!=first_store+2) step();
      CHECK(acknowledgements==first_store);
      if (alias) send(0x02008607); // vle8.v v12,(x1)
      send(0x0ff0000f);
      CHECK(ordered.empty() && retire_cycle>last_reply && acknowledgements==first_store+2);
    }
    displaced_store=false;
    // Four rows share one predicate row. With L1D blocked, VSSeq can capture
    // exactly two rows. Those sources may be overwritten, but v0 must survive.
    mixed=false; masked_store=true;
    constexpr unsigned row_bytes=sizeof(row_memory_in.presponse.pbits);
    send((4*row_bytes<<20) | (4u<<7) | 0x13); // x4 = VL
    send((1u<<20) | (4u<<15) | 0x72d7);      // vsetvli x5,x4,e8,m2
    send(0x05a00313);                         // x6 = 0x5a
    for (bool overwrite_mask : {false,true}) {
      const unsigned rd=overwrite_mask ? 0 : 8;
      auto splat = [](unsigned vd, unsigned rs1) {
        return (23u<<26)|(1u<<25)|(rs1<<15)|(4u<<12)|(vd<<7)|0x57;
      };
      // Retirement launches the splats; their physical writes can arrive later.
      // Isolate the younger XOR's writes without assuming a fixed pipeline latency.
      watch_writes=true; vrf_rows.clear();
      send(splat(8,6)); send(splat(0,2)); // VS3 bytes 0x5a; v0 bytes 0x33
      while(vrf_rows.size()<8) step();
      auto setup=vrf_rows;
      std::sort(setup.begin(),setup.end());
      CHECK((setup==std::vector<unsigned>{0,1,2,3,16,17,18,19}));
      first_store=requests; hold_requests=true; vrf_rows.clear();
      send(0x00008427); // vse8.v v8,(x1),v0.t
      send((11u<<26)|(1u<<25)|(rd<<20)|(rd<<15)|(rd<<7)|0x57); // vxor.vv rd,rd,rd
      for (unsigned n=0;n<30;++n) step();
      CHECK(requests==first_store && row_memory_out.prequest.pvalid);
      if (overwrite_mask) CHECK(vrf_rows.empty());
      else CHECK((vrf_rows==std::vector<unsigned>{16,17}));
      hold_requests=false; send(0x0ff0000f);
      while (vrf_rows.size()<4) step();
      CHECK(requests==first_store+4 && acknowledgements==requests);
      CHECK((vrf_rows==std::vector<unsigned>{2*rd,2*rd+1,2*rd+2,2*rd+3}));
      watch_writes=false;
    }
    // v0 enables elements 0 and 2*DLEN-bytes, leaving an interior hole and
    // an empty tail. No ACK exists for either skip, but FENCE needs both real ACKs.
    send(0x00100393); // x7=1
    send(((2*row_bytes)<<20)|(7u<<15)|(1u<<12)|(7u<<7)|0x13); // slli
    send(0x0013e393); // ori x7,x7,1
    send(0xc00072d7 | (2u<<23) | ((row_bytes/8)<<15)); // one or two 32-bit mask words
    send((23u<<26)|(1u<<25)|(7u<<15)|(4u<<12)|0x57); // splat low word of x7
    send((1u<<20)|(4u<<15)|0x72d7); // restore e8,m2 with four rows
    send((23u<<26)|(1u<<25)|(6u<<15)|(4u<<12)|(8u<<7)|0x57); // v8=0x5a
    first_store=requests; masked_store=false; sparse_store=true; mixed=true; release_at=cycle+80;
    send(0x00018427); // vse8.v v8,(x3),v0.t
    while (requests<first_store+2) step();
    CHECK(acknowledgements==first_store);
    send(0x0ff0000f);
    CHECK(requests==first_store+2 && acknowledgements==requests && retire_cycle>last_reply);
    // x3=0x1406 and x1=0x400 map to the same physical page. Only
    // the last element of the descending load aliases the older store.
    sparse_store=false; stride_order=true; first_store=requests; first_load=loads;
    send(0x000011b7); send(0x40618193); send(0xffd00213); // x4 = -3
    send(0xc00072d7 | (3u<<15)); // VL=3, e8
    release_at=cycle+250;
    send(0x02008427); while(ordered.empty()) step(); // vse8.v v8,(x1)
    send(0x0a418607); // vlse8.v v12,(x3),x4
    CHECK(cycle<release_at && loads==first_load && acknowledgements==first_store);
    release_at=cycle+4; send(0x0ff0000f);
    CHECK(loads==first_load+3 && replies==loads && retire_cycle>last_reply);
    // Last base whose complete ei8+word envelope fits. v8's byte offset 2
    // puts each word at the final lane on both DLENs; delay every ACK.
    send(0x00200313);
    send((23u<<26)|(1u<<25)|(6u<<15)|(4u<<12)|(8u<<7)|0x57); // v8 bytes = 2
    send(0xc00072d7|(16u<<20)|(2u<<15)); // e32,m1 VL=2
    send((23u<<26)|(1u<<25)|(2u<<15)|(4u<<12)|(12u<<7)|0x57); // v12 words = 0x33
    stride_order=false; indexed_order=true;
    index_base=0xefd; wide_scatter=true; index_proof=false; first_store=requests;
    send(0x000010b7); send(0xefd08093); // x1=0xefd
    release_at=cycle+80;
    send((3u<<26)|(1u<<25)|(8u<<20)|(1u<<15)|(12u<<7)|0x27u);
    while(ordered.empty()) step();
    CHECK(index_proof && cycle<release_at && acknowledgements==first_store);
    send(0x0ff0000f);
    CHECK(requests==first_store+4 && acknowledgements==requests && retire_cycle>last_reply);
    // One more byte would put the greatest index's suffix on the next page.
    // Actual offsets are both 2: the precise path may complete normally.
    index_base=0xefe; index_proof=false; first_load=loads;
    checked_gather=true; checked_address=index_base+2; checked_bytes=4; element_checks=0;
    send(0x00108093);
    send((3u<<26)|(1u<<25)|(8u<<20)|(1u<<15)|(12u<<7)|7u);
    send(0x0ff0000f);
    CHECK(!index_proof && element_checks==1 && loads==first_load+2 && replies==loads && retire_cycle>last_reply);
    checked_gather=false;
    wide_scatter=false; index_base=0x400;
    send(0x40000093); send(0xc00072d7|(3u<<15));
    send(0x05a00313); send((23u<<26)|(1u<<25)|(6u<<15)|(4u<<12)|(8u<<7)|0x57);
    first_load=loads; release_at=cycle+120;
    send((3u<<26)|(1u<<25)|(8u<<20)|(1u<<15)|(12u<<7)|7u); // vloxei8.v v12,(x1),v8
    while(ordered.empty()) step();
    CHECK(index_proof && loads==first_load+1 && cycle<release_at);
    send(0x0ff0000f);
    CHECK(loads==first_load+3 && replies==loads && retire_cycle>last_reply);
    // An unordered store may have multiple unacknowledged beats. A scalar FENCE following
    // it must wait for all three active bytes, not just issue or the first ACK.
    indexed_scatter=true; first_store=requests; release_at=cycle+80;
    send((1u<<26)|(1u<<25)|(8u<<20)|(1u<<15)|(12u<<7)|0x27u); // vsuxei8.v v12,(x1),v8
    while(ordered.size()<2) { CHECK(cycle+1<release_at); step(); }
    CHECK(acknowledgements==first_store);
    send(0x0ff0000f);
    CHECK(requests==first_store+3 && requests==acknowledgements && retire_cycle>last_reply);
    indexed_scatter=false;
    // Last certifiable base, then one byte beyond it. Actual byte offsets
    // still fit in the first page in both cases; the proof cannot assume that.
    for(unsigned base:{0xf00u,0xf01u}) {
      index_base=base; index_proof=false; first_load=loads;
      checked_gather=base==0xf01; checked_address=base+0x5a; checked_bytes=1; element_checks=0;
      send(0x000010b7); send(((base-0x1000)<<20)|(1u<<15)|(1u<<7)|0x13);
      send((3u<<26)|(1u<<25)|(8u<<20)|(1u<<15)|(12u<<7)|7u);
      send(0x0ff0000f);
      CHECK(index_proof==(base==0xf00));
      CHECK(element_checks==unsigned(checked_gather) && loads==first_load+3 && replies==loads && retire_cycle>last_reply);
    }
    checked_gather=false;
    // A legal narrow-index load can share the upper end of its data group.
    // Distinct register numbers alone must not let it retire as certified.
    index_base=0x400; index_proof=false; first_load=loads;
    send(0x40000093); send(0xc00072d7|(2u<<15)); // e8,m1
    send((23u<<26)|(1u<<25)|(4u<<12)|(11u<<7)|0x57); // v11 index bytes = 0
    send(0xc00072d7|(18u<<20)|(2u<<15)); // e32,m4: v8..v11 and v11
    checked_gather=true; checked_address=index_base; checked_bytes=4; element_checks=0;
    send((3u<<26)|(1u<<25)|(11u<<20)|(1u<<15)|(8u<<7)|7u);
    send(0x0ff0000f);
    CHECK(!index_proof && element_checks==1 && loads==first_load+2 && replies==loads && retire_cycle>last_reply);
    checked_gather=false;
    // Ordered ei16 scatter: two retired-state byte stores target the same
    // virtual address. FENCE cannot retire while the second ACK is delayed.
    indexed_order=false; indexed_scatter=false;
    send(0x00001337); send(0x00230313); // x6 = 0x1002
    send(0xc00072d7|(8u<<20)|(2u<<15)); // e16,m1
    send((23u<<26)|(1u<<25)|(6u<<15)|(4u<<12)|(8u<<7)|0x57);
    send(0xc00072d7|(2u<<15)); // e8,m1, ei16 uses v8..9
    send((20u<<26)|(1u<<25)|(17u<<15)|(2u<<12)|(12u<<7)|0x57); // vid.v v12
    send((1u<<25)|(12u<<20)|(2u<<15)|(4u<<12)|(12u<<7)|0x57); // vadd.vx v12,v12,x2
    iterative_scatter=true;
    for (bool overlap : {false,true}) {
      unordered_slices=overlap; first_store=requests; release_at=~0u; element_checks=0;
      send(((overlap?1u:3u)<<26)|(1u<<25)|(8u<<20)|(1u<<15)|(5u<<12)|(12u<<7)|0x27u);
      CHECK(element_checks==1 && requests==first_store+2 && acknowledgements==first_store+(overlap?0:1));
      send(0x0ff0000f);
      CHECK(requests==acknowledgements && scatter_bytes.empty() && ordered.empty() && retire_cycle>last_reply && data==0x34);
    }
    faultable_pair();
  });
}
