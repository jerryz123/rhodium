// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "test-sites.h"

using Row = std::array<std::uint32_t,4>;
using Word = std::uint64_t;
unsigned stalled_results = 0, granted_results = 0;
extern "C" void observe_load_grant(Word resetting, Word valid, Word ready, Word row,
                                   Word write_valid, Word write_ready, Word destination) {
  if (resetting) return;
  CHECK(bool(valid && ready) == bool(write_valid && write_ready));
  if (valid && ready) { CHECK(row == destination); ++granted_results; }
  if (valid && !ready) ++stalled_results;
}
constexpr Row data(unsigned r) { return {0x44556600u+r,0xfedcba98u-r,r,0x77889900u+r}; }

std::uint64_t next_event(unsigned site) {
  const auto& nodes=rheg::graph().nodes;
  return std::count_if(nodes.begin(),nodes.end(),[&](const auto& n){return n.first.site==site;});
}

std::set<std::uint64_t> parent_sequences(rheg::Ref child, unsigned site) {
  std::set<std::uint64_t> refs;
  for(const auto& [parent,target]:rheg::graph().edges) if(target.site==child.site && target.sequence==child.sequence) {
    CHECK(parent.site==site);
    refs.insert(parent.sequence);
  }
  return refs;
}

void offer_operands(bool store) {
  prepare_in.pvalid=1;
  prepare_in.pbits=store ? launch_1_in.pbits.pissue : launch_0_in.pbits.pissue;
}

