// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../../events/collector.h"
#include <algorithm>
#include <array>
#include <vector>

// Authored integer-vector stream shared by the native oracle test and real-core
// passivity fixture. Expected writes are generated independently of RTL/Sail.
namespace vector_test {
namespace o = rhodium::cosim::observation;
struct Program {
  std::vector<o::Record> records;
  std::array<std::array<std::uint8_t, 16>, 32> registers{};
  std::array<std::uint64_t,32> gprs{};
  std::array<std::uint8_t,4096> memory{};
  static constexpr std::uint64_t base = 0x80000000, data_base = base + 2048;
  unsigned vl = 0, vtype = 1U << 31, start = 0, vcsr = 0, status = 0;
  o::Record& add(unsigned insn) {
    const auto n = records.size();
    records.push_back({0, 0, n, {0, n}, o::Instruction{4*n,insn,4,4,{3,false},0},
      o::Retirement{4*(n+1),{3,false}}, {}, {}});
    return records.back();
  }
  void snapshots() {
    auto& r = records.back();
    unsigned i = 0;
    for (auto [address, value] : std::array<std::pair<unsigned,std::uint64_t>,4>{{
      {0xc20,vl}, {0xc21,vtype == (1U<<31) ? 1ULL<<63 : vtype}, {8,start}, {15,vcsr}}})
      r.effects[{1,i++}] = o::CsrUpdate{address,o::CsrOperation::AssignMasked,UINT64_MAX,value};
    r.effects[{1,4}]=o::CsrUpdate{0x300,o::CsrOperation::AssignMasked,(1ULL<<63)|0x600,
      (std::uint64_t(status)<<9) | (status==3 ? 1ULL<<63 : 0)};
  }
  void scalar(unsigned insn, unsigned rd = 0, std::uint64_t value = 0) {
    auto& r = add(insn);
    if (rd) { r.effects[{0,0}] = o::RegisterWrite{o::Bank::Integer,rd,0,UINT64_MAX,value}; gprs[rd]=value; }
    snapshots();
  }
  void configure(unsigned avl, unsigned sew, unsigned lmul = 0, bool agnostic = false) {
    scalar((avl<<20) | 0x93,1,avl);
    vtype = (sew<<3) | lmul | (agnostic ? 192 : 0);
    const auto max = ((lmul < 4 ? 16U << lmul : 16U >> (8-lmul)) >> sew);
    vl = std::min(avl,max); start = 0; status=3;
    scalar((vtype<<20) | (1<<15) | (7<<12) | (2<<7) | 0x57,2,vl);
  }
  void compute(unsigned op, unsigned vd, unsigned vs2, unsigned operand, unsigned form = 3, bool masked = false) {
    auto& r = add((op<<26) | (!masked<<25) | (vs2<<20) | (operand<<15) | (form<<12) | (vd<<7) | 0x57);
    const unsigned bytes = 1U << ((vtype>>3)&7);
    unsigned effect = 0;
    // Snapshot masks/sources: destination may alias either source, including v0.
    const auto old = registers;
    for (unsigned e = start; e < vl; ++e) {
      if (masked && !(old[0][e/8] & (1U<<(e%8)))) continue;
      const unsigned offset=e*bytes, reg=vd+offset/16, in=offset%16;
      auto read = [&](unsigned base) {
        std::uint64_t v=0;
        for (unsigned b=0;b<bytes;++b) v |= std::uint64_t(old[base+offset/16][in+b])<<(8*b);
        return v;
      };
      const auto a=read(vs2);
      const std::uint64_t b=form==0 ? read(operand) : form==4 ? gprs[operand] : std::uint64_t(std::int64_t(operand<16 ? int(operand) : int(operand)-32));
      const auto value=op==23 ? b : op==0 ? a+b : op==2 ? a-b : op==9 ? a&b : op==10 ? a|b : a^b;
      const std::uint64_t mask=bytes==8 ? UINT64_MAX : (1ULL<<(8*bytes))-1;
      r.effects[{5,effect++}]=o::RegisterWrite{o::Bank::Vector,reg,in*8,mask,value};
      for (unsigned j=0;j<bytes;++j) registers[reg][in+j]=value>>(8*j);
    }
    start=0;
    snapshots();
  }
  void access(bool store, unsigned reg, unsigned eew, unsigned offset, bool masked = false) {
    scalar((offset << 20) | (5<<15) | (6<<7) | 0x13,6,data_base+offset);
    auto& r=add((!masked<<25) | (6<<15) | ((eew ? eew+4 : 0)<<12) | (reg<<7) | (store ? 0x27 : 7));
    const auto old=registers;
    unsigned effect=0;
    for(unsigned e=start;e<vl;++e) {
      if(masked && !(old[0][e/8] & (1U<<(e%8)))) continue;
      for(unsigned byte=0;byte<(1U<<eew);++byte) {
        const auto index=e*(1U<<eew)+byte;
        const auto address=data_base+offset+index;
        auto& value=memory[address-base];
        auto& dest=registers[reg+index/16][index%16];
        if(store) value=dest; else dest=value;
        r.effects[{5,effect++}]=o::MemoryEffect{e,byte,store ? o::AccessKind::Store : o::AccessKind::Load,address,false,0,1,!store,store,store ? 0U : value,store ? value : 0U,o::AccessResult::Success};
        if(!store) r.effects[{5,effect++}]=o::RegisterWrite{o::Bank::Vector,reg+index/16,8*(index%16),255,value};
      }
    }
    start=0; snapshots();
  }
  Program(bool with_memory = false) {
    for(unsigned i=2048;i<memory.size();++i) memory[i]=(i*37+11)&255;
    scalar(0x20000093,1,0x200); // VS=Initial; configuration must dirty it.
    status=1;
    scalar(0x3000a073);
    configure(16,0);
    compute(23,0,0,21); // v0 bytes=0xf5: mixed active and inactive elements.
    compute(23,4,0,3);
    compute(0,6,4,4); // independent destinations allow overlapping instructions.
    compute(0,8,4,6,0);
    configure(11,0,0,true);
    compute(11,8,8,7,3,true);
    start=3; scalar(0x0081d073); // csrrwi x0,vstart,3.
    compute(0,8,8,1,3,true);
    configure(0,0);
    compute(23,8,0,9); // empty instruction must still close its producer.
    for (unsigned sew=1;sew<=3;++sew) {
      configure(16>>sew,sew);
      compute(23,10,0,15);
      compute(0,12,10,10,0);
      compute(2,12,12,10,0);
      compute(9,12,12,3);
      compute(10,12,12,4);
    }
    configure(32,0,1); // LMUL=2 crosses an architectural register boundary.
    compute(23,16,0,2);
    compute(0,18,16,5);
    configure(7,0,7,true); // fractional LMUL, with undisturbed physical tail.
    compute(23,20,0,6);
    scalar(0x00400093,1,4);
    scalar(0x01000193,3,16);
    vl=4; vtype=16; start=0;
    scalar(0x8030f157,2,4); // vsetvl x2,x1,x3: e32,m1.
    compute(23,22,0,3,4);
    compute(0,22,22,1,4);
    compute(23,0,0,0);
    compute(0,22,22,1,3,true); // all masked: no VRF effects, but an owned completion.
    start=5; scalar(0x0082d073);
    compute(0,22,22,1); // vstart beyond vl likewise closes without writes.
    scalar(0x00f3d073); vcsr=7; // csrrwi x0,vcsr,7.
    records.back().effects[{1,3}]=o::CsrUpdate{15,o::CsrOperation::AssignMasked,UINT64_MAX,7};
    scalar(0x00100093,1,1); // drain/snapshot boundary.
    if(with_memory) {
      scalar(0x00100293,5,1);
      scalar(0x01f29293,5,base); // slli x5,x5,31.
      scalar(0x40028293,5,base+1024);
      scalar(0x40028293,5,data_base);
      configure(16,0); compute(23,0,0,21);
      for(unsigned eew=0;eew<4;++eew) {
        configure(16>>eew,eew);
        compute(23,24,0,7); // Initialize inactive bytes before masked loads.
        access(false,24,eew,1U<<eew,true); // XLEN-unaligned, element aligned.
        access(false,26,eew,32); // Independent younger load may overlap completion.
        access(true,24,eew,128+(1U<<eew),true);
        access(false,26,eew,128+(1U<<eew));
        configure((16>>eew)-1,eew,0,true);
        access(false,28,eew,32+(1U<<eew));
        access(true,28,eew,192+(1U<<eew));
      }
      configure(16,0);
      start=3; scalar(0x0081d073);
      access(false,24,0,65);
      compute(23,0,0,0);
      access(false,24,0,65,true); // No requests or writes.
      configure(0,0); access(true,24,0,65);
      scalar(0x00100093,1,1);
    }
  }
};
}
