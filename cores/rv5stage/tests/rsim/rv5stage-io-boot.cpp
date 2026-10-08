// Checks rv5stage-io-boot arbitration and ordered memory effects.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
std::uint64_t entry_address = 0;

bool machine_software_interrupt_request = 0;

bool machine_software_interrupt_cleared = 0;

bool rom_active = 0;

int rom_delay = 0, rom_packet = 0, rom_line_reads = 0;

std::uint8_t rom_lines_seen = 0;

std::uint64_t rom_address;

int state = 0, delay_left = 0, latency = 0, cycle = 0;

int boot_reads = 0, stores = 0, completions = 0, interrupt_clears = 0,
    payload_fetches = 0;

std::uint64_t address;

std::uint64_t msip_address;

uint128 read_value;

std::uint32_t instruction_at(std::uint64_t pc) {
  if (pc >= UINT64_C(49152) && pc < UINT64_C(49152) + boot_words.size() * 4)
    return boot_words[(pc - UINT64_C(49152)) >> 2];
  switch (pc) {
  case UINT64_C(49408): {
    return UINT64_C(33463); // lui t0, 8
  } break;
  case UINT64_C(49412): {
    return UINT64_C(0x2a00313); // addi t1, zero, 42
  } break;
  case UINT64_C(49416): {
    return UINT64_C(0x62a423); // sw t1, 8(t0)
  } break;
  case UINT64_C(49420): {
    return UINT64_C(0xff0000f); // fence iorw, iorw
  } break;
  case UINT64_C(49424): {
    return UINT64_C(0x62a623); // sw t1, 12(t0)
  } break;
  case UINT64_C(49428): {
    return UINT64_C(0x10500073); // wfi
  } break;
  case UINT64_C(49432): {
    return UINT64_C(0xffdff06f); // j -4
  } break;
  default: {
    return UINT64_C(19);
  } break;
  }
}

void tick() {
  rising();
  falling();
}

void drive() {
  {
    imem_in = {};
    imem_in.preq.pready = !rom_active && cycle % 3 != 0;
    imem_in.pdat.presponse.pvalid = rom_active && rom_delay == 0;
    imem_in.pdat.presponse.pbits.popcode = UINT64_C(4);
    imem_in.pdat.presponse.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
        UINT64_C(4);
    imem_in.pdat.presponse.pbits.psrc_uid = UINT64_C(4);
    imem_in.pdat.presponse.pbits.ptgt_uid = UINT64_C(2);
    imem_in.pdat.presponse.pbits.pdata_uid = ((rom_packet)&low_mask(2));
    imem_in.pdat.presponse.pbits.pbyte_uenable = UINT64_MAX;
    for (int word_index = 0; word_index < 4; word_index++)
      bit_slice(imem_in.pdat.presponse.pbits.pdata, word_index * 32, 32) =
          instruction_at(rom_address +
                         ((rom_packet * 16 + word_index * 4) & low_mask(44)));
    umem_in = {};
    umem_in.preq.pready = state == 0 && cycle % 4 != 0;
    umem_in.pdat.prequest.pready = state == 3 && delay_left == 0;
    umem_in.pdat.presponse.pvalid = state == 1 && delay_left == 0;
    umem_in.pdat.presponse.pbits.popcode = UINT64_C(4);
    umem_in.pdat.presponse.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
        UINT64_C(4);
    write_bits(umem_in.pdat.presponse.pbits.pdata, 0, 128, read_value);
    umem_in.prsp.presponse.pvalid =
        (state == 2 || state == 4) && delay_left == 0;
    umem_in.prsp.presponse.pbits.popcode =
        state == 2 ? UINT64_C(6) : UINT64_C(4);
    umem_in.prsp.presponse.pbits.psrc_uid = UINT64_C(4);
    umem_in.prsp.presponse.pbits.pdbid_uor_ugroup_uid = UINT64_C(291);
  }
  machine_software_interrupt =
      machine_software_interrupt_request && !machine_software_interrupt_cleared;
  msip_address =
      UINT64_C(0x2000000) +
      (field(bit_slice(hart_id, 0, 42), 42, 2) | field(UINT64_C(0), 2, 0));
}