// Hold both replies until issue closes, then stall the VRF while both arrive.
// The first two masks differ; the final two rows take no cache or VS3 access.
void exercise(bool store, bool masked) {
  auto& launch_in = store ? launch_1_in : launch_0_in;
  auto& launch_out = store ? launch_1_out : launch_0_out;
  constexpr auto sequence = row_sites::vector_store_sequence, agen = row_sites::vector_store_agen;
  const auto first_row = next_event(sequence), first_agen = next_event(agen);
  const unsigned start = masked ? 1 : 0;
  launch_in.pbits = {};
  launch_in.pbits.pissue.pdescriptor.pstate.pvl = masked ? 59 : 32;
  launch_in.pbits.pissue.pdescriptor.pstate.pvstart = start;
  launch_in.pbits.pissue.pdescriptor.pstate.pvtype = 1; // e8,m2
  launch_in.pbits.pissue.pdescriptor.pcontrol.pstore = store;
  launch_in.pbits.pissue.pdescriptor.pmasked = masked;
  launch_in.pbits.pissue.pdescriptor.ppc = 0x1000 + 4 * masked + 8 * store;
  const bool aliases_v0 = store && masked;
  launch_in.pbits.pissue.pdescriptor.pinstruction = (aliases_v0 ? 0u : 8u) << 7;
  launch_in.pbits.pissue.pdescriptor.pscalar = 0x3ff0;
  launch_in.pbits.pissue.powner=3;
  launch_in.pbits.ppages[0]=0x60; launch_in.pbits.ppages[1]=0x90;
  launch_in.pvalid=1;
  offer_operands(store);
  if (!store) {
    const auto vd=std::uint64_t(masked ? 15 : 3)<<16;
    launch_in.pvalid=0; chain_in.polder_uwrites=1;
    eval(); CHECK(prepare_out.pready); tick_model(); prepare_in.pvalid=0;
    for (unsigned n=0;n<4;++n) {
      eval(); CHECK(!mask_0_out.psample.pvalid && !memory_out.prequest.pvalid && !issued_out.pvalid);
      CHECK(chain_out.powner==3 && chain_out.punread_uv0==masked && chain_out.punwritten_uvd==vd);
      tick_model();
    }
    launch_in.pvalid=1;
  }
  eval(); CHECK(launch_out.pready && (!store || prepare_out.pready)); tick_model();
  launch_in.pvalid=prepare_in.pvalid=0;
  chain_in.polder_uwrites=0;

  unsigned requests=0, responses=0, writes=0, tails=0, finished_count=0, source_reads=0;
  unsigned first_request=0, first_offer=0, first_write=0;
  std::array<unsigned,2> masks = masked ? std::array<unsigned,2>{0x1042,0xa801}
                                        : std::array<unsigned,2>{0xffff,0xffff};
  bool source_return=false;
  unsigned source_row=0;
  unsigned predicate_samples=0;
  const unsigned source_base=aliases_v0 ? 0 : 16;
  std::uint64_t unread=store ? std::uint64_t(masked ? 15 : 3)<<source_base : 0;
  unsigned predicate_claim=masked;
  const Row predicate{0xa8011043u,0,0,0};
  auto source = [&](unsigned r) { return aliases_v0 && r==0 ? predicate : data(r); };
  static std::uint64_t return_serial = 0, write_serial = 0;
  const auto check_transfer = [&](unsigned site, std::uint64_t& serial, bool fired, unsigned row) {
    const rheg::Ref ref{site, serial};
    const auto& graph = rheg::graph();
    CHECK(graph.nodes.contains(ref) == fired);
    if (!fired) return;
    CHECK(graph.field(ref, "row").unsigned_value() == row);
    CHECK(graph.field(ref, "owner").unsigned_value() == 3);
    CHECK(graph.field(ref, "pc").unsigned_value() == launch_in.pbits.pissue.pdescriptor.ppc);
    ++serial;
  };
  for (unsigned t=1;t<150;++t) {
    read_in.pready=1;
    mask_0_in.pdata.words=mask_1_in.pdata.words=predicate;
    read_result_in.pvalid=source_return;
    read_result_in.pbits.words=source(source_row);
    // Unmasked loads have an always-ready request port: demand consecutive accepts.
    memory_in.prequest.pready = !masked || (first_offer && t>=first_offer+5);
    memory_in.presponse.pvalid = tails && responses<2 && t>40;
    memory_in.presponse.pbits.words=data(responses);
    write_in.pready = first_write && t>=first_write+8;
    eval();
    CHECK(store_reads_out.punread_uvs3==unread && store_reads_out.punread_uv0==(store ? predicate_claim : 0));
    CHECK(chain_out.punread_uv0==(store ? 0 : predicate_claim));
    if (store && (unread || predicate_claim)) CHECK(store_reads_out.powner==3);
    if (source_return) unread &= ~(UINT64_C(1)<<(source_base+source_row));
    const bool sampled=(store ? mask_1_out : mask_0_out).psample.pvalid;
    if (t==1) CHECK(sampled==masked);
    if (sampled) {
      // Rows 2 and 3 skip VS3. All four rows use the same v0 shadow row,
      // including after physical row 0 has already been captured as VS3.
      if (store && predicate_samples>=2) unread &= ~(UINT64_C(1)<<(source_base+predicate_samples));
      if (++predicate_samples==4) predicate_claim=0;
    }
    CHECK(busy);
    if (!store) CHECK(!prepare_out.pready);
    if (memory_out.prequest.pvalid) {
      if (!first_offer) first_offer=t;
      const auto& req=memory_out.prequest.pbits;
      CHECK(requests<2 && req.paddress==(requests ? 0x90000 : 0x60ff0));
      CHECK(req.pbyte_umask==masks[requests] && bool(req.pstore)==store);
      if (store) CHECK(req.pdata.words==source(requests));
      if (memory_in.prequest.pready) {
        CHECK(!responses);
        if (!requests) first_request=t;
        else if (!masked && !store) CHECK(t==first_request+1);
        ++requests;
      }
    }
    if (issued_out.pvalid) { CHECK(issued_out.pbits.powner==3 && requests==2 && !responses); ++tails; }
    const bool returned = memory_in.presponse.pvalid && memory_out.presponse.pready;
    const unsigned returned_row = 16 + responses;
    if (returned) ++responses;
    // Returned data or an occupied, stalled VRF port is not a release.
    // Ignore the two empty tail rows; they can release without a grant.
    const auto vd=chain_out.punwritten_uvd;
    const unsigned remaining=store ? 0 : (3u & (3u<<writes));
    CHECK(((vd>>16)&3u)==remaining);
    if (!store && t==1) CHECK(chain_out.powner==3 && vd==(std::uint64_t(masked ? 15 : 3)<<16));
    if (t>1 && tails && !issued_out.pvalid) CHECK(vd==(std::uint64_t(remaining)<<16));
    if (write_out.pvalid) {
      if (!first_write) first_write=t;
      CHECK(!store && writes<2 && write_out.pbits.paddress==16+writes);
      CHECK(write_out.pbits.pdata.words==data(writes));
      for (unsigned b=0;b<16;++b)
        CHECK(((write_out.pbits.pmask.words[b/4]>>(8*(b%4)))&255)==((masks[writes]&(1u<<b)) ? 255u:0u));
      if (write_in.pready) { CHECK(responses==2); ++writes; }
    }
    if (finished_out.pvalid) {
      CHECK(finished_out.pbits.powner==3 && tails==1 && responses==2 && writes==(store ? 0u:2u));
      ++finished_count;
    }
    source_return=read_out.pvalid && read_in.pready;
    if (source_return) { CHECK(store && read_out.pbits==source_base+source_reads); source_row=source_reads++; }
    const bool written = write_out.pvalid && write_in.pready;
    const unsigned destination = write_out.pbits.paddress;
    tick_model();
    check_transfer(row_sites::vector_load_return, return_serial, !store && returned, returned_row);
    check_transfer(row_sites::vector_load_sequence, write_serial, written, destination);
    if (finished_count) break;
  }
  eval();
  CHECK(finished_count==1 && !busy && launch_out.pready && !finished_out.pvalid && !issued_out.pvalid);
  CHECK(source_reads==(store ? 2u:0u));
  if (store) {
    CHECK(next_event(sequence) == first_row + (masked ? 4 : 2));
    CHECK(next_event(agen) == first_agen + 2);
    for (unsigned i=0; i<2; ++i)
      CHECK(parent_sequences({agen, first_agen+i}, sequence) == std::set<std::uint64_t>{first_row+i});
  }
}

