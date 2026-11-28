// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <algorithm>
#include <bit>
#include <map>
#include <deque>
#include <cstring>
#include <type_traits>
#include <vector>

using Word = std::uint64_t;
constexpr unsigned metadata_entries = 16; // VLSU load-metadata slots (MemoryRequestEntries).
unsigned cycle_limit=16000; // Aggregate watchdog; row cadence is checked at the grants.
unsigned cycles, finishes, releases, narrow, scalar_returns;
Word scalar_value;
unsigned accrued_flags, flag_rows;
unsigned shared_fp_requests, direct_index_rows, checked_index_rows;
bool watch_memory_owner, remap_exact;
struct MemoryEvent { unsigned cycle; Word owner; bool checked; };
std::vector<MemoryEvent> memory_issues, memory_finishes, element_drains, window_done;
extern "C" void observe_memory_issue(Word resetting, Word valid, Word owner, Word checked) {
  if (!resetting && watch_memory_owner && valid) memory_issues.push_back({cycles,owner,bool(checked)});
}
extern "C" void observe_memory_finish(Word resetting, Word valid, Word owner, Word checked) {
  if (!resetting && watch_memory_owner && valid) memory_finishes.push_back({cycles,owner,bool(checked)});
}
extern "C" void observe_element_drain(Word resetting, Word valid, Word owner) {
  if (!resetting && watch_memory_owner && valid) element_drains.push_back({cycles,owner,true});
}
extern "C" void observe_window_done(Word resetting, Word valid, Word owner) {
  if (!resetting && watch_memory_owner && valid) window_done.push_back({cycles,owner,false});
}
extern "C" void observe_direct_index(Word resetting, Word valid, Word ready) {
  if(!resetting && valid && ready) ++direct_index_rows;
}
extern "C" void observe_checked_index(Word resetting, Word valid, Word ready) {
  if(!resetting && valid && ready) ++checked_index_rows;
}
extern "C" void observe_shared_fp(Word resetting, Word valid) {
  if(!resetting && valid) ++shared_fp_requests;
}
bool lookup, lookup_store, overlapped_read;
unsigned latency = 3;
bool block_returns, requests_always_ready, track_row_drain, hold_memory_drain;
std::vector<Word> addresses, writes;
std::vector<unsigned> request_cycles, load_return_cycles, load_write_cycles;
using Row = std::array<std::uint32_t,4>;
const Row initial{0x41414141,0x42424242,0x43434343,0x44444444};
std::map<Word,Row> stored;
struct Request { Word address; unsigned mask; bool store; Row data; unsigned due; };
std::deque<Request> pending;
Row stalled_data{};
Word stalled_address, lookup_address, commit_address;
unsigned stalled_mask;
std::vector<unsigned> masks;
std::vector<Word> precise_accesses, precise_stores;
bool refuse_page, segment_device;
std::vector<unsigned> services;
bool iterative, repair, split_reply;
std::vector<Word> probes, exact_probes;
unsigned split_faults;
static Word mapped_page(Word va) { return 0x80 + 5*(va>>12); }
Word last_fault, last_cause, last_vstart;
unsigned faults;
std::vector<Word> truncations, outcomes;
bool stalled, watch_writer, stall_second;
std::vector<unsigned> store_captures;
std::vector<unsigned> saturation_cycles;
unsigned writer_read_cycle, store_capture_cycle, store_offers, store_acks, hold_until;
using Wide = unsigned __int128;
// Compute in mathematical element precision, then round/clamp and repack.
// Unlike the RTL this oracle has no guarded byte lanes or shift-bank routing.
static std::pair<Word,bool> fixed_expected(unsigned fn, unsigned width, Word lhs, Word rhs, unsigned rm) {
  using Exact = __int128;
  const Exact modulus=Exact(1)<<width;
  auto extend=[&](Word w) -> Exact {
    Exact n=w&low_mask(width);
    return (fn&1) && n>=modulus/2 ? n-modulus : n;
  };
  Exact a=extend(lhs), b=extend(rhs);
  if(fn>=32 && fn<=35) {
    Exact exact=(fn&2) ? a-b : a+b;
    Exact lo=(fn&1) ? -modulus/2 : 0, hi=(fn&1) ? modulus/2-1 : modulus-1;
    return {Word(std::clamp(exact,lo,hi))&low_mask(width), exact<lo || exact>hi};
  }
  bool shift=fn>=42;
  Exact value=shift ? a : (fn&2) ? a-b : a+b;
  unsigned distance=shift ? rhs&(width-1) : 1;
  if(!distance) return {Word(value)&low_mask(width),false};
  Exact unit=Exact(1)<<distance, base=value>>distance, lost=value-base*unit;
  bool inc=rm==0 ? lost>=unit/2 : rm==1 ? lost>unit/2 || (lost==unit/2 && (base&1)) :
           rm==3 && lost && !(base&1);
  return {Word(base+inc)&low_mask(width),false};
}
struct Product { Word slot; Wide value; };
std::deque<Product> scalar_products;
std::vector<std::pair<Word,Word>> factors;
bool accept_products, return_products;
unsigned divides_sent, divides_returned;
unsigned fp_div_requests, fp_div_replies;

// Work in unsigned SEW magnitudes so MIN/-1 and 64-bit operands never invoke
// host signed overflow. RISC-V's zero divisor returns all ones or the dividend.
static Word divide_expected(Word a, Word b, unsigned width, unsigned fn) {
  const Word mask=low_mask(width), sign=Word(1)<<(width-1);
  a&=mask; b&=mask;
  const bool rem=fn>=34, neg_a=(fn&1) && (a&sign), neg_b=(fn&1) && (b&sign);
  if(!b) return rem ? a : mask;
  const Word ma=neg_a ? -a&mask : a, mb=neg_b ? -b&mask : b;
  const Word magnitude=rem ? ma%mb : ma/mb;
  return ((rem ? neg_a : neg_a!=neg_b) ? -magnitude : magnitude)&mask;
}
template<typename T> void drive_product(T& out, Wide value) {
  if constexpr (std::is_integral_v<T>) out=Word(value);
  else for(auto& chunk:out.words) { chunk=std::uint32_t(value); value>>=32; }
}
template<typename T> Wide factor(T value, bool sign) {
  const Wide modulus=Wide(1)<<(8*sizeof(T));
  return sign && (value & (modulus>>1)) ? Wide(value)-modulus : Wide(value);
}
unsigned mac_form, contested_reads;
extern "C" void observe_mac_ports(Word resetting, Word ex_valid, Word ex_ready, Word store_valid, Word store_ready) {
  if (!resetting && mac_form && ex_valid && store_valid) {
    ++contested_reads;
    if (mac_form==2) CHECK(bool(ex_ready)!=bool(store_ready));
    else CHECK(ex_ready && store_ready);
  }
}

extern "C" void observe_store_capture(Word resetting, Word valid) {
  if (!resetting && valid && watch_writer) { store_capture_cycle=cycles; store_captures.push_back(cycles); }
}

std::vector<unsigned> special_installs;
extern "C" void observe_special_install(Word resetting, Word valid) {
  if (!resetting && valid) special_installs.push_back(cycles);
}
unsigned memory_launch_cycle, execute_launch_cycle;
extern "C" void observe_memory_launch(Word resetting, Word lv, Word lr, Word sv, Word sr) {
  if (!resetting && ((lv && lr) || (sv && sr))) memory_launch_cycle=cycles;
}
extern "C" void observe_execute_launch(Word resetting, Word valid, Word ready) {
  if (!resetting && valid && ready) execute_launch_cycle=cycles;
}

using Beat = std::pair<unsigned,unsigned>; // cycle, physical row
std::vector<Beat> reads_alu, writes_alu;
std::vector<Beat> vrf_reads;
extern "C" void observe_vrf_reads(Word resetting, Word lhs, Word rhs, Word third) {
  if (!resetting && (lhs || rhs || third)) vrf_reads.emplace_back(cycles,lhs|(rhs<<1)|(third<<2));
}
std::vector<Beat> load_mask_samples;
extern "C" void observe_load_mask(Word resetting, Word valid, Word row) {
  if (!resetting && valid) load_mask_samples.emplace_back(cycles,row);
}
bool memory_during_reads;
unsigned blocked_load_writes;
struct MoveClaim { unsigned cycle; Word reads,writes,predicates; };
bool watch_moves;
std::vector<MoveClaim> move_claims;
extern "C" void observe_execute_read(Word resetting, Word valid, Word ready, Word row, Word reads, Word writes, Word predicates) {
  if(!resetting && watch_moves) move_claims.push_back({cycles,reads,writes,predicates});
  if (resetting || !valid || !ready) return;
  reads_alu.emplace_back(cycles,row);
  overlapped_read |= !pending.empty();
  if (watch_writer) writer_read_cycle = std::min(writer_read_cycle,cycles);
}
extern "C" void observe_execute_write(Word resetting, Word valid, Word row) {
  if (!resetting && valid) writes_alu.emplace_back(cycles,row);
}

