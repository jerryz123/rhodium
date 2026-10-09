// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "wide.hpp"
#include "../../../../chi/tests/rsim/request.hpp"
extern "C" void compack_bind();
extern "C" void compack_sample(unsigned reset, unsigned command,
                               unsigned instruction_rom, unsigned dfire,
                               unsigned dpacket, unsigned ifire,
                               unsigned ipacket, unsigned dack, unsigned ddbid,
                               unsigned iack, unsigned idbid, unsigned stalled,
                               unsigned dfinish, unsigned ifinish);
extern "C" void compack_check();
extern "C" void compack_finish();
using rsp_t = std::remove_cvref_t<decltype(dack_out)>;
void tick() {
  eval();

  compack_sample(unsigned(reset), unsigned(command_valid && command_ready),
                 unsigned(instruction_rom),
                 unsigned(ddata_in.pvalid && ddata_out.pready),
                 unsigned(ddata_in.pbits.pdata_uid),
                 unsigned(idata_in.pvalid && idata_out.pready),
                 unsigned(idata_in.pbits.pdata_uid),
                 unsigned(dack_out.pvalid && dack_in.pready),
                 unsigned(dack_out.pbits.ptxn_uid),
                 unsigned(iack_out.pvalid && iack_in.pready),
                 unsigned(iack_out.pbits.ptxn_uid),
                 unsigned((dack_out.pvalid && !dack_in.pready) ||
                          (iack_out.pvalid && !iack_in.pready)),
                 unsigned(d_complete && completion_ready),
                 unsigned(i_complete && completion_ready));
  tick_model();
  compack_check();
}
void clear() {
  reset = 1;
  command_valid = 0;
  completion_ready = 0;
  dreq_in = {};
  ireq_in = {};
  dack_in = {};
  iack_in = {};
  ddata_in = {};
  idata_in = {};
  tick();
  reset = 0;
  tick();
  CHECK(!dack_out.pvalid && !iack_out.pvalid && !d_complete && !i_complete);
}
void start(bool rom) {
  instruction_rom = rom;
  command_valid = 1;
  eval();
  CHECK(command_ready);
  tick();
  command_valid = 0;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick();
  CHECK(dreq_out.pvalid && ireq_out.pvalid &&
        dreq_out.pbits.popcode == UINT64_C(2) &&
        ireq_out.pbits.popcode == (rom ? UINT64_C(4) : UINT64_C(3)));
  dreq_in.pready = 1;
  ireq_in.pready = 1;
  tick();
  dreq_in.pready = 0;
  ireq_in.pready = 0;
}
void packet(int index) {
  ddata_in = {};
  idata_in = {};
  ddata_in.pvalid = 1;
  ddata_in.pbits.popcode = 4;
  ddata_in.pbits.ptgt_uid = 3;
  ddata_in.pbits.psrc_uid = 1;
  ddata_in.pbits.phome_unid_uor_upbha_uor_umismatched_umecid = 1;
  ddata_in.pbits.pdbid_uor_umecid = UINT64_C(2748);
  ddata_in.pbits.pdata_uid = ((index)&low_mask(2));
  ddata_in.pbits.pdata = wide(UINT64_C(305419896));
  idata_in = ddata_in;
  idata_in.pbits.ptgt_uid = 2;
  idata_in.pbits.psrc_uid = instruction_rom ? 4 : 1;
  idata_in.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
      instruction_rom ? 4 : 1;
  eval();
  CHECK(ddata_out.pready && idata_out.pready);
  tick();
  ddata_in.pvalid = 0;
  idata_in.pvalid = 0;
}
void line(bool reverse_order) {
  for (int index = 0; index < 4; ++index) {
    CHECK(!dack_out.pvalid && !iack_out.pvalid);
    packet(reverse_order ? 3 - index : (index + 1) % 4);
    for (unsigned repeat_index = 0; repeat_index < (index % 3); ++repeat_index)
      tick();
  }
}
void acknowledge() {
  rsp_t held_d, held_i;
  held_d = dack_out;
  held_i = iack_out;
  CHECK(held_d.pvalid && held_d.pbits.popcode == 2 &&
        held_d.pbits.ptxn_uid == UINT64_C(2748) &&
        held_i.pvalid == !instruction_rom &&
        (instruction_rom || (held_i.pbits.popcode == 2 &&
                             held_i.pbits.ptxn_uid == UINT64_C(2748))));
  for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index) {
    tick();
    CHECK((dack_out.pvalid == held_d.pvalid &&
           same_response(dack_out.pbits, held_d.pbits)) &&
          (iack_out.pvalid == held_i.pvalid &&
           same_response(iack_out.pbits, held_i.pbits)));
  }
  dack_in.pready = 1;
  tick();
  dack_in.pready = 0;
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
    tick();
  if (!instruction_rom) {
    CHECK((iack_out.pvalid == held_i.pvalid &&
           same_response(iack_out.pbits, held_i.pbits)));
    iack_in.pready = 1;
    tick();
    iack_in.pready = 0;
  }
  for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
    tick();
    CHECK(d_complete && i_complete && !dack_out.pvalid && !iack_out.pvalid &&
          !command_ready);
  }
  completion_ready = 1;
  tick();
  completion_ready = 0;
}
int main() {
  return run_test([] {
    reset = 1;
    command_valid = 0;
    instruction_rom = 0;
    completion_ready = 0;
    compack_bind();
    clear();

    start(0);
    packet(3);
    packet(1);
    clear();
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
      start(0);
      line(1);
      for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
        tick();
      clear();
    }

    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index) {
      start(0);
      line(1);
      acknowledge();
      start(1);
      line(0);
      acknowledge();
      start(0);
      line(0);
      acknowledge();
    }
    tick();
    compack_finish();
  });
}
