// Checks rv5stage-io-mshr arbitration and ordered memory effects.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
int accepted = 0, completed = 0, requests = 0, writes = 0, outstanding = 0;
void tick() {
  rising();
  falling();
}

void read_data(uint128 data) {
  chi_in.pdat.presponse.pvalid = 1;
  chi_in.pdat.presponse.pbits = {};
  chi_in.pdat.presponse.pbits.popcode = UINT64_C(4);
  chi_in.pdat.presponse.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
      UINT64_C(4);
  write_bits(chi_in.pdat.presponse.pbits.pdata, 0, 128, data);
}

void fetch() {
  instruction_in.prequest.pvalid = 1;
  instruction_in.prequest.pbits.paddress = UINT64_C(49164);
  settle();
  CHECK(instruction_out.prequest.pready);
  tick();
  instruction_in.prequest.pvalid = 0;
}

void contended_load(std::uint8_t flush_fetch,
                    std::uint16_t expected_writeback) {
  int initial_requests, initial_accepted, initial_completed;
  initial_requests = requests;
  initial_accepted = accepted;
  initial_completed = completed;
  fetch();
  tick(); // Fetch reaches CHI; its response is delayed.
  core_in.prequest.pvalid = 1;
  core_in.prequest.pbits = {};
  core_in.prequest.pbits.pmemory.paddress = UINT64_C(32772);
  core_in.prequest.pbits.pmemory.paccess = UINT64_C(1);
  core_in.prequest.pbits.pmemory.pwidth = UINT64_C(2);
  core_in.prequest.pbits.pmemory.pbyte_umask = UINT64_C(240);
  core_in.prequest.pbits.pmemory.punsigned = 1;
  core_in.prequest.pbits.pmemory.pcontext.pwriteback = expected_writeback;
  settle();
  CHECK(core_out.prequest.pready && !core_out.prequest_uaccess_ufault);
  CHECK(!core_out.pdrained);
  tick();
  // Live WB inputs may change after acceptance. Neither another IO request
  // nor a cached request may replace or pass the retained operation.
  core_in.prequest.pbits = {};
  core_in.prequest.pbits.pmemory.paddress = UINT64_C(32784);
  core_in.prequest.pbits.pmemory.paccess = UINT64_C(2);
  for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
    settle();
    CHECK(!core_out.prequest.pready && !core_out.pdrained &&
          !chi_out.preq.pvalid);
    tick();
  }
  core_in.prequest.pbits.pmemory.paddress = UINT64_C(4096);
  instruction_in.pflush = flush_fetch;
  tick();
  instruction_in.pflush = 0;
  instruction_in.presponse.pready = 0;
  read_data(((uint128(UINT64_C(0x806700000000)) << 64) |
             UINT64_C(0))); // jalr fetch response
  settle();
  CHECK(!core_out.presponse.pvalid && !core_out.prequest.pready &&
        !cache_out.prequest.pvalid &&
        instruction_out.presponse.pvalid == !flush_fetch &&
        chi_out.pdat.presponse.pready == flush_fetch);
  if (!flush_fetch) {
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    instruction_in.presponse.pready = 1;
  }
  tick();
  chi_in.pdat.presponse.pvalid = 0;
  // A new fetch is eligible, but the retained data operation must win.
  instruction_in.prequest.pvalid = 1;
  settle();
  CHECK(!instruction_out.prequest.pready && !core_out.prequest.pready);
  tick();
  chi_in.preq.pready = 0;
  for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
    settle();
    CHECK(chi_out.preq.pvalid &&
          chi_out.preq.pbits.paddress == UINT64_C(32772) &&
          chi_out.preq.pbits.popcode == UINT64_C(4) &&
          chi_out.preq.pbits.psize_uor_unum_ureq == UINT64_C(2) &&
          chi_out.preq.pbits.pmem_uattr.pdevice &&
          !chi_out.preq.pbits.pmem_uattr.pcacheable &&
          !chi_out.preq.pbits.pmem_uattr.pallocate && !core_out.pdrained);
    tick();
  }
  chi_in.preq.pready = 1;
  tick();
  instruction_in.prequest.pvalid = 0;
  core_in.prequest.pvalid = 0;
  // A data-owned transaction is not canceled by a subsequent fetch flush.
  instruction_in.pflush = 1;
  tick();
  instruction_in.pflush = 0;
  read_data(UINT64_C(0x8000000000000000));
  core_in.presponse.pready = 0;
  settle();
  CHECK(core_out.presponse.pvalid &&
        core_out.presponse.pbits.pdata == UINT64_C(0x80000000) &&
        core_out.presponse.pbits.pcontext.pwriteback == expected_writeback &&
        !core_out.pdrained);
  for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
    CHECK(!chi_out.pdat.presponse.pready && completed == initial_completed);
    tick();
    settle();
    CHECK(core_out.presponse.pvalid &&
          core_out.presponse.pbits.pdata == UINT64_C(0x80000000));
  }
  core_in.presponse.pready = 1;
  tick();
  chi_in.pdat.presponse.pvalid = 0;
  for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
    tick();
  CHECK(core_out.pdrained && requests == initial_requests + 2 &&
        accepted == initial_accepted + 1 && completed == initial_completed + 1);
}

void drive() {}