// Occupancy is per instruction: holding a returned load off the VRF does not
// prevent VLIQ delivery, but does prevent replacement of that load's VLSeq.
void single_load(bool fully_masked) {
  launch_0_in={}; memory_in={}; write_in={}; chain_in={};
  memory_in.prequest.pready=1;
  launch_0_in.pbits.pissue.powner=3; launch_0_in.pbits.pissue.pdescriptor.pstate.pvl=16; launch_0_in.pbits.pissue.pdescriptor.pinstruction=8<<7;
  launch_0_in.pbits.pissue.pdescriptor.pmasked=fully_masked;
  mask_0_in.pdata={};
  launch_0_in.pvalid=1; offer_operands(false); eval(); CHECK(launch_0_out.pready && prepare_out.pready); tick_model();
  prepare_in.pvalid=launch_0_in.pvalid=0;
  if (fully_masked) {
    const std::array sites{row_sites::vector_load_agen, row_sites::vector_load_return, row_sites::vector_load_sequence};
    const std::array before{next_event(sites[0]), next_event(sites[1]), next_event(sites[2])};
    for (unsigned wait=0; ; ++wait) {
      eval(); CHECK(wait<20 && !memory_out.prequest.pvalid && !write_out.pvalid);
      const bool finished=finished_out.pvalid;
      tick_model();
      if (finished) break;
    }
    for (unsigned i=0; i<sites.size(); ++i) CHECK(next_event(sites[i])==before[i]);
    return;
  }
  eval(); CHECK(memory_out.prequest.pvalid && issued_out.pvalid); tick_model();
  memory_in.presponse.pvalid=1; memory_in.presponse.pbits.words=data(0);
  eval(); CHECK(memory_out.presponse.pready); tick_model(); memory_in.presponse.pvalid=0;
  eval(); CHECK(delivered_out.pvalid && delivered_out.pbits.powner==3); tick_model();
  for (unsigned n=0;n<5;++n) {
    eval(); CHECK(write_out.pvalid && launch_0_out.pready && !prepare_out.pready && !finished_out.pvalid && !delivered_out.pvalid);
    CHECK(write_out.pbits.pdata.words==data(0)); tick_model();
  }
  write_in.pready=1; tick_model(); eval();
  CHECK(finished_out.pvalid && finished_out.pbits.powner==3); tick_model();
  eval(); CHECK(launch_0_out.pready);
}

