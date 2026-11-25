// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <cstring>

int main() {
  return run_test([] {
    reset=1; tick_model(); reset=0;
    for (unsigned row=0;row<5;++row) {
      auto &w=execute_writeback_in.pbits;
      w.paddress=row;
      std::memset(&w.pmask,0xff,sizeof(w.pmask));
      std::memset(&w.pdata,row+1,sizeof(w.pdata));
      execute_writeback_in.pvalid=1; eval(); tick_model();
    }
    execute_writeback_in.pvalid=0;
    auto &r=execute_read_in.pbits;
    r.pleft=1; r.psecond=2; r.pthird=3; r.pmask_urow=0; r.preads_uv0=1;
    r.psources.pvector_ulhs=r.psources.pvector_urhs=r.psources.paccumulator=1;
    execute_read_in.pvalid=row_read_in.pvalid=1; row_read_in.pbits=4;
    auto matches=[](const auto &data,unsigned byte) {
      const auto* p=reinterpret_cast<const unsigned char*>(&data);
      for (unsigned i=0;i<sizeof(data);++i) CHECK(p[i]==byte);
    };
    // An idle-port grant must not change which demand client wins next.
    for(bool borrow:{false,true}) {
      execute_read_in.pvalid=1; row_read_in.pvalid=0; eval(); tick_model(); // Give VS3 the next contested turn.
      execute_read_in.pvalid=0; lookahead_in.pvalid=borrow; lookahead_in.pbits=2;
      eval(); CHECK(lookahead_out.pready); tick_model();
      row_read_in.pvalid=execute_read_in.pvalid=lookahead_in.pvalid=1;
      eval(); CHECK(row_read_out.pready && !execute_read_out.pready && !lookahead_out.pready);
      CHECK(bool(lookahead_result_out.pvalid)==borrow && !row_result_out.pvalid);
      if(borrow) matches(lookahead_result_out.pbits,3);
      tick_model(); eval(); CHECK(row_result_out.pvalid && !lookahead_result_out.pvalid);
      matches(row_result_out.pbits,5);
      CHECK(execute_read_out.pready && !row_read_out.pready && !lookahead_out.pready);
      tick_model(); lookahead_in.pvalid=0;
    }
    auto step=[&](bool exec,bool store) {
      eval(); CHECK(bool(execute_read_out.pready)==exec && bool(row_read_out.pready)==store);
      tick_model(); eval();
      CHECK(bool(execute_result_out.pvalid)==exec && bool(row_result_out.pvalid)==store);
      if (store) matches(row_result_out.pbits,5);
      if (exec) {
        const auto &ops=execute_result_out.pbits;
        matches(ops.pdata[0],2); matches(ops.pdata[1],r.psecond+1);
        if (r.psources.pvector_urhs) matches(ops.pdata[2],4);
        matches(ops.ppredicate,1);
      }
    };
    // Both continuously requesting clients alternate grants.
    for (unsigned i=0;i<3;++i) { step(false,true); step(true,false); }
    // The scalar multiplicand is broadcast. Port 1 now reads old vd, and
    // both independently accepted data rows return on the same clock.
    r.psources.pvector_urhs=0; r.psecond=3; step(true,true); step(true,true);

    // All three predicate users select independently. Forward a partial write
    // to the selected row only, and retain the synchronous readers' snapshots.
    memory_mask_0_in.psample.pvalid=memory_mask_1_in.psample.pvalid=1;
    auto &w=execute_writeback_in.pbits;
    execute_writeback_in.pvalid=1;
    for (unsigned changed=0;changed<2;++changed) {
      w.paddress=changed;
      std::memset(&w.pmask,0x0f,sizeof(w.pmask));
      std::memset(&w.pdata,0x5a+changed,sizeof(w.pdata));
      memory_mask_0_in.psample.pbits=changed; memory_mask_1_in.psample.pbits=1-changed;
      r.pmask_urow=0;
      eval(); CHECK(execute_read_out.pready && row_read_out.pready);
      matches(memory_mask_0_out.pdata,10+changed); matches(memory_mask_1_out.pdata,changed ? 10 : 2);
      tick_model();
      r.pmask_urow=1;
      eval(); CHECK(execute_result_out.pvalid);
      matches(execute_result_out.pbits.ppredicate,10);
    }
  });
}
