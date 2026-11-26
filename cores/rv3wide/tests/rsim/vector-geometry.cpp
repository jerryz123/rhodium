// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <cstring>
#include <deque>

using Row = decltype(row_memory_in.presponse.pbits.pdata);
constexpr unsigned bytes = sizeof(Row);
std::array<unsigned char, 2*bytes> source{}, result{};
std::deque<decltype(row_memory_in.presponse.pbits)> replies;
unsigned loads, stores;

void step() {
  CHECK(test_cycles<500);
  row_memory_in.prequest.pready=test_cycles%4!=2;
  row_memory_in.presponse.pvalid=!replies.empty() && test_cycles%5!=3;
  if (!replies.empty()) row_memory_in.presponse.pbits=replies.front();
  memory_in.pdrained=replies.empty();
  eval();
  if (row_memory_in.presponse.pvalid && row_memory_out.presponse.pready) replies.pop_front();
  if (row_memory_in.prequest.pready && row_memory_out.prequest.pvalid) {
    const auto& r=row_memory_out.prequest.pbits;
    auto& count=r.pstore ? stores : loads;
    CHECK(count<2 && r.paddress==(r.pstore ? 0x2000u : 0x1000u)+count*bytes);
    Row value{};
    if (r.pstore) std::memcpy(result.data()+count*bytes,&r.pdata,bytes);
    else std::memcpy(&value,source.data()+count*bytes,bytes);
    for (unsigned i=0;i<bytes;++i) CHECK((r.pbyte_umask>>i)&1);
    auto& reply = replies.emplace_back(); reply.pstore = r.pstore; reply.pdata = value; ++count;
  }
  CHECK(!pipeline_out.prequest.pvalid);
  tick_model();
}
void send(unsigned insn, unsigned address=0) {
  instruction=insn; scalar=address; certificate_valid=1; pc+=4; request_valid=1;
  for (;;) { eval(); bool fire=request_ready; step(); if (fire) break; }
  request_valid=0;
}
int main() {
  return run_test([] {
    for (unsigned i=0;i<source.size();++i) source[i]=static_cast<unsigned char>(0xf0+3*i);
    reset=1; step(); reset=0; vl=2*bytes;
    send(0x02000407,0x1000);             // vle8.v v8
    start_index=1; vl=2*bytes-1;
    send((1u<<25)|(8u<<20)|(3u<<15)|(3u<<12)|(8u<<7)|0x57); // vadd.vi v8,v8,3
    start_index=0; vl=2*bytes;
    send(0x02000427,0x2000);             // vse8.v v8
    do { step(); eval(); } while (active || !replies.empty());
    CHECK(loads==2 && stores==2);
    for (unsigned i=0;i<result.size();++i)
      CHECK(result[i]==static_cast<unsigned char>(source[i]+(i && i+1<result.size() ? 3 : 0)));
  });
}