// Reconstruct byte ownership independently of the merger. The 20-byte case
// flushes its last row from carry alone; 32 bytes share the middle return.
// At e64 with two fields, each field visit uses only one side of that merge.
void displaced_lineage(unsigned length, bool segmented=false) {
  constexpr auto a=row_sites::vector_load_agen, r=row_sites::vector_load_return, w=row_sites::vector_load_sequence;
  const auto agen=next_event(a), reply=next_event(r), write=next_event(w);
  launch_0_in={}; memory_in={}; write_in={}; chain_in={};
  auto& d=launch_0_in.pbits.pissue.pdescriptor;
  d.pscalar=8; d.pstate.pvl=segmented ? length/16 : length; d.pinstruction=8u<<7;
  if(segmented) { d.pcontrol.pmemory_unf=1; d.pcontrol.pmemory_udata_ueew=3; d.pstate.pvtype=3u<<3; }
  launch_0_in.pbits.pissue.powner=2;
  launch_0_in.pvalid=1; offer_operands(false); eval();
  CHECK(launch_0_out.pready && prepare_out.pready);
  tick_model(); prepare_in.pvalid=launch_0_in.pvalid=0;
  unsigned sent=0,returned=0,written=0; bool done=false;
  for(unsigned t=0;t<80 && !done;++t) {
    memory_in.prequest.pready=t>=4;
    memory_in.presponse.pvalid=t>=10 && returned<sent;
    memory_in.presponse.pbits.words=data(returned);
    write_in.pready=t>=18;
    eval();
    if(memory_out.prequest.pvalid && memory_in.prequest.pready) {
      CHECK(memory_out.prequest.pbits.paddress==16u*sent); ++sent;
    }
    returned+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    written+=write_out.pvalid && write_in.pready;
    done=finished_out.pvalid;
    tick_model();
  }
  const unsigned visit_bytes=segmented ? 8 : 16;
  CHECK(done && sent==(8+length+15)/16 && returned==sent && written==(length+visit_bytes-1)/visit_bytes);
  for(unsigned i=0;i<returned;++i) {
    CHECK(parent_sequences({r,reply+i},a)==std::set<std::uint64_t>{agen+i});
  }
  for(unsigned i=0;i<written;++i) {
    std::set<std::uint64_t> expected;
    for(unsigned byte=i*visit_bytes;byte<std::min(length,(i+1)*visit_bytes);++byte)
      expected.insert(reply+(8+byte)/16);
    CHECK(parent_sequences({w,write+i},r)==expected);
  }
}

// A displaced contiguous store can join two rows. A resolved halfword
// splits one row across two requests. A scatter reuses its captured VS3
// for a subsequent element in the same row without a second register read.
enum class StorePath { contiguous, resolved, scatter };
void store_lineage(StorePath path, std::uint32_t predicate=UINT32_MAX) {
  const bool element = path != StorePath::contiguous;
  const bool scatter = path == StorePath::scatter;
  constexpr auto agen = row_sites::vector_store_agen;
  const auto sequence = scatter ? row_sites::scatter_capture : row_sites::vector_store_sequence;
  const auto first = next_event(sequence), request = next_event(agen);
  launch_1_in={}; memory_in={}; read_result_in={}; checked_element_in={};
  auto& issue=launch_1_in.pbits.pissue;
  auto& d=issue.pdescriptor;
  issue.powner=2;
  issue.pkind=scatter ? 6 : (element ? 4 : 0); // ResolvedStream, Resolved, Contiguous.
  d.pmasked=predicate!=UINT32_MAX;
  mask_1_in.pdata.words={predicate,0,0,0};
  d.pscalar=element ? 15 : 8;
  d.pstate.pvl=element ? 2 : 32;
  d.pstate.pvtype=element ? 8 : 0;
  d.pinstruction=8u<<7;
  d.pcontrol.pmemory=1;
  d.pcontrol.pstore=1;
  d.pcontrol.pmemory_udata_ueew=element ? 1 : 0;
  stream_live=stream_store=scatter;
  stream_owner=issue.powner;
  launch_1_in.pvalid=1;
  if (!scatter) offer_operands(true);
  eval(); CHECK(launch_1_out.pready && (scatter || prepare_out.pready));
  tick_model(); prepare_in.pvalid=launch_1_in.pvalid=0;
  unsigned sent=0, returned=0, reads=0;
  const unsigned expected=element && !scatter ? 2 : 3;
  bool continued=false, done=false;
  for (unsigned t=0; t<100 && !done; ++t) {
    read_in.pready=1;
    memory_in.prequest.pready=t>8 && t%3!=0;
    memory_in.presponse.pvalid=returned<sent;
    checked_element_in.pvalid=scatter && !continued && sent==2;
    checked_element_in.pbits.paddress=20;
    checked_element_in.pbits.prow=16;
    checked_element_in.pbits.poffset=2;
    eval();
    continued |= checked_element_in.pvalid && checked_element_out.pready;
    if (memory_out.prequest.pvalid && memory_in.prequest.pready) {
      CHECK(sent<expected && memory_out.prequest.pbits.pstore);
      ++sent;
    }
    returned+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    const bool reading=read_out.pvalid && read_in.pready;
    reads+=reading;
    done=scatter ? returned==expected : finished_out.pvalid;
    tick_model(); read_result_in.pvalid=reading;
  }
  stream_live=stream_store=0; checked_element_in={}; memory_in={}; read_result_in={};
  CHECK(done && sent==expected && returned==sent && reads==(element ? 1u : 2u));
  CHECK(next_event(sequence)==first+reads && next_event(agen)==request+sent);
  for (unsigned i=0; i<sent; ++i) {
    std::set<std::uint64_t> parents;
    if (element) parents.insert(first);
    else for (unsigned byte=0; byte<32; ++byte) {
      if ((8+byte)/16==i && ((predicate>>byte)&1))
        parents.insert(first+byte/16);
    }
    CHECK(parent_sequences({agen,request+i},sequence)==parents);
  }
  tick_model();
}

