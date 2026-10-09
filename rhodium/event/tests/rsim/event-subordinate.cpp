// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "wide.hpp"
extern "C" void event_subordinate_bind();
extern "C" void event_subordinate_sample(unsigned reset, unsigned req_fire,
                                         unsigned req_opcode, unsigned req_txn,
                                         unsigned rsp_fire, unsigned rsp_opcode,
                                         unsigned rsp_txn, unsigned dat_fire,
                                         unsigned dat_txn, unsigned write_fire,
                                         unsigned write_opcode,
                                         unsigned stalled);
extern "C" void event_subordinate_check();
extern "C" void event_subordinate_finish();
void tick() {
  eval();

  event_subordinate_sample(
      unsigned(reset), unsigned(port_in.preq.pvalid && port_out.preq.pready),
      unsigned(port_in.preq.pbits.popcode),
      unsigned(port_in.preq.pbits.ptxn_uid),
      unsigned(port_out.prsp.pvalid && port_in.prsp.pready),
      unsigned(port_out.prsp.pbits.popcode),
      unsigned(port_out.prsp.pbits.ptxn_uid),
      unsigned(port_out.pdat.presponse.pvalid && port_in.pdat.presponse.pready),
      unsigned(port_out.pdat.presponse.pbits.ptxn_uid),
      unsigned(port_in.pdat.prequest.pvalid && port_out.pdat.prequest.pready),
      unsigned(port_in.pdat.prequest.pbits.popcode),
      unsigned(
          (port_out.prsp.pvalid && !port_in.prsp.pready) ||
          (port_out.pdat.presponse.pvalid && !port_in.pdat.presponse.pready)));
  tick_model();
  event_subordinate_check();
}
void clear() {
  reset = 1;
  port_in = {};
  write_ready = 0;
  tick();
  reset = 0;
  tick();
  CHECK(!port_out.prsp.pvalid && !port_out.pdat.presponse.pvalid);
}
void credit() {
  port_in.preq = {};
  port_in.preq.pvalid = 1;
  port_in.pdat.prequest = {};
  port_in.pdat.prequest.pvalid = 1;
  eval();
  CHECK(port_out.preq.pready && port_out.pdat.prequest.pready);
  tick();
  port_in.preq = {};
  port_in.pdat.prequest = {};
}
void request(bool write_request) {
  port_in.preq = {};
  port_in.preq.pbits.popcode = write_request ? UINT64_C(29) : UINT64_C(4);
  port_in.preq.pbits.ptxn_uid = UINT64_C(2748);
  port_in.preq.pbits.psrc_uid = 3;
  port_in.preq.pbits.ptgt_uid = 13;
  port_in.preq.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget = 3;
  port_in.preq.pbits.preturn_utxn_uid_uor_ustash_ulpid = UINT64_C(2748);
  port_in.preq.pbits.psize_uor_unum_ureq = 3;
  port_in.preq.pvalid = 1;
  eval();
  CHECK(port_out.preq.pready);
  tick();
  port_in.preq = {};
}
void accept_rsp(std::uint8_t opcode) {
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
    tick();
  CHECK(port_out.prsp.pvalid && port_out.prsp.pbits.popcode == opcode &&
        port_out.prsp.pbits.ptxn_uid == UINT64_C(2748));
  port_in.prsp.pready = 1;
  tick();
  port_in.prsp.pready = 0;
}
void write_data() {
  port_in.pdat.prequest = {};
  port_in.pdat.prequest.pbits.popcode = 3;
  port_in.pdat.prequest.pbits.psrc_uid = 3;
  port_in.pdat.prequest.pbits.ptgt_uid = 13;
  port_in.pdat.prequest.pvalid = 1;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    tick();
    CHECK(!port_out.pdat.prequest.pready);
  }
  write_ready = 1;
  tick();
  write_ready = 0;
  port_in.pdat.prequest = {};
}
int main() {
  return run_test([] {
    reset = 1;
    write_ready = 0;
    event_subordinate_bind();
    port_in = {};
    clear();

    for (int phase = 0; phase < 4; ++phase) {
      request(phase != 0);
      if (phase >= 2)
        accept_rsp(6);
      if (phase == 3)
        write_data();
      credit();
      clear();
    }

    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index) {
      credit();
      request(0);
      credit();
      for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
        tick();
      CHECK(port_out.pdat.presponse.pvalid &&
            port_out.pdat.presponse.pbits.popcode == 4 &&
            port_out.pdat.presponse.pbits.ptxn_uid == UINT64_C(2748) &&
            wide_value(port_out.pdat.presponse.pbits.pdata) == UINT64_C(42));
      port_in.pdat.presponse.pready = 1;
      tick();
      port_in.pdat.presponse.pready = 0;
      request(1);
      credit();
      accept_rsp(6);
      credit();
      write_data();
      credit();
      accept_rsp(4);
    }
    tick();
    event_subordinate_finish();
  });
}
