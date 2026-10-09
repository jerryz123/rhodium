// Checks Home and victim-writeback ancestry using driver-selected requests and public REQ/DBID/DAT transfers.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../rheg/runtime/rheg.h"
#include "test-sites.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

namespace {
rheg::Graph expected;
std::uint64_t cycle = 0;
std::map<unsigned, std::uint64_t> sequences;
struct Owner {
  rheg::Ref request;
  std::optional<rheg::Ref> resident;
  unsigned txn,src,return_txn,return_nid,remaining,opcode;
  unsigned copyback_dbid=~0u,copyback_mask=0;
  bool done=false;
};
std::vector<Owner> owners;
unsigned responses=0,beats=0,stalled=0,resets=0,concurrent=0,reused=0,copybacks=0;
unsigned backing_reads=0,backing_writes=0,backing_beats=0,buffered_writes=0;
std::optional<rheg::Ref> expected_backing;
std::map<unsigned,rheg::Ref> backing_transactions,backing_dbids;
std::array<unsigned,2> allocations{};
bool resetting=false;
[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr,"Home trace: %s at cycle %llu\n",message,(unsigned long long)cycle); std::abort();
}
rheg::Ref node(unsigned site, unsigned width, unsigned __int128 payload) {
  rheg::Ref ref{site,sequences[site]++};
  auto& n = expected.nodes[ref]; n.present = true; n.cycle = cycle; n.width = width;
  for (unsigned i = 0; i < (width+31)/32; ++i) n.words[i] = std::uint32_t(payload >> (i*32));
  return ref;
}
Owner& owner(unsigned txn,unsigned target,bool data) {
  Owner* found=nullptr;
  for(auto& candidate:owners) if(!candidate.done &&
      (data ? candidate.return_txn==txn && candidate.return_nid==target : candidate.txn==txn && candidate.src==target)) {
    if(found) fail("ambiguous public transaction identity");
    found=&candidate;
  }
  if(!found || !found->resident) fail("response without accepted resident");
  return *found;
}
void end(Owner& owner) {
  expected.record_end(*owner.resident,cycle); owner.done=true;
}
}
extern "C" void event_home_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
// The stimulus identifies the causal admission explicitly, independently of
// DUT slot selection, rewritten TxnIDs, victim addresses, or observed edges.
extern "C" void event_home_expect_backing(unsigned owner_age) {
  if(expected_backing || owner_age>=owners.size()) fail("invalid backing-request expectation");
  const auto& accepted=owners[owners.size()-1-owner_age];
  if(accepted.done || !accepted.resident) fail("backing request requires live accepted owner");
  expected_backing=accepted.resident;
}
extern "C" void event_home_sample(unsigned reset, unsigned request_fire, std::uint64_t address,
    unsigned request_opcode, unsigned request_txn, unsigned request_src, unsigned return_txn, unsigned return_nid, unsigned request_size,
    unsigned response_fire, unsigned response_opcode, unsigned response_txn, unsigned response_tgt, unsigned response_dbid,
    unsigned data_fire, unsigned data_opcode, unsigned data_txn, unsigned data_tgt, unsigned data_id,
    unsigned request_data_fire, unsigned request_data_opcode, unsigned request_data_txn, unsigned request_data_id,
    unsigned backing_fire, std::uint64_t backing_address, unsigned backing_opcode, unsigned backing_txn,
    unsigned backing_data_fire, unsigned backing_data_txn, unsigned backing_data_id,
    unsigned backing_response_fire, unsigned backing_response_opcode, unsigned backing_response_txn, unsigned backing_response_dbid,
    unsigned output_stalled) {
  using namespace test_sites;
  resetting=reset;
  if(reset) {
    for(const auto& owner:owners) resets+=!owner.done;
    expected.clear(); sequences.clear(); owners.clear(); expected_backing.reset();
    backing_transactions.clear(); backing_dbids.clear(); cycle=0; return;
  }
  unsigned live=0; for(const auto& owner:owners) live+=!owner.done;
  concurrent+=live>1;
  if (response_fire) {
    auto& accepted=owner(response_txn,response_tgt,false);
    auto child=node(response,24,(response_opcode<<19)|(response_txn<<7)|response_tgt);
    expected.edges.insert({*accepted.resident,child}); ++responses;
    if(response_opcode==4) end(accepted);
    if(response_opcode==5) accepted.copyback_dbid=response_dbid;
  }
  if (data_fire) {
    auto& accepted=owner(data_txn,data_tgt,true);
    auto child=node(data,25,(data_opcode<<21)|(data_txn<<9)|(data_tgt<<2)|data_id);
    expected.edges.insert({*accepted.resident,child}); ++beats;
    if(!accepted.remaining) fail("too many data beats");
    if(!--accepted.remaining) end(accepted);
  }
  if(request_data_fire && request_data_opcode==2) {
    bool found=false;
    for(auto& accepted:owners) if(!accepted.done && accepted.copyback_dbid==request_data_txn) {
      accepted.copyback_mask|=1u<<request_data_id; found=true;
    }
    if(!found) fail("copyback data without grant");
  }
  if(request_fire) {
    const auto parent=node(request,70,(static_cast<unsigned __int128>(address)<<26)|
      (std::uint64_t(request_opcode)<<19)|(request_txn<<7)|request_src);
    owners.push_back({parent,{},request_txn,request_src,return_txn,return_nid,
      request_size>4 ? 1u<<(request_size-4) : 1u,request_opcode});
  }
  if(backing_fire) {
    if(!expected_backing) fail("backing request without stimulus expectation");
    auto child=node(backing_request,63,(static_cast<unsigned __int128>(backing_address)<<19)|(backing_opcode<<12)|backing_txn);
    expected.edges.insert({*expected_backing,child});
    backing_transactions.insert_or_assign(backing_txn,*expected_backing);
    expected_backing.reset();
    if(backing_opcode==4) ++backing_reads;
    else { ++backing_writes; buffered_writes+=backing_txn>=2; }
  }
  if(backing_response_fire && backing_response_opcode==6) {
    auto found=backing_transactions.find(backing_response_txn);
    if(found==backing_transactions.end()) fail("DBID without a backing request");
    backing_dbids.insert_or_assign(backing_response_dbid,found->second);
  }
  if(backing_data_fire) {
    auto found=backing_dbids.find(backing_data_txn);
    if(found==backing_dbids.end()) fail("write data without a public DBID grant");
    auto child=node(backing_data,14,(backing_data_txn<<2)|backing_data_id);
    expected.edges.insert({found->second,child}); ++backing_beats;
  }
  stalled += output_stalled;
}
extern "C" void event_home_check() {
  using namespace test_sites;
  const auto& actual=rheg::graph();
  // Slot selection is not reconstructed from private FSM state. Admission is
  // matched by cycle; response parents then follow public request/return IDs.
  // Generic bank tests separately score exact allocation indices and releases.
  for(auto& accepted:owners) if(!accepted.resident) {
    unsigned found=0;
    for(const auto& [ref,n]:actual.nodes) if(n.cycle==cycle &&
        (ref.site==home_transaction0 || ref.site==home_transaction1)) {
      const unsigned slot=ref.site==home_transaction0 ? 0 : 1;
      const auto& request=expected.nodes.at(accepted.request);
      accepted.resident=node(ref.site,request.width,0);
      if(accepted.resident->sequence!=ref.sequence) fail("resident occurrence sequence");
      expected.nodes.at(*accepted.resident).words=request.words;
      expected.edges.insert({accepted.request,*accepted.resident});
      reused+=allocations[slot]++>0; ++found;
    }
    if(found!=1) fail("accepted request requires exactly one residency");
  }
  for(auto& accepted:owners) if(accepted.resident && !accepted.done) {
    const auto& actual_node=actual.nodes.at(*accepted.resident);
    if(actual_node.end_cycle) {
      // Copyback ends after buffered ingress and internal array work, not a
      // public completion. Check receipt of every packet before retirement.
      // Read/write completion endpoints above are cycle-exact expectations.
      if(accepted.opcode!=0x1b || accepted.copyback_mask!=15 || *actual_node.end_cycle!=cycle)
        fail("premature or unexplained residency end");
      end(accepted); ++copybacks;
    }
  }
  actual.validate();
  if(actual.json()!=expected.json()) fail("exact occurrence graph mismatch");
  if(!resetting) ++cycle;
}
extern "C" void event_home_finish() {
  rheg::graph().validate();
  if(!beats || !responses || !stalled || !resets || !concurrent || !reused || !copybacks || !allocations[1] ||
     !backing_reads || !backing_writes || !backing_beats || !buffered_writes || expected_backing)
    fail("Home trace coverage incomplete");
  std::printf("Home ownership passed: beats=%u responses=%u concurrent=%u slot-reuse=%u copybacks=%u stalls=%u pending-resets=%u\n",
    beats,responses,concurrent,reused,copybacks,stalled,resets);
  std::printf("Home subordinate ancestry passed: reads=%u writes=%u data=%u buffered-writes=%u\n",
    backing_reads,backing_writes,backing_beats,buffered_writes);
}