// The seed halfword straddles two physical beats. Later splat writes must
// inherit both parents without a new memory request or a live return.
void splat_lineage() {
  launch_0_in={}; memory_in={}; write_in={}; chain_in={}; splat_in={};
  constexpr unsigned return_site=row_sites::vector_load_return, write_site=row_sites::vector_load_sequence;
  const auto first=next_event(return_site), write=next_event(write_site);
  auto& issue=launch_0_in.pbits.pissue; auto& d=issue.pdescriptor;
  issue.pkind=4; issue.powner=2; d.pscalar=15; d.pstate.pvl=3; d.pstate.pvtype=8;
  d.pinstruction=8u<<7; d.pcontrol.pmemory=1; d.pcontrol.pmemory_uaddressing=1; d.pcontrol.pmemory_udata_ueew=1;
  launch_0_in.pvalid=1; offer_operands(false); eval();
  CHECK(launch_0_out.pready && prepare_out.pready);
  tick_model(); prepare_in.pvalid=launch_0_in.pvalid=0;
  unsigned sent=0,replies=0,writes=0;
  for(unsigned t=0;t<50 && writes<3;++t) {
    memory_in.prequest.pready=1; memory_in.presponse.pvalid=replies<sent;
    write_in.pready=t%3!=0;
    splat_in.pvalid=writes>0; splat_in.pbits.prow=16; splat_in.pbits.plane=2*writes; splat_in.pbits.peew=1;
    eval();
    sent+=memory_out.prequest.pvalid;
    replies+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    writes+=write_out.pvalid && write_in.pready;
    tick_model();
  }
  splat_in.pvalid=0;
  CHECK(sent==2 && replies==2 && writes==3);
  for(unsigned i=0;i<writes;++i) {
    const std::set<std::uint64_t> expected{first,first+1};
    CHECK(parent_sequences({write_site,write+i},return_site)==expected);
  }
}

