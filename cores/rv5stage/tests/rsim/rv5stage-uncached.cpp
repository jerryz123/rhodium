// Checks rv5stage-uncached arbitration and ordered memory effects.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
constexpr unsigned LOAD = 1, READ_NO_SNP = 4, COMP_DATA = 4;
void tick() {
  settle();
  rising();
  settle();
  falling();
  settle();
}

void present_read_data(uint128 data) {
  chi_in.pdat.presponse.pbits = {};
  chi_in.pdat.presponse.pbits.popcode = COMP_DATA;
  chi_in.pdat.presponse.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
      UINT64_C(4);
  write_bits(chi_in.pdat.presponse.pbits.pdata, 0, 128, data);
  chi_in.pdat.presponse.pvalid = UINT64_C(1);
}

void drive() {}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = UINT64_C(1);
  node_id = UINT64_C(5);
  {
    instruction_in = {};
    core_in = {};
    core_in.presponse.pready = UINT64_C(1);
    chi_in = {};
    tick();
    reset = UINT64_C(0);
    chi_in.preq.pready = UINT64_C(1);
    chi_in.pdat.prequest.pready = UINT64_C(1);

    // A presented data load wins arbitration over a simultaneous instruction
    // fetch and retains ownership of the eventual response.
    instruction_in.prequest.pvalid = UINT64_C(1);
    instruction_in.prequest.pbits.paddress = UINT64_C(49164);
    instruction_in.prequest.pbits.pcacheable = UINT64_C(0);
    instruction_in.prequest.pbits.pdevice = UINT64_C(0);
    instruction_in.presponse.pready = UINT64_C(0);
    core_in.prequest.pvalid = UINT64_C(1);
    core_in.prequest.pbits.prequest.paddress = UINT64_C(32768);
    core_in.prequest.pbits.prequest.paccess = LOAD;
    core_in.prequest.pbits.prequest.pwidth = UINT64_C(3);
    core_in.prequest.pbits.prequest.pbyte_umask = UINT64_C(255);
    core_in.prequest.pbits.pdevice = UINT64_C(1);
    settle();
    CHECK(core_out.prequest.pready && !instruction_out.prequest.pready &&
          !chi_out.preq.pvalid);
    tick();
    core_in.prequest.pvalid = UINT64_C(0);
    settle();
    CHECK(chi_out.preq.pvalid &&
          chi_out.preq.pbits.paddress == UINT64_C(32768) &&
          chi_out.preq.pbits.popcode == READ_NO_SNP &&
          chi_out.preq.pbits.psize_uor_unum_ureq == UINT64_C(3) &&
          chi_out.preq.pbits.pmem_uattr.pdevice &&
          !chi_out.preq.pbits.pmem_uattr.pcacheable &&
          !chi_out.preq.pbits.pmem_uattr.pallocate);
    tick();
    settle();
    CHECK(!chi_out.preq.pvalid && !instruction_out.prequest.pready);

    present_read_data(UINT64_C(0x123456789abcdef));
    settle();
    CHECK(core_out.presponse.pvalid &&
          core_out.presponse.pbits.pdata == UINT64_C(0x123456789abcdef) &&
          !instruction_out.presponse.pvalid);
    tick();
    chi_in.pdat.presponse.pvalid = UINT64_C(0);

    // The waiting fetch now emits one aligned four-byte ReadNoSnp. The
    // non-device, non-cacheable attributes match a BootROM PMA.
    settle();
    CHECK(instruction_out.prequest.pready && !chi_out.preq.pvalid);
    tick();
    instruction_in.prequest.pvalid = UINT64_C(0);
    settle();
    CHECK(chi_out.preq.pvalid &&
          chi_out.preq.pbits.paddress == UINT64_C(49164) &&
          chi_out.preq.pbits.ptgt_uid == UINT64_C(4) &&
          chi_out.preq.pbits.popcode == READ_NO_SNP &&
          chi_out.preq.pbits.psize_uor_unum_ureq == UINT64_C(2) &&
          !chi_out.preq.pbits.pmem_uattr.pdevice &&
          !chi_out.preq.pbits.pmem_uattr.pcacheable &&
          !chi_out.preq.pbits.pmem_uattr.pallocate);
    tick();
    present_read_data(((uint128(UINT64_C(0x44332211ccccdddd)) << 64) |
                       UINT64_C(0xaaaabbbb12345678)));
    settle();
    CHECK(instruction_out.presponse.pvalid &&
          instruction_out.presponse.pbits.pword == UINT64_C(0x44332211) &&
          !chi_out.pdat.presponse.pready && !core_out.presponse.pvalid);
    instruction_in.presponse.pready = UINT64_C(1);
    settle();
    CHECK(chi_out.pdat.presponse.pready);
    tick();
    chi_in.pdat.presponse.pvalid = UINT64_C(0);

    // Withdrawal before CHI acceptance cancels only the fetch owner. No reply
    // is owed, even when the outgoing request has been backpressured.
    chi_in.preq.pready = UINT64_C(0);
    instruction_in.prequest.pvalid = UINT64_C(1);
    instruction_in.prequest.pbits.paddress = UINT64_C(49152);
    tick();
    instruction_in.prequest.pvalid = UINT64_C(0);
    settle();
    CHECK(chi_out.preq.pvalid && !core_out.pdrained);
    instruction_in.pflush = UINT64_C(1);
    chi_in.preq.pready = UINT64_C(1);
    settle();
    CHECK(!chi_out.preq.pvalid);
    tick();
    instruction_in.pflush = UINT64_C(0);
    settle();
    CHECK(core_out.pdrained && !instruction_out.presponse.pvalid);

    // A flushed fetch still drains its already-issued CHI transaction but no
    // longer publishes the returned instruction word.
    instruction_in.prequest.pvalid = UINT64_C(1);
    instruction_in.prequest.pbits.paddress = UINT64_C(49152);
    settle();
    CHECK(instruction_out.prequest.pready && !chi_out.preq.pvalid);
    tick();
    instruction_in.prequest.pvalid = UINT64_C(0);
    settle();
    CHECK(chi_out.preq.pvalid);
    tick();
    instruction_in.pflush = UINT64_C(1);
    tick();
    instruction_in.pflush = UINT64_C(0);
    present_read_data(((uint128(UINT64_C(0xfeedfacedeadbeef)) << 64) |
                       UINT64_C(0x123456789abcdef)));
    settle();
    CHECK(chi_out.pdat.presponse.pready && !instruction_out.presponse.pvalid);
    tick();
    chi_in.pdat.presponse.pvalid = UINT64_C(0);
    settle();
    CHECK(core_out.pdrained);

    // One accepted, arbitrarily aligned zero request stays outstanding across
    // eight acknowledged writes, including backpressure at every CHI boundary.
    core_in.prequest.pvalid = UINT64_C(1);
    core_in.prequest.pbits.prequest.paddress = UINT64_C(49215);
    core_in.prequest.pbits.prequest.paccess = UINT64_C(6);
    core_in.prequest.pbits.prequest.pcontext.pwriteback = UINT64_C(0);
    core_in.prequest.pbits.pdevice = UINT64_C(0);
    tick();
    core_in.prequest.pvalid = UINT64_C(0);
    for (int word = 0; word < 8; word++) {
      chi_in.preq.pready = UINT64_C(0);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
        settle();
        CHECK(chi_out.preq.pvalid &&
              chi_out.preq.pbits.paddress ==
                  UINT64_C(49152) + ((word * 8) & low_mask(44)) &&
              chi_out.preq.pbits.popcode == UINT64_C(28) &&
              chi_out.preq.pbits.psize_uor_unum_ureq == UINT64_C(3) &&
              !chi_out.preq.pbits.pmem_uattr.pdevice &&
              !chi_out.preq.pbits.pmem_uattr.pcacheable && !core_out.pdrained &&
              !core_out.presponse.pvalid && !core_out.prequest.pready);
        tick();
      }
      chi_in.preq.pready = UINT64_C(1);
      tick();
      chi_in.prsp.presponse.pbits = {};
      chi_in.prsp.presponse.pbits.popcode = UINT64_C(6);
      chi_in.prsp.presponse.pbits.psrc_uid = UINT64_C(4);
      chi_in.prsp.presponse.pbits.pdbid_uor_ugroup_uid = UINT64_C(51);
      chi_in.prsp.presponse.pvalid = UINT64_C(1);
      settle();
      CHECK(chi_out.prsp.presponse.pready);
      tick();
      chi_in.prsp.presponse.pvalid = UINT64_C(0);
      chi_in.pdat.prequest.pready = UINT64_C(0);
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
        settle();
        CHECK(chi_out.pdat.prequest.pvalid &&
              read_bits(chi_out.pdat.prequest.pbits.pdata, 0, 128) == 0 &&
              chi_out.pdat.prequest.pbits.pbyte_uenable ==
                  (((word & 1) != 0) ? UINT64_C(65280) : UINT64_C(255)) &&
              chi_out.pdat.prequest.pbits.ptxn_uid == UINT64_C(51) &&
              !core_out.presponse.pvalid && !core_out.pdrained);
        tick();
      }
      chi_in.pdat.prequest.pready = UINT64_C(1);
      tick();
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
        CHECK(!core_out.presponse.pvalid && !core_out.pdrained);
        tick();
      }
      chi_in.prsp.presponse.pbits.popcode = UINT64_C(4);
      chi_in.prsp.presponse.pvalid = UINT64_C(1);
      settle();
      CHECK(core_out.presponse.pvalid == (word == 7) && !core_out.pdrained);
      tick();
      chi_in.prsp.presponse.pvalid = UINT64_C(0);
    }
    settle();
    CHECK(core_out.pdrained && !core_out.presponse.pvalid &&
          !chi_out.preq.pvalid);

    // Device writes must use Home-issued DBIDs, not direct write transfer.
    core_in.prequest.pvalid = UINT64_C(1);
    core_in.prequest.pbits.prequest.paddress = UINT64_C(32772);
    core_in.prequest.pbits.prequest.paccess = UINT64_C(2); // Store
    core_in.prequest.pbits.prequest.pwidth = UINT64_C(2);
    core_in.prequest.pbits.prequest.pbyte_umask = UINT64_C(240);
    core_in.prequest.pbits.prequest.pdata = UINT64_C(0x12345678);
    core_in.prequest.pbits.pdevice = UINT64_C(1);
    settle();
    CHECK(core_out.prequest.pready);
    tick();
    core_in.prequest.pvalid = 0;
    settle();
    CHECK(chi_out.preq.pvalid && chi_out.preq.pbits.popcode == UINT64_C(28) &&
          !chi_out.preq.pbits.psnp_uattr_uor_udo_udwt);
    tick();
    chi_in.prsp.presponse.pvalid = 1;
    chi_in.prsp.presponse.pbits = {};
    chi_in.prsp.presponse.pbits.popcode = UINT64_C(6); // DBIDResp
    chi_in.prsp.presponse.pbits.psrc_uid = UINT64_C(4);
    chi_in.prsp.presponse.pbits.pdbid_uor_ugroup_uid = UINT64_C(291);
    tick();
    chi_in.prsp.presponse.pvalid = 0;
    settle();
    CHECK(chi_out.pdat.prequest.pvalid &&
          chi_out.pdat.prequest.pbits.ptgt_uid == UINT64_C(4) &&
          chi_out.pdat.prequest.pbits.ptxn_uid == UINT64_C(291) &&
          chi_out.pdat.prequest.pbits.pbyte_uenable == UINT64_C(240) &&
          read_bits(chi_out.pdat.prequest.pbits.pdata, 0, 128) ==
              UINT64_C(0x1234567800000000));
    tick();
    chi_in.prsp.presponse.pvalid = 1;
    chi_in.prsp.presponse.pbits.popcode = UINT64_C(4); // Comp
    settle();
    CHECK(core_out.presponse.pvalid);
    tick();
    chi_in.prsp.presponse.pvalid = 0;
    settle();
    CHECK(core_out.pdrained);

    // Non-cacheable aliases of coherent RAM retain the physical snoop domain.
    for (int device = 0; device < 2; device++) {
      core_in.prequest.pvalid = 1;
      core_in.prequest.pbits.prequest.paddress = UINT64_C(4096);
      core_in.prequest.pbits.prequest.paccess = LOAD;
      core_in.prequest.pbits.prequest.pwidth = 3;
      core_in.prequest.pbits.pdevice = ((device)&low_mask(1));
      tick();
      core_in.prequest.pvalid = 0;
      settle();
      CHECK(chi_out.preq.pvalid && chi_out.preq.pbits.popcode == UINT64_C(3) &&
            chi_out.preq.pbits.ptgt_uid == 1 &&
            chi_out.preq.pbits.psnp_uattr_uor_udo_udwt &&
            !chi_out.preq.pbits.pmem_uattr.pcacheable &&
            !chi_out.preq.pbits.pmem_uattr.pallocate &&
            chi_out.preq.pbits.pmem_uattr.pdevice == ((device)&low_mask(1)));
      tick();
      present_read_data(UINT64_C(0xabcdef));
      chi_in.pdat.presponse.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
          1;
      tick();
      chi_in.pdat.presponse.pvalid = 0;
      core_in.prequest.pvalid = 1;
      core_in.prequest.pbits.prequest.paccess = 2;
      core_in.prequest.pbits.prequest.pbyte_umask = UINT64_C(255);
      tick();
      core_in.prequest.pvalid = 0;
      settle();
      CHECK(chi_out.preq.pvalid && chi_out.preq.pbits.popcode == UINT64_C(24) &&
            chi_out.preq.pbits.psnp_uattr_uor_udo_udwt &&
            !chi_out.preq.pbits.pmem_uattr.pallocate);
      tick();
      chi_in.prsp.presponse.pvalid = 1;
      chi_in.prsp.presponse.pbits = {};
      chi_in.prsp.presponse.pbits.popcode = 6;
      chi_in.prsp.presponse.pbits.psrc_uid = 1;
      chi_in.prsp.presponse.pbits.pdbid_uor_ugroup_uid = UINT64_C(564);
      tick();
      chi_in.prsp.presponse.pvalid = 0;
      settle();
      CHECK(chi_out.pdat.prequest.pvalid &&
            chi_out.pdat.prequest.pbits.ptxn_uid == UINT64_C(564) &&
            chi_out.pdat.prequest.pbits.ptgt_uid == 1);
      tick();
      chi_in.prsp.presponse.pvalid = 1;
      chi_in.prsp.presponse.pbits.popcode = 4;
      tick();
      chi_in.prsp.presponse.pvalid = 0;
      CHECK(core_out.pdrained);
    }

    throw Finished{};
  }
}

int main() {
  return run_test([] {
    cycle_limit = 1000000;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
