// Checks vector event occurrences against issue, compute maturity, and tagged memory-response transfers.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "event-vector_manifest.h"
#include <array>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {
using rheg::Ref;
struct Attempt { Ref ref; unsigned tag, index; bool memory, enabled; };
struct Owner { Attempt attempt; bool done; std::uint64_t due; };
struct IssueOwner { Ref ref; unsigned next_index=0, authorized_index=0; };
std::array<std::optional<Attempt>,3> pipe;
std::deque<Owner> owners;
std::deque<IssueOwner> issue_owners;
std::optional<Ref> macro;
std::optional<Ref> offered_macro;
std::uint64_t cycle=0, launches=0, issues=0, completions=0, stalls=0;
unsigned destination=0, length=0, macro_instruction=0;
unsigned issued_count=0, complete_count=0, retry_count=0, fault_count=0, truncate_count=0;
unsigned beat_launch_count=0, first_cycle_launch_count=0;
unsigned late_count=0, out_of_order=0, reset_pending=0, no_write=0, stall_count=0, launch_stall_count=0;
constexpr std::array<const char*,7> launch_reasons={"setup_wait","first_source_wait","second_source_wait",
  "destination_wait","mask_wait","gather_source_wait","fetch_wait"};
std::array<unsigned,launch_reasons.size()> launch_reason_counts{};
bool resetting=true, writes=true;
struct Expected { Ref ref; std::optional<Ref> parent; };
std::vector<Expected> expected;
unsigned expected_index=0;
bool have_issue=false;
bool resident=false;
std::optional<Ref> expected_issue_macro;
std::map<Ref,std::uint64_t> releases;
[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr,"vector trace cycle %llu: %s\n",(unsigned long long)cycle,message);
  std::abort();
}
bool equal(Ref a, Ref b) { return a.site==b.site && a.sequence==b.sequence; }
void expect(unsigned site, std::uint64_t sequence, std::optional<Ref> parent) {
  expected.push_back({{site,sequence},parent});
}
void check_parent(Ref child, Ref parent) {
  unsigned count=0;
  for(const auto& edge:rheg::graph().edges) if(equal(edge.second,child)) {
    if(!equal(edge.first,parent)) fail("wrong occurrence parent");
    ++count;
  }
  if(count!=1 || rheg::graph().nodes.at(child).ancestry_unknown) {
    std::fprintf(stderr,"child %u:%llu parent %u:%llu count %u unknown %u\n",
      child.site,(unsigned long long)child.sequence,parent.site,(unsigned long long)parent.sequence,
      count,unsigned(rheg::graph().nodes.at(child).ancestry_unknown));
    fail("missing or incomplete parent");
  }
}
Ref only_parent(Ref child, unsigned site) {
  std::optional<Ref> found;
  for(const auto& edge:rheg::graph().edges) if(equal(edge.second,child)) {
    if(edge.first.site!=site || found) fail("wrong or multiple occurrence parents");
    found=edge.first;
  }
  if(!found || rheg::graph().nodes.at(child).ancestry_unknown) fail("missing occurrence parent");
  return *found;
}
}
extern "C" void vector_trace_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
// The response driver deliberately returns younger slots first. Ownership is
// established solely from previously accepted public issues, never graph data.
extern "C" unsigned vector_trace_response() {
  for(auto it=owners.rbegin();it!=owners.rend();++it)
    if(!it->done && it->due<=cycle) return 0x100|it->attempt.tag;
  return 0;
}
extern "C" void vector_trace_sample(unsigned reset, unsigned launch, unsigned instruction,
    unsigned vl, unsigned issue, unsigned tag, unsigned memory, unsigned enabled,
    unsigned commit, unsigned disposition, unsigned slow, unsigned response,
    unsigned response_tag, unsigned cancel, unsigned issue_done, unsigned sequenced) {
  resetting=reset; expected.clear(); have_issue=false; expected_issue_macro.reset();
  if(reset) {
    if(!owners.empty() || pipe[0] || pipe[1] || pipe[2]) ++reset_pending;
    pipe={}; owners.clear(); issue_owners.clear(); macro.reset(); offered_macro.reset();
    releases.clear(); resident=false;
    cycle=launches=issues=completions=stalls=0;
    return;
  }
  const auto issuing_macro=issue_owners.empty() ? std::nullopt : std::optional<Ref>{issue_owners.front().ref};
  const auto issuing_index=issue_owners.empty() ? 0 : issue_owners.front().next_index;
  offered_macro=issuing_macro;
  const bool serialized=resident && ((macro_instruction&0x7f)==0x07 || (macro_instruction&0x7f)==0x27);
  if(serialized && bool(sequenced)!=bool(issue_done)) fail("serialized sequencing release changed");
  if(sequenced) {
    if(!resident || !macro) fail("sequencing completion without a resident macro");
    if(!releases.emplace(*macro,cycle).second) fail("duplicate sequencing completion");
    macro.reset(); resident=false;
  }
  if(launch) {
    if(resident) fail("macro replaced active sequencing");
    macro=Ref{vector_sites::sequencer,launches++};
    resident=true;
    expect(macro->site,macro->sequence,{});
    destination=(instruction>>7)&31; length=vl; macro_instruction=instruction;
    writes=(instruction&0x7f)!=0x27 && ((instruction>>25)&1) && vl!=0;
    issue_owners.push_back({*macro});
  }
  if(response) {
    bool found=false;
    for(auto& owner:owners) if(owner.attempt.tag==response_tag) {
      if(owner.done) fail("duplicate response");
      if(&owner!=&owners.front()) ++out_of_order;
      owner.done=true; found=true; break;
    }
    if(!found) fail("response without an accepted slot");
  }
  // The ordered head bypasses completion storage when its response arrives;
  // an older already-complete head drains on the same edge as before.
  bool drained=false;
  if(!owners.empty() && owners.front().done) {
    expect(vector_sites::complete,completions++,owners.front().attempt.ref);
    owners.pop_front(); ++late_count; ++complete_count; drained=true;
  }
  // Non-memory compute becomes durable directly from the one-stage private
  // execute path. It either drains at the ordered head or waits there behind
  // the single write already selected for this cycle.
  if(pipe[0] && !pipe[0]->memory) {
    owners.push_back({*pipe[0],true,cycle});
    if(!drained && owners.size()==1) {
      expect(vector_sites::complete,completions++,pipe[0]->ref);
      owners.pop_front(); ++complete_count; drained=true;
    }
  }
  if(bool(pipe[2])!=bool(commit || (cancel && pipe[2]))) fail("feedback latency changed");
  if(commit) {
    if(!pipe[2]) fail("feedback without issue");
    if(!pipe[2]->memory) {
      if(disposition!=0) fail("compute received a memory disposition");
    } else if(disposition==0) {
      for(const auto& owner:owners) if(owner.attempt.tag==pipe[2]->tag) fail("live slot reused");
      owners.push_back({*pipe[2],!pipe[2]->memory || !slow || !pipe[2]->enabled,cycle+10+(3-pipe[2]->tag)*3});
      if(!issue_owners.empty()) issue_owners.front().authorized_index=pipe[2]->index+1;
    } else if(disposition==1) {
      if(issue_owners.empty()) fail("retry without issue owner");
      ++retry_count; issue_owners.front().next_index=issue_owners.front().authorized_index;
    }
    else if(disposition==2) ++fault_count;
    else ++truncate_count;
  }
  std::optional<Attempt> incoming;
  if(issue) {
    if(!issuing_macro) fail("issue without retained owner");
    incoming=Attempt{{vector_sites::issue,issues++},tag,issuing_index,bool(memory),bool(enabled)};
    issue_owners.front().next_index=issuing_index+1;
    expect(vector_sites::issue,incoming->ref.sequence,{});
    expected_issue_macro=issuing_macro;
    expected_index=incoming->index; have_issue=true; ++issued_count;
  }
  if(issue_done) {
    if(!issuing_macro) fail("issue completion without owner");
    issue_owners.pop_front();
  }
  if(cancel || (commit && disposition!=0)) pipe={};
  else { pipe[2]=pipe[1]; pipe[1]=pipe[0]; pipe[0]=incoming; }
}
extern "C" void vector_trace_check() {
  auto& graph=rheg::graph();
  graph.validate();
  if(resetting) { if(!graph.nodes.empty()) fail("reset retained graph"); return; }
  for(const auto& [ref,node]:graph.nodes) if(ref.site==vector_sites::sequencer) {
    if(node.end_cycle.has_value()!=bool(releases.count(ref))) fail("residency open/released mismatch");
    if(node.end_cycle && *node.end_cycle!=releases.at(ref)) fail("wrong residency release cycle");
  }
  unsigned actual=0;
  for(const auto& pair:graph.nodes) if(pair.second.cycle==cycle) {
    const auto ref=pair.first;
    if(ref.site==vector_sites::launch) {
      const auto owner=only_parent(ref,vector_sites::sequencer);
      if(pair.second.cycle<=graph.nodes.at(owner).cycle ||
          (graph.nodes.at(owner).end_cycle && pair.second.cycle>*graph.nodes.at(owner).end_cycle))
        fail("read plan outside sequencer residency");
      if(graph.field(ref,"packed").unsigned_value()!=0) fail("elementwise launch marked packed");
      for(const char* reason:launch_reasons)
        if(graph.field(ref,reason).unsigned_value()) fail("accepted read plan has a stall reason");
      if(graph.field(ref,"op_index").unsigned_value()==0 && pair.second.cycle==graph.nodes.at(owner).cycle+1)
        ++first_cycle_launch_count;
      ++beat_launch_count;
    } else if(ref.site==vector_sites::launch_stall) {
      const auto owner=only_parent(ref,vector_sites::sequencer);
      if(pair.second.cycle<=graph.nodes.at(owner).cycle ||
          (graph.nodes.at(owner).end_cycle && pair.second.cycle>*graph.nodes.at(owner).end_cycle))
        fail("read-plan stall outside sequencer residency");
      bool blocked=false;
      for(std::size_t index=0;index<launch_reasons.size();++index) {
        const bool reason=graph.field(ref,launch_reasons[index]).unsigned_value()!=0;
        blocked|=reason;
        launch_reason_counts[index]+=reason;
      }
      if(!blocked) fail("read-plan stall has no blocked acceptance");
      ++launch_stall_count;
    } else if(ref.site==vector_sites::stall) {
      if(!offered_macro) fail("stall without issue owner");
      const auto launch=only_parent(ref,vector_sites::launch);
      if(!equal(only_parent(launch,vector_sites::sequencer),*offered_macro)) fail("stall inherited wrong sequencer");
      ++stall_count; ++stalls;
    } else ++actual;
  }
  if(actual!=expected.size()) fail("wrong number of transfer occurrences");
  for(const auto& event:expected) {
    auto it=graph.nodes.find(event.ref);
    if(it==graph.nodes.end() || it->second.cycle!=cycle) fail("missing or mistimed occurrence");
    if(event.parent) check_parent(event.ref,*event.parent);
    if(event.ref.site==vector_sites::sequencer) {
      if(graph.field(event.ref,"pc").unsigned_value()!=0x100) fail("launch PC");
      if(graph.field(event.ref,"instruction").unsigned_value()!=macro_instruction ||
          graph.field(event.ref,"vl").unsigned_value()!=length) fail("launch snapshot");
    } else if(event.ref.site==vector_sites::issue) {
      const auto launch=only_parent(event.ref,vector_sites::launch);
      if(!expected_issue_macro || !equal(only_parent(launch,vector_sites::sequencer),*expected_issue_macro))
        fail("issue inherited the wrong sequencer launch");
      if(graph.nodes.at(launch).cycle>=cycle ||
          graph.field(launch,"op_index").unsigned_value()!=expected_index ||
          graph.field(launch,"first").unsigned_value()!=graph.field(event.ref,"first").unsigned_value() ||
          graph.field(launch,"end").unsigned_value()!=graph.field(event.ref,"end").unsigned_value() ||
          graph.field(launch,"last").unsigned_value()!=graph.field(event.ref,"last").unsigned_value() ||
          graph.field(launch,"empty").unsigned_value()!=graph.field(event.ref,"empty").unsigned_value())
        fail("issue did not preserve its earlier read plan");
      if(!have_issue || graph.field(event.ref,"op_index").unsigned_value()!=expected_index) fail("issue operation index");
      if(graph.field(event.ref,"first").unsigned_value()!=expected_index) fail("issue element range");
      const auto end=length==0 ? 0 : expected_index+1;
      if(graph.field(event.ref,"end").unsigned_value()!=end ||
          graph.field(event.ref,"last").unsigned_value()!=(end==length) ||
          graph.field(event.ref,"empty").unsigned_value()!=(length==0)) fail("issue range flags");
    } else if(event.ref.site==vector_sites::complete) {
      if(graph.field(event.ref,"destination").unsigned_value()!=destination) fail("completion destination");
      const bool write=graph.field(event.ref,"write_enabled").unsigned_value();
      if(write!=writes) fail("completion write qualification");
      if(!write) ++no_write;
    }
  }
  ++cycle;
}
extern "C" void vector_trace_finish() {
  if(beat_launch_count<40 || !first_cycle_launch_count || issued_count<40 || complete_count<30 || !retry_count || !fault_count || !truncate_count ||
      !late_count || !reset_pending || !no_write || !stall_count || !launch_stall_count)
    fail("missing retry/fault/truncation/ordered-drain/reset/stall/no-write coverage");
  if(!launch_reason_counts[1] || !launch_reason_counts[6]) fail("missing source or fetch launch-stall coverage");
  if(!out_of_order) fail("missing out-of-order response coverage");
  std::printf("Vector lineage passed: %u issues, %u completions, %u delayed, %u out-of-order responses, %u retries, %u issue stalls, %u launch stalls\n",
      issued_count,complete_count,late_count,out_of_order,retry_count,stall_count,launch_stall_count);
  for(std::size_t index=0;index<launch_reasons.size();++index)
    std::printf("  %s: %u\n",launch_reasons[index],launch_reason_counts[index]);
}