void observe() {
  {
    if (reset) {
      defer(state, 0);
      defer(delay_left, 0);
      defer(cycle, 0);
      defer(boot_reads, 0);
      defer(stores, 0);
      defer(completions, 0);
      defer(interrupt_clears, 0);
      defer(payload_fetches, 0);
      defer(machine_software_interrupt_cleared, 0);
      defer(read_value, 0);
      defer(address, 0);
      defer(rom_active, 0);
      defer(rom_delay, 0);
      defer(rom_packet, 0);
      defer(rom_address, 0);
      defer(rom_line_reads, 0);
      defer(rom_lines_seen, 0);
    } else {
      defer(cycle, cycle + 1);
      if (delay_left != 0)
        defer(delay_left, delay_left - 1);

      CHECK(!dmem_out.prequests.pvalid && !imem_out.prsp.prequester.pvalid);
      if (rom_delay != 0)
        defer(rom_delay, rom_delay - 1);
      if (imem_out.preq.pvalid && imem_in.preq.pready) {
        CHECK(imem_out.preq.pbits.popcode == UINT64_C(4) &&
              imem_out.preq.pbits.psize_uor_unum_ureq == 6 &&
              imem_out.preq.pbits.psrc_uid == 2 &&
              imem_out.preq.pbits.ptgt_uid == 4 &&
              !imem_out.preq.pbits.pexp_ucomp_uack &&
              !imem_out.preq.pbits.pmem_uattr.pallocate &&
              !imem_out.preq.pbits.pmem_uattr.pcacheable &&
              !imem_out.preq.pbits.pmem_uattr.pdevice &&
              !imem_out.preq.pbits.pmem_uattr.pearly_uwrite_uacknowledge &&
              imem_out.preq.pbits.paddress >= UINT64_C(49152) &&
              imem_out.preq.pbits.paddress < UINT64_C(49664) &&
              bit_slice(imem_out.preq.pbits.paddress, 0, 6) == 0 &&
              !bit_slice(rom_lines_seen,
                         bit_slice(imem_out.preq.pbits.paddress, 6, 3), 1));
        defer(rom_active, 1);
        defer(rom_address, imem_out.preq.pbits.paddress);
        defer(rom_delay, latency);
        defer(rom_packet, 0);
        defer(rom_line_reads, rom_line_reads + 1);
        // Fetch may speculate past WFI while older IO retires. Every such
        // read must still acquire a new ROM line, never repeat a resident one.
        defer(bit_slice(rom_lines_seen,
                        unsigned(bits(imem_out.preq.pbits.paddress, 8, 6)), 1),
              1);
        if (imem_out.preq.pbits.paddress == UINT64_C(49408))
          defer(payload_fetches, payload_fetches + 1);
      }
      if (imem_in.pdat.presponse.pvalid && imem_out.pdat.presponse.pready) {
        defer(rom_packet, rom_packet + 1);
        defer(rom_delay, latency);
        if (rom_packet == 3)
          defer(rom_active, 0);
      }
      if (umem_out.preq.pvalid && umem_in.preq.pready) {
        defer(address, umem_out.preq.pbits.paddress);
        CHECK(state == 0 && umem_out.preq.pbits.ptgt_uid == UINT64_C(4) &&
              !umem_out.preq.pbits.pmem_uattr.pcacheable &&
              !umem_out.preq.pbits.pmem_uattr.pallocate);
        defer(delay_left, latency);
        if (umem_out.preq.pbits.popcode == UINT64_C(4)) {
          defer(state, 1);
          CHECK(umem_out.preq.pbits.paddress == UINT64_C(32768) &&
                umem_out.preq.pbits.psize_uor_unum_ureq == UINT64_C(3));
          defer(boot_reads, boot_reads + 1);
          defer(read_value, ((entry_address)&low_mask(128)));
        } else {
          CHECK(umem_out.preq.pbits.popcode == UINT64_C(28) &&
                umem_out.preq.pbits.psize_uor_unum_ureq == UINT64_C(2));
          if (umem_out.preq.pbits.paddress == msip_address) {
            CHECK(interrupt_clears == 0 && stores == 0);
          } else {
            CHECK(umem_out.preq.pbits.paddress ==
                      (stores == 0 ? UINT64_C(32776) : UINT64_C(32780)) &&
                  stores < 2 && completions == stores);
            defer(stores, stores + 1);
          }
          defer(state, 2);
        }
      }
      if (umem_in.pdat.presponse.pvalid && umem_out.pdat.presponse.pready)
        defer(state, 0);
      if (umem_in.prsp.presponse.pvalid && umem_out.prsp.presponse.pready) {
        defer(delay_left, latency);
        if (state == 2)
          defer(state, 3);
        else {
          if (address == msip_address) {
            defer(interrupt_clears, interrupt_clears + 1);
            defer(machine_software_interrupt_cleared, 1);
          } else {
            defer(completions, completions + 1);
          }
          defer(state, 0);
        }
      }
      if (umem_out.pdat.prequest.pvalid && umem_in.pdat.prequest.pready) {
        CHECK(umem_out.pdat.prequest.pbits.ptxn_uid == UINT64_C(291) &&
              umem_out.pdat.prequest.pbits.pbyte_uenable ==
                  (address == msip_address
                       ? (UINT64_C(15) << bit_slice(address, 0, 4))
                       : (address == UINT64_C(32776) ? UINT64_C(3840)
                                                     : UINT64_C(61440))) &&
              read_bits(umem_out.pdat.prequest.pbits.pdata, 0, 128) ==
                  (address == msip_address
                       ? UINT64_C(0)
                       : (uint128(42) << (8 * bit_slice(address, 0, 4)))));
        defer(state, 4);
        defer(delay_left, latency + 8);
      }
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  hart_id = 0;
  {
    for (int run = 0; run < 3; run++) {
      latency = run == 0 ? 0 : run == 1 ? 3 : 17;
      reset = 1;
      hart_id = 0;
      entry_address = 0;
      machine_software_interrupt_request = 0;
      tick();
      reset = 0;
      for (unsigned repeat_index = 0; repeat_index < (300); ++repeat_index)
        tick();
      CHECK(boot_reads == 0 && stores == 0 && payload_fetches == 0 &&
            bit_slice(rom_lines_seen, 0, 1));
      entry_address = UINT64_C(49408);
      for (unsigned repeat_index = 0; repeat_index < (100); ++repeat_index)
        tick();
      CHECK(boot_reads == 0 && stores == 0 && payload_fetches == 0);
      machine_software_interrupt_request = 1;
      for (int wait_cycle = 0; wait_cycle < 3000 && completions != 2;
           wait_cycle++)
        tick();
      CHECK(completions == 2 && interrupt_clears == 1 && boot_reads == 1 &&
            payload_fetches == 1 && bit_slice(rom_lines_seen, 0, 1) &&
            bit_slice(rom_lines_seen, 4, 1));
      for (unsigned repeat_index = 0; repeat_index < (80); ++repeat_index)
        tick();
      CHECK(completions == 2 && stores == 2);
    }
    reset = 1;
    hart_id = 1;
    entry_address = UINT64_C(49408);
    machine_software_interrupt_request = 0;
    tick();
    reset = 0;
    for (unsigned repeat_index = 0; repeat_index < (600); ++repeat_index)
      tick();
    CHECK(bit_slice(rom_lines_seen, 0, 1) && boot_reads == 0 &&
          payload_fetches == 0 && stores == 0 && state == 0);
    machine_software_interrupt_request = 1;
    for (int wait_cycle = 0; wait_cycle < 3000 && completions != 2;
         wait_cycle++)
      tick();
    CHECK(completions == 2 && interrupt_clears == 1 && boot_reads == 1 &&
          payload_fetches == 1 && stores == 2);

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
