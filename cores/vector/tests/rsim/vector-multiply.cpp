// Checks packed multiply scheduling and masked architectural results through the production vector engine.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <array>
#include <bit>
#include <deque>
#include <vector>
using Word = std::remove_cvref_t<decltype(scalar)>;
using U128 = unsigned __int128;
using I128 = __int128;
#ifndef VECTOR_WORD_WIDTH
#define VECTOR_WORD_WIDTH 64
#endif
constexpr unsigned W = VECTOR_WORD_WIDTH, VLEN = 256;
std::array<std::uint8_t, 4096> mem{};
unsigned cycles = 0, beats = 0, scalar_count = 0, retires = 0, saturation = 0;
unsigned fp_count = 0, last_fp_tag = 0, response_stalls = 0, fp_stalls = 0;
unsigned retries = 0;
std::vector<unsigned> issue_cycles;
std::uint64_t rng = 0xbead61728;
std::uint64_t random_word() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
std::uint64_t mask(unsigned n) { return n == 64 ? UINT64_MAX : (UINT64_C(1) << n) - 1; }
std::uint64_t get(unsigned address, unsigned bytes) {
  CHECK(address + bytes <= mem.size());
  std::uint64_t v = 0;
  for (unsigned i = 0; i < bytes; ++i) v |= std::uint64_t(mem[address+i]) << (8*i);
  return v;
}
void put(unsigned address, unsigned bytes, std::uint64_t v) {
  CHECK(address + bytes <= mem.size());
  for (unsigned i = 0; i < bytes; ++i) mem[address+i] = v >> (8*i);
}
struct Reply { unsigned tag; Word value; unsigned due; };
std::deque<Reply> replies;
void tick() {
  CHECK(cycles < 3000000);
  eval();
  hit_data = 0;
  if (attempt_out.pvalid && attempt_out.pbits.pmemory && !attempt_out.pbits.pstore)
    hit_data = Word(get(attempt_out.pbits.paddress, 1u << attempt_out.pbits.pmemory_uwidth));
  response_in.pvalid = !replies.empty() && replies.front().due <= cycles;
  if (response_in.pvalid) {
    response_in.pbits.ptag = replies.front().tag;
    response_in.pbits.pdata = replies.front().value;
  }
  dut.eval();
  bool fp_consumed = fp_return_valid && fp_return_ready;
  if (!reset) {
    if (issued) { ++beats; issue_cycles.push_back(cycles); }
    scalar_count += scalar_launched;
    retires += retired;
    saturation += saturated;
    response_stalls += response_in.pvalid && !response_out.pready;
    fp_stalls += fp_return_valid && !fp_return_ready;
    if (fp_launch) { ++fp_count; last_fp_tag = fp_launch_tag; }
    if (response_in.pvalid && response_out.pready) replies.pop_front();
    if (attempt_out.pvalid && attempt_out.pbits.pmemory && retry) ++retries;
    if (attempt_out.pvalid && attempt_out.pbits.pmemory && attempt_out.pbits.penabled && !retry) {
      auto &t = attempt_out.pbits;
      if (t.pstore) put(t.paddress, 1u << t.pmemory_uwidth, t.pstore_udata);
      if (slow) replies.push_back({unsigned(t.pcompletion_utag), Word(hit_data), cycles + 11});
    }
  }
  tick_model(); ++cycles;
  if (fp_consumed) fp_return_valid = 0;
  eval();
}
void drain() {
  unsigned deadline = cycles + 4000;
  while (active || !replies.empty()) { CHECK(cycles < deadline); tick(); }
  for (int i = 0; i < 4; ++i) tick();
}
void launch(std::uint32_t insn, unsigned sew, unsigned length, unsigned start = 0, Word value = 0, unsigned rounding = 0, unsigned lmul = 0) {
  instruction = insn; vtype = (sew << 3) | lmul; vl = length; vstart = start; scalar = value; vxrm = rounding;
  request_valid = 1;
  unsigned deadline = cycles + 4000;
  for (;;) {
    dut.eval(); bool accepted = request_ready;
    tick(); if (accepted) break;
    CHECK(cycles < deadline);
  }
  request_valid = 0;
}
void load(unsigned reg, unsigned address, unsigned length = 32) {
  launch((1u << 25) | (reg << 7) | 7, 0, length, 0, Word(address)); drain();
}
void dump(unsigned reg, unsigned address, unsigned count = 32) {
  launch((1u << 25) | (reg << 7) | 0x27, 0, count, 0, Word(address)); drain();
}
I128 extend(std::uint64_t a, unsigned bits, bool sign) {
  return sign && (a >> (bits-1)) ? I128(a) - (I128(1) << bits) : I128(a);
}
std::uint32_t multiply(unsigned op, bool vx, bool masked, bool fractional, unsigned vd = 16, unsigned vs2 = 8) {
  return (op << 26) | (!masked << 25) | (vs2 << 20) | (12 << 15) |
         ((fractional ? (vx ? 4 : 0) : (vx ? 6 : 2)) << 12) | (vd << 7) | 0x57;
}
void check_case(unsigned sew, unsigned op, bool vx, unsigned variant, unsigned rounding = 0, bool fractional = false) {
  const unsigned bits = 8u << sew, bytes = bits/8, n = 32/bytes;
  const bool wide = op >= 56, masked = variant >= 2;
  const unsigned outbits = bits * (wide ? 2 : 1), outbytes = outbits/8;
  const unsigned length = variant == 1 ? n-1 : n, start = variant == 1 || variant == 3 ? 1 : 0;
  const Word scalar_value = Word(random_word() & mask(W));
  for (unsigned i = 0; i < 1024; ++i) mem[i] = random_word();
  // Put saturating products in inactive lanes as well as active ones.
  if (fractional) for (unsigned i = 0; i < n; ++i) {
    put(256+i*bytes, bytes, UINT64_C(1) << (bits-1));
    put(384+i*bytes, bytes, i%3 ? UINT64_C(1) << (bits-1) : 3);
  }
  mem[0] = variant == 4 ? 0 : 0xa6; mem[1] = variant == 4 ? 0 : 0x59;
  mem[2] = mem[3] = variant == 4 ? 0 : 0x96;
  load(0, 0); load(8, 256); load(12, 384); load(16, 512); if (wide) load(17, 544);
  std::array<std::uint8_t,64> expected{};
  for (unsigned i = 0; i < (wide ? 64u : 32u); ++i) expected[i] = mem[512+i];
  bool expect_sat = false;
  for (unsigned i = start; i < length; ++i) {
    if (masked && !(mem[i/8] & (1u << (i%8)))) continue;
    auto a = get(256+i*bytes, bytes), b = vx ? std::uint64_t(scalar_value)&mask(bits) : get(384+i*bytes, bytes);
    auto old = get(512+i*outbytes, outbytes);
    bool sa = fractional || op == 38 || op == 39 || op == 58 || op == 59 || op == 61 || op == 62;
    bool sb = fractional || op == 39 || op == 59 || op == 61 || op == 63;
    U128 product = U128(extend(a,bits,sa)) * U128(extend(b,bits,sb));
    std::uint64_t result = std::uint64_t(product);
    if (fractional) {
      I128 p = I128(product), scale = I128(1) << (bits-1);
      I128 q = p / scale, rem = p % scale;
      if (rem < 0) { --q; rem += scale; }
      bool inc = rounding == 0 ? rem >= scale/2 : rounding == 1 ? rem > scale/2 || (rem == scale/2 && (q&1)) : rounding == 3 && rem && !(q&1);
      q += inc;
      if (q > scale-1) { q = scale-1; expect_sat = true; }
      if (q < -scale) { q = -scale; expect_sat = true; }
      result = std::uint64_t(q);
    } else if (op == 36 || op == 38 || op == 39) result = std::uint64_t(product >> bits);
    else if (op == 41) result = a + b*old;
    else if (op == 43) result = a - b*old;
    else if (op == 45 || op >= 60) result = old + result;
    else if (op == 47) result = old - result;
    for (unsigned j = 0; j < outbytes; ++j) expected[i*outbytes+j] = result >> (8*j);
  }
  const unsigned oldbeats=beats, oldscalar=scalar_count, oldsat=saturation;
  issue_cycles.clear();
  // Packed work must not depend on scalar multiplier readiness.
  scalar_available = bits == W;
  launch(multiply(op,vx,masked,fractional), sew, length, start, scalar_value, rounding);
  drain(); scalar_available = 1;
  const unsigned lanes = bits == W ? 1 : W/outbits;
  const unsigned first = start & ~(lanes-1);
  CHECK(beats-oldbeats == (length-first+lanes-1)/lanes);
  CHECK((saturation != oldsat) == expect_sat);
  if (bits < W) {
    CHECK(scalar_count == oldscalar);
    // The first bankful runs without inter-beat bubbles, including widened beats.
    for (unsigned i = 1; i < std::min<std::size_t>(8,issue_cycles.size()); ++i)
      CHECK(issue_cycles[i] == issue_cycles[i-1]+1);
  }
  dump(16, 2048); if (wide) dump(17, 2080);
  for (unsigned i = 0; i < (wide ? 64u : 32u); ++i) {
    if (mem[2048+i] != expected[i]) {
      std::fprintf(stderr,"W%u SEW%u op%u vx%d variant%u round%u byte%u got %02x expected %02x\n",W,bits,op,vx,variant,rounding,i,mem[2048+i],expected[i]);
      CHECK(false);
    }
  }
}
void overlaps() {
  for (unsigned i=0;i<1024;++i) mem[i]=0;
  for (unsigned i=0;i<32;++i) { mem[256+i]=2; mem[384+i]=3; }
  load(8,256); load(12,384); load(16,512); load(20,640); load(24,768);
  // Older full-width requests wait at the scalar boundary while younger
  // subword beats use the dedicated banks. Both return calendars stay live.
  scalar_available=0;
  unsigned before=beats;
  launch(multiply(37,false,false,false,20), std::countr_zero(W/8), 2);
  while (beats < before+2) tick();
  launch(multiply(37,false,false,false),0,32);
  for (int i=0;i<8;++i) tick();
  CHECK(beats > before+2);
  scalar_available=1;
  // A dependent store is admitted before the multiply finishes; row hazards
  // must hold its reads until the corresponding packed data is available.
  dump(16,2048);
  for (unsigned i=0;i<32;++i) CHECK(mem[2048+i]==6);
  dump(20,2112);
  for (unsigned i=0;i<2;++i) CHECK(get(2112+i*W/8,W/8)==(get(256+i*W/8,W/8)*get(384+i*W/8,W/8) & mask(W)));
  // Exercise all return alignments against a packed stream. Variable FP and
  // delayed memory producers hold their response until the VRF port is free.
  for (unsigned offset=0;offset<8;++offset) {
    slow=1; launch((1u<<25)|(24u<<7)|7,0,1,0,Word(256));
    while (replies.empty()) tick();
    slow=0;
    unsigned prior_fp=fp_count;
    launch((1u<<25)|(8u<<20)|(12u<<15)|(1u<<12)|(28u<<7)|0x57,2,1);
    while (fp_count==prior_fp) tick();
    unsigned tag=last_fp_tag;
    launch(multiply(37,false,false,false),0,32);
    for (unsigned i=0;i<offset;++i) tick();
    fp_return_tag=tag; fp_return_value=0x12345678; fp_return_valid=1;
    drain();
    CHECK(!fp_return_valid);
    dump(28,2176); CHECK(get(2176,4)==0x12345678);
    dump(24,2208); CHECK(mem[2208]==2);
    dump(16,2048); for (unsigned i=0;i<32;++i) CHECK(mem[2048+i]==6);
  }
  CHECK(fp_stalls > 0);
  // A younger memory replay flushes speculative stages, not accepted packed
  // results. Retry once and then verify both the old multiply and new load.
  launch(multiply(37,false,false,false),0,32);
  retry=1;
  unsigned old_retries=retries;
  launch((1u<<25)|(24u<<7)|7,0,1,0,Word(256));
  while (retries==old_retries) tick();
  retry=0;
  drain();
  dump(16,2048); for (unsigned i=0;i<32;++i) CHECK(mem[2048+i]==6);
  dump(24,2208); CHECK(mem[2208]==2);
  // Reset destroys an in-flight epoch, including its calendar reservations.
  before=beats; launch(multiply(37,false,false,false),0,32);
  while (beats < before+2) tick();
  reset=1; tick(); tick(); reset=0;
  for (unsigned i=0;i<12;++i) tick();
  CHECK(!active);
  check_case(0,37,false,0);
}
int main() {
  return run_test([] {
  reset = 1; schedule_enable = scalar_available = 1;
  tick(); tick(); reset = 0;
  unsigned cases = 0;
  for (unsigned sew = 0; (8u << sew) <= W; ++sew) {
    for (unsigned op : {36u,37u,38u,39u,41u,43u,45u,47u,56u,58u,59u,60u,61u,62u,63u}) {
      if (op >= 56 && (8u << sew) == W) continue;
      for (bool vx : {false,true}) {
        if (op == 62 && !vx) continue;
        for (unsigned variant=0;variant<5;++variant) { check_case(sew,op,vx,variant); ++cases; }
      }
    }
    for (bool vx : {false,true}) for (unsigned rm=0;rm<4;++rm) for (unsigned variant=0;variant<5;++variant) {
      check_case(sew,39,vx,variant,rm,true); ++cases;
    }
  }
  overlaps();
  std::printf("W%u: %u packed/scalar multiply cases, %u cycles\n",W,cases,cycles);
  });
}
