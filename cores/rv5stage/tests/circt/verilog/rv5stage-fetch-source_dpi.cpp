// Checks original cursor/admission behavior and exact selected parents from public inputs.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "rv5stage-fetch-source_manifest.h"
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {
struct Cursor { std::uint64_t pc=0, target=0; bool continuation=false; std::optional<rheg::Ref> parent; } held;
std::optional<Cursor> s1;
bool running=true;
rheg::Graph expected;
std::map<unsigned, std::uint64_t> sequences;
std::uint64_t cycle=0;
unsigned restart_parents=0, replay_parents=0, successor_parents=0, held_parents=0;
unsigned blocked_restarts=0, replacements=0, clears=0, pending_resets=0;
[[noreturn]] void fail(const char* reason) { std::fprintf(stderr,"fetch-source cycle %llu: %s\n",(unsigned long long)cycle,reason); std::abort(); }
rheg::Ref node(unsigned site, std::uint64_t pc, std::optional<rheg::Ref> parent={}, bool unknown=false) {
  rheg::Ref ref{site,sequences[site]++};
  auto& n=expected.nodes[ref]; n.present=true; n.cycle=cycle; n.width=64;
  n.words[0]=std::uint32_t(pc); n.words[1]=std::uint32_t(pc>>32); n.ancestry_unknown=unknown;
  if(parent) expected.edges.insert({*parent,ref});
  return ref;
}
}
extern "C" void fetch_source_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void fetch_source_sample(unsigned reset, unsigned active, unsigned space, unsigned clear,
    unsigned restart, std::uint64_t restart_pc, unsigned replay, std::uint64_t replay_pc,
    unsigned replay_cont, std::uint64_t replay_target, unsigned valid, unsigned ready,
    std::uint64_t pc, unsigned cont, std::uint64_t target, unsigned stage_valid, std::uint64_t stage_pc) {
  if(reset) {
    pending_resets+=held.parent.has_value() || s1.has_value();
    held={}; s1.reset(); running=true; expected.clear(); sequences.clear(); cycle=0; return;
  }
  const bool command=clear || restart;
  const bool enabled=restart || (active && running && !command && space);
  if(bool(valid)!=enabled) fail("admission changed");
  if(bool(stage_valid)!=s1.has_value() || (s1 && stage_pc!=s1->pc)) fail("S1 replacement or timing changed");
  Cursor candidate=held;
  std::optional<rheg::Ref> restart_ref, replay_ref;
  if(restart) restart_ref=node(test_sites::restart,restart_pc);
  if(replay) replay_ref=node(test_sites::replay,replay_pc);
  Cursor next;
  if(s1) next={s1->continuation?s1->target:(s1->pc&~3ULL)+4,0,false,s1->parent};
  if(restart) candidate={restart_pc,restart_pc,false,restart_ref};
  else if(replay) candidate={replay_pc,replay_target,bool(replay_cont),replay_ref};
  else if(s1) candidate=next;
  // Also compare inactive offer payloads: selection must not depend on admission.
  if(pc!=candidate.pc || bool(cont)!=candidate.continuation || target!=candidate.target) fail("candidate payload changed");
  std::optional<Cursor> incoming;
  if(valid && ready) {
    const auto ref=node(test_sites::frontend_s0_request,pc,candidate.parent,!candidate.parent);
    incoming=Cursor{pc,target,bool(cont),ref};
    if(restart) ++restart_parents; else if(replay) ++replay_parents;
    else if(s1) ++successor_parents; else if(held.parent) ++held_parents;
  }
  blocked_restarts+=restart && !ready;
  replacements+=(restart || replay) && held.parent.has_value() && !ready;
  if(command) {
    if(restart) { held.pc=restart_pc; held.parent=restart_ref; }
    held.continuation=false; running=restart; clears+=clear;
  } else if(incoming) {
    held.pc=(pc&~3ULL)+4; held.continuation=false; held.parent=incoming->parent;
  } else if(replay) held={replay_pc,replay_target,bool(replay_cont),replay_ref};
  else if(s1) held=next;
  s1=incoming; ++cycle;
}
extern "C" void fetch_source_check() {
  if(rheg::graph().json()!=expected.json()) fail("selected occurrence graph mismatch");
}
extern "C" void fetch_source_finish() {
  rheg::graph().validate();
  if(!restart_parents || !replay_parents || !successor_parents || !held_parents || !blocked_restarts || !replacements || !clears || !pending_resets)
    fail("missing candidate, replacement, clear, or reset coverage");
  std::printf("Fetch-source exact behavior/ancestry passed: %u restart, %u replay, %u successor, %u held parents; %u blocked restarts, %u replacements, %u clears, %u pending resets\n",
      restart_parents,replay_parents,successor_parents,held_parents,blocked_restarts,replacements,clears,pending_resets);
}