// Feed three-cycle L1 hit replies, or hold the VRF to fill the registered window.
void writeback_window(bool block_write) {
  constexpr unsigned credits=5, rows=12, claims=(1u<<rows)-1;
  launch_0_in={}; memory_in={}; chain_in={}; write_in={};
  memory_in.prequest.pready=write_in.pready=1;
  chain_in.polder_uaccesses=std::uint64_t(block_write)<<16;
  launch_0_in.pbits.ppages[0]=0x60;
  launch_0_in.pbits.pissue.pdescriptor.pstate.pvl=rows*16; launch_0_in.pbits.pissue.pdescriptor.pstate.pvtype=3; // e8,m8
  launch_0_in.pbits.pissue.pdescriptor.pinstruction=8u<<7; launch_0_in.pbits.pissue.powner=2;
  launch_0_in.pvalid=1; offer_operands(false); eval(); CHECK(launch_0_out.pready && prepare_out.pready); tick_model(); prepare_in.pvalid=launch_0_in.pvalid=0;
  unsigned requests=0, responses=0, grants=0, finishes=0;
  unsigned cycle=0;
  std::array<unsigned,rows> accepted{};
  auto edge=[&] {
    memory_in.presponse.pvalid=responses<requests && cycle>=accepted[responses]+3;
    memory_in.presponse.pbits.words=data(responses);
    eval();
    CHECK(chain_out.punwritten_uvd==(std::uint64_t(claims & (claims<<grants))<<16));
    if (memory_out.prequest.pvalid) {
      CHECK(requests<rows && memory_out.prequest.pbits.paddress==0x60000+16*requests);
      accepted[requests]=cycle;
      if (!block_write) CHECK(cycle==accepted[0]+requests);
      ++requests;
    }
    responses+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    if (write_out.pvalid) {
      CHECK(!chain_in.polder_uaccesses && grants<responses && chain_out.pwrite_uowner==2);
      CHECK(write_out.pbits.paddress==16+grants && write_out.pbits.pdata.words==data(grants));
      grants+=write_in.pready;
    }
    finishes+=finished_out.pvalid;
    CHECK(requests-grants<=credits && (!finishes || grants==rows));
    tick_model(); ++cycle;
  };
  if (!block_write) {
    for (unsigned t=0;t<rows+10 && !finishes;++t) edge();
    CHECK(requests==rows && responses==rows && grants==rows && finishes==1);
    return;
  }
  for (unsigned t=0;t<10;++t) edge();
  CHECK(requests==credits && responses==credits && !grants);
  chain_in.polder_uaccesses=0; write_in.pready=0;
  for (unsigned t=0;t<8;++t) { eval(); CHECK(write_out.pvalid); edge(); }
  CHECK(requests==credits && !grants);
  write_in.pready=1; edge(); CHECK(requests==credits && grants==1);
  write_in.pready=0; edge(); CHECK(requests==credits+1);
  for (unsigned t=0;t<8;++t) edge();
  CHECK(requests==credits+1 && responses==credits+1 && grants==1);
  write_in.pready=1; for (unsigned t=0;t<rows+8 && !finishes;++t) edge();
  CHECK(grants==rows && finishes==1);
}

// An older cursor crosses from PPN 0x60 to PPN 0x90. A different VA mapped
// onto PPN 0x90 must wait at byte 0, but not at byte 1 of the very same row.
void physical_ranges(bool older_store, bool alias) {
  auto& launch_in = older_store ? launch_1_in : launch_0_in;
  auto& launch_out = older_store ? launch_1_out : launch_0_out;
  launch_in={}; prepare_in={}; memory_in={}; write_in={}; chain_in={};
  read_result_in={}; read_in.pready=1;
  launch_in.pbits.pissue.pdescriptor.pscalar=0x3ff0; launch_in.pbits.pissue.pdescriptor.pstate.pvl=17;
  launch_in.pbits.pissue.pdescriptor.pcontrol.pstore=older_store; launch_in.pbits.pissue.pdescriptor.pinstruction=8<<7;
  launch_in.pbits.pissue.powner=3; launch_in.pbits.ppages[0]=0x60; launch_in.pbits.ppages[1]=0x90;
  launch_in.pvalid=1;
  offer_operands(older_store);
  eval(); CHECK(launch_out.pready && prepare_out.pready); tick_model();
  // Probe the peer without committing it: both directions of the alias
  // comparison holds through delivery; a later VRF grant is not a memory hazard.
  launch_in.pvalid=prepare_in.pvalid=0;
  auto& peer_in = older_store ? launch_0_in : launch_1_in;
  auto& peer_out = older_store ? launch_0_out : launch_1_out;
  peer_in={};
  peer_in.pbits.pissue.pdescriptor.pscalar=0x8000; peer_in.pbits.pissue.pdescriptor.pstate.pvstart=alias ? 0 : 1;
  peer_in.pbits.pissue.pdescriptor.pstate.pvl=16; peer_in.pbits.pissue.pdescriptor.pcontrol.pstore=!older_store;
  peer_in.pbits.pissue.pdescriptor.pprecertified=1; peer_in.pbits.pissue.pdescriptor.pcertificate.pppn=0x90;
  peer_in.pbits.ppages[0]=peer_in.pbits.ppages[1]=0x90;
  launch_in.pbits.ppages[0]=0xab; launch_in.pbits.ppages[1]=0xcd;
  unsigned requests=0,responses=0,grants=0;
  bool delivered=false;
  for (unsigned n=0;n<60;++n) {
    memory_in.prequest.pready=n>=6;
    memory_in.presponse.pvalid=n>=20 && responses<requests;
    write_in.pready=n>=30;
    eval();
    CHECK(bool(peer_out.pready)==(!alias || delivered));
    if (memory_out.prequest.pvalid && memory_in.prequest.pready) {
      CHECK(requests<2 && memory_out.prequest.pbits.paddress==(requests ? 0x90000u : 0x60ff0u));
      CHECK(memory_out.prequest.pbits.pbyte_umask==(requests ? 1u : 0xffffu));
      ++requests;
    }
    responses+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    grants+=write_out.pvalid && write_in.pready;
    delivered |= delivered_out.pvalid;
    const bool done=finished_out.pvalid;
    bool reading=read_out.pvalid;
    tick_model();
    read_result_in.pvalid=reading;
    if (done) break;
  }
  eval(); CHECK(requests==2 && responses==2 && grants==(older_store ? 0u : 2u));
  CHECK(!busy && peer_out.pready);
}