extern "C" void observe_row_write(Word resetting, Word valid, Word ready, Word row) {
  if (!resetting && valid && !ready) ++blocked_load_writes;
  if (!resetting && valid && ready) {
    writes.push_back(row);
    load_write_cycles.push_back(cycles);
  }
}
void step() {
  CHECK(++cycles < cycle_limit);
  product_in.pready=accept_products;
  product_result_in.pvalid=return_products && !scalar_products.empty();
  if (!scalar_products.empty()) {
    product_result_in.pbits.ptag.pslot=scalar_products.front().slot;
    drive_product(product_result_in.pbits.pvalue,scalar_products.front().value);
  }
  pipeline_in.presponse.pvalid = lookup;
  pipeline_in.presponse.pbits.poutcome = (refuse_page && lookup_address >= 0xf000) ? 4 : lookup_store ? 2 : 1;
  pipeline_in.pcommit_uready = 1;
  memory_in.pdrained = !hold_memory_drain && (!track_row_drain || pending.empty());
  row_memory_in.prequest.pready = cycles >= hold_until && (requests_always_ready || cycles % 4 != 0);
  row_memory_in.presponse.pvalid = !pending.empty() && !block_returns && cycles >= pending.front().due;
  if (!pending.empty()) {
    row_memory_in.presponse.pbits.pstore = pending.front().store;
    Word address = pending.front().address;
    row_memory_in.presponse.pbits.pdata.words = stored.contains(address) ? stored.at(address) : initial;
  }
  eval();
  if(iterative) {
    split_in.prequest.pready=1;
    split_in.presponse.pvalid=split_reply;
    split_in.presponse.pbits.ppage_ufault=1;
    split_in.presponse.pbits.pfault_uaddress=0x3000;
    if(checking) {
      const Word first=checked_range.pfirst, last=checked_range.plast;
      probes.push_back(first);
      precheck_safe=(first>>12)!=8 && (repair || (last>>12)!=3) && !(refuse_page && last>=0xf000);
      window_pages[0]=mapped_page(first); window_pages[1]=mapped_page(last);
    }
    eval();
    split_reply=split_out.prequest.pvalid && split_in.prequest.pready;
    if(split_reply) { CHECK(!repair && split_out.prequest.pbits.paddress==0x2fff); ++split_faults; }
  }
  if (exact_offer) {
    const Word address=exact_span.pfirst;
    exact_probes.push_back(address);
    const bool denied=(refuse_page && address>=0xf000) || (iterative && !repair && address==0x3000);
    exact_mapping.ppage=(iterative || remap_exact)?mapped_page(address):address>>12;
    exact_mapping.pservice=denied ? 3 : segment_device && address==0xefff ? 2 : 0;
    exact_mapping.pcause=exact_span.pstore?15:13;
    eval();
  }
  if (!reset) {
    divides_sent+=divide_sent;
    fp_div_requests+=fp_div_sent;
    fp_div_replies+=fp_div_returned;
    divides_returned+=divide_returned;
    if(product_result_in.pvalid && product_result_out.pready) scalar_products.pop_front();
    if(product_out.pvalid && product_in.pready) {
      const auto& p=product_out.pbits;
      factors.emplace_back(p.poperands.pleft,p.poperands.pright);
      const auto& m=p.poperands.pmode;
      scalar_products.push_back({p.ptag.pslot,factor(p.poperands.pleft,m.pleft_usigned)*factor(p.poperands.pright,m.pright_usigned)});
    }
    const auto& req = row_memory_out.prequest.pbits;
    if (stalled) CHECK(row_memory_out.prequest.pvalid && req.paddress == stalled_address && req.pdata.words == stalled_data && req.pbyte_umask == stalled_mask);
    stalled = row_memory_out.prequest.pvalid && !row_memory_in.prequest.pready;
    if (stalled) { stalled_address = req.paddress; stalled_data = req.pdata.words; stalled_mask = req.pbyte_umask; }
    if (row_memory_out.prequest.pvalid && row_memory_in.prequest.pready) {
      CHECK(req.pbyte_umask != 0);
      pending.push_back({req.paddress,req.pbyte_umask,bool(req.pstore),req.pdata.words,cycles+latency});
      const auto stores = std::count_if(pending.begin(), pending.end(), [](const auto& r) { return r.store; });
      CHECK(stores <= 5 && pending.size() - stores <= metadata_entries);
      masks.push_back(req.pbyte_umask);
      services.push_back(req.pservice);
      addresses.push_back(req.paddress);
      request_cycles.push_back(cycles);
      memory_during_reads |= !reads_alu.empty() && reads_alu.size() < 16;
      if (req.pstore) ++store_offers;
      if (req.pstore && stall_second && addresses.size() == 1) hold_until = cycles + latency + 24;
    }
    if (!pending.empty() && pending.front().store) CHECK(stores_pending && active);
    if (row_memory_in.presponse.pvalid && row_memory_out.presponse.pready) {
      const auto completed = pending.front(); pending.pop_front();
      if (!completed.store) load_return_cycles.push_back(cycles);
      else {
        ++store_acks;
        auto& dst = stored.try_emplace(completed.address,initial).first->second;
        for (unsigned b=0;b<16;++b) if (completed.mask & (1u<<b))
          reinterpret_cast<unsigned char*>(dst.data())[b] = reinterpret_cast<const unsigned char*>(completed.data.data())[b];
      }
    }
    if (fp_flags_valid) { accrued_flags |= fp_flags; ++flag_rows; }
    if (saturated) saturation_cycles.push_back(cycles);
    if (scalar_valid) { scalar_value = scalar_data; ++scalar_returns; }
    if (refuse_page && pipeline_out.pcommit) precise_stores.push_back(commit_address);
    if (outcome_valid && disposition == 2) { last_fault=fault_address; last_cause=fault_cause; }
    if (outcome_valid) outcomes.push_back(disposition);
    if (truncate_out.pvalid) truncations.push_back(truncate_out.pbits);
    if (fault_start_valid) { ++faults; last_vstart=fault_start; }
    finishes += done;
    releases += released;
    narrow += pipeline_out.prequest.pvalid;
  }
  lookup = pipeline_out.prequest.pvalid;
  commit_address = lookup_address;
  lookup_address = pipeline_out.prequest.pbits.pmemory.paddress;
  if (lookup && refuse_page) precise_accesses.push_back(lookup_address);
  lookup_store = pipeline_out.prequest.pbits.pmemory.paccess == 2;
  tick_model();
  eval();
}
Word op(unsigned fn, unsigned s2, unsigned s1, unsigned mode, unsigned dest) {
  return fn << 26 | 1u << 25 | s2 << 20 | s1 << 15 | mode << 12 | dest << 7 | 0x57;
}
void send(Word inst, Word src = 0, bool fast = true) {
  instruction = inst; pc += 4; scalar = src; certificate_valid = fast;
  request_valid = 1;
  for (;;) { eval(); bool accepted = request_ready; step(); if (accepted) break; }
  request_valid = 0;
}
void drain() { do { step(); } while (active || !pending.empty()); }
int main() {
  return run_test([] {
    stride = 8; reset = 1; step(); step(); reset = 0;
    vl = 32; vtype = start_index = 0; precheck_safe = 1;
    window_pages[0] = 0x60; window_pages[1] = 0x90;
    const auto load = [](unsigned rd) { return 0x02000007u | (rd << 7); };
    // Fill the owner ring behind one blocked load with three zero-work descriptors.
    // None may reclaim an owner, start a sequencer, or issue a memory transaction.
    {
      vl=1; block_returns=true; send(load(8),0x2000);
      while(pending.empty()) step();
      auto mem=memory_launch_cycle, ex=execute_launch_cycle, before=finishes;
      auto requests=addresses.size(), captures=vrf_reads.size(); auto flags=flag_rows;
      vl=0; send(op(0,8,9,0,16));
      vl=3; start_index=3; send(load(10),0x3000,false);
      send(0x0a000027u | (12u<<7),0x4000,false); // Empty vsse8.v
      for(unsigned i=0;i<8;++i) step();
      CHECK(active && loads_pending && !stores_pending && finishes==before);
      CHECK(addresses.size()==requests && vrf_reads.size()==captures && flag_rows==flags);
      CHECK(memory_launch_cycle==mem && execute_launch_cycle==ex);
      CHECK(!checking && !exact_offer && request_ready);
      // The full owner ring cannot dispatch the fifth descriptor, but WB has
      // one credit. It remains occupied until an owner becomes available.
      send(op(0,8,9,0,20));
      for(unsigned i=0;i<4;++i) { eval(); CHECK(!request_ready); step(); }
      CHECK(memory_launch_cycle==mem && execute_launch_cycle==ex && finishes==before);
      block_returns=false; drain();
      CHECK(finishes==before+5 && !active && !loads_pending);
      start_index=0; vl=0;
      const auto empty_scan_reads=vrf_reads.size(), empty_scan_writes=writes_alu.size();
      const auto installs=special_installs.size();
      for(unsigned selector:{16u,17u}) { // vcpop.m / vfirst.m
        const auto returns=scalar_returns, completed=finishes;
        stall_gpr=1; send(op(16,8,selector,2,5));
        for(unsigned i=0;i<8;++i) step();
        CHECK(gpr_booking && scalar_returns==returns && finishes==completed);
        stall_gpr=0; drain();
        for(unsigned i=0;i<2;++i) step();
        CHECK(scalar_returns==returns+1 && finishes==completed+1);
        CHECK(scalar_value==(selector==16 ? 0 : low_mask(8*sizeof(memory_in.presponse.pbits.pdata))));
      }
      CHECK(special_installs.size()==installs);
      CHECK(vrf_reads.size()==empty_scan_reads && writes_alu.size()==empty_scan_writes);
    }
    for (bool conflict : {false, true}) {
      vl=128; vtype=2;
      send(op(23, 0, 1, 4, 8), 0x12); // v8 holds source bytes.
      drain();
      overlapped_read = false;
      addresses.clear(); writes.clear();
      unsigned rd = conflict ? 8 : 16;
      const auto before = finishes;
      reads_alu.clear(); load_write_cycles.clear();
      send(op(39,8,3,3,24)); // vmv4r.v: eight DLEN rows
      vl=32; vtype=0;
      send(load(rd), 0x2000);
      drain();
      CHECK(finishes == before + 2);
      CHECK((addresses == std::vector<Word>{0x2000, 0x2010}));
      CHECK((writes == std::vector<Word>{rd * 2u, rd * 2u + 1}));
      CHECK(reads_alu.size()==8 && load_write_cycles.size()==2);
      if (conflict) for(unsigned i=0;i<2;++i) CHECK(load_write_cycles[i]>reads_alu[i].first);
      send(op(16, 24, 0, 2, 5)); drain(); CHECK(scalar_value == 0x12);
      // Sample the upper half of the second physical row, not just byte 0.
      send(op(12, rd, 31, 3, 6)); drain();
      send(op(16, 6, 0, 2, 5)); drain(); CHECK(scalar_value == 0x44);
    }
    send(op(23,0,1,4,9),0); drain(); // Zero addend for the RAW/WAW probes below.
    // Reverse the age order: withhold the load return while the younger
    // independent add actually captures operands, not just its descriptor.
    overlapped_read = false; latency = 12;
    send(load(16), 0x2000);
    send(op(0, 8, 9, 0, 24)); drain();
    CHECK(overlapped_read && narrow == 0);
    // RAW and WAW use committed rows, not returned requests or macro drain.
    // Hold just the second response: the first dependent result must reach the VRF.
    for (bool waw : {false,true}) {
      reads_alu.clear(); writes_alu.clear(); load_write_cycles.clear(); block_returns=true;
      send(load(14), 0x2000);
      send(waw ? op(0,9,7,3,14) : op(0,14,9,0,24));
      while (pending.size()<2) step();
      CHECK(reads_alu.empty() && load_write_cycles.empty());
      block_returns=false; while (pending.size()==2) step(); block_returns=true;
      for (unsigned i=0;i<10;++i) step();
      CHECK(load_write_cycles.size()==1 && reads_alu.size()==1 && writes_alu.size()==1);
      CHECK(reads_alu[0].first>load_write_cycles[0] && writes_alu[0].second==(waw ? 28u:48u));
      block_returns=false; drain();
      CHECK(reads_alu.size()==2 && load_write_cycles.size()==2 && reads_alu[1].first>load_write_cycles[1]);
      send(op(16,waw ? 14 : 24,0,2,5)); drain(); CHECK(scalar_value==(waw ? 7u : 0x41u));
    }
    latency = 3;
    // The two virtual pages are not physically adjacent.
    addresses.clear(); const auto before = releases;
    send(load(0), 0x3ff0, false); drain();
    CHECK((addresses == std::vector<Word>{0x60ff0, 0x90000}));
    CHECK(releases == before + 1);
    // Nonzero vstart aligns independently in the register and in memory.
    addresses.clear(); writes.clear(); start_index = 16;
    send(load(12), 0x4000); drain();
    CHECK((addresses == std::vector<Word>{0x4010} && writes == std::vector<Word>{25}));
    start_index = 0;
    addresses.clear(); vl = 17;
    send(load(10), 0x5000); drain();
    CHECK((addresses==std::vector<Word>{0x5000,0x5010}) && masks.back()==1);
    addresses.clear();
    auto previous = narrow;
    vl = 16; send(load(10), 0x5008); drain();
    CHECK((addresses==std::vector<Word>{0x5000,0x5010}) && masks[masks.size()-2]==0xff00 && masks.back()==0xff && narrow==previous);
    addresses.clear(); send(load(10) & ~(1u<<25),0x5008); drain();
    CHECK((addresses==std::vector<Word>{0x5000,0x5010}) && masks[masks.size()-2]==0x4100 && masks.back()==0x41 && narrow==previous);
    send(load(10) & ~(1u << 25), 0x5000); drain();
    addresses.clear();
    previous = narrow; precheck_safe = 0;
    send(load(10), 0x5008, false); drain();
    CHECK(addresses.size()==16 && narrow==previous);

    const auto store = [](unsigned vs3) { return 0x02000027u | (vs3 << 7); };
    vl = 32; precheck_safe = 1; latency = 12;
    // The middle physical beat contributes to both register rows. Release
    // only its first completed row to the younger add; withhold the last beat.
    stored[0x60ff0]={1,2,3,4}; stored[0x90000]={5,6,7,8}; stored[0x90010]={9,10,11,12};
    block_returns=true; addresses.clear(); reads_alu.clear(); load_write_cycles.clear();
    send(load(12),0x3ff4,false); send(op(0,12,0,3,24));
    while (pending.size()!=2) step();
    const auto prior_replies=load_return_cycles.size();
    block_returns=false; while (load_return_cycles.size()<prior_replies+2) step(); block_returns=true;
    for (unsigned i=0;i<12;++i) step();
    CHECK((addresses==std::vector<Word>{0x60ff0,0x90000,0x90010}));
    CHECK(load_write_cycles.size()==1 && reads_alu.size()==1 && reads_alu[0].first>load_write_cycles[0]);
    block_returns=false; drain(); CHECK(load_write_cycles.size()==2 && reads_alu.size()==2 && reads_alu[1].first>load_write_cycles[1]);
    send(store(24),0x7500); drain();
    CHECK((stored.at(0x7500)==Row{2,3,4,5} && stored.at(0x7510)==Row{6,7,8,9}));
    // The older gather computes the second register of VS3's EMUL=2 group.
    // The store must capture that result, not the stale contents of v9.
    const unsigned offers_before=store_offers, acks_before=store_acks;
    send(load(8), 0x2000); drain();
    send(op(23, 0, 1, 4, 7), 0x5a); drain();
    send(op(23, 0, 1, 4, 6), 0); drain();
    send(op(12, 7, 6, 0, 9));
    send(store(8) | (5<<12), 0x3000); drain(); // vse16.v v8,(base)
    CHECK(store_offers == offers_before+4 && store_acks == acks_before+4);
    CHECK(stored.at(0x3000) == initial && stored.at(0x3010) == initial);
    for (Word a : {0x3020,0x3030}) for (auto w : stored.at(a)) CHECK(w == 0x5a5a5a5a);

    // Same two-page store with an independent vs. aliasing younger writer.
    // Block the request and delay ACK: VS3 becomes writable at final capture,
    // the checked window releases on final request, and memory remains ordered.
    for (bool native_writer : {false,true}) for (bool alias : {false,true}) {
      send(load(8), 0x2000); drain();
      addresses.clear(); overlapped_read = false; latency = 30; stall_second = true;
      auto acks = store_acks, rel = releases;
      hold_until = cycles + 18;
      send(store(8), 0x3ff0, false);
      watch_writer = true; store_captures.clear(); writer_read_cycle = ~0u; store_capture_cycle=0;
      if(native_writer) send(op(0,6,1,4,alias ? 8 : 10),0x66);
      else send(op(39,6,0,3,alias ? 8 : 10)); // vmv1r.v uses VS2 only.
      while (addresses.size() < 2) step();
      CHECK((addresses == std::vector<Word>{0x60ff0, 0x90000}));
      CHECK(releases == rel + 1 && store_acks == acks + 1 && stores_pending);
      CHECK(store_capture_cycle && writer_read_cycle != ~0u);
      CHECK(store_captures.size()==2);
      if (alias) {
        CHECK(writer_read_cycle>store_captures.front());
        CHECK(reads_alu.back().first>store_captures.back());
      } else CHECK(writer_read_cycle<store_capture_cycle);
      send(load(18), 0x2000);
      while (addresses.size() == 2) step();
      CHECK(addresses[2] == 0x2000 && store_acks == acks+1);
      watch_writer = false;
      drain();
      stall_second = false;
      CHECK(stored.at(0x60ff0) == initial && stored.at(0x90000) == initial);
    }
    // Compare every element, including carries/borrows at each lane boundary.
    // Load and store bytes independent of the arithmetic SEW.
    latency = 2;
    for (unsigned i=0; i<2; ++i) {
      stored[0x8000+16*i] = {0xff00ffffu+i,0x7fffffffu,0x01000000u,0x7f805500u};
      stored[0x8100+16*i] = {0x0101ff03u,0x80000002u+i,0xffffffffu,0x807f5500u};
    }
    vtype=0; vl=32; send(load(8),0x8000); send(load(12),0x8100); drain();
    constexpr auto xlen_bytes = sizeof(memory_in.presponse.pbits.pdata);
    for (unsigned sew=0; (1u<<sew)<=xlen_bytes; ++sew) {
      unsigned bytes=1u<<sew;
      for (unsigned fn : {0u,2u,4u,5u,6u,7u,9u,10u,11u,37u,40u,41u}) for (unsigned mode : {0u,3u,4u}) {
        const bool minmax=fn>=4 && fn<=7;
        if ((fn==2 || minmax) && mode==3) continue; // Neither subtract nor min/max has a .vi form.
        vtype=sew<<3; vl=32/bytes; reads_alu.clear(); writes_alu.clear();
        const Word scalar_src=0xf0203040800102ffULL;
        const unsigned imm = fn >= 37 ? 31 : 29;
        send(op(fn,8,mode==3 ? imm : 12,mode,16),scalar_src); drain();
        CHECK(reads_alu.size()==2 && writes_alu.size()==2);
        CHECK(reads_alu[1].first==reads_alu[0].first+1 && writes_alu[1].first==writes_alu[0].first+1);
        vtype=0; vl=32; send(store(16),0x8200); drain();
        for (unsigned i=0; i<2; ++i) {
          Row expected{};
          for (unsigned b=0; b<16; b+=bytes) {
            Word a=0, other=0;
            std::memcpy(&a,reinterpret_cast<const char*>(stored.at(0x8000+16*i).data())+b,bytes);
            std::memcpy(&other,reinterpret_cast<const char*>(stored.at(0x8100+16*i).data())+b,bytes);
            Word rhs = mode==0 ? other : mode==3 ? (fn >= 37 ? Word(imm) : Word(-3)) : scalar_src;
            Word out = fn==0 ? a+rhs : fn==2 ? a-rhs : fn==9 ? a&rhs : fn==10 ? a|rhs : a^rhs;
            if (minmax) {
              rhs &= ~Word(0) >> (64-8*bytes);
              const Word sign=(fn&1) ? Word(1)<<(8*bytes-1) : 0;
              const bool less=(a^sign)<(rhs^sign);
              out=(fn<6 ? less : !less) ? a : rhs;
            }
            unsigned n = rhs & (8*bytes-1);
            if (fn == 37) out = a << n;
            if (fn == 40 || fn == 41) out = a >> n;
            if (fn == 41 && n && (a >> (8*bytes-1))) out |= ~Word(0) << (8*bytes-n);
            std::memcpy(reinterpret_cast<char*>(expected.data())+b,&out,bytes);
          }
          CHECK(stored.at(0x8200+16*i)==expected);
        }
      }
    }
    // Decode every ordinary multiply form. Narrow lanes use both DLEN rows
    // without the scalar service; compare signed/unsigned high halves independently.
    for (unsigned size=1,sew=0; size<xlen_bytes; size*=2,++sew) {
      const unsigned bits=8*size;
      const Word mask=(Word(1)<<bits)-1, sign=Word(1)<<(bits-1);
      for (unsigned fn : {36u,37u,38u,39u}) for (unsigned form : {2u,6u}) {
        vtype=sew<<3; vl=32/size; reads_alu.clear(); writes_alu.clear();
        Word scalar_rhs=0xf9e8d7c6;
        send(op(fn,8,12,form,16),scalar_rhs); drain();
        CHECK(reads_alu.size()==2 && writes_alu.size()==2);
        vtype=0; vl=32; send(store(16),0x8200); drain();
        for (unsigned r=0;r<2;++r) for (unsigned off=0;off<16;off+=size) {
          Word a=0,b=0,actual=0;
          std::memcpy(&a,reinterpret_cast<const char*>(stored.at(0x8000+16*r).data())+off,size);
          std::memcpy(&b,reinterpret_cast<const char*>(stored.at(0x8100+16*r).data())+off,size);
          std::memcpy(&actual,reinterpret_cast<const char*>(stored.at(0x8200+16*r).data())+off,size);
          if (form==6) b=scalar_rhs & mask;
          if (fn>=38 && (a&sign)) a|=~mask;
          if (fn==39 && (b&sign)) b|=~mask;
          CHECK(actual==(((a*b)>>(fn==37 ? 0 : bits)) & mask));
        }
      }
    }
    // The first two independent adds beat both older products to writeback;
    // the third must wait at read issue for their two booked edges.
    vtype=0; vl=32; reads_alu.clear(); writes_alu.clear();
    send(op(45,8,1,6,16),3);
    vtype=1; vl=64; send(op(0,4,1,3,24)); drain();
    CHECK(reads_alu.size()==6 && writes_alu.size()==6);
    const auto t=reads_alu.front().first;
    const unsigned read_at[]={0,1,2,3,6,7}, write_rows[]={48,49,32,33,50,51};
    for (unsigned i=0;i<6;++i) {
      CHECK(reads_alu[i].first==t+read_at[i]);
      CHECK(writes_alu[i]==Beat(t+4+i,write_rows[i]));
    }
    // Fill the fixed pipe, then return an independent load while VXSeq still
    // wants new bookings. The load must get a free edge before the MAC drains.
    // A younger short add uses the same port and all results land exactly once.
    block_returns=true; vtype=0; vl=16;
    load_write_cycles.clear(); reads_alu.clear(); writes_alu.clear();
    send(load(3),0x8000); while (pending.empty()) step();
    vtype=3; vl=256; send(op(45,8,1,6,16),3);
    while (writes_alu.size()<2) step();
    block_returns=false;
    vtype=0; vl=16; send(op(0,4,1,3,24)); drain();
    CHECK(load_write_cycles.size()==1 && reads_alu.size()==17 && writes_alu.size()==17);
    unsigned products=0, adds=0;
    for (auto [edge,row] : writes_alu) {
      CHECK(edge!=load_write_cycles[0]);
      if (row==48) { CHECK(edge==reads_alu[16].first+2); ++adds; }
      else {
        CHECK(row==32+products && edge==reads_alu[products].first+6);
        if (products==15) CHECK(load_write_cycles[0]<edge);
        ++products;
      }
    }
    CHECK(products==16 && adds==1);
    // The consumer can read the first product row before the last one has
    // drained, but neither read can use a slot that is merely result-ready.
    vtype=1; vl=64; reads_alu.clear(); writes_alu.clear();
    send(op(37,8,12,2,16)); send(op(0,16,1,3,24)); drain();
    CHECK(reads_alu.size()==8 && writes_alu.size()==8);
    for (unsigned i=0;i<4;++i) CHECK(reads_alu[4+i].first>writes_alu[i].first);
    CHECK(reads_alu[4].first<writes_alu[3].first);
    // An m8 group is sixteen physical rows. Check actual handshakes, including
    // overlap with younger disjoint memory and WAR on an older unread source.
    for (bool to_store : {false,true}) for (bool alias : {false,true}) {
      vtype=3; vl=256; reads_alu.clear(); writes_alu.clear(); request_cycles.clear(); load_write_cycles.clear(); memory_during_reads=false;
      send(op(0,8,1,3,24));
      vtype=0; vl=16;
      send(to_store ? store(alias ? 31 : 3) : load(alias ? 15 : 3),to_store ? 0x8300 : 0x8100);
      drain();
      if (!alias) CHECK(memory_during_reads);
      else if (to_store) CHECK(request_cycles.front()>writes_alu.at(14).first);
      else CHECK(request_cycles.front()<reads_alu.at(14).first && load_write_cycles.front()>reads_alu.at(14).first);
      if (!alias && !to_store) CHECK(blocked_load_writes > 0);
      CHECK(reads_alu.size()==16 && writes_alu.size()==16);
      for (unsigned i=0;i<16;++i) {
        CHECK(reads_alu[i].second==16+i && writes_alu[i].second==48+i);
        if (to_store || alias) CHECK(reads_alu[i].first==reads_alu[0].first+i);
        CHECK(writes_alu[i].first==reads_alu[i].first+2);
      }
    }
    // WAW then RAW, including an in-place update. The younger store must read
    // the last writer. A restarted row leaves the first row untouched.
    vtype=0; vl=32; send(load(16),0x8000); send(load(12),0x8100); drain();
    start_index=16;
    send(op(0,8,1,3,16)); send(op(11,12,31,3,16)); send(op(11,16,31,3,16));
    start_index=0; send(store(16),0x8200); drain();
    CHECK(stored.at(0x8200)==stored.at(0x8000) && stored.at(0x8210)==stored.at(0x8110));
    // Seed full registers using ordinary memory transactions. Check untouched
    // bytes as well as active results; agnostic policy also preserves them.
    const auto masked_add = op(0,8,8,0,16) & ~(1u<<25);
    constexpr Word src=0x9000, dst=0xa000, mask=0xb000, zero=0xb100, out=0xc000;
    for (unsigned r=0; r<16; ++r) {
      stored[src+16*r] = {0x817ec3ff+r,0x1ff7a302,0xf0ff0021,0xc6440100+r};
      stored[dst+16*r].fill(0x5a39c61d);
    }
    stored[mask] = {0x2496a55a,0x89070310,0x7fab1208,0xee825621};
    stored[mask+16] = {0x5312fe0b,0x9327add0,0x2098906f,0x0000ffff};
    stored[zero] = stored[zero+16] = Row{};
    auto state = [](unsigned sew, unsigned lmul, unsigned start, unsigned length) {
      vtype=(sew<<3)|lmul; start_index=start; vl=length;
    };
    // Whole transfers use the encoded EEW and register count, never vl/LMUL.
    // Restart inside a row (inside register two for vmv4r). An unaligned
    // store checks the entire untouched prefix as well as the copied suffix.
    const unsigned prior_budget=cycle_limit-cycles;
    cycle_limit=cycles+2200;
    for(unsigned n:{1u,2u,4u,8u}) {
      const unsigned group=(n-1)<<29, sew=1, width_code=5<<12, whole=8<<20;
      state(sew,0,0,0); send(load(8)|group|whole|width_code,src);
      send(load(16)|group|whole|width_code,dst); drain();
      const unsigned restart=n==4 ? 19 : 3;
      state(sew,0,restart,0); reads_alu.clear(); writes_alu.clear(); request_cycles.clear();
      send(op(39,8,n-1,3,16));
      state(0,0,0,0); send(store(16)|group|whole,out+1); drain();
      CHECK(reads_alu.size()==2*n-restart/8 && writes_alu.size()==reads_alu.size());
      if(n==8) CHECK(request_cycles.front()<writes_alu.back().first);
      for(unsigned i=0;i<32*n;++i) {
        auto byte=[](Word base,unsigned offset){return reinterpret_cast<const unsigned char*>(stored.at(base+16*(offset/16)).data())[offset%16];};
        CHECK(byte(out,1+i)==byte(i<2*restart?dst:src,i));
      }
    }
    // Mask memory counts bytes (ceil(vl/8)); vstart skips bytes, not bits.
    // A whole-register load at nonzero vstart instead counts encoded elements.
    state(0,0,0,0); send(load(0)|(8u<<20),dst); drain();
    state(0,3,2,137); addresses.clear(); masks.clear(); send(load(0)|(11u<<20),src); drain();
    CHECK((masks==std::vector<unsigned>{0xfffc,3}));
    for(unsigned r=0;r<2;++r) stored[out+16*r].fill(0xabababab);
    state(0,3,1,137); masks.clear(); send(store(0)|(11u<<20),out); drain();
    CHECK((masks==std::vector<unsigned>{0xfffe,3}));
    for(unsigned i=0;i<32;++i) {
      auto byte=[&](Word a){return reinterpret_cast<const unsigned char*>(stored.at(a+16*(i/16)).data())[i%16];};
      CHECK(byte(out)==(!i || i>=18 ? 0xab : byte(i<2?dst:src)));
    }
    state(1,0,3,0); masks.clear(); send(load(16)|(8u<<20)|(5u<<12),src); drain();
    CHECK((masks==std::vector<unsigned>{0xffc0,0xffff}));
    // Declined mask and whole-register loads still use VLSeq. A younger copy waits for their writes.
    for(unsigned mode:{8u,11u}) {
      state(0,0,0,mode==8?0:9);
      precheck_safe=0; addresses.clear(); auto lookups=narrow;
      send(load(8)|(mode<<20),src,false);
      state(0,0,0,0); send(op(39,8,0,3,16)); drain();
      CHECK(addresses.size()==(mode==8?32u:2u) && narrow==lookups);
      precheck_safe=1;
      state(0,0,0,1); send(op(16,16,0,2,5)); drain(); CHECK(scalar_value==(Word(reinterpret_cast<const signed char*>(stored.at(src).data())[0]) & low_mask(8*xlen_bytes)));
    }
    // Scalar extracts still write at vl=0. Hold each port unavailable after
    // capture: no duplicate reservation, VRF write, or premature completion.
    for(bool fp:{false,true}) for(unsigned lmul:{2u,3u}) {
      const Word inserted=fp?UINT64_C(0xffffffffffff3c00):UINT64_C(0xfffffffffffffe81);
      constexpr Word row9=Word(1)<<18; // First DLEN row of v9, not the base of an m4/m8 group.
      // A live insert with nonzero vstart still writes element zero, not element one.
      state(1,lmul,1,2); watch_moves=true; move_claims.clear(); reads_alu.clear(); writes_alu.clear();
      const auto prior_reads=vrf_reads.size();
      send(op(16,0,1,fp?5:6,9),inserted); drain();
      CHECK(reads_alu.size()==1 && writes_alu.size()==1 && writes_alu.front().second==18 && vrf_reads.size()==prior_reads);
      for(const auto& c:move_claims)
        CHECK(!c.reads && !c.predicates && c.writes==(c.cycle>execute_launch_cycle && c.cycle<=reads_alu.front().first ? row9 : 0));
      const auto before_insert=execute_launch_cycle;
      state(1,lmul,0,0); move_claims.clear(); reads_alu.clear(); writes_alu.clear();
      send(op(16,0,1,fp?5:6,9),0); drain();
      CHECK(execute_launch_cycle==before_insert && reads_alu.empty() && writes_alu.empty());
      for(const auto& c:move_claims) CHECK(!c.reads && !c.writes && !c.predicates);
      start_index=1;
      stall_fpr=fp; stall_gpr=!fp; unsigned completed=finishes;
      move_claims.clear(); send(op(16,9,0,fp?1:2,5));
      for(unsigned t=0;t<12;++t) step();
      CHECK(bool(fpr_booking)==fp && bool(gpr_booking)==!fp && finishes==completed && !scalar_valid && !fpr_valid);
      CHECK(reads_alu.size()==1 && reads_alu.front().second==18 && vrf_reads.size()==prior_reads+1);
      for(const auto& c:move_claims)
        CHECK(!c.writes && !c.predicates && c.reads==(c.cycle>execute_launch_cycle && c.cycle<=reads_alu.front().first ? row9 : 0));
      watch_moves=false;
      stall_fpr=stall_gpr=0; step();
      CHECK(bool(fpr_valid)==fp && bool(scalar_valid)==!fp);
      CHECK((fp?fpr_data:scalar_data)==(inserted & low_mask(8*xlen_bytes)));
      drain(); CHECK(finishes==completed+1 && reads_alu.size()==1 && writes_alu.empty());
    }
    cycle_limit=cycles+prior_budget;
    // Withhold the second load row: masked vmin may produce its first row and
    // VSSeq may capture it, but neither may advance past the missing source.
    state(0,0,0,32); send(load(0),mask); send(load(24),dst); drain();
    load_write_cycles.clear(); reads_alu.clear(); writes_alu.clear(); store_captures.clear();
    block_returns=true; send(load(8),src); while (pending.size()!=2) step();
    state(0,0,1,31); send(op(5,8,1,4,24) & ~(1u<<25),0x20);
    state(0,0,0,32); watch_writer=true; send(store(24),out);
    CHECK(reads_alu.empty() && store_captures.empty());
    block_returns=false; while (pending.size()==2) step(); block_returns=true;
    for (unsigned i=0;i<16;++i) step();
    CHECK(load_write_cycles.size()==1 && reads_alu.size()==1 && writes_alu.size()==1 && store_captures.size()==1);
    block_returns=false; drain(); watch_writer=false;
    CHECK(load_write_cycles.size()==2 && reads_alu.size()==2 && writes_alu.size()==2 && store_captures.size()==2);
    for (unsigned r=0;r<2;++r) {
      CHECK(load_write_cycles[r]<reads_alu[r].first && writes_alu[r]==Beat(reads_alu[r].first+2,48+r));
      CHECK(writes_alu[r].first<store_captures[r]);
      for (unsigned b=0;b<16;++b) {
        const unsigned i=16*r+b;
        auto byte=[&](Word base){return (stored.at(base+16*r)[b/4]>>(8*(b%4)))&255;};
        const bool active=i>=1 && i<31 && (stored.at(mask)[0]>>i&1);
        CHECK(byte(out)==(active ? (byte(src)>=128 ? byte(src) : std::min(byte(src),32u)) : byte(dst)));
      }
    }
    // Compare full data rows into fragments of v0. A younger masked load may
    // sample each half only after its closing write, independently of the other.
    constexpr Word cmp_a=0x11000, cmp_b=0x12000;
    for(unsigned i=0;i<256;++i) {
      const unsigned a=(i*37)&255, b=i%3 ? (255-a) : a;
      stored[cmp_a+16*(i/16)][(i/4)%4]|=std::uint32_t(a)<<(8*(i%4));
      stored[cmp_b+16*(i/16)][(i/4)%4]|=std::uint32_t(b)<<(8*(i%4));
    }
    state(0,3,0,256); send(load(8),cmp_a); send(load(16),cmp_b); drain();
    for(unsigned fn=24;fn<32;++fn) for(unsigned form : {0u,3u,4u}) {
      if((form==0 && fn>=30) || (form==3 && (fn==26 || fn==27))) continue;
      const bool masked=form!=4, chain=fn==24 && form==0;
      state(0,0,0,32); send(load(0),mask); drain();
      if(chain) {
        // Keep the final source row for the first v0 fragment group unavailable.
        // Reload the same bytes, so the stall cannot change the expected comparison.
        state(0,0,16,32); block_returns=true; send(load(11),cmp_a+96);
        while(pending.empty()) step();
      }
      state(0,3,5,251); writes_alu.clear(); load_mask_samples.clear();
      send(op(fn,8,form==0 ? 16 : 30,form,0) & ~(unsigned(masked)<<25),0xfe);
      if(chain) {
        state(0,3,0,251);
        instruction=load(24)&~(1u<<25); pc+=4; scalar=cmp_a; request_valid=1;
        for(unsigned i=0;i<20;++i) {
          eval(); bool taken=request_valid && request_ready; step(); if(taken) request_valid=0;
        }
        CHECK(writes_alu.size()==7 && load_mask_samples.empty());
        block_returns=false;
        while(request_valid) { eval(); bool taken=request_ready; step(); if(taken) request_valid=0; }
      }
      drain();
      CHECK(writes_alu.size()==16);
      for(unsigned r=0;r<16;++r) CHECK(writes_alu[r].second==r/8);
      if(chain) {
        CHECK(!load_mask_samples.empty() && load_mask_samples.front().second==0 && load_mask_samples.back().second==1);
        CHECK(load_mask_samples.front().first<writes_alu.back().first);
        for(auto [edge,row]:load_mask_samples) CHECK(row<2 && edge>writes_alu[8*row+7].first);
      }
      state(0,0,0,32); send(store(0),out); drain();
      for(unsigned i=0;i<256;++i) {
        auto bit=[&](Word base){return (stored.at(base+16*(i/128))[(i%128)/32]>>(i%32))&1u;};
        const unsigned a=(i*37)&255, b=form ? 254 : i%3 ? 255-a : a;
        const unsigned sa=a^128, sb=b^128;
        bool truth[]={a==b,a!=b,a<b,sa<sb,a<=b,sa<=sb,a>b,sa>sb};
        CHECK(bit(out)==(i>=5 && i<251 && (!masked || bit(mask)) ? truth[fn-24] : bit(mask)));
      }
    }
    // All 15 encodings: produce carry/borrow bits in v0, then consume those
    // bits as arithmetic inputs. Prestart/tail storage starts nonzero.
    const unsigned remaining_budget=cycle_limit-cycles;
    cycle_limit=cycles+2500;
    for(bool sub : {false,true}) for(unsigned form : {0u,3u,4u}) for(bool input : {false,true}) {
      if(sub && form==3) continue;
      const unsigned rhs=form ? 30 : 16, fn=sub ? 18 : 16;
      state(0,3,0,256); send(load(24),cmp_a);
      state(0,0,0,32); send(load(0),mask); drain();
      state(0,3,5,251); writes_alu.clear(); reads_alu.clear();
      send(op(fn+1,8,rhs,form,0) & ~(unsigned(input)<<25),0xfe);
      send(op(fn,8,rhs,form,24) & ~(1u<<25),0xfe); drain();
      CHECK(writes_alu.size()==32 && reads_alu.size()==32);
      CHECK(reads_alu[16].first<writes_alu[15].first);
      for(unsigned r=0;r<16;++r) {
        CHECK(writes_alu[r].second==r/8 && writes_alu[16+r].second==48+r);
        CHECK(reads_alu[16+r].first>writes_alu[8*(r/8)+7].first);
      }
      state(0,0,0,32); send(store(0),out);
      state(0,3,0,256); send(store(24),out+256); drain();
      for(unsigned i=0;i<256;++i) {
        auto bit=[&](Word base){return (stored.at(base+16*(i/128))[(i%128)/32]>>(i%32))&1u;};
        unsigned a=i*37%256,b=form ? 254 : i%3 ? 255-a : a,c=input && bit(mask);
        bool body=i>=5 && i<251, carry=sub ? a<b+c : a+b+c>255;
        CHECK(bit(out)==(body ? carry : bit(mask)));
        unsigned data=(stored.at(out+256+16*(i/16))[(i/4)%4]>>(8*(i%4)))&255;
        CHECK(data==(body ? (sub ? a-b-carry : a+b+carry)&255 : a));
      }
    }
    // An outstanding load owns v0. Neither no-input mask-result opcode
    // reads that register implicitly, so both can write v4 before it returns.
    state(0,0,0,32); block_returns=true; writes_alu.clear();
    send(load(0),mask); while(pending.empty()) step();
    for(unsigned fn : {17u,19u}) send(op(fn,8,16,0,4));
    for(unsigned i=0;i<16;++i) step();
    CHECK(writes_alu.size()==4 && !pending.empty());
    block_returns=false; drain();
    // E16 exercises selector expansion across bytes and sign extension of
    // the immediate. Observe real VRF ports, not just a VXSeq issue token.
    cycle_limit=cycles+1800;
    for(bool merge:{false,true}) for(unsigned form:{0u,3u,4u}) {
      state(0,0,0,32); send(load(0),mask); send(load(8),src); send(load(16),cmp_b); send(load(24),dst); drain();
      state(1,0,1,15); vrf_reads.clear(); writes_alu.clear();
      send(op(23,merge ? 8 : 0,form ? 30 : 16,form,24) & ~(unsigned(merge)<<25),0x1234); drain();
      const unsigned ports=(merge ? 1 : 0)|(form==0 ? 2 : 0);
      CHECK(writes_alu.size()==2 && vrf_reads.size()==(ports ? 2 : 0));
      for(auto [edge,read_ports]:vrf_reads) CHECK(read_ports==ports);
      state(0,0,0,32); send(store(24),out); drain();
      for(unsigned i=0;i<16;++i) {
        auto half=[&](Word base){return (stored.at(base+16*(i/8))[(i/2)%4]>>(16*(i%2)))&65535u;};
        const auto rhs=form==3 ? 0xfffe : form==4 ? 0x1234 : half(cmp_b);
        CHECK(half(out)==(i==0 || i==15 ? half(dst) : merge && !(stored.at(mask)[0]>>i&1) ? half(src) : rhs));
      }
    }
    // Hold v0's load response: the splat completes without a read, while
    // copying an explicitly named v0 waits and takes the vector RHS port.
    state(0,0,0,32); block_returns=true; send(load(0),mask); while(pending.empty()) step();
    vrf_reads.clear(); writes_alu.clear();
    send(op(23,0,9,3,4)); send(op(23,0,1,4,5),11); send(op(23,0,0,0,6));
    for(unsigned i=0;i<10;++i) step();
    CHECK(writes_alu.size()==4 && vrf_reads.empty());
    block_returns=false; drain();
    CHECK(writes_alu.size()==6 && vrf_reads.size()==2);
    for(auto [edge,ports]:vrf_reads) CHECK(ports==2);
    send(store(6),out); drain(); CHECK(stored.at(out)==stored.at(mask) && stored.at(out+16)==stored.at(mask+16));
    // VMADC produces alternating selectors. A delayed load owns the last
    // source fragment of its first mask row while a younger VMERGE is offered.
    constexpr Word striped=0x13000;
    for(unsigned r=0;r<16;++r) stored[striped+16*r].fill(0x00ff00ff);
    state(0,3,0,256); send(load(8),striped); send(load(16),cmp_b); send(load(24),dst); drain();
    state(0,0,16,32); block_returns=true; send(load(11),striped+96); while(pending.empty()) step();
    state(0,3,3,253); writes_alu.clear(); reads_alu.clear(); send(op(17,8,1,3,0));
    instruction=op(23,8,16,0,24)&~(1u<<25); pc+=4; request_valid=1;
    for(unsigned i=0;i<20;++i) {
      eval(); bool taken=request_valid && request_ready; step(); if(taken) request_valid=0;
    }
    CHECK(reads_alu.size()==7 && writes_alu.size()==7);
    block_returns=false;
    while(request_valid) { eval(); bool taken=request_ready; step(); if(taken) request_valid=0; }
    drain(); CHECK(reads_alu.size()==32 && writes_alu.size()==32);
    CHECK(reads_alu[16].first<writes_alu[15].first);
    for(unsigned r=0;r<16;++r) CHECK(reads_alu[16+r].first>writes_alu[8*(r/8)+7].first);
    state(0,3,0,256); send(store(24),out); drain();
    for(unsigned i=0;i<256;++i) {
      auto byte=[&](Word base){return (stored.at(base+16*(i/16))[(i/4)%4]>>(8*(i%4)))&255u;};
      CHECK(byte(out)==(i<3 || i>=253 ? byte(dst) : i%2 ? byte(striped) : byte(cmp_b)));
    }
    // FP routing never rounds its operands. Exercise both a valid boxed sNaN
    // and an invalid box wherever FLEN exceeds SEW, and full-width payloads otherwise.
    cycle_limit=cycles+1800;
    constexpr Word fp_mask=0x14000, fp_source=0x14100, fp_stripes=0x14200;
    stored[fp_mask].fill(0xaaaaaaaa); stored[fp_mask+16]=stored[fp_mask];
    const auto fp_requests_before=shared_fp_requests, flags_before=flag_rows;
    for(unsigned sew=1,size=2;size<=xlen_bytes;++sew,size*=2) {
      const unsigned count=32/size;
      const Word low_mask=~Word(0)>>(64-8*size);
      const Word snan=size==2 ? 0x7c23 : size==4 ? 0x7f800023 : UINT64_C(0x7ff0000087654321);
      const Word canonical=size==2 ? 0x7e00 : 0x7fc00000;
      for(unsigned i=0;i<count;++i) {
        Word lane=snan+0x40+i;
        std::memcpy(reinterpret_cast<char*>(stored[fp_source+16*(i*size/16)].data())+i*size%16,&lane,size);
      }
      for(bool merge : {false,true}) for(bool bad_box : {false,true}) {
        if(bad_box && size==xlen_bytes) continue;
        // Clear just the top FLEN bit of an otherwise correct box.
        Word scalar_bits=snan|~low_mask;
        if(bad_box) scalar_bits^=Word(1)<<(8*xlen_bytes-1);
        state(0,0,0,32); send(load(8),fp_source); send(load(24),dst); drain();
        block_returns=!merge; send(load(0),fp_mask);
        if(merge) drain(); else while(pending.empty()) step();
        vrf_reads.clear(); writes_alu.clear();
        state(sew,0,1,count-1);
        send(op(23,merge ? 8 : 0,1,5,24)&~(unsigned(merge)<<25),scalar_bits);
        if(!merge) {
          for(unsigned i=0;i<12;++i) step();
          CHECK(writes_alu.size()==2 && vrf_reads.empty() && !pending.empty());
          block_returns=false;
        }
        drain(); CHECK(writes_alu.size()==2 && vrf_reads.size()==(merge ? 2 : 0));
        for(auto [edge,ports]:vrf_reads) CHECK(ports==1);
        CHECK(shared_fp_requests==fp_requests_before && flag_rows==flags_before);
        state(0,0,0,32); send(store(24),out); drain();
        for(unsigned i=0;i<count;++i) {
          auto lane=[&](Word base) {
            Word value=0;
            std::memcpy(&value,reinterpret_cast<const char*>(stored.at(base+16*(i*size/16)).data())+i*size%16,size);
            return value;
          };
          CHECK(lane(out)==(i==0 || i==count-1 ? lane(dst) : merge && !(i%2) ? lane(fp_source) : bad_box ? canonical : snan));
        }
      }
    }
    // A late L1D response stalls VMADC's final fragment of v0. VFMERGE
    // must wait for its booked write, then use the completed alternating mask.
    for(unsigned r=0;r<16;++r) {
      stored[fp_stripes+16*r]={~0u,0,~0u,0};
      stored[fp_source+16*r].fill(0x7f800051+r);
    }
    // VMSEQ.VI -1 produces the same mask as VMADC.VIM +1 for these words.
    for(bool compare:{false,true}) {
      state(0,3,0,256); send(load(8),fp_stripes); send(load(16),fp_source); send(load(24),dst); drain();
      state(0,0,16,32); block_returns=true; send(load(15),fp_stripes+14*16); while(pending.empty()) step();
      state(2,3,1,63); reads_alu.clear(); writes_alu.clear(); send(op(compare?24:17,8,compare?31:1,3,0));
      instruction=op(23,16,1,5,24)&~(1u<<25); pc+=4; scalar=UINT64_C(0xffffffff7f800022); request_valid=1;
      for(unsigned i=0;i<30;++i) {
        eval(); bool taken=request_valid && request_ready; step(); if(taken) request_valid=0;
      }
      CHECK(reads_alu.size()==15 && writes_alu.size()==15);
      block_returns=false;
      while(request_valid) { eval(); bool taken=request_ready; step(); if(taken) request_valid=0; }
      drain(); CHECK(writes_alu.size()==32 && reads_alu.size()==32);
      CHECK(reads_alu[16].first>writes_alu[15].first && shared_fp_requests==fp_requests_before && flag_rows==flags_before);
      state(0,3,0,256); send(store(24),out); drain();
      for(unsigned i=0;i<64;++i) {
        auto word=[&](Word base){return stored.at(base+16*(i/4))[i%4];};
        CHECK(word(out)==(i==0 || i==63 ? word(dst) : i%2 ? word(fp_source) : 0x7f800022u));
      }
    }
    cycle_limit=cycles+remaining_budget;
    // Mask registers are single registers even at m8. Exercise unaligned
    // register numbers, in-place writes and both partial edge bytes.
    for(unsigned fn=0x18;fn<=0x1f;++fn) {
      state(0,0,0,32); send(load(5),src); send(load(7),mask); drain();
      const unsigned first=fn%2 ? 131 : 5, end=fn%2 ? 149 : 251;
      state(0,3,first,end); reads_alu.clear(); writes_alu.clear();
      send(op(fn,5,7,2,5)); drain();
      CHECK(reads_alu.size()==2-first/128 && writes_alu.size()==reads_alu.size());
      state(0,0,0,32); send(store(5),out); drain();
      for(unsigned i=0;i<256;++i) {
        auto bit=[&](Word base){return (stored.at(base+16*(i/128))[(i%128)/32]>>(i%32))&1u;};
        unsigned a=bit(src), b=bit(mask);
        unsigned truth[]={a&!b,a&b,a|b,a^b,a|!b,!(a&b),!(a|b),!(a^b)};
        CHECK(bit(out)==(i>=first && i<end ? truth[fn-0x18] : a));
      }
    }
    // The older store still needs v0[1], but never touches v0[0]. Hold the
    // request port so VS3 cannot finish capturing the predicated store;
    // merely delaying ACKs does not keep its register source live.
    constexpr Word old_pred=0xe000, flip_pred=0xe100, store_base=0xf000;
    for(unsigned r=0;r<2;++r) {
      stored[old_pred+16*r].fill(0x55555555); stored[flip_pred+16*r].fill(~0u);
    }
    state(0,0,0,32); send(load(0),old_pred); send(load(8),flip_pred); drain();
    block_returns=true; writes_alu.clear(); addresses.clear(); masks.clear(); load_mask_samples.clear();
    hold_until=cycles+80;
    state(0,3,128,256); send(store(16) & ~(1u<<25),store_base);
    state(0,3,0,256); send(op(0x1b,0,8,2,0)); send(load(24) & ~(1u<<25),src);
    for(unsigned i=0;i<32;++i) step();
    CHECK(writes_alu.size()==1 && writes_alu.front().second==0 && !load_mask_samples.empty());
    for(auto [sample,row]:load_mask_samples) CHECK(row==0 && sample>writes_alu.front().first);
    hold_until=cycles; block_returns=false; drain();
    CHECK(writes_alu.size()==2 && writes_alu.back().second==1 && load_mask_samples.back().second==1);
    for(auto [sample,row]:load_mask_samples) CHECK(sample>writes_alu.at(row).first);
    unsigned loads=0,stores=0;
    for(unsigned i=0;i<addresses.size();++i) {
      bool is_store=addresses[i]>=store_base;
      CHECK(masks[i]==(is_store ? 0x5555 : 0xaaaa));
      if(is_store) ++stores; else ++loads;
    }
    CHECK(stores==8 && loads==16);
    auto check_data = [&](unsigned rows, unsigned bytes, unsigned first, unsigned end, bool predicated, bool shift = false) {
      for (unsigned r=0; r<rows; ++r) {
        Row expected=stored.at(dst+16*r);
        for (unsigned b=0; b<16; b+=bytes) {
          unsigned i=(16*r+b)/bytes;
          bool enabled=(stored.at(mask+16*(i/128))[(i%128)/32]>>(i%32))&1;
          if (i<first || i>=end || (predicated && !enabled)) continue;
          Word a=0;
          std::memcpy(&a,reinterpret_cast<const char*>(stored.at(src+16*r).data())+b,bytes);
          a = shift ? a >> 3 : a+a;
          std::memcpy(reinterpret_cast<char*>(expected.data())+b,&a,bytes);
        }
        CHECK(stored.at(out+16*r)==expected);
      }
    };
    state(0,3,0,256); send(load(8),src); drain();
    state(0,0,0,32); send(load(0),mask); drain();
    // No drain between the producer load, masked vsrl.vi and consuming store.
    state(0,0,0,32); send(load(16),dst); send(load(8),src);
    state(1,0,1,15); send(op(40,8,3,3,16) & ~(1u<<25));
    state(0,0,0,32); send(store(16),out); drain(); check_data(2,2,1,15,true,true);
    for (unsigned sew=0; (1u<<sew)<=xlen_bytes; ++sew) for (bool predicated : {false,true}) {
      const unsigned bytes=1u<<sew, first=1, end=32/bytes-1;
      state(0,0,0,32); send(load(16),dst); drain();
      state(sew,0,first,end);
      if (predicated) vtype |= 0xc0;
      reads_alu.clear(); writes_alu.clear();
      send(predicated ? masked_add : op(0,8,8,0,16)); drain();
      CHECK(reads_alu.size()==2 && writes_alu.size()==2);
      CHECK(reads_alu.back().first==reads_alu.front().first+1 && writes_alu.back().first==writes_alu.front().first+1);
      state(0,0,0,32); send(store(16),out); drain(); check_data(2,bytes,first,end,predicated);
    }
    // Fractional LMUL: prestart and tail in the same row; leave the rest of vd untouched.
    send(load(16),dst); drain();
    state(0,7,3,11); reads_alu.clear(); send(masked_add); drain(); CHECK(reads_alu.size()==1);
    state(0,0,0,32); send(store(16),out); drain(); check_data(2,1,3,11,true);

    // Force a load return to take the physical write port mid-stream.
    // The waiting load may stall new reads, never an already promised write.
    state(0,3,0,256); send(load(16),dst); drain();
    const unsigned stalls=blocked_load_writes;
    state(0,3,13,243); send(masked_add);
    state(0,0,0,16); send(load(3),zero); drain();
    CHECK(blocked_load_writes>stalls);
    state(0,3,0,256); send(store(16),out); drain(); check_data(16,1,13,243,true);

    // Each half of v0 waits for its producer independently. A younger
    // writer to that half waits for its last snapshot, not its first.
    // The second half uses v0 bits 128..255, including a fully disabled last row.
    state(0,0,0,32); send(load(0),zero); drain();
    state(0,3,0,256); send(load(16),dst); drain();
    latency=12;
    reads_alu.clear(); writes_alu.clear(); request_cycles.clear(); load_write_cycles.clear();
    const unsigned retired=finishes;
    state(0,0,0,32); send(load(0),mask);
    state(0,3,3,253); send(masked_add);
    state(0,0,0,32); send(load(0),zero); drain();
    CHECK(finishes==retired+3 && reads_alu.size()==16 && writes_alu.size()==16);
    CHECK(load_write_cycles.size()==4 && request_cycles.size()==4);
    for (unsigned half=0;half<2;++half) {
      CHECK(reads_alu[8*half].first>load_write_cycles[half]);
      CHECK(load_write_cycles[2+half]>reads_alu[8*half+7].first);
    }
    CHECK(request_cycles[2]<reads_alu.back().first);
    for (unsigned r=0;r<16;++r) {
      CHECK(reads_alu[r].second==16+r && writes_alu[r].first==reads_alu[r].first+2);
      // A waiting v0 return can occupy a gap between booked ALU writes.
      CHECK(writes_alu[r].second==32+r && writes_alu[r].first>reads_alu[r].first);
    }
    state(0,3,0,256); send(store(16),out); drain(); check_data(16,1,3,253,true);
    // An entirely false predicate still closes exactly one instruction owner.
    state(0,0,0,32); send(load(16),dst); drain();
    const unsigned before_empty=finishes;
    writes_alu.clear(); send(masked_add); drain();
    CHECK(finishes==before_empty+1 && writes_alu.size()==2);
    send(store(16),out); drain();
    CHECK(stored.at(out)==stored.at(dst) && stored.at(out+16)==stored.at(dst+16));

    // A single read seeds all active writes; the younger v0 writer cannot
    // change the predicate observed by the rest of the splat.
    send(load(0),mask); drain();
    state(0,3,0,256); send(load(16),dst); drain();
    const unsigned remaining_after_splat=cycle_limit-cycles;
    cycle_limit=cycles+800;
    previous=narrow; addresses.clear(); stored[0xd000]=Row{};
    send(0x08000007u | (16u<<7),0xd000); // masked vlse8.v v16,(base),x0
    state(0,0,1,31); reads_alu.clear(); send(op(11,8,8,0,0)); drain();
    CHECK(narrow==previous && addresses==std::vector<Word>{0xd000} && reads_alu.size()==2);
    state(0,3,0,256); send(store(16),out); drain();
    for (unsigned i=0;i<256;++i) {
      const bool enabled=(stored.at(mask+16*(i/128))[(i%128)/32]>>(i%32))&1;
      const auto byte=[&](Word base) { return (stored.at(base+16*(i/16))[(i%16)/4]>>(8*(i%4)))&255; };
      CHECK(byte(out)==(enabled ? 0 : byte(dst)));
    }
    cycle_limit=cycles+remaining_after_splat;
    // EEW=16, SEW=8: vstart and vl bound elements, not physical byte lanes.
    // Force an older native writer of v0 and a younger one around the memory operation.
    constexpr Word pred = 0xd800;
    stored[pred] = {0x8493,0,0,0}; stored[pred+16] = Row{};
    for (bool is_store : {false,true}) {
      state(0,0,0,32); send(load(0),zero); send(load(12),src); send(load(16),dst); drain();
      for (unsigned r=0;r<3;++r) stored[out+16*r]=stored.at(dst+16*r);
      send(load(8),pred); drain();
      send(op(0,8,0,3,0)); // vadd.vi v0,v8,0
      addresses.clear(); masks.clear();
      state(0,0,1,15);
      constexpr unsigned displacement=6;
      send(((is_store ? store(12) : load(16)) | (5u<<12)) & ~(1u<<25),(is_store ? out : src)+displacement);
      state(0,0,0,32); send(op(11,8,8,0,0)); drain(); // vxor.vv v0,v8,v8
      const Word base=is_store ? out : src;
      std::vector<Word> expect_addresses;
      std::vector<unsigned> expect_masks;
      for (unsigned r=0;r<3;++r) {
        unsigned enabled=0;
        for (unsigned i=1;i<15;++i) if ((0x8493u>>i & 1) && (2*i+displacement)/16==r)
          enabled|=3u<<((2*i+displacement)%16);
        if (enabled) { expect_addresses.push_back(base+16*r); expect_masks.push_back(enabled); }
      }
      CHECK(addresses==expect_addresses && masks==expect_masks);
      if (!is_store) { send(store(16),out); drain(); }
      auto byte=[&](Word a){ return reinterpret_cast<const unsigned char*>(stored.at(a & ~Word{15}).data())[a%16]; };
      for (unsigned i=0;i<(is_store ? 48u : 32u);++i) {
        const unsigned logical=is_store ? i-displacement : i;
        bool active_byte=logical/2>=1 && logical/2<15 && (0x8493u & (1u<<(logical/2)));
        CHECK(byte(out+i)==byte(active_byte ? src+logical+(is_store ? 0 : displacement) : dst+i));
      }
      // A zero final row (indeed, both rows) consumes the pinned window once.
      addresses.clear(); writes.clear();
      auto rel=releases, fin=finishes;
      send((is_store ? store(12) : load(16)) & ~(1u<<25),0x3ff4,false); drain();
      CHECK(addresses.empty() && writes.empty() && releases==rel+1 && finishes==fin+1);
    }
    // Sparse rows on nonadjacent physical pages. Cross the v0 shadow-word
    // boundary, skipping interior rows, and release the window on an empty tail.
    for (bool is_store : {false,true}) {
      stored[pred]={1,0,0,0}; stored[pred+16]={2,0,0,0};
      state(0,0,0,32); send(load(0),pred); drain();
      state(0,3,0,175); addresses.clear(); masks.clear(); writes.clear();
      const unsigned rel=releases, fin=finishes;
      send((is_store ? store(8) : load(16)) & ~(1u<<25),0x3ff7,false); drain();
      CHECK((addresses==std::vector<Word>{0x60ff0,0x90070}));
      CHECK((masks==std::vector<unsigned>{1u<<7,1u<<8}) && releases==rel+1 && finishes==fin+1);
      if (!is_store) CHECK((writes==std::vector<Word>{32,40}));
    }
    // Live rows 0 and 2 surround an empty row. The skipped tail generates no
    // response. A dependent add can pass row 1, but not the outstanding row 2.
    stored[pred]={1,1,0,0};
    state(0,0,0,32); send(load(0),pred); drain();
    state(0,1,0,64); reads_alu.clear(); load_write_cycles.clear(); addresses.clear();
    block_returns=true; send(load(16) & ~(1u<<25),0x3ff4,false); send(op(0,16,0,3,24));
    while (pending.size()<2) step();
    CHECK(reads_alu.empty() && (addresses==std::vector<Word>{0x60ff0,0x90010}));
    block_returns=false; while (pending.size()==2) step(); block_returns=true;
    for (unsigned i=0;i<20;++i) step();
    CHECK(load_write_cycles.size()==1 && reads_alu.size()==2 && reads_alu[0].first>load_write_cycles[0]);
    block_returns=false; drain();
    CHECK(load_write_cycles.size()==2 && reads_alu.size()==4 && reads_alu[2].first>load_write_cycles[1]);
    // Deny the second page. A failed envelope check is not a trap: precise
    // execution must consult v0, preserve earlier effects and fault only at an active element.
    for (bool is_store : {false,true}) for (bool active_fault : {false,true}) {
      state(0,0,0,32);
      stored[pred][0]=0x42u | (active_fault ? 1u<<19 : 0u);
      send(load(0),pred); send(load(16),dst); drain();
      addresses.clear(); precise_accesses.clear(); precise_stores.clear(); refuse_page=true; precheck_safe=0;
      unsigned prior_faults=faults;
      send((is_store ? store(16) : load(16)) & ~(1u<<25),0xeff2,false); drain();
      refuse_page=false; precheck_safe=1;
      CHECK((addresses==std::vector<Word>{0xeff0,0xeff0}) && precise_accesses.empty() && precise_stores.empty());
      CHECK(faults==prior_faults+unsigned(active_fault));
      if (active_fault) CHECK(last_fault==0xf005 && last_cause==(is_store ? 15u : 13u) && last_vstart==19);
      if (!is_store) {
        send(store(16),out); drain();
        for (unsigned r=0;r<2;++r) {
          Row expected_data=stored.at(dst+16*r);
          if (r==0) for (unsigned b:{1u,6u}) {
            const Row &source=stored.contains(0xeff0)?stored.at(0xeff0):initial;
            reinterpret_cast<unsigned char*>(expected_data.data())[b]=reinterpret_cast<const unsigned char*>(source.data())[2+b];
          }
          CHECK(stored.at(out+16*r)==expected_data);
        }
      }
    }
    // Hold an unrelated older load's responses. The store can capture both
    // source rows, so even an aliasing add can run before the store issues.
    // Repeat across owner wrap and check the bytes the store actually captured.
    for (bool alias : {false,true}) {
      state(0,0,0,32); send(load(8),src); drain();
      block_returns=true; reads_alu.clear(); addresses.clear();
      const auto fin=finishes, acks=store_acks, rel=releases;
      send(load(20),dst); while (pending.size()<2) step();
      hold_until=cycles+80; // Stall subsequent grants independently of the credit budget.
      send(store(8),0x3ff0,false); send(op(0,12,1,3,alias ? 8 : 24));
      for (unsigned i=0;i<24;++i) step();
      CHECK((addresses==std::vector<Word>{dst,dst+16}));
      CHECK(reads_alu.size()==2 && finishes==fin && releases==rel);
      latency=48; hold_until=0; block_returns=false;
      while (addresses.size()!=4 || reads_alu.size()!=2) step();
      CHECK(store_acks==acks); // WAR ends at capture, not acknowledgement.
      drain();
      CHECK(finishes==fin+3 && releases==rel+1 && stored.at(0x60ff0)==stored.at(src) && stored.at(0x90000)==stored.at(src+16));
    }
    // A queued load claims its destination before it reaches the engine. Hold
    // the preceding store acknowledgement; neither RAW nor WAW may pass.
    for (bool waw : {false,true}) {
      block_returns=true; addresses.clear(); reads_alu.clear(); load_write_cycles.clear();
      send(store(8),out); while (pending.size()<2) step();
      hold_until=cycles+80;
      send(load(16),dst);
      send(waw ? op(0,12,1,3,16) : op(0,16,1,3,24));
      for (unsigned i=0;i<20;++i) step();
      CHECK(reads_alu.empty() && (addresses==std::vector<Word>{out,out+16}));
      hold_until=0; block_returns=false; drain();
      CHECK(load_write_cycles.size()==2 && reads_alu.size()==2);
      for (unsigned r=0;r<2;++r) CHECK(reads_alu[r].first>load_write_cycles[r]);
    }
    // A one-row masked load can capture its last use of v0 before an aliasing
    // store is acknowledged. Its dependent reader still needs the VRF write.
    state(0,0,0,16); stored[pred]={0xa35c,0,0,0};
    send(load(0),pred); drain();
    block_returns=true; addresses.clear(); masks.clear(); writes_alu.clear(); load_write_cycles.clear();
    const auto acks_before_load=store_acks;
    send(store(8),out); send(load(16) & ~(1u<<25),out);
    send(op(11,8,8,0,0));  // overwrite v0 with zero
    send(op(0,16,0,3,24)); // read vd
    for (unsigned i=0;i<20;++i) step();
    CHECK(store_acks==acks_before_load && pending.size()==1 && pending.front().store);
    CHECK((addresses==std::vector<Word>{out}) && writes_alu.size()==1 && writes_alu[0].second==0);
    block_returns=false; while (store_acks==acks_before_load) step(); block_returns=true;
    for (unsigned i=0;i<20;++i) step();
    CHECK((addresses==std::vector<Word>{out,out}) && masks.back()==0xa35c);
    CHECK(pending.size()==1 && !pending.front().store && writes_alu.size()==1 && load_write_cycles.empty());
    block_returns=false; drain();
    CHECK(load_write_cycles.size()==1 && writes_alu.size()==2 && writes_alu[1].first>load_write_cycles[0]);
    // Early capture must also wait for an older whole-register copy producing v0.
    send(op(23,0,1,4,28),0xa5); send(op(23,0,1,4,29),0); drain();
    addresses.clear(); masks.clear();
    send(op(39,28,0,3,0)); // vmv1r.v copies the mask through VXSeq.
    send(load(16) & ~(1u<<25),src); drain();
    CHECK((addresses==std::vector<Word>{src}) && masks.back()==0xa5a5);
    // With two rows sharing v0, the first captured mask is not the last use.
    // The younger writer must wait until VLSeq can sample the second mask.
    state(0,0,0,32); stored[pred]={0xffff0000,0,0,0}; stored[pred+16]=Row{};
    send(load(0),pred); drain();
    block_returns=true; addresses.clear(); masks.clear(); writes_alu.clear();
    send(store(8),out); send(load(16) & ~(1u<<25),out);
    send(op(11,8,8,0,0));
    for (unsigned i=0;i<16;++i) step();
    CHECK(writes_alu.empty() && (addresses==std::vector<Word>{out,out+16}));
    block_returns=false; while (pending.size()) step(); block_returns=true;
    for (unsigned i=0;i<24;++i) step();
    CHECK((addresses==std::vector<Word>{out,out+16,out+16}) && masks.back()==0xffff);
    CHECK(pending.size()==1 && writes_alu.size()==2);
    block_returns=false; drain();
    // Both the disjoint store and add may launch, but neither may consume
    // stale v0. Withhold its producer's responses and observe both boundaries.
    block_returns=true; reads_alu.clear();
    const auto before_v0=cycles, before_masked_store=store_offers;
    send(load(0),mask); send(store(8) & ~(1u<<25),out); send(op(0,0,1,3,24));
    for (unsigned i=0;i<12;++i) step();
    CHECK(memory_launch_cycle>before_v0 && execute_launch_cycle>before_v0);
    CHECK(reads_alu.empty() && store_offers==before_masked_store);
    block_returns=false; drain();
    CHECK(reads_alu.size()==2);
    // Younger loads of the last source or destination of an active m8 add
    // can fetch early. The row's old reader or writer still wins the VRF.
    const auto saved_latency=latency;
    latency=2;
    for (unsigned rd : {14u,30u,6u}) {
      state(0,3,0,256); send(load(8),src); drain();
      reads_alu.clear(); writes_alu.clear(); load_return_cycles.clear(); load_write_cycles.clear();
      send(op(0,8,0,3,24));
      state(0,0,0,32); send(load(rd),dst); send(op(0,rd,0,3,4)); drain();
      CHECK(reads_alu.size()==18 && writes_alu.size()==18 && load_return_cycles.size()==2);
      CHECK(load_return_cycles[1]<reads_alu[12].first);
      for (unsigned r=0;r<2;++r) {
        if (rd==14) CHECK(load_write_cycles[r]>reads_alu[12+r].first);
        if (rd==30) CHECK(load_write_cycles[r]>writes_alu[12+r].first);
        CHECK(reads_alu[16+r].first>load_write_cycles[r]);
      }
      send(store(30),out); drain(); send(store(4),out+32); drain();
      for (unsigned r=0;r<2;++r) {
        CHECK(stored.at(out+16*r)==stored.at((rd==30 ? dst : src+192)+16*r));
        CHECK(stored.at(out+32+16*r)==stored.at(dst+16*r));
      }
    }
    latency=saved_latency;
    // A masked load accepts row 0 but skips row 1. Row 1's old value is
    // ready immediately; skipping it must not release row 0's pending write.
    state(0,0,0,32); stored[pred]={0xffff,0,0,0}; stored[pred+16]=Row{};
    send(load(0),pred); send(load(16),dst); drain();
    block_returns=true; reads_alu.clear(); addresses.clear();
    send(load(16) & ~(1u<<25),src);
    state(0,0,16,32); send(op(0,16,0,3,24));
    for (unsigned i=0;i<20;++i) step();
    CHECK((addresses==std::vector<Word>{src}) && reads_alu.size()==1 && reads_alu[0].second==33);
    state(0,0,0,16); send(op(0,16,0,3,24));
    for (unsigned i=0;i<8;++i) step();
    CHECK(reads_alu.size()==1);
    block_returns=false; drain();
    state(0,0,0,32); send(store(24),out); drain();
    CHECK(stored.at(out)==stored.at(src) && stored.at(out+16)==stored.at(dst+16));

    // Stream an m8 producer into a store of the very first register of its
    // group. Both stored rows must be new while later rows are still executing.
    state(0,0,0,32); send(load(8),src); drain();
    reads_alu.clear(); writes_alu.clear(); request_cycles.clear();
    state(0,3,0,256); send(op(0,8,0,3,24));
    state(0,0,0,32); send(store(24),out); drain();
    CHECK(request_cycles.size()==2 && writes_alu.size()==16);
    for (unsigned r=0;r<2;++r) {
      CHECK(request_cycles[r]>writes_alu[r].first && request_cycles[r]<writes_alu.back().first);
      CHECK(stored.at(out+16*r)==stored.at(src+16*r));
    }
    // Release just one load row. The dependent store captures it, then a
    // younger VXOR overwrites it without either store row reaching L1D.
    state(0,0,0,32); addresses.clear(); store_captures.clear();
    load_write_cycles.clear(); writes_alu.clear(); watch_writer=block_returns=true;
    const auto before_capture=releases, before_store_issue=store_offers;
    send(load(16),src);
    while (pending.size()!=2) step();
    const Row untouched{0xcccccccc,0xcccccccc,0xcccccccc,0xcccccccc};
    stored[0x60ff0]=stored[0x90000]=stored[0x90010]=untouched;
    send(store(16),0x3ff4,false); // two VS3 rows across three cache beats, with independent PPNs
    send(op(11,16,16,0,16)); // vxor.vv v16,v16,v16
    for (unsigned i=0;i<8;++i) step();
    CHECK(store_captures.empty() && writes_alu.empty());
    hold_until=cycles+80;
    block_returns=false; while (pending.size()==2) step(); block_returns=true;
    for (unsigned i=0;i<12;++i) step();
    CHECK(load_write_cycles.size()==1 && store_captures.size()==1 && writes_alu.size()==1);
    CHECK(load_write_cycles[0]<store_captures[0] && store_captures[0]<writes_alu[0].first);
    CHECK(writes_alu[0].second==32 && addresses.size()==2 && releases==before_capture);
    hold_until=cycles+60; block_returns=false;
    while (writes_alu.size()!=2) step();
    CHECK(load_write_cycles.size()==2 && store_captures.size()==2 && writes_alu[1].second==33);
    CHECK(load_write_cycles[1]<store_captures[1] && store_captures[1]<writes_alu[1].first);
    CHECK(store_offers==before_store_issue && releases==before_capture && cycles<hold_until);
    drain(); watch_writer=false;
    CHECK((addresses==std::vector<Word>{src,src+16,0x60ff0,0x90000,0x90010}) && releases==before_capture+1);
    const auto &lo=stored.at(src), &hi=stored.at(src+16);
    CHECK((stored.at(0x60ff0)==Row{untouched[0],lo[0],lo[1],lo[2]}));
    CHECK((stored.at(0x90000)==Row{lo[3],hi[0],hi[1],hi[2]}));
    CHECK((stored.at(0x90010)==Row{hi[3],untouched[1],untouched[2],untouched[3]}));
    send(store(16),out); drain(); CHECK(stored.at(out)==Row{} && stored.at(out+16)==Row{});
    // Four VS3 rows and no cache grants: reserve exactly two, then resume.
    state(0,1,0,64); send(load(8),src); drain();
    watch_writer=true; store_captures.clear(); addresses.clear(); hold_until=cycles+48;
    send(store(8),out);
    for (unsigned i=0;i<24;++i) step();
    CHECK(addresses.empty() && store_captures.size()==2);
    drain(); watch_writer=false;
    CHECK(store_captures.size()==4 && (addresses==std::vector<Word>{out,out+16,out+32,out+48}));
    for (unsigned r=0;r<4;++r) CHECK(stored.at(out+16*r)==stored.at(src+16*r));
    // Both load descriptors fit before either returns. The occupied owner
    // ring leaves one WB credit for a dependent store; a sixth descriptor stalls.
    state(0,0,0,16); addresses.clear(); writes.clear(); reads_alu.clear();
    const auto before_run=finishes;
    track_row_drain=block_returns=true;
    send(load(16),dst);
    while (pending.empty()) step();
    send(store(8),out); send(op(0,16,0,3,24));
    auto blocked = [&](Word insn) {
      instruction=insn; certificate_valid=1; request_valid=1; scalar=src;
      for (unsigned i=0;i<4;++i) { eval(); CHECK(!request_ready); step(); }
      request_valid=0;
    };
    send(load(12),src); send(store(12),out+16); blocked(op(0,8,0,3,10));
    CHECK((addresses==std::vector<Word>{dst,out,src}) && writes.empty() && reads_alu.empty());
    block_returns=false; drain(); track_row_drain=false;
    CHECK(finishes==before_run+5 && stored.at(out)==stored.at(src) && stored.at(out+16)==stored.at(src));
    CHECK((addresses==std::vector<Word>{dst,out,src,out+16}));
    send(store(24),out); drain(); CHECK(stored.at(out)==stored.at(dst));

    // Four disjoint stores may issue before the first ACK, even though VSSeq
    // has only one active operand cursor. All four must eventually write their bytes.
    state(0,0,0,16); addresses.clear(); block_returns=true;
    const std::vector<Word> disjoint_stores{0x56000,0x56040,0x56080,0x560c0};
    for (auto addr:disjoint_stores) send(store(8),addr);
    for (unsigned i=0;i<32;++i) step();
    CHECK(pending.size()==4 && addresses==disjoint_stores);
    block_returns=false; drain();
    for (auto addr:addresses) CHECK(stored.at(addr)==stored.at(src));

    // VSSeq has already captured the bytes, but VSAQ still owns the ACK.
    // A waiting load observes only bytes enabled by the older masked store.
    stored[pred]={0x0ff0,0,0,0}; stored[out]=initial;
    send(load(0),pred); drain();
    addresses.clear(); masks.clear(); writes.clear();
    track_row_drain=block_returns=true;
    state(0,0,0,32); send(store(16) & ~(1u<<25),out);
    while (pending.empty()) step();
    state(0,0,0,16); send(load(24),out);
    send(store(8),out+0x80);
    while (addresses.size()<2) step();
    CHECK((addresses==std::vector<Word>{out,out+0x80}) && (masks==std::vector<unsigned>{0x0ff0,0xffff}) && writes.empty());
    block_returns=false; drain(); track_row_drain=false;
    CHECK(std::count(addresses.begin(),addresses.end(),out)==2);
    CHECK(std::count(addresses.begin(),addresses.end(),out+0x80)==1 && addresses.size()==3);
    CHECK((writes==std::vector<Word>{48}));
    const Row merged{initial[0],stored.at(dst)[1],stored.at(dst)[2],initial[3]};
    send(store(24),out+16); drain(); CHECK(stored.at(out+16)==merged);
    // The older virtual span maps onto two nonadjacent physical pages. A
    // younger identity-mapped VA aliases only its last page. Free a credit
    // without releasing that page; the peer must still wait in both orders.
    for (bool store_first : {false,true}) {
      state(0,0,0,32); addresses.clear(); track_row_drain=block_returns=true;
      send(store_first ? store(8) : load(16),0x3ff0,false);
      while (pending.size()!=2) step();
      state(0,0,0,16);
      send(store_first ? load(16) : store(8),0x90000);
      block_returns=false; while (pending.size()==2) step(); block_returns=true;
      for (unsigned i=0;i<20;++i) step();
      CHECK((addresses==std::vector<Word>{0x60ff0,0x90000}) && pending.size()==1);
      block_returns=false; drain(); track_row_drain=false;
      CHECK((addresses==std::vector<Word>{0x60ff0,0x90000,0x90000}));
    }
    // A load supplies two FP32 source rows for one integer16 destination.
    // Keep its second response pending after the first fragment writes:
    // the younger store must wait, even with a free VRF read port.
    stored[src]={0x3f800000,0x40000000,0x40400000,0x40800000};
    stored[src+16]={0x40a00000,0x40c00000,0x40e00000,0x41000000};
    state(0,0,0,32); block_returns=true; send(load(8),src);
    while(pending.size()<2) step();
    reads_alu.clear(); writes_alu.clear(); store_captures.clear(); watch_writer=true;
    state(1,0,0,8); send(op(0x12,8,17,1,16)); // vfncvt.x.f.w
    state(0,0,0,16); send(store(16),out);
    const auto before_fp_fragment=load_return_cycles.size();
    block_returns=false; while(load_return_cycles.size()==before_fp_fragment) step(); block_returns=true;
    for(unsigned i=0;i<18;++i) step();
    CHECK(writes_alu.size()==1 && writes_alu[0].second==32 && store_captures.empty());
    block_returns=false; drain(); watch_writer=false;
    CHECK(writes_alu.size()==2 && store_captures.size()==1 && store_captures[0]>writes_alu[1].first);
    CHECK((stored.at(out)==Row{0x20001,0x40003,0x60005,0x80007}));

    // Hold the real iterative reply while VSSeq owns a younger dependent
    // store. One active lane divides by zero; a masked 0/0 must not set NV.
    state(2,0,0,4);
    stored[src]={0,0x40800000,0,0}; send(load(8)|(6u<<12),src);
    send(op(23,0,0,3,12)); send(op(23,0,1,3,16)); drain();
    // Only lane 1 is active; every other lane would compute 0/0.
    stored[mask]={2,0,0,0}; state(0,0,0,4); send(load(0),mask); drain();
    state(2,0,0,4); fp_div_enable=1; fp_div_release=0; accrued_flags=flag_rows=0;
    send(op(0x20,8,12,1,16) & ~(1u<<25)); while(!fp_div_requests) step();
    watch_writer=true; store_captures.clear(); send(store(16)|(6u<<12),out);
    for(unsigned i=0;i<40;++i) step();
    CHECK(store_captures.empty() && !fp_div_replies && !flag_rows);
    fp_div_release=1; drain(); watch_writer=false;
    CHECK(fp_div_requests==1 && fp_div_replies==1 && store_captures.size()==1 && accrued_flags==8);
    CHECK((stored.at(out)==Row{1,0x7f800000,1,1}));
    fp_div_enable=fp_div_release=0;

    // The FP service cannot even accept its request yet. Only independent
    // adds may pass; each consumer must wait for its own divider's row write.
    const unsigned remaining_before_divides=cycle_limit-cycles;
    cycle_limit=cycles+300;
    state(2,0,0,1); stored[src]={0x40900000,0,0,0}; stored[dst]={0x40000000,0,0,0};
    send(load(8)|(6u<<12),src); send(load(12)|(6u<<12),dst); drain();
    // The successor installs on the divide's read edge, not at its first
    // scalar request. Only an independent successor can read on the next edge.
    for(bool fp : {false,true}) for(bool dependent : {false,true}) {
      cycle_limit=cycles+180;
      divide_enable=fp_div_enable=divide_release=fp_div_release=0;
      divides_sent=fp_div_requests=0; reads_alu.clear(); writes_alu.clear();
      send(op(32,8,12,fp?1:2,16)); send(op(0,dependent?16:8,0,3,24));
      CHECK(reads_alu.size()==1 && execute_launch_cycle==reads_alu.front().first);
      for(unsigned i=0;i<6;++i) step();
      CHECK(!divides_sent && !fp_div_requests);
      if(dependent) CHECK(reads_alu.size()==1 && writes_alu.empty());
      else CHECK(reads_alu.size()==2 && reads_alu.back().first==execute_launch_cycle+1 && writes_alu.size()==1);
      divide_enable=fp_div_enable=divide_release=fp_div_release=1; drain();
      CHECK(reads_alu.size()==2 && writes_alu.size()==1 && divides_sent==unsigned(!fp) && fp_div_requests==unsigned(fp));
      send(store(24)|(6u<<12),out); drain();
      CHECK(stored.at(out)[0]==(dependent ? (fp ? 0x40100000u : 1u) : 0x40900000u));
    }
    cycle_limit=cycles+300;
    divide_enable=1; fp_div_enable=divide_release=fp_div_release=0;
    divides_sent=divides_returned=fp_div_requests=fp_div_replies=0;
    send(op(32,8,12,1,18)); send(op(32,8,12,2,16));
    reads_alu.clear(); writes_alu.clear();
    send(op(0,8,0,3,24)); while(writes_alu.empty()) step();
    CHECK(writes_alu.back().second==48 && divides_sent==1 && !divides_returned && !fp_div_requests);
    reads_alu.clear(); writes_alu.clear();
    send(op(0,18,0,3,20));
    for(unsigned i=0;i<8;++i) step();
    CHECK(reads_alu.empty());
    fp_div_enable=1; while(!fp_div_requests) step();
    for(unsigned i=0;i<5;++i) step();
    CHECK(reads_alu.empty() && !fp_div_replies);
    fp_div_release=1;
    while(writes_alu.empty()) step();
    CHECK(writes_alu.back().second==40 && reads_alu.size()==1 && fp_div_replies==1 && !divides_returned);
    send(op(0,16,0,3,22));
    for(unsigned i=0;i<8;++i) step();
    CHECK(reads_alu.size()==1);
    divide_release=1; drain();
    CHECK(reads_alu.size()==2 && writes_alu.back().second==44 && divides_returned==1);
    send(store(20)|(6u<<12),out); drain(); CHECK(stored.at(out)[0]==0x40100000); // 4.5 / 2.0
    send(store(22)|(6u<<12),out); drain(); CHECK(stored.at(out)[0]==1);
    // Only element zero divides. The second row must retire without a
    // request or flag update and leave every byte of its destination intact.
    cycle_limit=cycles+200;
    stored[mask]={1,0,0,0}; state(0,0,0,1); send(load(0),mask); drain();
    state(2,0,0,8); send(op(23,0,7,3,16)); drain();
    fp_div_requests=fp_div_replies=flag_rows=0;
    send(op(32,8,12,1,16)&~(1u<<25)); drain();
    CHECK(fp_div_requests==1 && fp_div_replies==1 && flag_rows==1);
    send(store(16)|(6u<<12),out); drain();
    CHECK((stored.at(out)==Row{0x40100000,7,7,7}) && (stored.at(out+16)==Row{7,7,7,7}));
    fp_div_enable=fp_div_release=divide_enable=divide_release=0;
    cycle_limit=cycles+remaining_before_divides;

    // Both forms of each native fixed FP operation run without a shared FP responder.
    // Lane 1 in each row is an sNaN. The masked destination and the tail survive.
    for (unsigned r=0; r<2; ++r) {
      stored[src+16*r] = {0x3fc00000,0x7f800001,0xc0000000,0x3f800000};
      stored[dst+16*r].fill(0x3f000000);
    }
    stored[mask] = {0xdd,0,0,0};
    state(0,0,0,32); send(load(0),mask); send(load(8),src); send(load(12),dst); drain();
    // Two neighboring FP32 conversions of the same row use distinct policies.
    // Lane 0 contains 1.5, lane 1 is a masked sNaN, and old vd is 0.5.
    const unsigned remaining_before_conversions=cycle_limit-cycles;
    cycle_limit=cycles+4*latency+80; // Two destination initializations and two result stores.
    send(load(16),dst); send(load(20),dst); drain();
    state(2,0,0,4); frm=3; accrued_flags=flag_rows=0;
    send(op(0x12,8,1,1,16) & ~(1u<<25)); // dynamic RUP
    send(op(0x12,8,7,1,20) & ~(1u<<25)); // fixed RTZ
    frm=1; drain(); frm=0;
    CHECK(accrued_flags==1 && flag_rows==2); // NX from lane 0, never NV
    send(store(16)|(6u<<12),out); send(store(20)|(6u<<12),out+16); drain();
    CHECK((stored.at(out)==Row{2,0x3f000000,0xfffffffe,1}));
    CHECK((stored.at(out+16)==Row{1,0x3f000000,0xfffffffe,1}));
    cycle_limit=cycles+remaining_before_conversions;
    // Decode real VX/VF slides; the scalar insertion is a bit move even
    // when a forwarded vector lane is an sNaN.
    for(unsigned form:{5u,6u}) for(unsigned fn:{14u,15u}) {
      state(2,0,0,7); flag_rows=0; writes_alu.clear();
      const unsigned inserted=0x7f800002;
      send(op(fn,8,1,form,16),UINT64_C(0xffffffff00000000)|inserted); drain();
      CHECK(flag_rows==0 && writes_alu.size()==2);
      state(0,0,0,32); send(store(16),out); drain();
      for(unsigned i=0;i<7;++i) {
        const bool inject=fn==14 ? i==0 : i==6;
        const unsigned j=fn==14 ? i-1 : i+1;
        CHECK(stored.at(out+16*(i/4))[i%4]==(inject ? inserted : stored.at(src+16*(j/4))[j%4]));
      }
    }
    // The explicit selection mask may be v0. A sparse selection emits
    // exactly two writes, the second partial; keep the untouched tail.
    state(0,0,0,32); send(load(16),dst); drain();
    state(2,0,0,7); writes_alu.clear(); send(op(23,8,0,2,16)); drain();
    CHECK(writes_alu.size()==2);
    state(0,0,0,32); send(store(16),out); drain();
    CHECK((stored.at(out)==Row{0x3fc00000,0xc0000000,0x3f800000,0x3fc00000}));
    CHECK((stored.at(out+16)==Row{0xc0000000,0x3f000000,0x3f000000,0x3f000000}));
    send(load(20),zero); drain();
    state(2,0,0,7); writes_alu.clear();
    send(op(23,8,20,2,16)); // No selected elements; the next store reads the old vd.
    state(0,0,0,32); send(store(16),out+32); drain();
    CHECK(writes_alu.empty() && stored.at(out+32)==stored.at(out) && stored.at(out+48)==stored.at(out+16));
    for (unsigned fn : {0u,2u,36u}) for (unsigned form : {1u,5u}) {
      state(0,0,0,32); send(load(16),dst); drain();
      state(2,0,0,7); accrued_flags=flag_rows=0; reads_alu.clear();
      const auto before_shared_fp=shared_fp_requests;
      send(op(fn,8,12,form,16) & ~(1u<<25), UINT64_C(0xffffffff3f000000)); drain();
      CHECK(shared_fp_requests==before_shared_fp && accrued_flags==0 && flag_rows==2 && reads_alu.size()==2);
      state(0,0,0,32); send(store(16),out); drain();
      const Row expected = fn==0 ? Row{0x40000000,0x3f000000,0xbfc00000,0x3fc00000}
                         : fn==2 ? Row{0x3f800000,0x3f000000,0xc0200000,0x3f000000}
                                 : Row{0x3f400000,0x3f000000,0xbf800000,0x3f000000};
      CHECK(stored.at(out)==expected);
      Row tail=expected; tail.back()=0x3f000000; CHECK(stored.at(out+16)==tail);
    }
    state(2,0,0,8); accrued_flags=0; send(op(0,8,12,1,16)); drain(); CHECK(accrued_flags==16);
    state(0,0,0,32); send(store(16),out); drain(); CHECK(stored.at(out)[1]==0x7fc00000);
    // 0.5 plus half its ULP rounds upward under RUP, setting NX.
    state(2,0,0,8); frm=3; accrued_flags=0;
    send(op(0,12,1,5,16), UINT64_C(0xffffffff33000000)); drain();
    CHECK(accrued_flags==1);
    state(0,0,0,32); send(store(16),out); drain(); CHECK(stored.at(out)[0]==0x3f000001);
    if (xlen_bytes==8) {
      state(2,0,0,8); accrued_flags=0;
      send(op(0,12,1,5,16), 0x3f000000); drain(); CHECK(accrued_flags==0);
      state(0,0,0,32); send(store(16),out); drain(); CHECK(stored.at(out)[0]==0x7fc00000);
    }
    // The older load supplies old vd one row at a time. Row 0's exact fused
    // result (1+2^-23)*(1-2^-23)-1 = -2^-46 would be zero if rounded twice.
    frm=0;
    for (unsigned r=0;r<2;++r) {
      stored[src+16*r]={0x3f800001,0x3f800000,0,0};
      stored[dst+16*r].fill(0x3f7ffffe);
      stored[zero+16*r]={0xbf800000,0x7f800001,0x40000000,0xc0400000};
    }
    state(0,0,0,32); send(load(8),src); send(load(12),dst); drain();
    for (unsigned form : {1u,5u}) {
      state(0,0,0,32); load_write_cycles.clear(); reads_alu.clear(); writes_alu.clear();
      accrued_flags=flag_rows=0; block_returns=true;
      send(load(16),zero); while (pending.size()<2) step();
      state(2,0,0,7);
      send(op(0x2c,8,12,form,16) & ~(1u<<25),UINT64_C(0xffffffff3f7ffffe));
      state(0,0,0,32); watch_writer=true; store_captures.clear();
      send(store(16),out);
      for (unsigned i=0;i<8;++i) step();
      CHECK(reads_alu.empty() && store_captures.empty());
      const auto before_return=load_return_cycles.size();
      block_returns=false; while (load_return_cycles.size()==before_return) step(); block_returns=true;
      for (unsigned i=0;i<18;++i) step();
      CHECK(load_write_cycles.size()==1 && reads_alu.size()==1 && writes_alu.size()==1 && store_captures.size()==1);
      CHECK(load_write_cycles[0]<reads_alu[0].first && writes_alu[0].first<store_captures[0]);
      block_returns=false; drain(); watch_writer=false;
      CHECK(flag_rows==2 && !accrued_flags && reads_alu.size()==2 && writes_alu.size()==2 && store_captures.size()==2);
      CHECK(load_write_cycles[1]<reads_alu[1].first && writes_alu[1].first<store_captures[1]);
      const Row expected{0xa8800000,0x7f800001,0x40000000,0xc0400000};
      CHECK(stored.at(out)==expected && stored.at(out+16)==expected);
    }
    // Now execute only the sNaN accumulator that the preceding masks preserved.
    state(2,0,1,2); accrued_flags=0; send(op(0x2c,8,12,1,16)); drain();
    CHECK(accrued_flags==16);
    state(0,0,0,16); send(store(16),out); drain(); CHECK(stored.at(out)[1]==0x7fc00000);
    // A long FMA and an independent store have overlapping operand demand.
    // Store capture is observed on the synchronous return, one edge after grant.
    for (unsigned form : {1u,5u}) {
      reads_alu.clear(); writes_alu.clear(); store_captures.clear(); watch_writer=true;
      state(2,3,0,64); send(op(0x2c,8,16,form,24),UINT64_C(0xffffffff3f800000));
      state(0,0,0,32); send(store(0),out); drain(); watch_writer=false;
      CHECK(reads_alu.size()==16 && store_captures.size()==2);
      bool simultaneous=false;
      for (auto capture : store_captures) {
        CHECK(reads_alu.front().first < capture && capture <= reads_alu.back().first+1);
        for (auto read : reads_alu) simultaneous |= capture==read.first+1;
      }
      CHECK(simultaneous==(form==5));
    }
    // A blocked load receives an unbooked edge in the middle of the FMA.
    // A consumer of the group may capture each row only after its actual write.
    auto stalls_before_fma=blocked_load_writes;
    latency=2; requests_always_ready=true; block_returns=true;
    state(0,0,0,16); send(load(1),src);
    state(2,3,0,64); writes_alu.clear(); send(op(0x2c,8,16,1,24));
    while (writes_alu.size()<2) step();
    block_returns=false;
    while (blocked_load_writes==stalls_before_fma && writes_alu.size()<16) step();
    state(0,3,0,256); watch_writer=true; store_captures.clear(); send(store(24),out); drain(); watch_writer=false;
    CHECK(blocked_load_writes>stalls_before_fma && writes_alu.size()==16 && store_captures.size()==16);
    for (unsigned r=0;r<16;++r) CHECK(store_captures[r]>writes_alu[r].first+1);
    // Ordinary multiply uses the same multiplier without changing signed zero.
    stored[src]={0x80000000,0,0x80000000,0}; stored[dst]={0x40000000,0xc0000000,0xc0000000,0x40000000};
    state(0,0,0,16); send(load(8),src); send(load(12),dst); drain();
    for (unsigned rounding : {0u,2u}) {
      state(2,0,0,4); frm=rounding; accrued_flags=0; send(op(36,8,12,1,16)); drain();
      state(0,0,0,16); send(store(16),out); drain();
      CHECK((stored.at(out)==Row{0x80000000,0x80000000,0,0}) && !accrued_flags);
    }
    // Across checked dispatch there is one granted row each cycle, even as the
    // unit switches integer -> fused arithmetic -> add, and owners change.
    stored[src][0]=0x3f800000; stored[dst][0]=0x40000000; stored[out][0]=0x40800000;
    state(0,0,0,16); send(load(8),src); send(load(12),dst); send(load(20),out); drain();
    state(2,0,0,1); reads_alu.clear(); writes_alu.clear();
    send(op(0,8,12,0,16)); send(op(0x2c,8,12,1,20)); send(op(0,8,12,1,24)); drain();
    CHECK(reads_alu.size()==3 && writes_alu.size()==3);
    for (unsigned i=0;i<3;++i) {
      CHECK(reads_alu[i].first==reads_alu[0].first+i && writes_alu[i].second==32+8*i);
    }
    const unsigned expected[]={0x7f800000,0x40c00000,0x40400000};
    for (unsigned i=0;i<3;++i) {
      state(0,0,0,4); send(store(16+4*i),out); drain(); CHECK(stored.at(out)[0]==expected[i]);
    }
    // A dependent successor takes the cursor on the producer's read edge,
    // but it cannot sample the producer's destination until after writeback.
    state(2,0,0,1); reads_alu.clear(); writes_alu.clear();
    send(op(0,8,12,0,16)); send(op(0,16,12,1,24)); drain();
    CHECK(reads_alu.size()==2 && writes_alu.size()==2);
    CHECK(execute_launch_cycle==reads_alu[0].first && reads_alu[1].first>writes_alu[0].first);

    // Pin the drain frontier with an unrelated load. A issues, B takes the
    // cursor but waits on the load, and a store can still capture A's result.
    state(0,0,0,16); reads_alu.clear(); writes_alu.clear(); store_captures.clear();
    watch_writer=block_returns=true;
    const auto pinned=finishes;
    send(load(4),src); while (pending.empty()) step();
    send(op(0,12,0,3,16)); // A: copy v12 to v16
    send(op(0,4,0,3,24));  // B waits for v4
    CHECK(reads_alu.size()==1 && execute_launch_cycle==reads_alu.front().first);
    send(store(16),out);
    for (unsigned i=0;i<16;++i) step();
    CHECK(finishes==pinned && reads_alu.size()==1 && writes_alu.size()==1);
    CHECK(store_captures.size()==1 && store_captures.front()>writes_alu.front().first+1);
    block_returns=false; drain(); watch_writer=false;
    CHECK(stored.at(out)==stored.at(dst));

    // Conversely, a younger writer to A's already captured source need not
    // wait for A's destination write. Both one-row reads must issue consecutively.
    reads_alu.clear(); writes_alu.clear();
    send(op(0,12,0,3,16)); send(op(0,8,0,3,12)); drain();
    CHECK(reads_alu.size()==2 && writes_alu.size()==2);
    CHECK(reads_alu[1].first==reads_alu[0].first+1 && reads_alu[1].first<writes_alu[0].first);

    // A successful read proof never substitutes for failed write permission.
    addresses.clear(); state(0,0,0,16); precheck_safe = 0;
    auto refused = store_offers; previous = narrow;
    send(store(8), 0x7004, false); drain();
    CHECK(addresses.size()==16 && store_offers==refused+16 && narrow==previous);
    // Load old vd into two rows, then hold its second reply. A masked MAC
    // and its store consumer must each use only the row actually written.
    stored[mask]={0xb6db6db6,0,0,0};
    for (unsigned r=0;r<2;++r) {
      stored[src+16*r]={0x8001fc05,0x42a090de,0x1234ba19,0x782df856};
      stored[dst+16*r]={0xc9fe19a7,0x509a2f73,0x845e188a,0x6bb8a095};
      stored[zero+16*r]={0x191e14a9+r,0x399ceafe,0x2776ca57,0xa227fa3c};
    }
    state(0,0,0,32); send(load(0),mask); send(load(8),src); send(load(12),dst); drain();
    const Word rhs=0xf532dc7b;
    for (unsigned sew=0,size=1;size<xlen_bytes;++sew,size*=2)
      for (unsigned fn : {41u,43u,45u,47u}) for (unsigned form : {2u,6u}) {
        state(0,0,0,32); block_returns=true;
        reads_alu.clear(); writes_alu.clear(); load_write_cycles.clear();
        send(load(16),zero); while (pending.size()<2) step();
        state(sew,0,1,32/size-1); send(op(fn,8,12,form,16) & ~(1u<<25),rhs);
        CHECK(reads_alu.empty());
        const auto returned=load_return_cycles.size();
        block_returns=false; while (load_return_cycles.size()==returned) step(); block_returns=true;
        for (unsigned i=0;i<16;++i) step();
        CHECK(load_write_cycles.size()==1 && reads_alu.size()==1 && writes_alu.size()==1);
        CHECK(load_write_cycles[0]<reads_alu[0].first);
        block_returns=false; state(0,0,0,32); send(store(16),out); drain();
        CHECK(load_write_cycles.size()==2 && reads_alu.size()==2 && writes_alu.size()==2);
        CHECK(load_write_cycles[1]<reads_alu[1].first);
        for (unsigned r=0;r<2;++r) {
          Row expected=stored.at(zero+16*r);
          for (unsigned b=0;b<16;b+=size) {
            unsigned i=(16*r+b)/size;
            if (!i || i==32/size-1 || !(stored[mask][0]>>i & 1)) continue;
            Word a=0,k=0,old=0;
            std::memcpy(&a,reinterpret_cast<const char*>(stored.at(src+16*r).data())+b,size);
            std::memcpy(&k,reinterpret_cast<const char*>(stored.at(dst+16*r).data())+b,size);
            std::memcpy(&old,reinterpret_cast<const char*>(expected.data())+b,size);
            if (form==6) k=rhs;
            Word result=fn==41 ? old*k+a : fn==43 ? a-old*k : fn==45 ? a*k+old : old-a*k;
            std::memcpy(reinterpret_cast<char*>(expected.data())+b,&result,size);
          }
          CHECK(stored.at(out+16*r)==expected);
        }
      }
    // Observe simultaneous requests at the physical register view: a .vv
    // MAC and VSSeq share port 3, whereas a .vx MAC leaves it free.
    for (unsigned fn : {45u,63u}) for (unsigned form : {2u,6u}) {
      mac_form=form; contested_reads=0; reads_alu.clear(); store_captures.clear(); watch_writer=true;
      state(0,fn==45 ? 3 : 2,0,fn==45 ? 256 : 128); send(op(fn,8,16,form,24),rhs);
      state(0,0,0,32); send(store(4),out); drain();
      CHECK(contested_reads && reads_alu.size()==16 && store_captures.size()==2);
      mac_form=0; watch_writer=false;
    }
    // A younger add reads the final four rows of a 16-row vmacc.vx. Its
    // descriptor can replace the MAC before those products are writable.
    state(0,3,0,256);
    send(op(23,0,1,4,8),5); send(op(23,0,1,4,16),7); drain();
    reads_alu.clear(); writes_alu.clear();
    send(op(45,8,1,6,16),3);
    state(0,1,0,64); send(op(0,22,1,3,24)); drain();
    CHECK(reads_alu.size()==20 && writes_alu.size()==20);
    for (unsigned i=0;i<16;++i) {
      CHECK(reads_alu[i]==Beat(reads_alu[0].first+i,16+i));
      CHECK(writes_alu[i]==Beat(writes_alu[0].first+i,32+i));
    }
    for (unsigned i=0;i<4;++i) {
      CHECK(reads_alu[16+i].second==44+i);
      CHECK(reads_alu[16+i].first>writes_alu[12+i].first);
    }
    CHECK(reads_alu[16].first<writes_alu[15].first);
    send(store(24),out); drain();
    for (unsigned i=0;i<4;++i) CHECK((stored.at(out+16*i)==Row{0x17171717,0x17171717,0x17171717,0x17171717}));
    // The copy and the older MAC share the fixed write calendar. Each
    // physical write cycle has one owner despite their different latencies.
    state(0,0,0,32); send(op(23,0,1,4,5),0x5a); drain();
    state(0,3,0,256); writes_alu.clear();
    send(op(45,8,1,6,16),3);
    state(0,0,0,16); send(op(39,5,0,3,4)); drain();
    CHECK(writes_alu.size()==18);
    for(unsigned i=1;i<18;++i) CHECK(writes_alu[i].first>writes_alu[i-1].first);
    send(store(4),out); drain();
    CHECK((stored.at(out)==Row{0x5a5a5a5a,0x5a5a5a5a,0x5a5a5a5a,0x5a5a5a5a}));
    // All widening product encodings, including signed vs2 x unsigned rhs.
    // Restart inside the upper half; compare prestart, masked and tail bytes
    // against the original destination, not just the active products.
    stored[mask]={0x6d9b3e57,0,0,0};
    for (unsigned r=0;r<2;++r) {
      stored[src+16*r]={0x81abfe91+r,0x20469acd,0xa73186f2,0xe19fd3e8+r};
      stored[dst+16*r]={0xf2a5ffbc,0x904389e1+r,0xd5e38297,0x2931d3bc};
    }
    state(0,0,0,32); send(load(0),mask); send(load(8),src); send(load(12),dst); drain();
    constexpr Word accumulator=0xdd00;
    for (unsigned r=0;r<4;++r)
      stored[accumulator+16*r]={0xfff0ffe1+r,0x23458765+r,0x77f00f03-r,0xfefe8000-r};
    for (unsigned size=1,sew=0;size<xlen_bytes;size*=2,++sew)
      for (unsigned fn : {56u,58u,59u,60u,61u,62u,63u}) for (unsigned form : {2u,6u}) {
        if (fn==62 && form==2) continue; // vwmaccus is scalar-vector only.
        const unsigned first=8/size+1, end=32/size-1;
        state(0,1,0,64); send(load(16),accumulator); drain();
        reads_alu.clear(); writes_alu.clear(); state(sew,0,first,end);
        send(op(fn,8,12,form,16) & ~(1u<<25),rhs); drain();
        const unsigned first_row=first*2*size/16, last_row=(end-1)*2*size/16;
        CHECK(reads_alu.size()==last_row-first_row+1 && writes_alu.size()==reads_alu.size());
        for (unsigned r=first_row;r<=last_row;++r) {
          CHECK(reads_alu[r-first_row].second==16+r/2);
          CHECK(writes_alu[r-first_row]==Beat(reads_alu[r-first_row].first+6,32+r));
        }
        state(0,1,0,64); send(store(16),out); drain();
        const Word low=(Word(1)<<(8*size))-1, sign=(low+1)>>1;
        for (unsigned r=0;r<4;++r) {
          Row expected=stored.at(accumulator+16*r);
          for (unsigned off=0;off<16;off+=2*size) {
            unsigned i=(16*r+off)/(2*size);
            if (i<first || i>=end || !(stored[mask][0]>>i & 1)) continue;
            Word a=0,b=0;
            unsigned addr=i*size;
            std::memcpy(&a,reinterpret_cast<const char*>(stored.at(src+(addr&~15u)).data())+addr%16,size);
            std::memcpy(&b,reinterpret_cast<const char*>(stored.at(dst+(addr&~15u)).data())+addr%16,size);
            if (form==6) b=rhs & low;
            if ((fn==58 || fn==59 || fn==61 || fn==62) && (a&sign)) a|=~low;
            if ((fn==59 || fn==61 || fn==63) && (b&sign)) b|=~low;
            Word old=0;
            if (fn>=60) std::memcpy(&old,reinterpret_cast<const char*>(expected.data())+off,2*size);
            Word product=a*b+old;
            std::memcpy(reinterpret_cast<char*>(expected.data())+off,&product,2*size);
          }
          CHECK(stored.at(out+16*r)==expected);
        }
      }
    // A younger load of *either* narrow source may fetch early, but cannot
    // overwrite row six until its second use. A store chains off the first
    // widened row before the last product writes. Check both ends' data.
    for (unsigned replace : {11u,15u}) {
      state(0,2,0,128); send(op(23,0,1,4,8),2); send(op(23,0,1,4,12),3); drain();
      reads_alu.clear(); writes_alu.clear(); load_write_cycles.clear(); load_return_cycles.clear();
      store_captures.clear(); watch_writer=true; latency=2; requests_always_ready=true;
      send(op(56,8,12,2,16));
      state(0,0,0,16); send(load(replace),src); send(store(16),out); drain(); watch_writer=false;
      CHECK(reads_alu.size()==16 && writes_alu.size()==16 && load_write_cycles.size()==1);
      CHECK(load_return_cycles[0]<reads_alu[13].first && load_write_cycles[0]>reads_alu[13].first);
      CHECK(store_captures.size()==1 && store_captures[0]>writes_alu[0].first && store_captures[0]<writes_alu.back().first);
      CHECK((stored.at(out)==Row{0x60006,0x60006,0x60006,0x60006}));
      state(0,0,0,32); send(store(22),out); drain();
      CHECK((stored.at(out+16)==Row{0x60006,0x60006,0x60006,0x60006}));
    }
    // Chain a widening MAC between a load and store. Withhold the second
    // accumulator row: exactly one product may book, write and reach VSSeq.
    state(0,0,0,32); send(op(23,0,1,4,8),3); drain();
    reads_alu.clear(); writes_alu.clear(); load_write_cycles.clear();
    block_returns=true; send(load(16),accumulator); while (pending.size()<2) step();
    state(0,0,0,16); send(op(60,8,1,6,16),7);
    state(0,0,0,32); watch_writer=true; store_captures.clear(); send(store(16),out);
    for (unsigned t=0;t<8;++t) step();
    CHECK(reads_alu.empty() && store_captures.empty());
    const auto before_acc=load_return_cycles.size();
    block_returns=false; while (load_return_cycles.size()==before_acc) step(); block_returns=true;
    for (unsigned t=0;t<18;++t) step();
    CHECK(load_write_cycles.size()==1 && reads_alu.size()==1 && writes_alu.size()==1 && store_captures.size()==1);
    block_returns=false; drain(); watch_writer=false;
    CHECK(load_write_cycles.size()==2 && reads_alu.size()==2 && writes_alu.size()==2 && store_captures.size()==2);
    for (unsigned r=0;r<2;++r) {
      CHECK(load_write_cycles[r]<reads_alu[r].first && writes_alu[r]==Beat(reads_alu[r].first+6,32+r));
      CHECK(store_captures[r]>writes_alu[r].first);
      Row expected=stored.at(accumulator+16*r);
      for (auto &w:expected) w=((w+21)&0xffff) | (((w>>16)+21)<<16);
      CHECK(stored.at(out+16*r)==expected);
    }
    // v17 is both the narrow factor and the high half of the v16:v17
    // accumulator. Compare with the original physical rows before any writes.
    state(0,1,0,64); send(load(16),accumulator); drain();
    reads_alu.clear(); writes_alu.clear(); state(0,0,0,32);
    send(op(60,17,1,6,16),3); drain();
    CHECK(reads_alu.size()==4 && writes_alu.size()==4);
    state(0,1,0,64); send(store(16),out); drain();
    for (unsigned r=0;r<4;++r) {
      CHECK(reads_alu[r].second==34+r/2 && writes_alu[r].second==32+r);
      Row expected=stored.at(accumulator+16*r);
      const Row &factor=stored.at(accumulator+32+16*(r/2));
      for (unsigned j=0;j<8;++j) {
        auto p=reinterpret_cast<unsigned char*>(expected.data())+2*j;
        std::uint16_t old; std::memcpy(&old,p,2);
        old+=3*reinterpret_cast<const unsigned char*>(factor.data())[8*(r%2)+j];
        std::memcpy(p,&old,2);
      }
      CHECK(stored.at(out+16*r)==expected);
    }
    // Positive and negative ties distinguish all four vxrm modes. Read back
    // actual VRF bytes for both operand forms and every native element width.
    constexpr int rounded[4][4]={{1,2,0,-1},{0,2,0,-2},{0,1,-1,-2},{1,1,-1,-1}};
    for (unsigned size=1,sew=0;size<xlen_bytes;size*=2,++sew) {
      Word half=Word(1)<<(8*size-2);
      for (unsigned r=0;r<2;++r) for (unsigned b=0;b<16;b+=size) {
        const int input[]={1,3,-1,-3};
        Word a=input[(b/size)%4];
        std::memcpy(reinterpret_cast<char*>(stored[src+16*r].data())+b,&a,size);
        std::memcpy(reinterpret_cast<char*>(stored[dst+16*r].data())+b,&half,size);
      }
      state(0,0,0,32); send(load(8),src); send(load(12),dst); drain();
      for (unsigned rm=0;rm<4;++rm) for (unsigned form : {0u,4u}) {
        vxrm=rm; state(sew,0,0,32/size); reads_alu.clear(); writes_alu.clear(); saturation_cycles.clear();
        send(op(39,8,12,form,16),half);
        vxrm=(rm+1)%4; // The issued descriptor, not this live input, owns rounding.
        drain();
        CHECK(saturation_cycles.empty() && reads_alu.size()==2 && writes_alu.size()==2);
        CHECK(reads_alu[1].first==reads_alu[0].first+1 && writes_alu[1].first==writes_alu[0].first+1);
        state(0,0,0,32); send(store(16),out); drain();
        for (unsigned r=0;r<2;++r) for (unsigned b=0;b<16;b+=size) {
          Word expected=rounded[rm][(b/size)%4];
          CHECK(!std::memcmp(reinterpret_cast<const char*>(stored.at(out+16*r).data())+b,&expected,size));
        }
      }
    }
    // Saturation in prestart, predicate holes or tail alone has no effect;
    // enabling a single saturating byte on row 1 pulses only on its VRF write.
    stored[mask]={0xfffffffd,0,0,0}; stored[src].fill(0); stored[src+16].fill(0);
    stored[src][0]=0x8080; stored[src+16][3]=0x80000000;
    state(0,0,0,32); send(load(0),mask); drain();
    for (bool enable : {false,true}) {
      if (enable) stored[src+16][0]=0x80;
      state(0,0,0,32); send(load(8),src); drain();
      state(0,0,1,31); writes_alu.clear(); saturation_cycles.clear();
      send(op(39,8,1,4,16) & ~(1u<<25),0x80); drain();
      CHECK(writes_alu.size()==2 && saturation_cycles.size()==unsigned(enable));
      if (enable) CHECK(saturation_cycles.front()==writes_alu[1].first);
      state(0,0,0,32); send(store(16),out); drain();
      CHECK((stored.at(out+16)[0]&255)==(enable ? 127 : 0));
    }
    // A younger VXSeq add reads B. Giving VLSeq the first interleaved beat
    // writes both low halves, but must not wake the add until B's high half lands.
    state(0,0,0,16); writes.clear(); load_write_cycles.clear(); reads_alu.clear();
    block_returns=true; send(load(16)|(1u<<29),src);
    while(pending.size()<2) step();
    send(op(0,17,1,3,24));
    const auto before_half=load_return_cycles.size();
    block_returns=false; while(load_return_cycles.size()==before_half) step(); block_returns=true;
    for(unsigned t=0;t<15;++t) step();
    CHECK((writes==std::vector<Word>{32,34}) && reads_alu.empty());
    block_returns=false; drain();
    CHECK((writes==std::vector<Word>{32,34,32,34}) && reads_alu.size()==1);
    CHECK(reads_alu.front().first>load_write_cycles.back() && reads_alu.front().second==34);
    // Check each field against an independent byte oracle before re-interleaving.
    for (unsigned fields=2; fields<=8; ++fields) {
      const unsigned lmul=fields<=4 ? 1 : 0, spacing=1u<<lmul, field_bytes=32*spacing;
      for (unsigned eew=0,size=1;size<=xlen_bytes;size*=2,++eew) {
        cycle_limit=cycles+1400;
        // Exercise fractional EMUL as well: EEW8 at SEW16 and LMUL1.
        const unsigned sew=(fields==7 && eew==0) ? 1 : eew;
        const unsigned elements=(field_bytes/size)>>(sew-eew), limit=elements-1;
        const unsigned width=eew ? 4+eew : 0, seg=((fields-1u)<<29)|(width<<12);
        const Word base=0xe000+size, dest=0xd000+size;
        const Word pred=UINT64_C(0xb434ff000f3c6ab3);
        stored[mask]={std::uint32_t(pred),std::uint32_t(pred>>32),0,0};
        for(unsigned r=0;r<1+fields*field_bytes/16;++r) {
          stored[0xe000+16*r]={0x310203a4+r,0x50607892+r,0xcc0dfede - r,0x11223344+r};
          stored[0xd000+16*r].fill(0x5b5b5b5b);
        }
        state(0,0,0,32); send(load(0),mask); drain();
        state(0,lmul,0,field_bytes);
        for(unsigned f=0;f<fields;++f) send(op(23,0,1,4,16+spacing*f),0x39);
        drain();
        const auto fallback_before=narrow;
        auto check_requests=[&](Word start) {
          std::map<Word,unsigned> expected;
          for(unsigned i=1;i<limit;++i) if((pred>>i)&1)
            for(unsigned b=0;b<fields*size;++b) {
              const Word va=start+i*fields*size+b;
              expected[va&~Word(15)] |= 1u<<(va%16);
            }
          CHECK(expected.size()==addresses.size() && expected.size()==masks.size());
          unsigned j=0;
          for(auto [address,mask]:expected) { CHECK(addresses[j]==address && masks[j]==mask); ++j; }
        };
        state(sew,lmul,1,limit); addresses.clear(); masks.clear();
        send((load(16)|seg)&~(1u<<25),base); drain(); check_requests(base);
        CHECK(!addresses.empty() && narrow==fallback_before);
        auto byte_at=[&](Word addr) {
          return reinterpret_cast<const unsigned char*>(stored.at(addr&~Word(15)).data())[addr%16];
        };
        state(0,lmul,0,field_bytes);
        for(unsigned f=0;f<fields;++f) {
          send(store(16+spacing*f),out); drain();
          for(unsigned b=0;b<field_bytes;++b) {
            unsigned i=b/size;
            bool enabled=i>=1 && i<limit && ((pred>>i)&1);
            CHECK(byte_at(out+b)==(enabled ? byte_at(base+(fields*i+f)*size+b%size) : 0x39));
          }
        }
        state(sew,lmul,1,limit); addresses.clear(); masks.clear(); hold_until=cycles+96;
        send((store(16)|seg)&~(1u<<25),dest);
        while(!stalled && cycles<hold_until) step();
        CHECK(stalled && addresses.empty()); drain(); check_requests(dest);
        CHECK(!addresses.empty() && narrow==fallback_before);
        for(unsigned b=0;b<fields*field_bytes+16;++b) {
          const int relative=int(b)-int(size);
          const unsigned i=relative<0 ? elements : unsigned(relative)/(fields*size);
          bool enabled=i>=1 && i<limit && ((pred>>i)&1);
          CHECK(byte_at(0xd000+b)==(enabled ? byte_at(base+relative) : 0x5b));
        }
      }
    }
    // Actual masked VLSE/VSSE with rs2!=x0 (fixture stride=8). A younger
    // add must see each completed row, never one of its individual replies.
    cycle_limit=cycles+1000;
    constexpr unsigned strided_encoding=(2u<<26)|(1u<<20);
    state(0,0,0,32); stored[mask]={0x00050003,0,0,0};
    send(load(0),mask); drain();
    reads_alu.clear(); load_write_cycles.clear(); addresses.clear(); masks.clear();
    previous=narrow; block_returns=true;
    send((load(16)|strided_encoding)&~(1u<<25),src);
    send(op(0,16,1,3,24)); while(pending.size()<2) step();
    CHECK(reads_alu.empty()); block_returns=false;
    while(load_write_cycles.empty()) step();
    block_returns=true; for(unsigned t=0;t<12;++t) step();
    CHECK(load_write_cycles.size()==1 && reads_alu.size()==1 && reads_alu[0].first>load_write_cycles[0]);
    block_returns=false; drain();
    CHECK(reads_alu.size()==2 && load_write_cycles.size()==2 && reads_alu[1].first>load_write_cycles[1]);
    CHECK((addresses==std::vector<Word>{src,src,src+128,src+144}));
    CHECK((masks==std::vector<unsigned>{1,256,1,1}) && narrow==previous);
    addresses.clear(); masks.clear();
    send((store(24)|strided_encoding)&~(1u<<25),out); drain();
    CHECK((addresses==std::vector<Word>{out,out,out+128,out+144}));
    CHECK((masks==std::vector<unsigned>{1,256,1,1}) && narrow==previous);
    for(unsigned i:{0u,1u,16u,18u}) {
      const unsigned lane=(i*8)%16;
      auto at=[&](Word a){return reinterpret_cast<const unsigned char*>(stored.at(a+(i*8-lane)).data())[lane];};
      CHECK(at(out)==((at(src)+1)&255));
    }
    // With the first ordered cache reply held, the special sequencer can
    // still capture both index rows. Overwrite both in the VRF and check
    // that subsequent requests use the original buffered indices.
    constexpr Word index_src=0x9300, data_src=0x9400, gather_src=0x9600, scatter_dst=0x9800;
    constexpr unsigned indexed=(3u<<26)|(4u<<20);
    state(0,0,0,32);
    for(unsigned i=0;i<32;++i) {
      reinterpret_cast<unsigned char*>(stored[index_src+(i&~15u)].data())[i%16]=i==1?3:0x85;
      reinterpret_cast<unsigned char*>(stored[data_src+(i&~15u)].data())[i%16]=0x40+i;
    }
    stored[gather_src+0x80]={0x11111111,0xb2b2b2b2,0,0};
    stored[gather_src]={0x99999999,0,0,0};
    send(load(4),index_src); send(load(8),data_src); drain();
    send(op(23,0,1,4,16),0x35); drain();
    // Identical masked ei8 instructions, differing only in WB proof. Each
    // original index row transfers once to precisely one consumer, even when
    // a duplicate-address store is waiting for the first acknowledgement.
    cycle_limit+=800;
    const auto saved_safe=precheck_safe;
    for(bool scatter : {false,true}) for(bool fast : {true,false}) {
      const Word base=scatter?scatter_dst:gather_src;
      const auto insn=((scatter?store(8):load(16))|indexed)&~(1u<<25);
      addresses.clear(); masks.clear(); exact_probes.clear(); direct_index_rows=checked_index_rows=0;
      precheck_safe=0; block_returns=true;
      send(insn,base,fast);
      while(pending.empty()) step();
      for(unsigned i=0;i<8;++i) step();
      CHECK(addresses.size()==1 && pending.size()==1);
      block_returns=false; drain();
      CHECK(direct_index_rows==(fast?2u:0u) && checked_index_rows==(fast?0u:2u));
      CHECK((addresses==std::vector<Word>{base+128,base,base+128,base+128}));
      CHECK((masks==std::vector<unsigned>{32,8,32,32}));
      CHECK(exact_probes.size()==(fast?0u:4u));
    }
    precheck_safe=saved_safe;
    // v0 still enables elements 0,1,16,18.
    writes_alu.clear(); addresses.clear(); masks.clear(); block_returns=true; previous=narrow;
    send((load(16)|indexed)&~(1u<<25),gather_src);
    send(op(11,4,4,0,4)); // vxor.vv v4,v4,v4
    for(unsigned t=0;t<24;++t) step();
    CHECK(addresses.size()==1 && pending.size()==1);
    CHECK(writes_alu.size()==2 && writes_alu[0].second==8 && writes_alu[1].second==9);
    block_returns=false; drain();
    CHECK(writes_alu.size()==2 && writes_alu[1].second==9 && narrow==previous);
    CHECK((addresses==std::vector<Word>{gather_src+128,gather_src,gather_src+128,gather_src+128}));
    send(store(16),out); drain();
    for(unsigned i=0;i<32;++i) {
      unsigned expected=i==1 ? 0x99 : (i==0 || i==16 || i==18) ? 0xb2 : 0x35;
      CHECK(reinterpret_cast<const unsigned char*>(stored[out+(i&~15u)].data())[i%16]==expected);
    }
    // Replace the index group, then scatter distinct source values onto
    // duplicate addresses. Withhold the first ACK before allowing any later store.
    send(load(4),index_src); drain(); addresses.clear(); masks.clear(); block_returns=true;
    send((store(8)|indexed)&~(1u<<25),scatter_dst);
    for(unsigned t=0;t<24;++t) step();
    CHECK(addresses.size()==1 && pending.size()==1);
    block_returns=false; drain();
    CHECK((addresses==std::vector<Word>{scatter_dst+128,scatter_dst,scatter_dst+128,scatter_dst+128}));
    CHECK((masks==std::vector<unsigned>{32,8,32,32}) && narrow==previous);
    CHECK(reinterpret_cast<const unsigned char*>(stored.at(scatter_dst+128).data())[5]==0x52);
    CHECK(reinterpret_cast<const unsigned char*>(stored.at(scatter_dst).data())[3]==0x41);
    // Two unordered gather requests in flight are not a ready destination.
    // The younger add may read each row only after its merged physical write.
    reads_alu.clear(); load_write_cycles.clear(); block_returns=true;
    const unsigned unordered_index=(1u<<26)|(4u<<20);
    send((load(16)|unordered_index)&~(1u<<25),gather_src);
    send(op(0,16,1,3,24)); while(pending.size()<2) step();
    for(unsigned t=0;t<8;++t) step();
    CHECK(load_write_cycles.empty() && reads_alu.empty() && pending.size()==2);
    block_returns=false; drain();
    CHECK(load_write_cycles.size()==2 && reads_alu.size()==2 && narrow==previous);
    for(unsigned r=0;r<2;++r) CHECK(reads_alu[r].second==32+r && reads_alu[r].first>load_write_cycles[r]);
    // Every active wide element names the same split address. Stop after
    // just the prefix returns: neither the write nor its dependent may fire.
    for(unsigned e=1;e<=(xlen_bytes==4?2u:3u);++e) {
      cycle_limit=cycles+600;
      const unsigned size=1u<<e;
      state(0,0,0,32); stored[mask]={5,0,0,0}; send(load(0),mask);
      send(op(23,0,1,4,4),15); send(op(23,0,1,4,16),0x35); drain();
      state(e,0,0,32/size);
      stored[gather_src]={0x15141312,0x25242322,0x35343332,0x45444342};
      stored[gather_src+16]={0x55545352,0x65646362,0x75747372,0x85848382};
      addresses.clear(); masks.clear(); reads_alu.clear(); load_return_cycles.clear(); load_write_cycles.clear();
      block_returns=true;
      send((load(16)|unordered_index)&~(1u<<25),gather_src);
      send(op(0,16,1,3,24)); while(pending.size()<2) step();
      block_returns=false; while(load_return_cycles.empty()) step(); block_returns=true;
      for(unsigned t=0;t<8;++t) step();
      CHECK(load_write_cycles.empty() && reads_alu.empty());
      block_returns=false; drain();
      const std::vector<Word> beats{gather_src,gather_src+16,gather_src,gather_src+16};
      unsigned suffix=(1u<<(size-1))-1;
      CHECK(addresses==beats && (masks==std::vector<unsigned>{0x8000,suffix,0x8000,suffix}));
      state(0,0,0,32); send(store(16),out); drain();
      auto mem_byte=[&](Word a) {return reinterpret_cast<const unsigned char*>(stored.at(a&~Word(15)).data())[a%16];};
      for(unsigned b=0;b<32;++b)
        CHECK(mem_byte(out+b)==((b/size==0 || b/size==2)?mem_byte(gather_src+15+b%size):0x35));
      state(e,0,0,32/size); addresses.clear(); masks.clear(); block_returns=true;
      send((store(8)|indexed)&~(1u<<25),scatter_dst);
      while(pending.empty()) step();
      for(unsigned t=0;t<8;++t) step();
      CHECK(pending.size()==1 && addresses.size()==1); block_returns=false; drain();
      CHECK((addresses==std::vector<Word>{scatter_dst,scatter_dst+16,scatter_dst,scatter_dst+16}));
      CHECK((masks==std::vector<unsigned>{0x8000,suffix,0x8000,suffix}) && narrow==previous);
      for(unsigned b=0;b<size;++b) CHECK(mem_byte(scatter_dst+15+b)==0x40+2*size+b);
    }
    // Without the one-page proof a crossing store retains precise vstart/tval.
    state(0,0,0,2); addresses.clear(); precise_stores.clear(); refuse_page=true;
    const unsigned prior_faults=faults;
    precheck_safe=0; previous=narrow; send(store(16)|strided_encoding,0xeff8,false); drain();
    CHECK(addresses==std::vector<Word>{0xeff0} && narrow==previous && faults==prior_faults+1 && last_fault==0xf000 && last_vstart==1);
    CHECK(precise_stores.empty()); refuse_page=false;
    cycle_limit=cycles+1800;
    state(0,0,last_vstart,2); addresses.clear(); exact_probes.clear();
    send(store(16)|strided_encoding,0xeff8,false); drain();
    CHECK(addresses==std::vector<Word>{0xf000} && exact_probes==addresses && faults==prior_faults+1);

    // Skip both the completed prefix and masked-off elements before translation.
    state(0,0,0,16); stored[mask]=Row{0x1a,0,0,0}; send(load(0),mask); send(load(8),data_src); drain();
    stride=-0x1000; state(0,0,2,5);
    for(bool writing:{false,true}) {
      exact_probes.clear(); addresses.clear();
      send(((writing?store(8):load(16))|strided_encoding)&~(1u<<25),0xf020,false); drain();
      CHECK((exact_probes==std::vector<Word>{0xc020,0xb020}) && addresses==exact_probes);
    }

    // At EEW16 an encoded x0 reads once, but a register containing zero
    // reads once per enabled element. Both splat the same merged halfword.
    stored[0x7310]=Row{0xa1b2c3d4,0,0,0}; stride=0;
    for(bool encoded:{false,true}) {
      state(0,0,0,16); send(op(23,0,1,4,16),0x35); drain();
      state(0,0,1,5); addresses.clear(); exact_probes.clear(); masks.clear();
      const unsigned addr_mode=(2u<<26)|(encoded?0u:1u<<20)|(5u<<12);
      send((load(16)|addr_mode)&~(1u<<25),0x7311,false); drain();
      CHECK(addresses==std::vector<Word>(encoded?1:3,0x7310) && exact_probes==std::vector<Word>(encoded?1:3,0x7311));
      CHECK(masks==std::vector<unsigned>(addresses.size(),6));
      state(0,0,0,16); send(store(16),out); drain();
      auto* dst_bytes=reinterpret_cast<const unsigned char*>(stored.at(out).data());
      for(unsigned b=0;b<16;++b) CHECK(dst_bytes[b]==((0x1au>>(b/2))&1 ? (b%2?0xb2:0xc3) : 0x35));
    }
    state(0,0,1,5); addresses.clear(); masks.clear();
    send((store(8)|strided_encoding)&~(1u<<25),0x7513,false); drain();
    CHECK(addresses==std::vector<Word>(3,0x7510) && masks==std::vector<unsigned>(3,8));
    CHECK(reinterpret_cast<const unsigned char*>(stored.at(0x7510).data())[3]==0x44);
    // A retained positive range proof supplies the PPN and is released once,
    // even though the x0 macro cannot go through ordinary bulk issue.
    precheck_safe=1; window_pages[0]=0x87; stored[0x87000]=initial;
    const unsigned before_release=releases;
    addresses.clear(); exact_probes.clear();
    send((load(16)|(2u<<26))&~(1u<<25),0x7000,false); drain();
    CHECK(addresses==std::vector<Word>{0x87000} && exact_probes.empty() && releases==before_release+1);
    precheck_safe=0;
    state(0,0,2,3); exact_probes.clear(); addresses.clear();
    send((load(16)|(2u<<26))&~(1u<<25),0xf000,false); drain();
    CHECK(exact_probes.empty() && addresses.empty());

    // Unsigned byte offsets cross a page and alias. A delayed first ACK
    // must exclude the second ordered store, not just delay retirement.
    state(0,0,0,16); stored[index_src]=Row{0xfefe03fe,0xfe,0,0}; send(load(4),index_src); drain();
    state(0,0,1,5); block_returns=true; addresses.clear(); masks.clear();
    send((store(8)|indexed)&~(1u<<25),0xaff1,false);
    while(pending.empty()) step();
    for(unsigned t=0;t<20;++t) step();
    CHECK(addresses==std::vector<Word>{0xaff0} && pending.size()==1 && stores_pending);
    block_returns=false; drain();
    CHECK((addresses==std::vector<Word>{0xaff0,0xb0e0,0xb0e0}) && (masks==std::vector<unsigned>{16,0x8000,0x8000}));
    CHECK(reinterpret_cast<const unsigned char*>(stored.at(0xb0e0).data())[15]==0x44);
    exact_probes.clear(); addresses.clear();
    send((load(16)|unordered_index)&~(1u<<25),0xaff1,false); drain();
    CHECK((exact_probes==std::vector<Word>{0xaff4,0xb0ef,0xb0ef}) && (addresses==std::vector<Word>{0xaff0,0xb0e0,0xb0e0}));
    state(0,0,0,16); send(store(16),out); drain();
    auto* gathered=reinterpret_cast<const unsigned char*>(stored.at(out).data());
    CHECK(gathered[1]==0x41 && gathered[3]==0x44 && gathered[4]==0x44 && narrow==previous);
    // Element 3 faults after element 1's store. Do not report it while
    // the prefix ACK is stalled; restarting must not reissue element 1.
    state(0,0,1,5); refuse_page=true; block_returns=true; exact_probes.clear(); addresses.clear();
    const unsigned scatter_faults=faults;
    send((store(8)|indexed)&~(1u<<25),0xeff1,false);
    while(exact_probes.size()!=2) step();
    CHECK((exact_probes==std::vector<Word>{0xeff4,0xf0ef}) && addresses==std::vector<Word>{0xeff0});
    for(unsigned t=0;t<8;++t) step();
    CHECK(faults==scatter_faults && stores_pending);
    block_returns=false; drain();
    CHECK(faults==scatter_faults+1 && last_vstart==3 && last_fault==0xf0ef && last_cause==15);
    refuse_page=false; state(0,0,last_vstart,5); addresses.clear();
    send((store(8)|indexed)&~(1u<<25),0xeff1,false); drain();
    CHECK(addresses==std::vector<Word>(2,0xf0e0) && reinterpret_cast<const unsigned char*>(stored.at(0xf0e0).data())[15]==0x44 && narrow==previous);
    // Each checked field is a separate row-port access. Restart is in
    // segments, so reexecute field zero of the faulting segment, not segment zero.
    constexpr unsigned seg2=1u<<29;
    cycle_limit=cycles+2000; state(0,0,0,2);
    const auto before_segments=narrow;
    refuse_page=true; exact_probes.clear(); addresses.clear(); masks.clear();
    send(store(8)|seg2,0xeffd,false);
    while(addresses.size()!=3) step();
    block_returns=true;
    while(exact_probes.size()!=4) step();
    CHECK((exact_probes==std::vector<Word>{0xeffd,0xeffe,0xefff,0xf000}));
    const auto waiting_faults=faults;
    for(unsigned t=0;t<12;++t) step();
    CHECK(stores_pending && faults==waiting_faults && pending.size()==1);
    block_returns=false; drain();
    CHECK(faults==waiting_faults+1 && last_fault==0xf000 && last_vstart==1 && last_cause==15);
    CHECK((masks==std::vector<unsigned>{0x2000,0x4000,0x8000}));
    state(0,0,last_vstart,2); refuse_page=false; exact_probes.clear(); addresses.clear();
    send(store(8)|seg2,0xeffd,false); drain();
    CHECK((exact_probes==std::vector<Word>{0xefff,0xf000}) && (addresses==std::vector<Word>{0xeff0,0xf000}));

    // Field zero is device memory; field one faults. Neither read nor write
    // may touch the device before the segment's remaining translations succeed.
    for(bool writing:{false,true}) {
      state(0,0,1,2); refuse_page=segment_device=true;
      auto prior=faults; addresses.clear(); exact_probes.clear();
      auto ins=(writing?store(8):load(16))|seg2;
      send(ins,0xeffd,false); drain();
      CHECK((exact_probes==std::vector<Word>{0xefff,0xf000}) && addresses.empty());
      CHECK(faults==prior+1 && last_fault==0xf000 && last_vstart==1 && last_cause==(writing?15u:13u));
      refuse_page=false; state(0,0,last_vstart,2); exact_probes.clear(); services.clear();
      remap_exact=true;
      hold_until=cycle_limit;
      send(ins,0xeffd,false);
      while(!stalled) step();
      CHECK((exact_probes==std::vector<Word>{0xefff,0xf000}) && addresses.empty());
      // Both pages must already be pinned. Change the live reply while the
      // device field is held, then check both nonadjacent physical addresses.
      exact_mapping.ppage=0xbad;
      for(unsigned t=0;t<5;++t) step();
      CHECK(stalled && addresses.empty());
      hold_until=0; drain();
      CHECK((addresses==std::vector<Word>{(mapped_page(0xefff)<<12)|0xff0,mapped_page(0xf000)<<12}));
      CHECK(exact_probes.size()==2 && faults==prior+1);
      CHECK((services==std::vector<unsigned>{2,0}));
      remap_exact=false;
      segment_device=false;
    }
    // Mask is per segment. Segment one crosses onto the denied page but
    // does not probe either field. Segment zero is the only pair transferred.
    state(0,0,0,1); stored[mask]=Row{1,0,0,0}; send(load(0),mask); drain();
    state(0,0,0,2); refuse_page=true; addresses.clear(); exact_probes.clear();
    auto masked_faults=faults;
    send((load(16)|seg2)&~(1u<<25),0xeffd,false); drain();
    CHECK((exact_probes==std::vector<Word>{0xeffd,0xeffe}) && addresses.size()==2 && faults==masked_faults);
    refuse_page=false;

    // Place the denied byte at the same field of segment 0 or 3. Neither
    // masked-off elements nor a nonzero restart renumber the faulting element.
    for (unsigned fields : {1u,3u,8u}) for (unsigned scenario=0;scenario<7;++scenario) {
      cycle_limit=cycles+1000;
      const bool masked=scenario==2 || scenario==4;
      const bool empty=scenario==5, success=scenario>=4;
      const unsigned stop=scenario==0?0:3, start=scenario==3?stop:empty?6:0;
      const unsigned bad_field=fields/2, length=5;
      const Word base=scenario==6?0xd010:0xf000-stop*fields-bad_field;
      if (masked) {
        state(0,0,0,1); stored[mask]=Row{scenario==2?1u<<stop:0u,0,0,0}; send(load(0),mask); drain();
      }
      state(0,0,start,length); refuse_page=true;
      exact_probes.clear(); writes.clear(); addresses.clear(); outcomes.clear(); truncations.clear();
      const auto prior_faults=faults, prior_done=finishes, prior_narrow=narrow;
      unsigned ins=load(16)|(0x10u<<20)|((fields-1)<<29);
      send(masked?ins&~(1u<<25):ins,base,false); drain();
      std::vector<Word> expected_probes, expected_writes;
      for(unsigned i=start;i<length;++i) {
        if(masked && (scenario==4 || i!=stop)) continue;
        for(unsigned f=0;f<fields;++f) {
          const auto addr=base+i*fields+f;
          expected_probes.push_back(addr);
          if(addr==0xf000) break;
          expected_writes.push_back(2*(16+f));
        }
        if(!success && i==stop) break;
      }
      CHECK(exact_probes==expected_probes && writes==expected_writes && addresses.size()==writes.size() && narrow==prior_narrow);
      const bool trap=!success && !stop, shortened=!success && stop;
      CHECK(outcomes==std::vector<Word>{trap?2u:shortened?3u:0u} && finishes==prior_done+!trap);
      CHECK(faults==prior_faults+trap && (!trap || (last_fault==0xf000 && last_cause==13 && last_vstart==0)));
      CHECK(truncations==(shortened?std::vector<Word>{stop}:std::vector<Word>{}));
      refuse_page=false;
    }

    // Use the real VRF for indices and destination. A restart must work from
    // the partially overwritten group, without reseeding it or retaining VPS state.
    for(unsigned shape=0;shape<3;++shape) for(unsigned mop : {1u,3u}) {
      cycle_limit=cycles+2600;
      const unsigned sew=shape==2, ie=shape==1, size=1u<<sew, idx_size=1u<<ie;
      const unsigned vd=16, vs2=vd+(shape==2), lmul=shape==2;
      constexpr unsigned count=28, bad=23;
      constexpr Word seed=0x8200, base=0xef80;
      auto byte=[&](Word a)->unsigned char& {return reinterpret_cast<unsigned char*>(stored[a&~Word(15)].data())[a%16];};
      std::vector<unsigned char> image(64,0x75);
      std::vector<Word> offsets(count);
      for(unsigned i=0;i<count;++i) {
        offsets[i]=i==bad?0xa0:((i*13)%49)*2;
        for(unsigned j=0;j<idx_size;++j) image[(vs2-vd)*32+i*idx_size+j]=offsets[i]>>(8*j);
        for(unsigned j=0;j<size;++j) byte(base+offsets[i]+j)=0xb0+(i%16)+j;
      }
      for(unsigned b=0;b<64;++b) byte(seed+b)=image[b];
      state(0,1,0,64); send(load(vd),seed); drain();
      const unsigned gather=load(vd)|(mop<<26)|(vs2<<20)|(ie?5u<<12:0);
      const auto before_fault=faults, before_narrow=narrow;
      for(unsigned attempt=0;attempt<2;++attempt) {
        const unsigned begin=attempt?last_vstart:0, end=attempt?count:bad;
        state(sew,lmul,begin,count); addresses.clear(); masks.clear(); outcomes.clear();
        refuse_page=!attempt; precheck_safe=0;
        send(gather,base,false);
        // Stop a response at the first index-row boundary, with prior results
        // already in the aliased VRF. On restart stall the first suffix return.
        while(addresses.size()<(attempt?1u:16/idx_size)) step();
        block_returns=true;
        for(unsigned t=0;t<12;++t) step();
        CHECK(!pending.empty() && outcomes.empty());
        block_returns=false; drain();
        CHECK(addresses.size()==end-begin && masks.size()==addresses.size() && narrow==before_narrow);
        for(unsigned i=begin;i<end;++i) {
          const Word a=base+offsets[i];
          CHECK(addresses[i-begin]==(a&~Word(15)) && masks[i-begin]==(low_mask(size)<<(a%16)));
          for(unsigned j=0;j<size;++j) image[i*size+j]=byte(a+j);
        }
        CHECK(outcomes==std::vector<Word>{attempt?0u:2u} && faults==before_fault+1);
        CHECK(last_vstart==bad && last_fault==base+offsets[bad] && last_cause==13);
        // Inspect the entire two-register image, including the as-yet unread indices.
        state(0,1,0,64); send(store(vd),out); drain();
        for(unsigned b=0;b<64;++b) CHECK(byte(out+b)==image[b]);
      }
    }

    // Index aliases the second VS3 field. Field zero supplies distinct values
    // to duplicate addresses, so a dropped/reordered store is observable.
    {
      cycle_limit=cycles+400;
      constexpr Word base=0x8540;
      const unsigned offsets[]={4,4,10,4};
      state(0,0,0,4);
      stored[index_src]=Row{0x040a0404,0,0,0}; send(load(17),index_src);
      stored[data_src]=Row{0x45444342,0,0,0}; send(load(16),data_src); drain();
      addresses.clear(); masks.clear(); block_returns=true;
      const auto acks=store_acks, old_narrow=narrow;
      send(store(16)|(1u<<29)|(3u<<26)|(17u<<20),base,false);
      for(unsigned j=0;j<8;++j) {
        while(addresses.size()<=j) step();
        for(unsigned t=0;t<6;++t) step();
        const unsigned off=offsets[j/2]+j%2;
        CHECK(pending.size()==1 && stores_pending && addresses.size()==j+1 && store_acks==acks+j);
        CHECK(addresses.back()==base && masks.back()==(1u<<off));
        CHECK(reinterpret_cast<const unsigned char*>(pending.front().data.data())[off]==(j%2?offsets[j/2]:0x42+j/2));
        block_returns=false; step(); block_returns=true;
      }
      block_returns=false; drain();
      CHECK(store_acks==acks+8 && narrow==old_narrow && reinterpret_cast<unsigned char*>(stored.at(base).data())[4]==0x45);
    }
    // Unit, negative stride and duplicate-index segments use the same field cursor.
    // Check data both ways, up to eight fields; LMUL2 fields are two registers apart.
    for(unsigned fields=2;fields<=8;++fields) for(unsigned mode:{0u,2u,1u,3u}) {
      cycle_limit=cycles+1200;
      const unsigned lmul=fields<=4 ? 1:0, spacing=1u<<lmul, seg=(fields-1)<<29;
      const unsigned memop=seg|(mode<<26)|((mode==2?1u:mode?4u:0u)<<20);
      const Word base=0x6500;
      stride=Word(-16);
      state(0,0,0,4); stored[index_src]=Row{0x11111709,0,0,0}; send(load(4),index_src); drain();
      auto byte=[&](Word a)->unsigned char& {return reinterpret_cast<unsigned char*>(stored[a&~Word(15)].data())[a%16];};
      for(unsigned i=0;i<96;++i) byte(base-48+i)=0xa0+i;
      auto va=[&](unsigned i,unsigned f){return base+(mode==2?-Word(16*i):mode?(i==1?23:17):i*fields)+f;};
      std::vector<Word> expected;
      for(unsigned i=1;i<=3;++i) for(unsigned f=0;f<fields;++f) expected.push_back(va(i,f));
      state(0,lmul,1,4); exact_probes.clear(); send(load(16)|memop,base,false); drain();
      CHECK(exact_probes==expected);
      for(unsigned f=0;f<fields;++f) {
        state(0,0,0,4); send(store(16+spacing*f),out); drain();
        for(unsigned i=1;i<=3;++i) CHECK(byte(out+i)==byte(va(i,f)));
        stored[out]=Row{0x30201000+f*0x01010101,0,0,0}; send(load(16+spacing*f),out); drain();
      }
      state(0,lmul,1,4); exact_probes.clear(); addresses.clear(); masks.clear();
      if(mode==3) block_returns=true;
      send(store(16)|memop,base,false);
      if(mode==3) {
        while(pending.empty()) step();
        for(unsigned t=0;t<12;++t) step();
        CHECK(addresses.size()==1 && stores_pending);
        block_returns=false;
      }
      drain(); CHECK(exact_probes==expected && addresses.size()==expected.size());
      std::map<Word,unsigned> final_data;
      for(unsigned j=0;j<expected.size();++j) {
        CHECK(addresses[j]==(expected[j]&~Word(15)) && masks[j]==(1u<<(expected[j]%16)));
        final_data[expected[j]]=16*(1+j/fields)+j%fields;
      }
      for(auto [a,value]:final_data) CHECK(byte(a)==value);
    }
    // SEW32 with EEW16 and LMUL1 gives fractional EMUL: fields still
    // occupy separate registers. Field 1 straddles two nonadjacent PPNs.
    state(2,0,0,1); addresses.clear(); masks.clear(); writes.clear(); probes.clear();
    const Word low=mapped_page(0x2fff)*4096+0xff0, high=mapped_page(0x3000)*4096;
    stored[low].fill(0x12121212); stored[high].fill(0x34343434);
    iterative=repair=true;
    send(load(24)|(2u<<29)|(5u<<12),0x2ffd,false); drain();
    CHECK((addresses==std::vector<Word>{low,low,high,high}) && (masks==std::vector<unsigned>{0x6000,0x8000,1,6}));
    CHECK((writes==std::vector<Word>{48,50,52}) && (probes==std::vector<Word>{0x2000,0x2fff,0x3000}));
    iterative=repair=false;
    state(0,0,0,2);
    const unsigned halfwords[]={0x1212,0x3412,0x3434};
    for(unsigned f=0;f<3;++f) {
      send(store(24+f),out); drain();
      CHECK((stored.at(out)[0]&0xffff)==halfwords[f]);
    }
    CHECK(narrow==before_segments);
    precheck_safe=1; stride=8;
    // A wide index names byte offsets, not data elements. Only active
    // elements are probed. The precise split responder faults the suffix
    // after the earlier gather has actually reached the VRF.
    for(unsigned ie=1;ie<=(xlen_bytes==4?2u:3u);++ie) {
      constexpr Word base=0x1000;
      const unsigned index_bytes=1u<<ie, sew=ie==1?2:ie==3?1:0, bytes=1u<<sew;
      cycle_limit=cycles+1200;
      state(0,0,0,32);
      for(unsigned i=0;i<32;++i) reinterpret_cast<unsigned char*>(stored[index_src+(i&~15u)].data())[i%16]=0;
      const Word far=ie==1?0xe000:ie==2?0x80006000:UINT64_C(0x100006000);
      const Word offsets[]={far-base,ie==1?far-base+4:Word(0x7000),0x1fff,far-base};
      for(unsigned i=0;i<4;++i) for(unsigned b=0;b<index_bytes;++b)
        reinterpret_cast<unsigned char*>(stored[index_src+((i*index_bytes+b)&~15u)].data())[(i*index_bytes+b)%16]=offsets[i]>>(8*b);
      stored[mask]={ie==1 ? 15u : 13u,0,0,0};
      send(load(4),index_src); send(load(0),mask);
      send(op(23,0,1,4,16),0x35); drain();
      state(sew,0,0,4); probes.clear(); addresses.clear();
      iterative=true; repair=ie!=1;
      const unsigned ins=(1u<<26)|(4u<<20)|((4+ie)<<12);
      const Word first_pa=mapped_page(far)*4096;
      const Word low_pa=mapped_page(0x2fff)*4096+0xff0, high_pa=mapped_page(0x3000)*4096;
      stored[first_pa].fill(0x22222222); stored[low_pa].fill(0x66666666); stored[high_pa].fill(0x77777777);
      const auto before_faults=faults;
      block_returns=true;
      send((load(16)|ins)&~(1u<<25),base,false);
      while(pending.empty()) step();
      for(unsigned n=0;n<16;++n) step();
      if (repair) {
        while(addresses.size()<2) step();
        CHECK(addresses[0]==first_pa && addresses[1]==low_pa);
      } else {
        while(probes.size()<2) step();
        CHECK((probes==std::vector<Word>{far,0x2fff}) && (addresses==std::vector<Word>{first_pa,first_pa}));
      }
      CHECK(faults==before_faults && !split_reply);
      block_returns=false; drain();
      if(ie==1) {
        CHECK(faults==before_faults+1 && split_faults==0 && last_vstart==2 && last_fault==0x3000 && last_cause==13);
        CHECK((probes==std::vector<Word>{far,0x2fff}) && (addresses==std::vector<Word>{first_pa,first_pa}));
        iterative=false; state(0,0,0,16); send(store(16),out); drain();
        for(unsigned b=0;b<16;++b) CHECK(reinterpret_cast<unsigned char*>(stored[out].data())[b]==(b<8?0x22:0x35));
        iterative=repair=true; state(sew,0,last_vstart,4); probes.clear(); addresses.clear();
        send((load(16)|ins)&~(1u<<25),base,false); drain();
        CHECK((probes==std::vector<Word>{0x2fff,far}) && (addresses==std::vector<Word>{low_pa,high_pa,first_pa}));
      } else {
        CHECK(faults==before_faults && (probes==std::vector<Word>{far,bytes==1?0x2000u:0x2fffu,far}));
        const std::vector<Word> expected=bytes==1?std::vector<Word>{first_pa,low_pa,first_pa}:std::vector<Word>{first_pa,low_pa,high_pa,first_pa};
        CHECK(addresses==expected);
      }
      iterative=false; state(0,0,0,4*bytes); send(store(16),out); drain();
      for(unsigned b=0;b<4*bytes;++b) {
        const unsigned i=b/bytes;
        CHECK(reinterpret_cast<unsigned char*>(stored[out].data())[b]==(i==1 && ie!=1?0x35:i!=2?0x22:b%bytes?0x77:0x66));
      }
      // Ordered aliases: the second store of element 3 overwrites element 0.
      state(sew,0,0,4); iterative=repair=true; probes.clear(); addresses.clear();
      block_returns=true; send(((store(8)|ins|(3u<<26))&~(1u<<25)),base,false);
      while(pending.empty()) step();
      for(unsigned t=0;t<16;++t) step();
      const Word middle=bytes==1?0x2000:0x2fff;
      CHECK(stores_pending && probes==(ie==1?std::vector<Word>{far}:std::vector<Word>{far,middle}) && (addresses==std::vector<Word>{first_pa}));
      block_returns=false; drain();
      CHECK((probes==std::vector<Word>{far,middle,far}));
      step(); CHECK(!stores_pending);
      for(unsigned b=0;b<bytes;++b) CHECK(reinterpret_cast<unsigned char*>(stored[first_pa].data())[b]==0x40+3*bytes+b);
      iterative=false;
    }
    // The same eight in-page bytes surround an inaccessible index. Toggling
    // its predicate must report the element address, not the probed page base.
    for (bool enable_bad : {false,true}) {
      cycle_limit=cycles+1000;
      state(0,0,0,32);
      const unsigned offsets[]={0x40,0x11,0x1043,0x09};
      for(unsigned i=0;i<10;++i) {
        auto* p=reinterpret_cast<unsigned char*>(stored[index_src+((2*i)&~15u)].data())+(2*i%16);
        p[0]=offsets[i%4]; p[1]=offsets[i%4]>>8;
      }
      stored[mask]={enable_bad?0x3bfu:0x3bbu,0,0,0};
      send(load(4),index_src); send(load(0),mask); send(op(23,0,1,4,16),0x35); drain();
      for(unsigned offset:{0x40u,0x11u,0x09u})
        stored[mapped_page(0xe000)*4096+(offset&~15u)].fill(0x22222222);
      state(0,0,0,10); iterative=repair=refuse_page=true;
      probes.clear(); addresses.clear(); precise_accesses.clear();
      const auto prior=faults;
      send((load(16)|(1u<<26)|(4u<<20)|(5u<<12))&~(1u<<25),0xe000,false);
      drain();
      if(enable_bad) {
        CHECK((probes==std::vector<Word>{0xe000,0xf000}) && addresses.size()==2);
        CHECK(precise_accesses.empty());
        CHECK(faults==prior+1 && last_vstart==2);
        CHECK(last_fault==0xf043 && last_cause==13);
      } else CHECK((probes==std::vector<Word>{0xe000}) && addresses.size()==8 && faults==prior && precise_accesses.empty());
      iterative=refuse_page=false;
      state(0,0,0,10); send(store(16),out); drain();
      for(unsigned i=0;i<10;++i) {
        bool updated=enable_bad ? i<2 : i%4!=2;
        CHECK(reinterpret_cast<const unsigned char*>(stored[out].data())[i]==(updated?0x22:0x35));
      }
    }
    // Exercise the real shared VRF port: a VS3 stream and a non-row-aligned
    // slide read the same group. The consuming store must see each new row.
    cycle_limit=cycles+2000;
    state(0,2,0,128);
    constexpr Word slide_src=0xa100, copy=0xa200, slide_dst=0xa300;
    for(unsigned i=0;i<128;++i)
      reinterpret_cast<unsigned char*>(stored[slide_src+(i&~15u)].data())[i%16]=i+19;
    send(load(8),slide_src); drain();
    writes_alu.clear(); store_captures.clear(); watch_writer=true;
    send(store(8),copy);
    send(op(15,8,3,3,16)); // vslidedown.vi v16,v8,3
    send(store(16),slide_dst); drain();
    watch_writer=false;
    CHECK(writes_alu.size()==8 && store_captures.size()==16);
    CHECK(writes_alu.front().first<store_captures[7]);
    for(unsigned i=0;i<128;++i) {
      CHECK(reinterpret_cast<unsigned char*>(stored.at(copy+(i&~15u)).data())[i%16]==i+19);
      CHECK(reinterpret_cast<unsigned char*>(stored.at(slide_dst+(i&~15u)).data())[i%16]==(i+3<128 ? i+22 : 0));
    }
    // A slide spanning two source rows and a gather revisiting both rows
    // each produce one partial destination row. Dispatch the add on its grant.
    constexpr Word gather_index=0xa400;
    stored[gather_index]={0x021b011b,0,0,0};
    state(0,0,0,4); send(load(12),gather_index); drain();
    for(bool gather : {false,true}) for(bool dependent : {false,true}) {
      reads_alu.clear(); writes_alu.clear();
      send(gather ? op(12,8,12,0,24) : op(15,8,15,3,24));
      const auto first_launch = execute_launch_cycle;
      CHECK(first_launch==cycles); // empty credit: no added dispatch stage
      send(op(0,dependent?24:8,1,3,28));
      CHECK(cycles==first_launch+1 && execute_launch_cycle==first_launch);
      while(execute_launch_cycle==first_launch) {
        eval(); CHECK(request_ready); step(); // VXDQ can buffer more work while VXSeq is occupied.
      }
      eval(); CHECK(request_ready);
      drain();
      CHECK(reads_alu.size()==2 && writes_alu.size()==2);
      CHECK(execute_launch_cycle==reads_alu[0].first && writes_alu[0].second==48 && writes_alu[1].second==56);
      if(dependent) CHECK(reads_alu[1].first>writes_alu[0].first);
      else CHECK(reads_alu[1].first==execute_launch_cycle+1);
      send(store(28),out); drain();
      for(unsigned i=0;i<4;++i) {
        unsigned index=!dependent ? i : gather ? (stored[gather_index][0]>>(8*i))&255 : 15+i;
        CHECK(((stored[out][0]>>(8*i))&255)==20+index);
      }
    }
    // Turn over both cursors. A gather can read independent indices early
    // even when its data row depends on the old destination.
    stored[gather_index][0]=0x02000301; send(load(12),gather_index); drain();
    const unsigned indices[]={1,3,0,2};
    for(bool first_gather : {false,true}) for(bool next_gather : {false,true})
      for(unsigned dependency=0;dependency<(next_gather?3u:2u);++dependency) {
        cycle_limit=cycles+200;
        send(op(23,0,0,3,24)); drain(); // Poison the previous destination before each pair.
        reads_alu.clear(); writes_alu.clear(); vrf_reads.clear();
        send(first_gather ? op(12,8,12,0,24) : op(15,8,1,3,24));
        unsigned data=dependency==1 ? 24 : 8, index=dependency==2 ? 24 : 12;
        send(next_gather ? op(12,data,index,0,28) : op(15,data,0,3,28)); drain();
        CHECK(reads_alu.size()==2 && writes_alu.size()==2 && writes_alu[0].second==48 && writes_alu[1].second==56);
        CHECK(execute_launch_cycle==reads_alu[0].first);
        std::vector<unsigned> younger;
        for(auto [edge,ports]:vrf_reads) if(edge>execute_launch_cycle) {
          CHECK(ports==4); younger.push_back(edge);
        }
        CHECK(younger.size()==(next_gather?2u:1u));
        if(next_gather ? dependency==2 : dependency==1) CHECK(younger[0]>writes_alu[0].first);
        else CHECK(younger[0]==execute_launch_cycle+1);
        if(next_gather && dependency==1) CHECK(younger[1]>writes_alu[0].first);
        send(store(28),out); drain();
        auto first=[&](unsigned i) { return 19+(first_gather ? indices[i] : i+1); };
        for(unsigned i=0;i<4;++i) {
          unsigned ix=next_gather ? (dependency==2 ? first(i) : indices[i]) : i;
          CHECK(((stored[out][0]>>(8*i))&255)==(dependency==1 ? first(ix) : 19+ix));
        }
      }
    cycle_limit=cycles+2000;
    // Compress into v24, then immediately slide its result. An empty
    // selection must preserve the old bytes; a partial one only replaces
    // the prefix. Both owners must retire even with only one physical write.
    for(unsigned mask_bits:{0u,5u}) {
      stored[gather_index][0]=mask_bits; send(load(12),gather_index);
      send(op(23,0,7,3,24)); drain();
      const auto before=finishes;
      writes_alu.clear(); vrf_reads.clear();
      send(op(23,8,12,2,24)); send(op(15,24,0,3,28)); drain();
      CHECK(finishes==before+2 && writes_alu.size()==(mask_bits?2u:1u) && writes_alu.back().second==56);
      if(mask_bits) CHECK(writes_alu.front().second==48 && writes_alu.front().first<execute_launch_cycle);
      CHECK(vrf_reads.back().first==execute_launch_cycle+1 && vrf_reads.back().second==4);
      send(store(28),out); drain();
      CHECK(stored.at(out)[0]==(mask_bits?0x07071513u:0x07070707u));
    }
    // Slide predicates come from v0; compress reads its explicit selection
    // register while its VS2 still waits for the slide's masked writeback.
    stored[copy][0]=5; stored[gather_index][0]=15;
    send(load(0),copy); send(load(12),gather_index); send(op(23,0,7,3,24)); drain();
    reads_alu.clear(); writes_alu.clear(); vrf_reads.clear();
    special_installs.clear();
    send(op(15,8,0,3,24)&~(1u<<25)); send(op(23,24,12,2,28)); drain();
    CHECK(special_installs.size()==2 && special_installs.back()==execute_launch_cycle);
    CHECK(writes_alu.size()==2 && writes_alu.front().second==48 && writes_alu.back().second==56);
    CHECK(!reads_alu.empty() && execute_launch_cycle==reads_alu.front().first);
    std::vector<unsigned> compress_reads;
    for(auto [edge,ports]:vrf_reads) if(edge>execute_launch_cycle) {
      CHECK(ports==4); compress_reads.push_back(edge);
    }
    CHECK(compress_reads.size()==2 && compress_reads.front()==execute_launch_cycle+1 && compress_reads.back()>writes_alu.front().first);
    send(store(28),out); drain(); CHECK(stored.at(out)[0]==0x07150713u);
    // EI16 indices span twice the data footprint. Revisit both ends of VS2
    // while a younger load is trying to replace it with different contents.
    state(0,1,0,64);
    for(unsigned i=0;i<32;++i) {
      const unsigned ix=i%3==0 ? 31 : i%3==1 ? 0 : 32;
      auto p=reinterpret_cast<unsigned char*>(stored[copy+((2*i)&~15u)].data())+(2*i%16);
      p[0]=ix; p[1]=0;
    }
    send(load(16),copy); drain();
    state(0,0,0,32); reads_alu.clear(); load_write_cycles.clear();
    send(op(14,8,16,0,24)); // vrgatherei16.vv v24,v8,v16
    send(load(8),slide_dst); drain();
    CHECK(reads_alu.size()==2 && load_write_cycles.size()==2 && load_write_cycles[0]>reads_alu.back().first);
    send(store(24),slide_dst); drain();
    for(unsigned i=0;i<32;++i)
      CHECK(reinterpret_cast<unsigned char*>(stored.at(slide_dst+(i&~15u)).data())[i%16]==(i%3==0 ? 50 : i%3==1 ? 19 : 0));
    // At e32,m4, EI16 indices use v8/v9, not v10. Replace v10 early, then
    // replace v9 in a separate run: its final index row must be captured first.
    for(unsigned replace:{10u,9u}) {
      constexpr Word indices=0x1e800, values=0x1e900, result=0x1ea00;
      cycle_limit=cycles+400;
      for(unsigned i=0;i<32;++i) {
        stored[values+16*(i/4)][i%4]=0xcafe0000+i;
        auto* ix=reinterpret_cast<unsigned char*>(stored[indices+16*(i/8)].data());
        ix[2*(i%8)]=(i&1)?0:31; ix[2*(i%8)+1]=0;
      }
      state(0,1,0,64); send(load(8),indices);
      state(0,2,0,128); send(load(16),values); drain();
      state(2,2,0,32); reads_alu.clear(); load_write_cycles.clear();
      send(op(14,16,8,0,24));
      state(0,0,0,32); send(load(replace),dst); drain();
      CHECK(reads_alu.size()==8 && load_write_cycles.size()==2);
      if(replace==10) CHECK(load_write_cycles.back()<reads_alu[3].first);
      else CHECK(load_write_cycles.back()>reads_alu[5].first);
      state(0,2,0,128); send(store(24),result); drain();
      for(unsigned i=0;i<32;++i) CHECK(stored.at(result+16*(i/4))[i%4]==0xcafe0000+((i&1)?0:31));
    }
    // Exercise the real Zvbb decode and both DLEN rows. A typed C++ rotate
    // wraps inside one element, independently of the RTL shifter's packing.
    auto check_zvbb = [&]<typename Lane>(unsigned sew) {
      constexpr unsigned lanes=32/sizeof(Lane), bits=8*sizeof(Lane);
      constexpr Word rhs_scalar=0x713d9a60b810e5c9ULL;
      constexpr unsigned predicate=0x96695aa6;
      for (unsigned i=0;i<lanes;++i) {
        const Lane left=Lane(0x8fb329da674c2d81ULL + 0x36b18dc5e713045bULL*i);
        const Lane right=Lane(0x59e24c168d37b600ULL + i*(bits-1));
        const unsigned off=i*sizeof(Lane);
        std::memcpy(reinterpret_cast<char*>(stored[src+(off&~15u)].data())+off%16,&left,sizeof(Lane));
        std::memcpy(reinterpret_cast<char*>(stored[copy+(off&~15u)].data())+off%16,&right,sizeof(Lane));
      }
      stored[mask]={predicate,0,0,0}; stored[dst].fill(0xa55ac33c); stored[dst+16]=stored[dst];
      cycle_limit=cycles+128; state(0,0,0,32);
      send(load(0),mask); send(load(8),src); send(load(12),copy); drain();
      struct Form { unsigned fn, mode, imm; };
      for (auto [fn,mode,imm] : {Form{1,0,0},{1,4,0},{21,0,0},{21,4,0},
                                 {20,0,0},{20,4,0},{20,3,32},{20,3,63}}) for (bool masked : {false,true}) {
        cycle_limit=cycles+128; state(0,0,0,32); send(load(24),dst); drain();
        const unsigned start=masked ? 1 : 0, limit=lanes-start;
        state(sew,0,start,limit); reads_alu.clear(); writes_alu.clear();
        // In vror.vi bit 26 supplies shamt[5]; bits 19:15 supply shamt[4:0].
        const auto inst=op(fn+(imm>>5),8,mode==3 ? imm&31 : 12,mode,24);
        send(inst & ~(unsigned(masked)<<25),rhs_scalar); drain();
        CHECK(reads_alu.size()==2 && writes_alu.size()==2);
        CHECK(reads_alu[1].first==reads_alu[0].first+1);
        for (unsigned r=0;r<2;++r) CHECK(writes_alu[r]==Beat(reads_alu[r].first+2,48+r));
        state(0,0,0,32); send(store(24),out); drain();
        for (unsigned i=0;i<lanes;++i) {
          const unsigned off=i*sizeof(Lane);
          auto lane=[&](Word base) {
            Lane value; std::memcpy(&value,reinterpret_cast<const char*>(stored.at(base+(off&~15u)).data())+off%16,sizeof(Lane));
            return value;
          };
          const Lane a=lane(src), b=mode==0 ? lane(copy) : Lane(mode==3 ? imm : rhs_scalar);
          const int count=int(b & (bits-1));
          const Lane result=fn==1 ? Lane(a & ~b) : fn==21 ? std::rotl(a,count) : std::rotr(a,count);
          const bool active=i>=start && i<limit && (!masked || ((predicate>>i)&1));
          CHECK(lane(out)==(active ? result : lane(dst)));
        }
      }
    };
    check_zvbb.operator()<std::uint8_t>(0);
    check_zvbb.operator()<std::uint16_t>(1);
    check_zvbb.operator()<std::uint32_t>(2);
    if constexpr (xlen_bytes==8) check_zvbb.operator()<std::uint64_t>(3);
    // Delay scalar product requests and replies independently.
    for(unsigned mode : {2u,6u}) {
      constexpr unsigned lanes=16/xlen_bytes;
      const unsigned active_bits=mode==2 ? (1u<<lanes)-1 : 1u<<(lanes-1);
      stored[mask]={active_bits,0,0,0};
      for(unsigned i=0;i<lanes;++i) {
        Word a=i+3,b=i+11;
        std::memcpy(reinterpret_cast<char*>(stored[src].data())+i*xlen_bytes,&a,xlen_bytes);
        std::memcpy(reinterpret_cast<char*>(stored[copy].data())+i*xlen_bytes,&b,xlen_bytes);
      }
      cycle_limit=cycles+300; state(0,0,0,16);
      send(load(0),mask); send(load(8),src); send(load(12),copy); drain();
      // The encoding is unchanged: narrow SEW books a SIMD product write,
      // then full SEW reaches the scalar service even with its request stalled.
      const Word multiply=op(37,8,12,mode,16) & ~(1u<<25);
      factors.clear(); reads_alu.clear(); writes_alu.clear();
      state(0,0,0,lanes); send(multiply,7); drain();
      CHECK(factors.empty() && reads_alu.size()==1 && writes_alu.size()==1 && writes_alu[0].first==reads_alu[0].first+6);
      state(xlen_bytes==4?2:3,0,0,lanes); factors.clear(); reads_alu.clear(); writes_alu.clear();
      send(multiply,7);
      while(!product_out.pvalid) step();
      for(unsigned i=0;i<4;++i) step();
      CHECK(reads_alu.size()==1 && factors.empty());
      accept_products=true;
      while(factors.size()<unsigned(std::popcount(active_bits))) step();
      for(unsigned i=0,k=0;i<lanes;++i) if(active_bits&(1u<<i)) {
        CHECK(factors[k].first==i+3 && factors[k].second==(mode==2 ? i+11 : 7));
        ++k;
      }
      send(op(0,8,12,0,20));
      while(writes_alu.empty()) step();
      CHECK(reads_alu.size()==2 && writes_alu.back().second==40 && scalar_products.size()==factors.size());
      send(op(0,16,16,0,24));
      for(unsigned i=0;i<5;++i) step();
      CHECK(reads_alu.size()==2);
      return_products=true; drain();
      CHECK(reads_alu.size()==3 && scalar_products.empty());
      state(0,0,0,16); send(store(24),out); drain();
      for(unsigned i=0;i<lanes;++i) if(active_bits&(1u<<i)) {
        Word value=0; std::memcpy(&value,reinterpret_cast<const char*>(stored[out].data())+i*xlen_bytes,xlen_bytes);
        CHECK(value==2*(i+3)*(mode==2 ? i+11 : 7));
      }
      accept_products=return_products=false;
    }
    // Two DLEN rows, a retained old vd, and a service whose actual inputs
    // select signedness. Negative factors distinguish all three high halves.
    constexpr unsigned n=32/xlen_bytes, w=8*xlen_bytes;
    const Word word_mask=low_mask(w), minimum=Word(1)<<(w-1);
    auto put=[&](Word base,unsigned i,Word v) {
      const unsigned b=i*xlen_bytes;
      std::memcpy(reinterpret_cast<char*>(stored[base+(b&~15u)].data())+b%16,&v,xlen_bytes);
    };
    auto get=[&](Word base,unsigned i) {
      const unsigned b=i*xlen_bytes; Word v=0;
      std::memcpy(&v,reinterpret_cast<const char*>(stored.at(base+(b&~15u)).data())+b%16,xlen_bytes); return v;
    };
    accept_products=return_products=true;
    for(unsigned fn:{36u,38u,39u,41u,43u,45u,47u}) for(unsigned form:{2u,6u}) {
      cycle_limit=cycles+300;
      for(unsigned i=0;i<n;++i) { put(src,i,-Word(3+i)); put(copy,i,-Word(5+i)); put(dst,i,19+2*i); }
      stored[mask]={0xfffffffdu,0,0,0};
      state(0,0,0,32); send(load(8),src); send(load(12),copy); send(load(16),dst); send(load(0),mask); drain();
      state(xlen_bytes==4?2:3,0,0,n); factors.clear(); reads_alu.clear(); vrf_reads.clear();
      send(op(fn,8,12,form,16)&~(1u<<25),-Word(11)); drain();
      CHECK(factors.size()==n-1 && reads_alu.size()==2 && vrf_reads.size()==2);
      for(auto [cycle,ports]:vrf_reads) CHECK(ports==(fn>=41 ? (form==2 ? 7u : 3u) : (form==2 ? 3u : 1u)));
      state(0,0,0,32); send(store(16),out); drain();
      for(unsigned i=0;i<n;++i) {
        const Word a=3+i, b=form==2 ? 5+i : 11, vd=19+2*i;
        const Word expected=i==1 ? vd : fn==36 ? -(a+b) : fn==38 ? -a : fn==39 ? 0 :
            fn==41 ? -vd*b-a : fn==43 ? vd*b-a : fn==45 ? vd+a*b : vd-a*b;
        CHECK(get(out,i)==(expected & word_mask));
      }
    }
    // An older load supplies old vd. Stall its reply, then stall the MAC
    // replies instead. A younger store must observe the MAC, not the load.
    cycle_limit=cycles+200; state(0,0,0,16); block_returns=true;
    send(load(16),dst); while(pending.empty()) step();
    state(xlen_bytes==4?2:3,0,0,n/2); factors.clear(); reads_alu.clear();
    send(op(45,8,12,2,16)); return_products=false;
    for(unsigned i=0;i<5;++i) step();
    CHECK(reads_alu.empty() && factors.empty());
    block_returns=false;
    while(factors.size()<n/2) step();
    CHECK(reads_alu.size()==1 && reads_alu.front().first>load_write_cycles.back());
    state(0,0,0,16); const auto previous_stores=store_offers;
    send(store(16),out); for(unsigned i=0;i<4;++i) step();
    CHECK(store_offers==previous_stores && scalar_products.size()==n/2);
    return_products=true; drain();
    CHECK(store_offers==previous_stores+1);
    for(unsigned i=0;i<n/2;++i) CHECK(get(out,i)==19+2*i+(3+i)*(5+i));

    // Half-scale products exercise all vxrm tie rules. With minimum as the
    // second factor only lane zero saturates; masking it must suppress vxsat.
    for(unsigned form:{0u,4u}) for(unsigned rm=0;rm<4;++rm) for(unsigned variant=0;variant<3;++variant) {
      cycle_limit=cycles+300;
      Word b=variant ? minimum : minimum/2;
      for(unsigned i=0;i<n;++i) {
        const Word a=variant && !i ? minimum : Word(i%2 ? -3 : 1);
        put(src,i,a); put(copy,i,b); put(dst,i,71);
      }
      stored[mask]={variant==2 ? 0xfffffffeu : 0xffffffffu,0,0,0};
      state(0,0,0,32); send(load(8),src); send(load(12),copy); send(load(16),dst); send(load(0),mask); drain();
      state(xlen_bytes==4?2:3,0,0,n); factors.clear(); saturation_cycles.clear(); vxrm=rm;
      send(op(39,8,12,form,16)&~(1u<<25),b); vxrm=rm^3; drain();
      CHECK(factors.size()==n-(variant==2) && saturation_cycles.size()==unsigned(variant==1));
      state(0,0,0,32); send(store(16),out); drain();
      for(unsigned i=0;i<n;++i) {
        const Word expected=variant ? (i==0 ? (variant==2 ? 71 : minimum-1) : (i%2 ? 3 : -Word(1))) :
            (i%2 ? Word(rounded[rm][3]) : Word(rounded[rm][0]));
        CHECK(get(out,i)==(expected & word_mask));
      }
    }
    // Exercise both DLEN rows with the RTL divider. Rotate signed, unsigned,
    // zero, and overflow cases through vector lanes and scalar broadcasts.
    divide_enable=divide_release=1;
    auto lane_at=[&](Word base,unsigned off) {
      return reinterpret_cast<unsigned char*>(stored[base+(off&~15u)].data())+off%16;
    };
    for(unsigned size=1,sew=0;size<=xlen_bytes;size*=2,++sew) {
      const unsigned lanes=32/size, width=size*8;
      const Word mask_bits=low_mask(width), min=Word(1)<<(width-1);
      const Word a[]={min,-Word(23),Word(29),-Word(37)};
      const Word b[]={mask_bits,0,-Word(3),Word(5)};
      for(unsigned i=0;i<lanes;++i) {
        std::memcpy(lane_at(src,i*size),&a[i%4],size);
        std::memcpy(lane_at(copy,i*size),&b[i%4],size);
        std::memset(lane_at(dst,i*size),0xa5,size);
      }
      stored[mask]={0xb5b5b5b5,0,0,0};
      cycle_limit=cycles+5000; state(0,0,0,32);
      send(load(8),src); send(load(12),copy); send(load(0),mask); drain();
      if (size==1) {
        // The signed remainder is outstanding when its unsigned quotient successor arrives.
        state(0,0,3,4); divide_release=0; divides_sent=divides_returned=0;
        send(op(35,8,12,2,16)); // vrem.vv: -37 % 5 = -2
        while(!divides_sent) step();
        send(op(32,8,1,6,20),7); // vdivu.vx: 219 / 7 = 31
        for(unsigned t=0;t<8;++t) step();
        CHECK(divides_sent==1 && !divides_returned);
        divide_release=1; drain(); CHECK(divides_sent==2 && divides_returned==2);
        state(0,0,0,4); send(store(16),out); send(store(20),out+16); drain();
        CHECK(*lane_at(out,3)==0xfe && *lane_at(out+16,3)==31);
      }
      for(unsigned fn:{32u,33u,34u,35u}) for(unsigned form:{2u,6u}) for(unsigned sample=0;sample<3;++sample) {
        cycle_limit=cycles+5000; state(0,0,0,32); send(load(16),dst); drain();
        const bool masked=sample==2;
        const Word rhs=sample==0 ? mask_bits : sample==1 ? 0 : 5;
        const unsigned start=masked, stop=lanes-masked;
        state(sew,0,start,stop); reads_alu.clear(); divides_sent=divides_returned=0; saturation_cycles.clear();
        send(op(fn,8,12,form,16)&~(unsigned(masked)<<25),rhs); drain();
        CHECK(reads_alu.size()==2 && saturation_cycles.empty());
        state(0,0,0,32); send(store(16),out); drain();
        unsigned active_lanes=0;
        for(unsigned i=0;i<lanes;++i) {
          const bool live=i>=start && i<stop && (!masked || (stored[mask][0]>>i&1));
          active_lanes+=live;
          Word expected=live ? divide_expected(a[i%4],form==2 ? b[i%4] : rhs,width,fn) : 0xa5a5a5a5a5a5a5a5ull;
          Word got=0; std::memcpy(&got,lane_at(out,i*size),size);
          CHECK(got==(expected&mask_bits));
        }
        CHECK(divides_sent==active_lanes && divides_returned==active_lanes);
      }
    }
    // A final granted read releases VXSeq, not the destination. Only a
    // physically written row can feed the dependent add. No active byte in
    // the second probe means no handshake with the stopped divider at all.
    state(0,0,0,1); cycle_limit=cycles+300; divides_sent=divides_returned=0;
    divide_enable=divide_release=0; reads_alu.clear(); writes_alu.clear();
    send(op(33,8,12,2,16)); for(unsigned i=0;i<6;++i) step();
    CHECK(reads_alu.size()==1 && !divides_sent);
    send(op(0,8,12,0,20)); while(writes_alu.empty()) step();
    CHECK(reads_alu.size()==2 && writes_alu.back().second==40 && !divides_sent && !divides_returned);
    send(op(0,16,16,0,24)); for(unsigned i=0;i<5;++i) step();
    CHECK(reads_alu.size()==2);
    divide_enable=1; while(!divides_sent) step();
    for(unsigned i=0;i<5;++i) step();
    CHECK(reads_alu.size()==2 && !divides_returned);
    divide_release=1; drain(); CHECK(divides_returned==1 && reads_alu.size()==3);
    stored[mask]={}; state(0,0,0,32); send(load(0),mask); drain();
    divide_enable=divide_release=0; divides_sent=divides_returned=0; reads_alu.clear();
    send(op(35,8,12,2,16)&~(1u<<25)); drain();
    CHECK(reads_alu.size()==2 && !divides_sent && !divides_returned);
    state(0,0,0,0); send(op(33,8,12,2,16)); drain();
    CHECK(!divides_sent && !divides_returned);

    // Keep VS2 unavailable behind a divided result. Once VPS has captured the
    // widening seed, a younger certified load can overwrite v9 before VXSeq reads VS2.
    cycle_limit=cycles+350;
    constexpr Word seed_image=0x1eb00;
    stored[seed_image]={0x1234,0,0,0};
    state(0,0,0,1); send(op(23,0,4,3,16)); drain(); // vs2 = 4
    state(0,0,0,2); block_returns=true; send(load(9),seed_image);
    while(pending.empty()) step();
    special_installs.clear(); reads_alu.clear(); vrf_reads.clear();
    state(0,2,0,1); send(op(48,16,9,0,24)); // vwredsumu.vs v24,v16,v9
    for(unsigned i=0;i<8;++i) step();
    CHECK(special_installs.size()==1 && reads_alu.empty() && vrf_reads.empty());
    block_returns=false; drain();
    state(0,0,0,2); send(store(24),out); drain(); CHECK((stored.at(out)[0]&0xffff)==0x1238);
    cycle_limit=cycles+350;
    divide_enable=1; divide_release=0;
    state(0,0,0,1); send(op(32,8,1,6,16),0); // unsigned divide by zero writes 0xff
    while(!divides_sent) step();
    state(0,2,0,1); send(op(48,16,9,0,24));
    reads_alu.clear(); load_write_cycles.clear();
    state(0,0,0,1); send(load(9),dst);
    for(unsigned i=0;i<35 && load_write_cycles.empty();++i) step();
    CHECK(load_write_cycles.size()==1 && reads_alu.empty() && !divides_returned);
    divide_release=1; drain();
    state(0,0,0,2); send(store(24),out); drain();
    CHECK((stored.at(out)[0]&0xffff)==0x1333);
    divide_enable=divide_release=0;

    // Real encodings and stored VRF data for every fixed-point form. The
    // two grants and their prebooked writes must each be back-to-back.
    for(unsigned size=1,sew=0;size<=xlen_bytes;size*=2,++sew) {
      unsigned width=8*size, lanes=32/size;
      Word top=Word(1)<<(width-1);
      Word a[]={top-1,top,3,Word(-3),0,1,Word(-1),5}, b[]={1,1,width-1,1,3,0,1,3};
      cycle_limit=cycles+300;
      for(unsigned i=0;i<lanes;++i) {
        std::memcpy(lane_at(src,i*size),&a[i%8],size);
        std::memcpy(lane_at(copy,i*size),&b[i%8],size);
      }
      state(0,0,0,32); send(load(8),src); send(load(12),copy); drain();
      for(unsigned fn:{8u,9u,10u,11u,32u,33u,34u,35u,42u,43u}) {
        bool average=fn<12, clamp=fn>=32 && fn<=35;
        for(unsigned operand:{0u,4u,3u}) {
          if(operand==3 && (average || fn==34 || fn==35)) continue;
          unsigned form=average ? operand+2 : operand;
          for(unsigned rm=0;rm<(clamp ? 1u : 4u);++rm) {
            cycle_limit=cycles+250;
            state(sew,0,0,lanes); vxrm=rm;
            reads_alu.clear(); writes_alu.clear(); saturation_cycles.clear();
            send(op(fn,8,operand==3 ? 1 : 12,form,16),1);
            vxrm=(rm+1)%4; drain();
            CHECK(reads_alu.size()==2 && writes_alu.size()==2);
            CHECK(reads_alu[1].first==reads_alu[0].first+1);
            state(0,0,0,32); send(store(16),out); drain();
            std::vector<unsigned> expected_pulses;
            for(unsigned row=0;row<2;++row) {
              CHECK(writes_alu[row]==Beat(reads_alu[row].first+2,32+row));
              bool saturated_row=false;
              for(unsigned i=row*lanes/2;i<(row+1)*lanes/2;++i) {
                auto [expected,overflow]=fixed_expected(fn,width,a[i%8],operand==0 ? b[i%8] : 1,rm);
                Word got=0; std::memcpy(&got,lane_at(out,i*size),size);
                CHECK(got==expected); saturated_row|=overflow;
              }
              if(saturated_row) expected_pulses.push_back(writes_alu[row].first);
            }
            CHECK(saturation_cycles==expected_pulses);
          }
        }
      }
    }
    // A delayed second load row blocks only that row of vsaddu and its
    // dependent store. Each store capture must follow the actual VXSeq write.
    cycle_limit=cycles+240; state(0,0,0,32); block_returns=true;
    reads_alu.clear(); writes_alu.clear(); load_write_cycles.clear();
    store_captures.clear(); watch_writer=true;
    send(load(8),src); while(pending.size()!=2) step();
    send(op(32,8,1,4,16),1); send(store(16),out);
    CHECK(reads_alu.empty() && store_captures.empty());
    auto returned_before=load_return_cycles.size();
    block_returns=false; while(load_return_cycles.size()==returned_before) step(); block_returns=true;
    for(unsigned t=0;t<12;++t) step();
    CHECK(load_write_cycles.size()==1 && writes_alu.size()==1 && store_captures.size()==1);
    block_returns=false; drain(); watch_writer=false;
    CHECK(load_write_cycles.size()==2 && reads_alu.size()==2 && writes_alu.size()==2 && store_captures.size()==2);
    for(unsigned r=0;r<2;++r) {
      CHECK(load_write_cycles[r]<reads_alu[r].first && writes_alu[r].first<store_captures[r]);
      for(unsigned i=16*r;i<16*r+16;++i)
        CHECK(*lane_at(out,i)==std::min(255u,unsigned(*lane_at(src,i))+1));
    }

    // Mixed-width operations advance by one row of the widest operand.
    // Check real ISA encodings, source addresses, physical write fragments
    // and stored bytes, including restart in a nonzero fraction.
    auto get_lane=[&](Word base,unsigned i,unsigned size) {
      Word value=0; std::memcpy(&value,lane_at(base,i*size),size); return value;
    };
    auto extend=[](Word v,unsigned width,bool sign) {
      Word top=Word(1)<<(width-1); return sign ? (v^top)-top : v;
    };
    for(unsigned off=0;off<256;++off) {
      *lane_at(src,off)=0x83+off*37; *lane_at(copy,off)=3+off*13;
      *lane_at(dst,off)=0x5a;
    }
    stored[mask]={0xdb6db6db,0xffffffff,0xffffffff,0xffffffff};
    cycle_limit=cycles+300; state(0,0,0,32); send(load(0),mask); drain();
    for(unsigned sew=0,size=1;size<xlen_bytes;++sew,size*=2) {
      unsigned w=8*size,n=32/size;
      cycle_limit=cycles+300; state(0,1,0,64); send(load(8),src); send(load(12),copy); drain();
      for(unsigned fn=44;fn<48;++fn) for(unsigned form:{0u,4u,3u}) for(unsigned rm=0;rm<(fn>=46?4u:1u);++rm) {
        cycle_limit=cycles+250; state(0,0,0,32); send(load(16),dst); drain();
        unsigned start=(rm&1) ? 8/size+1 : 1, end=n-1-(rm==2 ? 8/size : 0);
        state(sew,0,start,end); vxrm=rm; writes_alu.clear(); reads_alu.clear(); saturation_cycles.clear();
        send(op(fn,8,form==3?3:12,form,16)&~(1u<<25),3); drain();
        state(0,0,0,32); send(store(16),out); drain();
        std::vector<unsigned> pulses;
        unsigned first=start*size/8, last=(end-1)*size/8;
        CHECK(writes_alu.size()==last-first+1 && reads_alu.size()==writes_alu.size());
        for(unsigned k=first;k<=last;++k) {
          CHECK(reads_alu[k-first].second==16+k && writes_alu[k-first]==Beat(reads_alu[k-first].first+2,32+k/2));
          bool overflow=false;
          for(unsigned i=k*8/size;i<(k+1)*8/size;++i) {
            Word expected=get_lane(dst,i,size);
            if(i>=start && i<end && (stored[mask][0]>>i&1)) {
              Word a=get_lane(src,i,2*size), b=form==0 ? get_lane(copy,i,size) : 3;
              Word shifted=fixed_expected(42+(fn&1),2*w,a,b,fn>=46?rm:2).first;
              __int128 exact=shifted;
              if((fn&1) && (shifted>>(2*w-1))) exact-=(__int128(1)<<(2*w));
              if(fn>=46) {
                __int128 lo=fn&1 ? -(__int128(1)<<(w-1)) : 0, hi=lo+low_mask(w);
                overflow|=exact<lo || exact>hi; exact=std::clamp(exact,lo,hi);
              }
              expected=Word(exact)&low_mask(w);
            }
            CHECK(get_lane(out,i,size)==expected);
          }
          if(overflow) pulses.push_back(writes_alu[k-first].first);
        }
        CHECK(saturation_cycles==pulses);
      }
      for(unsigned wide_left:{0u,1u}) for(unsigned variant=0;variant<5;++variant) {
        bool shift=variant==4;
        if(shift && wide_left) continue;
        for(unsigned operand:{0u,4u,3u}) {
          if(!shift && operand==3) continue;
          unsigned fn=shift ? 53 : 48+4*wide_left+variant;
          cycle_limit=cycles+250; state(sew,0,0,n); writes_alu.clear(); reads_alu.clear();
          send(op(fn,8,operand==3?3:12,shift?operand:operand+2,16),3); drain();
          CHECK(writes_alu.size()==4 && reads_alu.size()==4);
          for(unsigned k=0;k<4;++k)
            CHECK(reads_alu[k].second==16+(wide_left?k:k/2) && writes_alu[k]==Beat(reads_alu[k].first+2,32+k));
          state(0,1,0,64); send(store(16),out); drain();
          for(unsigned i=0;i<n;++i) {
            unsigned asize=size<<wide_left;
            Word a=extend(get_lane(src,i,asize),asize*8,!shift && (variant&1));
            Word b=operand==0 ? extend(get_lane(copy,i,size),w,!shift && (variant&1)) : 3;
            Word expected=shift ? a<<(b&(2*w-1)) : variant&2 ? a-b : a+b;
            CHECK(get_lane(out,i,2*size)==(expected&low_mask(w*2)));
          }
        }
      }
    }
    // At LMUL=8 the narrow source can live across eight consecutive writes.
    for(unsigned power=1;(1u<<power)<=xlen_bytes;++power) for(bool sign:{false,true}) {
      unsigned ratio=1u<<power,n=256/ratio,start=5,end=n-1;
      cycle_limit=cycles+400; state(0,3,0,256); send(load(8),src); send(load(16),dst); drain();
      state(power,3,start,end); writes_alu.clear(); reads_alu.clear();
      send(op(18,8,8-2*power+sign,2,16)); drain();
      state(0,3,0,256); send(store(16),out); drain();
      unsigned first=start*ratio/16,last=(end-1)*ratio/16;
      CHECK(reads_alu.size()==last-first+1 && writes_alu.size()==reads_alu.size());
      for(unsigned k=first;k<=last;++k)
        CHECK(reads_alu[k-first].second==16+k/ratio && writes_alu[k-first].second==32+k);
      for(unsigned i=0;i<n;++i) {
        Word expected=i>=start && i<end ? extend(get_lane(src,i,1),8,sign) : get_lane(dst,i,ratio);
        CHECK(get_lane(out,i,ratio)==(expected&low_mask(8*ratio)));
      }
    }
    for(unsigned size=1,sew=0;size<=xlen_bytes;size*=2,++sew) {
      unsigned w=8*size,n=32/size;
      cycle_limit=cycles+300; state(0,0,0,32); send(load(8),src); drain();
      for(unsigned sel:{8u,9u,10u,12u,13u,14u,16u,17u}) {
        cycle_limit=cycles+250; state(sew,0,0,n); reads_alu.clear();
        send(sel<16 ? op(18,8,sel,2,16) : op(3,8,3,sel==16?4:3,16),3); drain();
        CHECK(reads_alu.size()==2);
        state(0,0,0,32); send(store(16),out); drain();
        for(unsigned i=0;i<n;++i) {
          Word a=get_lane(src,i,size), expected=0;
          if(sel<=10) for(unsigned bit=0;bit<w;++bit) {
            unsigned from=sel==8 ? (bit^7) : sel==9 ? (w-8-(bit&~7u))+(bit&7) : w-1-bit;
            expected|=((a>>from)&1)<<bit;
          }
          else if(sel==12) expected=std::countl_zero(a)-(64-w);
          else if(sel==13) expected=a ? std::countr_zero(a) : w;
          else if(sel==14) expected=std::popcount(a);
          else expected=(3-a)&low_mask(w);
          CHECK(get_lane(out,i,size)==expected);
        }
      }
    }
    // An actual store must not observe the low fragment as a complete row.
    // Offer the second wide source row only after the first result writes.
    cycle_limit=cycles+300; state(1,0,0,16); reads_alu.clear(); writes_alu.clear();
    watch_writer=true; store_captures.clear(); block_returns=true;
    send(load(8)|(5u<<12),src); while(pending.size()!=2) step();
    state(0,0,0,16); send(op(44,8,0,3,16)); send(store(16),out);
    auto before_fragment=load_return_cycles.size();
    block_returns=false; while(load_return_cycles.size()==before_fragment) step(); block_returns=true;
    for(unsigned t=0;t<12;++t) step();
    CHECK(reads_alu.size()==1 && writes_alu.size()==1 && store_captures.empty());
    block_returns=false; drain(); watch_writer=false;
    CHECK(writes_alu.size()==2 && store_captures.size()==1 && store_captures[0]>writes_alu[1].first);
    for(unsigned i=0;i<16;++i) CHECK(get_lane(out,i,1)==(get_lane(src,i,2)&255));

    // Legal in-place widening reads the high source register; legal in-place
    // narrowing writes the low register of that same group. Neither operation
    // may consume data already overwritten by one of its own booked writes.
    cycle_limit=cycles+300; state(0,1,0,64); send(load(8),src); drain();
    state(0,0,0,32); send(op(49,9,1,6,8),Word(-3)); drain();
    state(0,1,0,64); send(store(8),out); drain();
    for(unsigned i=0;i<32;++i)
      CHECK(get_lane(out,i,2)==((extend(get_lane(src,i+32,1),8,true)-3)&65535));
    state(0,0,0,32); send(op(45,8,1,3,8)); drain();
    send(store(8),out); drain();
    for(unsigned i=0;i<32;++i)
      CHECK(get_lane(out,i,1)==(((extend(get_lane(src,i+32,1),8,true)-3)>>1)&255));

    // A reduction can use its seed register as vd. Check every fold,
    // preserving vd beyond element zero and ignoring masked source lanes.
    auto maskbit=[&](Word address,unsigned i) {return (get_lane(address,i/8,1)>>(i%8))&1;};
    for(unsigned i=0;i<32;++i) *lane_at(mask,i)=i%3 ? 0x69 : 0xa6;
    state(0,0,0,32); send(load(0),mask); drain();
    state(0,1,0,64); send(load(8),src); drain();
    for(unsigned size=1,sew=0;size<=xlen_bytes;size*=2,++sew) for(unsigned fn:{0u,1u,2u,3u,4u,5u,6u,7u,48u,49u}) {
      const bool wide=fn>=48, sign=fn==5 || fn==7 || fn==49;
      if(wide && size==xlen_bytes) continue;
      const unsigned dest_size=size*(1+wide), n=64/size-1;
      cycle_limit=cycles+230; state(0,0,0,32); send(load(12),copy); drain();
      state(sew,1,0,n); writes_alu.clear();
      send(op(fn,8,12,wide?0:2,12)&~(1u<<25));
      state(0,0,0,32); send(store(12),out); drain();
      CHECK(writes_alu.size()==1 && writes_alu[0].second==24);
      Word acc=get_lane(copy,0,dest_size), bits=low_mask(8*dest_size);
      for(unsigned i=0;i<n;++i) if(maskbit(mask,i)) {
        Word a=extend(get_lane(src,i,size),8*size,sign)&bits;
        const Word signbit=sign ? Word(1)<<(8*dest_size-1) : 0;
        if(fn==0 || wide) acc=(acc+a)&bits;
        else if(fn==1) acc&=a; else if(fn==2) acc|=a; else if(fn==3) acc^=a;
        else if(((a^signbit)<(acc^signbit))==(fn<6)) acc=a;
      }
      CHECK(get_lane(out,0,dest_size)==acc);
      for(unsigned i=dest_size;i<32;++i) CHECK(get_lane(out,i,1)==get_lane(copy,i,1));
    }
    // A neutral lane is observable when every active signed minimum is
    // positive or every active signed maximum is negative. The masked lane
    // and tail deliberately contain values that would win if included.
    for(unsigned fn:{5u,7u,49u}) for(bool all_masked:{false,true}) {
      const bool wide=fn==49, minimum=fn==5;
      const unsigned dest_size=wide ? 4 : 2;
      const unsigned seed=wide ? 0x1357a246u : minimum ? 41u : 0xffd7u;
      stored[copy].fill(0xdeadbeef);
      stored[copy][0]=wide ? seed : (0xdead0000u|seed);
      stored[src]=wide ? Row{0x007ffffd,0x007f0004,0,0}
                       : minimum ? Row{0xff800023,0xff800009,0,0} : Row{0x007fffdd,0x007ffff7,0,0};
      stored[mask]={all_masked ? 0u : 5u,0,0,0};
      cycle_limit=cycles+300; state(0,0,0,16);
      send(load(12),copy); send(load(8),src); send(load(0),mask); drain();
      state(1,0,0,3); writes_alu.clear(); send(op(fn,8,12,wide?0:2,12)&~(1u<<25));
      state(0,0,0,16); send(store(12),out); drain();
      CHECK(writes_alu.size()==1);
      CHECK(get_lane(out,0,dest_size)==(all_masked ? seed : wide ? seed+1 : minimum ? 9u : 0xfff7u));
      for(unsigned i=dest_size;i<16;++i) CHECK(get_lane(out,i,1)==get_lane(copy,i,1));
    }
    // An ordered FP sum with no active elements copies even a signaling NaN
    // seed bit-for-bit; it must not perform an addition or raise invalid.
    cycle_limit=cycles+300; stored[copy][0]=0x7f800123; stored[mask].fill(0);
    state(0,0,0,16); send(load(12),copy); send(load(0),mask); drain();
    state(2,0,0,3); accrued_flags=flag_rows=0; send(op(3,8,12,1,12)&~(1u<<25));
    state(0,0,0,16); send(store(12),out); drain();
    CHECK(stored.at(out)==stored.at(copy) && !accrued_flags);

    // Withhold the second source row. Even though row zero has folded, VSSeq
    // cannot capture the aliased seed/destination until the only write lands.
    cycle_limit=cycles+300;
    stored[copy].fill(0xdeadbeef); stored[copy][0]=0x4b800000; // seed: 2^24
    stored[src]={0x3f800000,0x7f800001,0x7f800001,0x7f800001};
    stored[src+16]={0xcb800000,0x3f800000,0x7f800001,0x7f800001};
    stored[mask]={0x31,0,0,0};
    state(0,0,0,16); send(load(12),copy); send(load(0),mask); drain();
    state(0,0,0,32); block_returns=true; send(load(8),src); while(pending.size()<2) step();
    reads_alu.clear(); writes_alu.clear(); store_captures.clear(); accrued_flags=flag_rows=0; watch_writer=true;
    state(2,0,0,8); send(op(3,8,12,1,12)&~(1u<<25));
    state(0,0,0,16); send(store(12),out);
    const auto before_fold=load_return_cycles.size();
    block_returns=false; while(load_return_cycles.size()==before_fold) step(); block_returns=true;
    for(unsigned i=0;i<20;++i) step();
    CHECK(reads_alu.size()==1 && writes_alu.empty() && store_captures.empty() && !flag_rows);
    block_returns=false; drain(); watch_writer=false;
    CHECK(writes_alu.size()==1 && store_captures.size()==1 && store_captures[0]>writes_alu[0].first);
    CHECK(accrued_flags==1 && flag_rows==1 && (stored.at(out)==Row{0x3f800000,0xdeadbeef,0xdeadbeef,0xdeadbeef}));
    // Restore the prefix fixture's predicate after exercising reduction masking.
    for(unsigned i=0;i<32;++i) *lane_at(mask,i)=i%3 ? 0x69 : 0xa6;
    state(0,0,0,32); send(load(0),mask); drain();
    // Predicate/source bit zero is in the second DLEN group. Prefix carries
    // cross that boundary, including when the first group has no selected bit.
    for(unsigned i=0;i<32;++i) *lane_at(src,i)=i<16 ? 0 : i%2 ? 0x50 : 0x0c;
    state(0,0,0,32); send(load(8),src); drain();
    for(unsigned variant=0;variant<7;++variant) for(unsigned n:{0u,127u,237u}) {
      cycle_limit=cycles+270;
      const unsigned selectors[]={16,17,1,3,2,16,17};
      const bool scalar_dest=variant<2, element=variant>=5;
      const unsigned start=variant==6 && n ? 5 : 0;
      state(0,3,0,256); send(load(16),dst); drain();
      state(0,3,start,n); writes_alu.clear(); unsigned before=scalar_returns;
      send(op(scalar_dest?16:20,variant==6?0:8,selectors[variant],2,scalar_dest?5:16)&~(1u<<25)); drain();
      unsigned pop=0; Word first=low_mask(xlen_bytes*8);
      if(!scalar_dest) {state(0,3,0,256); send(store(16),out); drain();}
      for(unsigned i=0;i<256;++i) {
        bool active_lane=i>=start && i<n && maskbit(mask,i);
        bool selected=active_lane && maskbit(src,i);
        if(!scalar_dest && element)
          CHECK(get_lane(out,i,1)==(active_lane ? (variant==6 ? i : pop) : get_lane(dst,i,1)));
        if(!scalar_dest && !element)
          CHECK(maskbit(out,i)==(active_lane ? !pop && (variant==2 ? !selected : variant==3 || selected) : maskbit(dst,i)));
        if(selected) { if(!pop) first=i; ++pop; }
      }
      if(scalar_dest) CHECK(scalar_returns==before+1 && scalar_value==(variant ? first : pop) && writes_alu.empty());
      else if(n) CHECK(writes_alu.size()==(element ? (n+15)/16 : (n+127)/128));
    }

    // A direct owner must finish under backpressure before a checked
    // successor uses a distinct exact mapping. Later faults drain the prefix;
    // neither a slice nor an element completion releases the macro window.
    for (bool direct_store:{false,true}) for (bool fail:{false,true}) {
      cycle_limit=cycles+900;
      state(0,0,0,4);
      stored[0xd000]={0x20000000,0,0,0}; // ei16 offsets: 0, 0x2000
      send(load(4),0xd000); drain();
      state(0,0,0,1); send(op(23,0,1,4,8),0x5a); drain();
      const unsigned before_fin=finishes, before_fault=faults;
      addresses.clear(); masks.clear(); exact_probes.clear(); writes.clear();
      memory_issues.clear(); memory_finishes.clear(); element_drains.clear(); window_done.clear();
      watch_memory_owner=true; block_returns=true; precheck_safe=0; remap_exact=true;
      send(direct_store?store(8):load(8),0x21000);
      while(pending.empty()) step();
      state(0,0,0,2);
      instruction=load(16)|(1u<<26)|(4u<<20)|(5u<<12); // vluxei16.v
      pc+=4; scalar=0xe000; certificate_valid=0; request_valid=1;
      auto offered_step=[&] {
        eval(); const bool accepted=request_valid && request_ready;
        step(); if(accepted) request_valid=0;
      };
      for(unsigned t=0;t<10;++t) offered_step();
      CHECK(addresses.size()==1 && addresses[0]==0x21000 && exact_probes.empty());
      CHECK(memory_issues.size()==1 && !memory_issues[0].checked && memory_finishes.empty());
      CHECK(finishes==before_fin && faults==before_fault);
      CHECK(window_done.size()==1 && window_done[0].owner==memory_issues[0].owner);
      block_returns=false;
      while(memory_finishes.empty()) offered_step();
      CHECK(memory_finishes.size()==1 && !memory_finishes[0].checked &&
            memory_finishes[0].owner==memory_issues[0].owner);
      refuse_page=fail;
      while(addresses.size()<2 && faults==before_fault) offered_step();
      CHECK(!request_valid);
      block_returns=true;
      for(unsigned t=0;t<18;++t) step();
      CHECK(finishes==before_fin+1 && faults==before_fault && element_drains.empty());
      CHECK(memory_finishes.size()==1 && window_done.size()==1);
      block_returns=false; drain();
      CHECK(finishes==before_fin+2-unsigned(fail) && faults==before_fault+unsigned(fail));
      CHECK(memory_issues.size()==(fail?2u:3u));
      const Word checked_owner=memory_issues[1].owner;
      CHECK(checked_owner!=memory_issues[0].owner);
      for(unsigned i=1;i<memory_issues.size();++i) CHECK(memory_issues[i].checked && memory_issues[i].owner==checked_owner);
      CHECK(element_drains.size()==(fail?1u:2u));
      for(const auto& e:element_drains) CHECK(e.owner==checked_owner);
      CHECK(memory_finishes.size()==1 && window_done.size()==1);
      CHECK(writes.size()==unsigned(!direct_store)+(fail?1u:2u));
      CHECK(addresses[1]==(mapped_page(0xe000)<<12));
      if(fail) CHECK(last_fault==0x10000 && last_cause==13 && last_vstart==1);
      else CHECK(addresses.size()==3 && addresses[2]==(mapped_page(0x10000)<<12));
      watch_memory_owner=remap_exact=refuse_page=false; precheck_safe=1;
    }

    // Hold the first live destination row behind the integer divider. A writer
    // to a skipped move source or the register next to the scan mask must pass.
    for(bool scan:{false,true}) for(bool conflict:{false,true}) {
      cycle_limit=cycles+600;
      state(0,2,0,128); send(load(8),src); send(load(16),dst); drain();
      state(0,0,0,1); divide_enable=1; divide_release=0; divides_sent=0;
      send(op(32,12,1,6,scan?16:18),0); while(!divides_sent) step();
      reads_alu.clear(); writes_alu.clear(); load_write_cycles.clear();
      if(scan) {
        state(0,3,0,256); send(op(20,9,16,2,16)); // viota.m v16,v9
      } else {
        state(1,0,32,0); send(op(39,8,3,3,16)); // restart vmv4r at byte 64, vl=0
      }
      state(0,0,0,32); send(load(scan?(conflict?9:10):(conflict?10:8)),copy);
      for(unsigned i=0;i<24;++i) step();
      CHECK(reads_alu.empty() && load_write_cycles.size()==(conflict?0u:2u));
      divide_release=1; drain(); CHECK(load_write_cycles.size()==2);
      if(scan) {
        CHECK(reads_alu.size()==16);
        if(conflict) {
          CHECK(load_write_cycles[0]>reads_alu[7].first && load_write_cycles[0]<reads_alu.back().first);
          CHECK(load_write_cycles[1]>reads_alu.back().first);
        }
        state(0,3,0,256); send(store(16),out); drain();
        unsigned pop=0;
        for(unsigned i=0;i<256;++i) {
          CHECK(get_lane(out,i,1)==pop);
          pop+=(get_lane(src,32+i/8,1)>>(i%8))&1;
        }
      } else {
        CHECK(reads_alu.size()==4);
        for(unsigned i=0;i<4;++i) CHECK(reads_alu[i].second==20+i);
        if(conflict) for(unsigned i=0;i<2;++i) CHECK(load_write_cycles[i]>reads_alu[i].first);
        state(0,2,0,128); send(store(16),out); drain();
        for(unsigned i=0;i<128;++i) CHECK(get_lane(out,i,1)==get_lane(i<64?dst:src,i,1));
      }
    }
    // An outstanding load owns v0. Unmasked vid can write while the load
    // waits; viota consumes the new v0 via VS2, masked vid via the shadow.
    for(unsigned which:{0u,1u,2u}) {
      cycle_limit=cycles+100;
      const bool iota=which==0, masked=which==2;
      state(0,0,0,16); block_returns=true; send(load(0),mask);
      while(pending.empty()) step();
      reads_alu.clear(); writes_alu.clear(); vrf_reads.clear(); load_write_cycles.clear();
      instruction=op(20,0,iota?16:17,2,8) & ~(unsigned(masked)<<25);
      pc+=4; request_valid=1;
      for(unsigned t=0;t<40;++t) {
        if(t==20) {
          CHECK(writes_alu.size()==unsigned(!iota && !masked) && vrf_reads.empty());
          block_returns=false;
        }
        eval(); const bool accepted=request_valid && request_ready; step();
        if(accepted) request_valid=0;
      }
      CHECK(!request_valid); drain();
      CHECK(load_write_cycles.size()==1 && writes_alu.size()==1 && reads_alu.size()==1);
      CHECK((reads_alu.front().first>load_write_cycles.front())==(iota || masked));
      CHECK(vrf_reads.size()==unsigned(iota));
      if(iota) CHECK(vrf_reads.front().second==1);
    }
    // vmfeq writes two FP32 fragments of v0. The following vfclass must
    // consume the finished predicate but write full element values.
    cycle_limit=cycles+300;
    for(unsigned r=0;r<2;++r) {
      stored[src+16*r]={0x3f800000,0xbf800000,0x3f800000,0xbf800000};
      stored[copy+16*r].fill(0x3f800000);
      stored[dst+16*r].fill(0x77777777);
    }
    stored[src][0]=0x7f800001; // masked-off signaling NaN
    stored[mask].fill(~0u); stored[mask][0]=0xfffffffe;
    state(0,0,0,32); send(load(8),src); send(load(16),copy);
    send(load(24),dst); send(load(0),mask); drain();
    state(2,0,0,8); reads_alu.clear(); writes_alu.clear(); accrued_flags=flag_rows=0;
    send(op(0x18,8,16,1,0)&~(1u<<25));   // vmfeq.vv v0,v8,v16,v0.t
    send(op(0x13,8,16,1,24)&~(1u<<25));  // vfclass.v v24,v8,v0.t
    drain();
    CHECK(reads_alu.size()==4 && writes_alu.size()==4);
    CHECK(writes_alu[0].second==0 && writes_alu[1].second==0 && reads_alu[2].first>writes_alu[1].first);
    CHECK(flag_rows==2 && !accrued_flags);
    state(0,0,0,32); send(store(0),out); send(store(24),out+32); drain();
    CHECK(stored.at(out)[0]==0xffffff54);
    for(unsigned i=0;i<8;++i) CHECK(get_lane(out+32,i,4)==((0x54u>>i&1)?0x40u:0x77777777u));

    // A delayed source row holds vrsub.vi in VXSeq while ordinary vsub.vx is offered.
    cycle_limit=cycles+240; state(0,0,0,8);
    stored[src].fill(0x800201ff); block_returns=true; send(load(8),src);
    while(pending.empty()) step();
    reads_alu.clear(); writes_alu.clear(); load_write_cycles.clear();
    send(op(3,8,3,3,16)); // v16 = 3 - v8
    instruction=op(2,8,1,4,24); scalar=5; pc+=4; request_valid=1; // v24 = v8 - 5
    for(unsigned t=0;t<8;++t) {
      eval(); const bool accepted=request_ready && request_valid; step();
      if(accepted) request_valid=0;
    }
    CHECK(reads_alu.empty() && writes_alu.empty());
    block_returns=false;
    while(request_valid) { eval(); const bool accepted=request_ready; step(); if(accepted) request_valid=0; }
    drain();
    CHECK(load_write_cycles.size()==1 && reads_alu.size()==2 && writes_alu.size()==2);
    CHECK(reads_alu.front().first>load_write_cycles.front());
    CHECK(reads_alu[1].first==reads_alu[0].first+1);
    CHECK(writes_alu[0]==Beat(reads_alu[0].first+2,32) && writes_alu[1]==Beat(reads_alu[1].first+2,48));
    send(store(16),out); send(store(24),out+16); drain();
    for(unsigned i=0;i<8;++i) {
      const auto x=get_lane(src,i,1);
      CHECK(get_lane(out,i,1)==((3-x)&255) && get_lane(out+16,i,1)==((x-5)&255));
    }
    send(op(31,8,1,3,16)); send(op(30,8,1,3,24)); // vmsgt.vi / vmsgtu.vi against 1
    state(0,0,0,1); send(store(16),out); send(store(24),out+16); drain();
    CHECK(get_lane(out,0,1)==0x44 && get_lane(out+16,0,1)==0xdd);

    // Use x1 and f1 with different values. Requiring FS or f1 is not a request
    // for FP execution: only add uses local arithmetic, and only divide goes shared.
    cycle_limit=cycles+320;
    stored[src].fill(0x40c00000); stored[mask].fill(~0u);
    state(0,0,0,1); send(load(0),mask); drain();
    state(2,0,0,1); send(load(8)|(6u<<12),src); drain();
    separate_fpr=1; fpr_source=UINT64_C(0xffffffff40000000) & low_mask(8*xlen_bytes);
    fp_div_enable=fp_div_release=1;
    const Word operations[]={op(23,8,1,4,16)&~(1u<<25),op(23,8,1,5,16)&~(1u<<25),
                             op(0,8,1,5,16),op(32,8,1,5,16)};
    const unsigned family_result[]={11,0x40000000,0x41000000,0x40400000};
    for(unsigned i=0;i<4;++i) {
      instruction=operations[i]; disable_fp=1; frm=0; eval();
      CHECK(fp_illegal==(i!=0) && reads_frs1==(i!=0));
      disable_fp=0; frm=7; eval(); CHECK(fp_illegal==(i>=2));
      frm=0; eval(); CHECK(!fp_illegal);
      const auto requests=shared_fp_requests, divs=fp_div_requests, flags=flag_rows;
      send(instruction,11); drain();
      CHECK((shared_fp_requests!=requests)==(i==3) && fp_div_requests==divs+unsigned(i==3));
      CHECK(flag_rows==flags+unsigned(i>=2));
      send(store(16)|(6u<<12),out); drain(); CHECK(get_lane(out,0,4)==family_result[i]);
    }

    // A scalar observer must see the store even after the last vector owner
    // is reclaimed, until the memory system independently reports drain.
    cycle_limit = cycles + 80;
    const auto before_store = finishes, before_ack = store_acks;
    state(0,0,0,1);
    block_returns = true;
    send(store(8), out);
    while (pending.empty()) step();
    CHECK(stores_pending && active && pending.size() == 1);
    hold_memory_drain = true;
    block_returns = false;
    do {
      step();
      CHECK(stores_pending);
    } while (active || !pending.empty());
    CHECK(finishes == before_store + 1 && store_acks == before_ack + 1);
    for (unsigned i = 0; i < 4; ++i) { step(); CHECK(stores_pending); }
    hold_memory_drain = false;
    step();
    CHECK(!stores_pending);

    // Delay a displaced oldest load, then return six fragments during a
    // younger FMA. Those fragments alone must yield five complete rows.
    cycle_limit=cycles+320;
    state(0,3,0,256); requests_always_ready=true; block_returns=true;
    load_return_cycles.clear(); load_write_cycles.clear(); writes_alu.clear();
    send(load(0),src+8);
    for(unsigned i=0;i<24;++i) step();
    CHECK(pending.size()==metadata_entries && load_return_cycles.empty());
    state(2,3,0,64); send(op(0x2c,8,16,1,24));
    while(writes_alu.size()<2) step();
    const auto denied=blocked_load_writes;
    block_returns=false;
    while(load_return_cycles.size()<6) step();
    block_returns=true;
    while(load_write_cycles.size()<5) step();
    CHECK(blocked_load_writes>denied && load_return_cycles.size()==6);
    block_returns=false; drain();
    CHECK(load_write_cycles.size()==16 && load_return_cycles.size()==17 && writes_alu.size()==16);
    state(0,3,0,256); send(store(0),out); drain();
    for(unsigned i=0;i<256;++i) {
      const Word addr=src+8+i;
      const Row& expected=stored.contains(addr & ~Word(15)) ? stored.at(addr & ~Word(15)) : initial;
      CHECK(get_lane(out,i,1)==(expected[(addr%16)/4]>>(8*(addr%4)) & 255u));
    }
    // B may issue its address while VLSeq is still waiting for A's reply.
    // C is independent but cannot skip B at the head of VXDQ to reach VXSeq.
    cycle_limit=cycles+160;
    state(0,0,0,16); addresses.clear(); reads_alu.clear(); block_returns=true;
    send(load(8),src); send(load(9),src+64); send(op(0,16,17,0,18));
    for (unsigned t=0;t<12;++t) step();
    CHECK((addresses==std::vector<Word>{src,src+64}) && pending.size()==2 && reads_alu.empty());
    block_returns=false; drain(); CHECK(!reads_alu.empty());

    // Drive real dispatch with one VLSeq and no replies. All four VLAQ owners
    // must reach L1D. Releasing responses must preserve each owner's data.
    cycle_limit=cycles+180;
    state(0,0,0,16); addresses.clear(); load_write_cycles.clear(); block_returns=true;
    for (unsigned i=0;i<4;++i) {
      stored[src+64*i].fill(0x11111111u*(i+1));
      send(load(8+i),src+64*i);
    }
    for (unsigned t=0;t<16;++t) step();
    CHECK(pending.size()==4 && load_write_cycles.empty());
    CHECK((addresses==std::vector<Word>{src,src+64,src+128,src+192}));
    block_returns=false; drain();
    for (unsigned i=0;i<4;++i) send(store(8+i),out+64*i);
    drain();
    for (unsigned i=0;i<4;++i) CHECK(stored.at(out+64*i)==stored.at(src+64*i));
  });
}
