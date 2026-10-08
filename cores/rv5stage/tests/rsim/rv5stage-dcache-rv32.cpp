// Preserves the rv5stage-dcache-rv32 cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
#include "amo.hpp"
// Checks RV32 buffered-store lanes, AMOs, byte blocks, delayed LR/SC, and reservation bounds.

// Supplies an independent scalar arithmetic oracle for cache-level AMO sweeps.

int requests = 0;

int responses = 0;

int acknowledgements = 0;

std::uint8_t expected_opcode = UINT64_C(7);

std::uint64_t expected_address = UINT64_C(4096);

void tick() {
  if (!reset) {
    if (chi_out.prequests.pvalid && chi_in.prequests.pready) {
      CHECK(chi_out.prequests.pbits.popcode == expected_opcode &&
            chi_out.prequests.pbits.paddress == expected_address &&
            chi_out.prequests.pbits.psize_uor_unum_ureq == 6);
      requests++;
    }
    if (chi_out.prequester_uresponses.pvalid &&
        chi_in.prequester_uresponses.pready)
      acknowledgements++;
    if (core_out.presponse.pvalid)
      responses++;
  }
  rising();
  falling();
}

void send_request(std::uint32_t address, std::uint8_t access,
                  std::uint8_t locality = 0, std::uint8_t size = 2,
                  std::uint8_t unsigned_load = 0, std::uint32_t data = 0,
                  std::uint8_t atomic = 0) {
  for (int cycle = 0; cycle < 100 && !core_out.prequest.pready; cycle++)
    tick();
  CHECK(core_out.prequest.pready);
  core_in.prequest.pbits = {};
  core_in.prequest.pbits.paddress = address;
  core_in.prequest.pbits.paccess = access;
  core_in.prequest.pbits.pwidth = size;
  core_in.prequest.pbits.pbyte_umask =
      ((((1 << (1 << size)) - 1) << bit_slice(address, 0, 2)) & low_mask(4));
  core_in.prequest.pbits.punsigned = unsigned_load;
  core_in.prequest.pbits.plocality = locality;
  core_in.prequest.pbits.pdata = data;
  core_in.prequest.pbits.patomic = atomic;
  core_in.prequest.pbits.pcontext.pwriteback =
      (access == UINT64_C(1) || access == UINT64_C(3) ||
       access == UINT64_C(4) || access == UINT64_C(5))
          ? UINT64_C(128)
          : UINT64_C(0);
  core_in.prequest.pvalid = 1;
  tick();
  core_in.prequest.pvalid = 0;
}

void expect_response(std::uint32_t expected) {
  for (int cycle = 0; cycle < 100 && !core_out.presponse.pvalid; cycle++)
    tick();
  CHECK(core_out.presponse.pvalid &&
        core_out.presponse.pbits.pdata == expected);
  tick();
}