// Hold the older store's source read. The younger load can issue first,
// but its blocked writeback must not starve either store address or ACK.
void blocked_store_source() {
  const auto stalled_before = stalled_results, granted_before = granted_results;
  launch_1_in={}; prepare_in={}; memory_in={}; write_in={}; chain_in={}; read_result_in={};
  read_in.pready=0;
  launch_1_in.pvalid=prepare_in.pvalid=1;
  launch_1_in.pbits.pissue.pdescriptor.pcontrol.pstore=1; launch_1_in.pbits.pissue.pdescriptor.pstate.pvl=32;
  launch_1_in.pbits.pissue.pdescriptor.pscalar=0x3000; launch_1_in.pbits.pissue.pdescriptor.pinstruction=8<<7;
  launch_1_in.pbits.pissue.powner=prepare_in.pbits.powner=3; launch_1_in.pbits.ppages[0]=0x60;
  prepare_in.pbits=launch_1_in.pbits.pissue;
  eval(); CHECK(launch_1_out.pready && prepare_out.pready); tick_model();
  prepare_in.pvalid=launch_1_in.pvalid=0; launch_0_in=launch_1_in; launch_0_in.pbits.pissue.powner=0; launch_0_in.pvalid=1; launch_0_in.pbits.pissue.pdescriptor.pcontrol.pstore=0;
  launch_0_in.pbits.pissue.pdescriptor.pscalar=0x7010; launch_0_in.pbits.pissue.pdescriptor.pinstruction=12<<7; launch_0_in.pbits.ppages[0]=0x70;
  offer_operands(false); eval(); CHECK(launch_0_out.pready && prepare_out.pready); tick_model(); prepare_in.pvalid=launch_0_in.pvalid=0;
  unsigned sent=0,received=0,written=0,finished=0;
  constexpr unsigned addresses[]={0x70010,0x60000,0x60010,0x70020};
  for (unsigned t=0;t<100 && finished!=3;++t) {
    read_in.pready=t>=3;
    memory_in.prequest.pready=t>=10; // held load offer cannot switch to the now-ready store
    memory_in.presponse.pvalid=received<sent && t>=15;
    memory_in.presponse.pbits.words=data(received);
    write_in.pready=(finished&2)!=0; // wait for store owner 3 before granting either load write
    eval();
    if (memory_out.prequest.pvalid) {
      CHECK(sent<4 && memory_out.prequest.pbits.paddress==addresses[sent]);
      CHECK(bool(memory_out.prequest.pbits.pstore)==(sent==1 || sent==2));
      sent+=memory_in.prequest.pready;
    }
    received+=memory_in.presponse.pvalid && memory_out.presponse.pready;
    if (write_out.pvalid) {
      CHECK(chain_out.pwrite_uowner==0 && write_out.pbits.paddress==24+written);
      CHECK(write_out.pbits.pdata.words==data(written ? 3 : 0));
      written+=bool(write_in.pready);
    }
    if (finished_out.pvalid) {
      if (finished_out.pbits.powner==3) { CHECK(!written && received>=3); finished|=2; }
      else { CHECK(finished_out.pbits.powner==0 && written==2); finished|=1; }
    }
    const bool capture=read_in.pready && read_out.pvalid;
    tick_model(); read_result_in.pvalid=capture;
  }
  CHECK(finished==3 && sent==4 && received==4);
  CHECK(stalled_results > stalled_before && granted_results == granted_before + 2);
}