void observe() {
  {
    if (reset) {
      accepted = 0;
      completed = 0;
      requests = 0;
      writes = 0;
      outstanding = 0;
    } else {
      if (core_in.prequest.pvalid && core_out.prequest.pready &&
          !core_out.prequest_uaccess_ufault)
        accepted++;
      if (core_out.presponse.pvalid && core_in.presponse.pready)
        completed++;
      if (chi_out.preq.pvalid && chi_in.preq.pready) {
        requests++;
        outstanding++;
      }
      if (chi_out.pdat.prequest.pvalid && chi_in.pdat.prequest.pready)
        writes++;
      if (chi_in.pdat.presponse.pvalid && chi_out.pdat.presponse.pready)
        outstanding--;
      if (chi_in.prsp.presponse.pvalid && chi_out.prsp.presponse.pready &&
          chi_in.prsp.presponse.pbits.popcode == UINT64_C(4))
        outstanding--;
      CHECK(outstanding >= 0 && outstanding <= 1 && completed <= accepted);
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  node_id = UINT64_C(5);
  {
    core_in = {};
    core_in.presponse.pready = UINT64_C(1);
    cache_in = {};
    instruction_in = {};
    chi_in = {};
    tick();
    reset = 0;
    cache_in.prequest.pready = 1;
    cache_in.pdrained = 1;
    chi_in.preq.pready = 1;
    chi_in.pdat.prequest.pready = 1;

    contended_load(0, memory_fp(UINT64_C(17), UINT64_C(1)));
    contended_load(
        1, (field(UINT64_C(3), 2, 7) |
            field(UINT64_C(15), 7,
                  0))); // Four-bit slot must survive retained RN-I ownership.
    contended_load(0, memory_integer(UINT64_C(31)));
    contended_load(1, UINT64_C(0));

    // Queue a store behind an unissued fetch, then cancel only the fetch.
    chi_in.preq.pready = 0;
    fetch();
    core_in.prequest.pvalid = 1;
    core_in.prequest.pbits = {};
    core_in.prequest.pbits.pmemory.paddress = UINT64_C(32772);
    core_in.prequest.pbits.pmemory.paccess = UINT64_C(2);
    core_in.prequest.pbits.pmemory.pwidth = UINT64_C(2);
    core_in.prequest.pbits.pmemory.pbyte_umask = UINT64_C(240);
    core_in.prequest.pbits.pmemory.pdata = UINT64_C(0x12345678);
    settle();
    CHECK(core_out.prequest.pready);
    tick();
    core_in.prequest = {};
    instruction_in.pflush = 1;
    tick();
    instruction_in.pflush = 0;
    tick();
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
      CHECK(chi_out.preq.pvalid &&
            chi_out.preq.pbits.paddress == UINT64_C(32772) &&
            chi_out.preq.pbits.popcode == UINT64_C(28) && !core_out.pdrained);
      tick();
    }
    chi_in.preq.pready = 1;
    tick();
    chi_in.prsp.presponse.pvalid = 1;
    chi_in.prsp.presponse.pbits = {};
    chi_in.prsp.presponse.pbits.popcode = UINT64_C(6);
    chi_in.prsp.presponse.pbits.psrc_uid = UINT64_C(4);
    chi_in.prsp.presponse.pbits.pdbid_uor_ugroup_uid = UINT64_C(291);
    tick();
    chi_in.prsp.presponse.pvalid = 0;
    chi_in.pdat.prequest.pready = 0;
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
      CHECK(chi_out.pdat.prequest.pvalid &&
            chi_out.pdat.prequest.pbits.ptxn_uid == UINT64_C(291) &&
            chi_out.pdat.prequest.pbits.pbyte_uenable == UINT64_C(240) &&
            read_bits(chi_out.pdat.prequest.pbits.pdata, 0, 128) ==
                UINT64_C(0x1234567800000000) &&
            !core_out.pdrained);
      tick();
    }
    chi_in.pdat.prequest.pready = 1;
    tick();
    for (int repeat_index = 0; repeat_index < (6); ++repeat_index) {
      CHECK(!core_out.pdrained && !core_out.presponse.pvalid &&
            !core_out.prequest.pready);
      tick();
    }
    chi_in.prsp.presponse.pbits.popcode = UINT64_C(4);
    chi_in.prsp.presponse.pvalid = 1;
    settle();
    CHECK(core_out.presponse.pvalid &&
          core_out.presponse.pbits.pcontext.pwriteback == UINT64_C(0));
    tick();
    chi_in.prsp.presponse.pvalid = 0;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    CHECK(core_out.pdrained && accepted == 5 && completed == 5 &&
          requests == 9 && writes == 1);

    // Reset clears a queued slot along with the shared transaction engine.
    fetch();
    tick();
    core_in.prequest.pvalid = 1;
    core_in.prequest.pbits.pmemory.paddress = UINT64_C(32768);
    core_in.prequest.pbits.pmemory.paccess = UINT64_C(1);
    tick();
    reset = 1;
    core_in = {};
    core_in.presponse.pready = UINT64_C(1);
    tick();
    reset = 0;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    CHECK(core_out.pdrained && core_out.prequest.pready &&
          !core_out.presponse.pvalid && !chi_out.preq.pvalid);

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