void drive() {
  virtual_lookup_in.pvalid = core_in.prequest.pvalid;
  virtual_lookup_in.pbits =
      core_in.prequest.pbits.paddress ^ UINT64_C(0x40000000);
}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = 1;
  node_id = UINT64_C(3);
  pipeline_lookup_in = {};
  {
    core_in = {};
    core_in.presponse.pready = UINT64_C(1);
    prefetch_in = {};
    chi_in = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick();
    reset = 0;
    chi_in.prequests.pready = 1;
    chi_in.prequester_uresponses.pready = 1;
    tick();
    send_request(UINT64_C(4159), UINT64_C(6));
    for (int cycle = 0; cycle < 100 && requests == 0; cycle++)
      tick();
    CHECK(requests == 1 && responses == 0 && !core_out.pdrained);
    for (int packet = 0; packet < 4; packet++) {
      for (int cycle = 0; cycle < 100 && !chi_out.presponse_udata.pready;
           cycle++)
        tick();
      CHECK(chi_out.presponse_udata.pready);
      chi_in.presponse_udata.pbits = {};
      write_bits(chi_in.presponse_udata.pbits.pdata, 0, 128, ~uint128(0));
      chi_in.presponse_udata.pbits.pbyte_uenable = UINT16_MAX;
      chi_in.presponse_udata.pbits.pdata_uid = ((packet)&low_mask(2));
      chi_in.presponse_udata.pbits.presp = UINT64_C(2);
      chi_in.presponse_udata.pbits.popcode = UINT64_C(4);
      chi_in.presponse_udata.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
          UINT64_C(1);
      chi_in.presponse_udata.pbits.pdbid_uor_umecid = UINT64_C(85);
      chi_in.presponse_udata.pbits.psrc_uid = UINT64_C(1);
      chi_in.presponse_udata.pbits.ptgt_uid = UINT64_C(3);
      chi_in.presponse_udata.pvalid = 1;
      tick();
      chi_in.presponse_udata.pvalid = 0;
    }
    expect_response(UINT64_C(0));
    CHECK(acknowledgements == 1 && responses == 1);
    for (int offset = 0; offset < 64; offset++) {
      send_request(UINT64_C(4096) + ((offset)&low_mask(32)), UINT64_C(6));
      send_request(UINT64_C(4096) + (((offset / 4) * 4) & low_mask(32)),
                   UINT64_C(1));
      expect_response(UINT64_C(0));
      expect_response(UINT64_C(0));
      for (int word = 0; word < 16; word++) {
        send_request(UINT64_C(4096) + ((word * 4) & low_mask(32)), UINT64_C(1));
        expect_response(UINT64_C(0));
      }
      CHECK(requests == 1);
    }
    CHECK(core_out.pdrained);
    // Colliding hinted misses must not evict the dirty zeroed line. Repeat
    // the same request with all selectors, signed/unsigned byte and half loads.
    expected_opcode = UINT64_C(2);
    expected_address = UINT64_C(8192);
    for (int hint = 1; hint <= 4; hint++) {
      send_request(UINT64_C(8252), UINT64_C(1), ((hint)&low_mask(3)),
                   hint <= 2 ? UINT64_C(0) : UINT64_C(1), !(hint & 1));
      for (int cycle = 0; cycle < 100 && requests != hint + 1; cycle++)
        tick();
      CHECK(requests == hint + 1);
      for (int packet = 3; packet >= 0; packet--) {
        for (int cycle = 0; cycle < 100 && !chi_out.presponse_udata.pready;
             cycle++)
          tick();
        CHECK(chi_out.presponse_udata.pready);
        write_bits(chi_in.presponse_udata.pbits.pdata, 0, 128, ~uint128(0));
        chi_in.presponse_udata.pbits.pdata_uid = ((packet)&low_mask(2));
        chi_in.presponse_udata.pbits.presp = UINT64_C(1);
        chi_in.presponse_udata.pvalid = 1;
        tick();
        chi_in.presponse_udata.pvalid = 0;
      }
      expect_response((hint & 1)  ? UINT64_C(0xffffffff)
                      : hint == 2 ? UINT64_C(255)
                                  : UINT64_C(65535));
      send_request(UINT64_C(4156), UINT64_C(1), ((hint)&low_mask(3)));
      expect_response(0);
      CHECK(requests == hint + 1 && acknowledgements == hint + 1);
    }
    // Populate the adjacent line without evicting the zeroed line at 0x1000.
    expected_opcode = UINT64_C(7);
    expected_address = UINT64_C(4160);
    send_request(UINT64_C(4223), UINT64_C(6));
    for (int cycle = 0; cycle < 100 && requests != 6; cycle++)
      tick();
    CHECK(requests == 6);
    for (int packet = 0; packet < 4; packet++) {
      for (int cycle = 0; cycle < 100 && !chi_out.presponse_udata.pready;
           cycle++)
        tick();
      CHECK(chi_out.presponse_udata.pready);
      write_bits(chi_in.presponse_udata.pbits.pdata, 0, 128, ~uint128(0));
      chi_in.presponse_udata.pbits.pdata_uid = ((packet)&low_mask(2));
      chi_in.presponse_udata.pbits.presp = UINT64_C(2);
      chi_in.presponse_udata.pvalid = 1;
      tick();
      chi_in.presponse_udata.pvalid = 0;
    }
    expect_response(0);
    for (int offset = 0; offset < 128; offset += 4) {
      send_request(UINT64_C(4096) + ((offset)&low_mask(32)), UINT64_C(3));
      expect_response(0);
      CHECK(core_out.preservation_uvalid);
      send_request(UINT64_C(4096) + ((offset ^ 64) & low_mask(32)),
                   UINT64_C(2));
      expect_response(0);
      CHECK(core_out.preservation_uvalid);
      send_request(UINT64_C(4096) + ((offset)&low_mask(32)), UINT64_C(4), 0, 2,
                   0, UINT64_C(4660));
      expect_response(0);
      CHECK(!core_out.preservation_uvalid);
      send_request(UINT64_C(4096) + ((offset)&low_mask(32)), UINT64_C(4), 0, 2,
                   0, UINT64_C(22136));
      expect_response(1);
      send_request(UINT64_C(4096) + ((offset)&low_mask(32)), UINT64_C(1));
      expect_response(UINT64_C(4660));
      send_request(UINT64_C(4096) + ((offset)&low_mask(32)), UINT64_C(3));
      expect_response(UINT64_C(4660));
      send_request(UINT64_C(4096) + ((offset ^ 4) & low_mask(32)), UINT64_C(4),
                   0, 2, 0, UINT64_C(22136));
      expect_response(1);
      CHECK(!core_out.preservation_uvalid && requests == 6);
      send_request(UINT64_C(4096) + ((offset ^ 4) & low_mask(32)), UINT64_C(1));
      expect_response(0);
      send_request(UINT64_C(4096) + ((offset)&low_mask(32)), UINT64_C(2));
      expect_response(0);
    }
    for (int operation = 0; operation < 9; operation++) {
      for (int sample = 0; sample < 4; sample++) {
        std::uint32_t left_value, right_value, result;
        left_value = ((amo_operand(sample)) & low_mask(32));
        right_value = ((amo_operand(sample ^ 1)) & low_mask(32));
        result = ((amo_reference(
                      (field(UINT64_C(0), 32, 32) | field(left_value, 32, 0)),
                      (field(UINT64_C(0), 32, 32) | field(right_value, 32, 0)),
                      operation, 1)) &
                  low_mask(32));
        send_request(UINT64_C(4156), UINT64_C(2), 0, 2, 0, left_value);
        expect_response(0);
        send_request(UINT64_C(4156), UINT64_C(5), 0, 2, 0, right_value,
                     ((operation)&low_mask(4)));
        expect_response(left_value);
        send_request(UINT64_C(4156), UINT64_C(1));
        expect_response(result);
      }
    }
    send_request(UINT64_C(4156), UINT64_C(2));
    expect_response(0);

    send_request(UINT64_C(4156), UINT64_C(3));
    expect_response(0);
    // Same-line mutation is a permitted conservative reservation invalidation.
    send_request(UINT64_C(4096), UINT64_C(2));
    expect_response(0);
    CHECK(!core_out.preservation_uvalid);
    send_request(UINT64_C(4156), UINT64_C(4), 0, 2, 0, UINT64_C(22136));
    expect_response(1);
    send_request(UINT64_C(4156), UINT64_C(3));
    expect_response(0);
    for (unsigned repeat_index = 0; repeat_index < (160); ++repeat_index)
      tick();
    CHECK(core_out.preservation_uvalid);
    send_request(UINT64_C(4156), UINT64_C(4), 0, 2, 0, UINT64_C(22136));
    expect_response(0);
    CHECK(requests == 6);
    send_request(UINT64_C(4156), UINT64_C(3));
    expect_response(UINT64_C(22136));
    {
      std::uint32_t expected;
      int old_responses;
      expected = UINT64_C(22136);
      for (int lane = 0; lane < 4; lane++) {
        old_responses = responses;
        pipeline_lookup_in = {
            .pvalid = 1,
            .pbits = {
                .pbyte_umask = static_cast<uint8_t>(
                    ((((1 << (1 << (UINT64_C(0)))) - 1)
                      << ((UINT64_C(0x4000103c) + ((lane)&low_mask(32))) % 4)) &
                     low_mask(4))),
                .paddress = static_cast<uint32_t>(UINT64_C(0x4000103c) +
                                                  ((lane)&low_mask(32))),
                .paccess = UINT64_C(2),
                .pwidth = UINT64_C(0),
                .punsigned = 0,
                .pdata = static_cast<uint32_t>(UINT64_C(160) +
                                               ((lane)&low_mask(32)))}};
        tick();
        pipeline_lookup_in = {};
        pipeline_in.prequest = {
            .pvalid = 1,
            .pbits = {.pbyte_umask = static_cast<uint8_t>(
                          ((((1 << (1 << (UINT64_C(0)))) - 1)
                            << ((UINT64_C(4156) + ((lane)&low_mask(32))) % 4)) &
                           low_mask(4))),
                      .paddress = static_cast<uint32_t>(UINT64_C(4156) +
                                                        ((lane)&low_mask(32))),
                      .paccess = UINT64_C(2),
                      .pwidth = UINT64_C(0),
                      .punsigned = 0,
                      .pdata = static_cast<uint32_t>(UINT64_C(160) +
                                                     ((lane)&low_mask(32)))}};
        settle();
        CHECK(pipeline_out.presponse.pvalid &&
              pipeline_out.presponse.pbits.poutcome == 2);
        tick();
        pipeline_in.prequest.pvalid = 0;
        pipeline_in.pcommit = 1;
        settle();
        CHECK(pipeline_out.pcommit_uready && !core_out.pdrained);
        tick();
        pipeline_in = {};
        CHECK(!core_out.preservation_uvalid);
        for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
          tick();
        CHECK(core_out.pdrained && responses == old_responses && requests == 6);
        bit_slice(expected, lane * 8, 8) = UINT64_C(160) + ((lane)&low_mask(8));
        send_request(UINT64_C(4156), UINT64_C(1));
        expect_response(expected);
      }
    }

    reset = 1;
    tick();
    CHECK(!core_out.preservation_uvalid);

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