// A valid offer to an occupied start port must not hide its eligible peer.
// Each head has a real descriptor; only the admitted pair may reach L1D.
void independent_starts(bool older_store) {
  launch_0_in={}; launch_1_in={}; prepare_in={}; memory_in={}; write_in={};
  read_result_in={}; chain_in={}; read_in.pready=0;
  auto& ld=launch_0_in.pbits.pissue.pdescriptor; auto& st=launch_1_in.pbits.pissue.pdescriptor;
  ld.pstate.pvl=st.pstate.pvl=16; ld.pinstruction=8u<<7; st.pinstruction=12u<<7;
  ld.pscalar=0x1000; st.pscalar=0x2000; st.pcontrol.pstore=1;
  ld.pprecertified=st.pprecertified=1; ld.pcertificate.pppn=0x31; st.pcertificate.pppn=0x43;
  launch_0_in.pbits.pissue.powner=2; launch_1_in.pbits.pissue.powner=3;
  launch_0_in.pbits.ppages[0]=0x31; launch_1_in.pbits.ppages[0]=0x43;
  launch_0_in.pvalid=!older_store; launch_1_in.pvalid=older_store;
  offer_operands(older_store);
  eval(); CHECK(prepare_out.pready && (older_store ? launch_1_out.pready : launch_0_out.pready));
  tick_model();
  launch_0_in.pvalid=launch_1_in.pvalid=1; offer_operands(!older_store);
  eval();
  CHECK(bool(launch_0_out.pready)==older_store && bool(launch_1_out.pready)==!older_store);
  CHECK(prepare_out.pready);
  tick_model(); launch_0_in.pvalid=launch_1_in.pvalid=prepare_in.pvalid=0;
  read_in.pready=1;
  unsigned sent=0,returned=0,done=0,seen=0;
  for (unsigned t=0;t<40 && done!=3;++t) {
    memory_in.prequest.pready=t>4; memory_in.presponse.pvalid=returned<sent;
    write_in.pready=1; eval();
    if (memory_out.prequest.pvalid && memory_in.prequest.pready) {
      const auto& r=memory_out.prequest.pbits;
      CHECK(r.paddress==(r.pstore ? 0x43000u : 0x31000u));
      CHECK(!(seen & (1u<<r.pstore))); seen|=1u<<r.pstore; ++sent;
    }
    returned+=memory_out.presponse.pready && memory_in.presponse.pvalid;
    if (finished_out.pvalid) {
      CHECK(finished_out.pbits.powner==2 || finished_out.pbits.powner==3);
      done|=1u<<(finished_out.pbits.powner-2);
    }
    const bool read=read_out.pvalid && read_in.pready;
    tick_model(); read_result_in.pvalid=read;
  }
  CHECK(seen==3 && done==3 && returned==2);
}

int main() {
  return run_test([] {
    rheg::graph().bind_manifest(rheg_generated::manifest());
    reset=1; tick_model(); reset=0;
    for (bool store : {false,true}) for (bool masked : {false,true}) exercise(store,masked);
    single_load(false);
    single_load(true);
    displaced_lineage(20);
    displaced_lineage(32);
    displaced_lineage(32,true);
    writeback_window(false);
    writeback_window(true);
    for (bool store : {false,true}) for (bool alias : {false,true}) physical_ranges(store,alias);
    blocked_store_source();
    for (bool store : {false,true}) independent_starts(store);
    splat_lineage();
    store_lineage(StorePath::contiguous);
    store_lineage(StorePath::contiguous,0xff00ffff); // Middle beat uses only carry.
    store_lineage(StorePath::contiguous,0xffff00ff); // Middle beat uses only the live row.
    store_lineage(StorePath::resolved);
    store_lineage(StorePath::scatter);
  });
}
