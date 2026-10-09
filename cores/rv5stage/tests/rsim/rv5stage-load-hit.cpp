// Preserves the rv5stage-load-hit cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
extern "C" void demand_init();
extern "C" void demand_sample(unsigned reset, unsigned attempt, unsigned fire,
                              unsigned cached, std::uint64_t address,
                              unsigned txfire);
extern "C" void demand_check(unsigned done);
using response_bits_t =
    std::remove_cvref_t<decltype(uncached_in.presponse.pbits)>;
// Checks real-core load timing, authorization, and exact demand-to-refill event ancestry.

bool done = 0;

bool refill_pending = 0, refill_active = 0, uncached_pending = 0;
std::uint32_t instruction_words[8];
std::uint8_t instruction_head = 0, instruction_tail = 0;
std::uint8_t instruction_count = 0;
std::uint64_t line_address;
std::uint16_t transaction_id;
response_bits_t uncached_response;
int cycle = 0, beat = 0, refills = 0, signatures = 0, hits = 0,
    device_reads = 0, ram_stores = 0;
int measured = 0, previous_issue = 0, chase_cycle = 0;
int refill_delay = 0, load_miss_hits = 0, store_miss_hits = 0;
int retry_phase = 0;
bool previous_load = 0;
std::uint64_t previous_address;
bool instruction_request_fire;
bool instruction_response_fire;

std::uint32_t load_insn(int rd, int rs1, int offset, int size) {
  return (((uint128(((offset)&low_mask(12))) & mask128(12)) << 20) |
          ((uint128(((rs1)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((size)&low_mask(3))) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(3)) & mask128(7)) << 0));
}
std::uint32_t add_insn(int rd, int rs1, int rs2, std::uint8_t word_op = 0) {
  return (((uint128(UINT64_C(0)) & mask128(7)) << 25) |
          ((uint128(((rs2)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(((rs1)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(word_op ? UINT64_C(59) : UINT64_C(51)) & mask128(7)) << 0));
}
std::uint32_t store_insn(int rs2, int offset, int base = 20) {
  return (((uint128(((offset >> 5) & low_mask(7))) & mask128(7)) << 25) |
          ((uint128(((rs2)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(((base)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
          ((uint128(((offset)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(35)) & mask128(7)) << 0));
}
std::uint32_t instruction_at(std::uint64_t address) {
  int n;
  n = int(address / 4);
  switch (n) {
  case 0: {
    return UINT64_C(5175); // lui x8,1
  } break;
  case 1: {
    return load_insn(5, 8, 0, 3); // cold line fill
  } break;
  case 2: {
    return add_insn(6, 5, 0); // wait for the cold load
  } break;
  case 3: {
    return UINT64_C(35383); // lui x20,8: signature device
  } break;
  case 8: {
    return load_insn(5, 8, 0, 3);
  } break;
  case 9: {
    return load_insn(6, 5, 0, 3); // immediately dependent address
  } break;
  case 10: {
    return add_insn(7, 6, 0);
  } break;
  case 11: {
    return store_insn(7, 0);
  } break;
  case 12: {
    return UINT64_C(267386895); // fence
  } break;
  case 13: {
    return UINT64_C(268698771); // x9 = input1 at 0x1100
  } break;
  case 14: {
    return UINT64_C(537134355); // x10 = input2 at 0x1200
  } break;
  case 15: {
    return UINT64_C(805569939); // x11 = output at 0x1300
  } break;
  case 16: {
    return UINT64_C(67405331); // x12 = input1 end
  } break;
  case 17: {
    return load_insn(5, 9, 0, 2); // warm both input lines
  } break;
  case 18: {
    return load_insn(6, 10, 0, 2);
  } break;
  case 19: {
    return store_insn(0, 0, 11); // acquire the output line
  } break;
  case 20: {
    return UINT64_C(267386895);
  } break;
  case 24: {
    return load_insn(5, 9, 0, 2);
  } break;
  case 25: {
    return load_insn(6, 10, 0, 2);
  } break;
  case 26: {
    return UINT64_C(4490387); // addi x9,x9,4
  } break;
  case 27: {
    return UINT64_C(4523283); // addi x10,x10,4
  } break;
  case 28: {
    return add_insn(7, 5, 6, 1);
  } break;
  case 29: {
    return UINT64_C(7708707); // sw x7,0(x11)
  } break;
  case 30: {
    return UINT64_C(4556179); // addi x11,x11,4
  } break;
  case 31: {
    return UINT64_C(4274295523); // bne x9,x12,-28
  } break;
  case 32: {
    return UINT64_C(267386895);
  } break;
  case 33: {
    return load_insn(7, 11, -64, 2); // observe buffered output stores
  } break;
  case 34: {
    return load_insn(7, 11, -4, 2);
  } break;
  case 96: {
    return store_insn(7, 8);
  } break;
  case 97: {
    return UINT64_C(267386895);
  } break;
  case 100: {
    return load_insn(5, 8, 24, 0); // lb -1
  } break;
  case 101: {
    return load_insn(6, 8, 24, 4); // lbu 255
  } break;
  case 102: {
    return add_insn(7, 5, 6);
  } break;
  case 103: {
    return store_insn(7, 16);
  } break;
  case 104: {
    return UINT64_C(267386895);
  } break;
  case 108: {
    return load_insn(5, 8, 24, 1); // lh -32513
  } break;
  case 109: {
    return load_insn(6, 8, 24, 5); // lhu 33023
  } break;
  case 110: {
    return add_insn(7, 5, 6);
  } break;
  case 111: {
    return store_insn(7, 24);
  } break;
  case 112: {
    return UINT64_C(267386895);
  } break;
  case 116: {
    return load_insn(5, 8, 16, 6); // lwu 4294967289
  } break;
  case 117: {
    return load_insn(6, 8, 16, 2); // lw -7
  } break;
  case 118: {
    return add_insn(7, 5, 6);
  } break;
  case 119: {
    return store_insn(7, 32);
  } break;
  case 120: {
    return UINT64_C(267386895);
  } break;
  case 124: {
    return load_insn(7, 8, 24, 3);
  } break;
  case 125: {
    return store_insn(7, 40);
  } break;
  case 126: {
    return UINT64_C(267386895);
  } break;
  case 128: {
    return UINT64_C(25271); // enable FS
  } break;
  case 129: {
    return UINT64_C(805478515); // csrs mstatus,x5
  } break;
  case 130: {
    return UINT64_C(17047687); // flw f1,16(x8)
  } break;
  case 131: {
    return UINT64_C(3758130131); // fmv.x.w x7,f1
  } break;
  case 132: {
    return store_insn(7, 48);
  } break;
  case 133: {
    return UINT64_C(267386895);
  } break;
  case 136: {
    return UINT64_C(25440519); // fld f2,24(x8)
  } break;
  case 137: {
    return UINT64_C(3791717331); // fmv.x.d x7,f2
  } break;
  case 138: {
    return store_insn(7, 56);
  } break;
  case 139: {
    return UINT64_C(267386895);
  } break;
  case 140: {
    return load_insn(7, 20, 0, 3); // side-effecting device read: WB only
  } break;
  case 141: {
    return store_insn(7, 64);
  } break;
  case 142: {
    return UINT64_C(267386895);
  } break;
  case 144: {
    return UINT64_C(69206931);
  } break;
  case 145: {
    return store_insn(7, 32, 8); // update the resident cache line
  } break;
  case 146: {
    return load_insn(7, 8, 32, 3); // must not bypass the older store
  } break;
  case 147: {
    return store_insn(7, 72);
  } break;
  case 148: {
    return UINT64_C(267386895);
  } break;
  case 152: {
    return load_insn(5, 8, 1024, 3); // delayed miss at 0x1400
  } break;
  case 153: {
    return load_insn(6, 8, 16, 3); // independent resident hit under the miss
  } break;
  case 154: {
    return add_insn(7, 6, 0);
  } break;
  case 155: {
    return add_insn(7, 7, 5); // consume both hit and deferred miss results
  } break;
  case 156: {
    return store_insn(7, 80);
  } break;
  case 157: {
    return UINT64_C(267386895);
  } break;
  case 160: {
    return store_insn(6, 1088, 8); // delayed ownership miss at 0x1440
  } break;
  case 161: {
    return load_insn(7, 8, 16, 3);
  } break;
  case 162: {
    return add_insn(7, 7, 0);
  } break;
  case 163: {
    return UINT64_C(267386895); // drain the accepted store miss
  } break;
  case 164: {
    return load_insn(6, 8, 1088, 3);
  } break;
  case 165: {
    return add_insn(7, 7, 6);
  } break;
  case 166: {
    return store_insn(7, 88);
  } break;
  case 167: {
    return UINT64_C(267386895);
  } break;
  case 172: {
    return UINT64_C(
        128975763); // x7=123, must survive the younger squashed lookup
  } break;
  case 173: {
    return UINT64_C(1073742483); // x5=0x400
  } break;
  case 174: {
    return UINT64_C(810717299); // csrw mtvec,x5
  } break;
  case 175: {
    return UINT64_C(66231); // x5=0x10000, unmapped
  } break;
  case 176: {
    return load_insn(6, 5, 0, 3); // access fault at WB
  } break;
  case 177: {
    return load_insn(7, 8, 0, 3); // younger warm lookup must not commit
  } break;
  case 178: {
    return store_insn(0, 96); // wrong path
  } break;
  case 256: {
    return store_insn(7, 96); // trap handler: precise preserved value
  } break;
  case 257: {
    return UINT64_C(111);
  } break;
  default: {
    return UINT64_C(19);
  } break;
  }
}
std::uint64_t data_at(std::uint64_t address) {
  if (address >= UINT64_C(4352) && address < UINT64_C(4416))
    return UINT64_C(18446744047939747833);
  if (address >= UINT64_C(4608) && address < UINT64_C(4672))
    return UINT64_C(38654705673);
  switch (address) {
  case UINT64_C(4096): {
    return UINT64_C(4104);
  } break;
  case UINT64_C(4104): {
    return UINT64_C(7);
  } break;
  case UINT64_C(4112): {
    return UINT64_C(42949672953);
  } break;
  case UINT64_C(4120): {
    return UINT64_C(18364758544493084927);
  } break;
  default: {
    return UINT64_C(1229782938247303441);
  } break;
  }
}

void drive() {
  instruction_request_fire =
      instruction_out.prequest.pvalid && instruction_in.prequest.pready;
  instruction_response_fire =
      instruction_in.presponse.pvalid && instruction_out.presponse.pready;
  {
    instruction_in = {};
    instruction_in.prequest.pready =
        instruction_out.pflush || instruction_count < 8;
    instruction_in.presponse.pvalid = instruction_count != 0;
    instruction_in.presponse.pbits.pword = instruction_words[instruction_head];
  }
  {
    uncached_in = {};
    uncached_in.prequest.pready = !uncached_pending;
    uncached_in.presponse.pvalid = uncached_pending;
    uncached_in.presponse.pbits = uncached_response;
    uncached_in.pdrained = !uncached_pending;
  }
  {
    chi_in = {};
    chi_in.prequests.pready = cycle % 4 != 0;
    chi_in.presponses.pvalid = retry_phase != 0;
    chi_in.presponses.pbits.popcode =
        retry_phase == 1 ? UINT64_C(3) : UINT64_C(7);
    chi_in.presponses.pbits.psrc_uid = UINT64_C(1);
    chi_in.presponses.pbits.ptgt_uid = UINT64_C(3);
    chi_in.presponses.pbits.ptxn_uid = transaction_id;
    chi_in.presponses.pbits.ppcrd_utype = UINT64_C(2);
    chi_in.prequester_uresponses.pready = 1;
    chi_in.prequest_udata.pready = 1;
    chi_in.presponse_udata.pvalid = refill_pending && refill_delay == 0;
    chi_in.presponse_udata.pbits.popcode = UINT64_C(4);
    chi_in.presponse_udata.pbits.presp =
        UINT64_C(2); // clean unique line permits the later local store
    chi_in.presponse_udata.pbits.pbyte_uenable = UINT64_C(65535);
    chi_in.presponse_udata.pbits.pdata_uid = ((beat)&low_mask(2));
    chi_in.presponse_udata.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
        UINT64_C(1);
    chi_in.presponse_udata.pbits.pdbid_uor_umecid = UINT64_C(85);
    chi_in.presponse_udata.pbits.ptxn_uid = transaction_id;
    chi_in.presponse_udata.pbits.psrc_uid = UINT64_C(1);
    chi_in.presponse_udata.pbits.ptgt_uid = UINT64_C(3);
    write_bits(
        chi_in.presponse_udata.pbits.pdata, 0, 128,
        (((uint128(data_at(line_address + ((16 * beat + 8) & low_mask(64)))) &
           mask128(64))
          << 64) |
         ((uint128(data_at(line_address + ((16 * beat) & low_mask(64)))) &
           mask128(64))
          << 0)));
  }
}

void observe() {
  demand_sample(int(reset), int(demand_attempt), int(demand_fire),
                int(cache_fire), cache_address,
                int(chi_out.prequests.pvalid && chi_in.prequests.pready));
  {
    defer(cycle, cycle + 1);
    if (reset) {
      defer(permit_demand, 0);
      defer(instruction_count, 0);
      defer(instruction_head, 0);
      defer(instruction_tail, 0);
      defer(refill_pending, 0);
      defer(refill_active, 0);
      defer(uncached_pending, 0);
      defer(refills, 0);
      defer(beat, 0);
      defer(signatures, 0);
      defer(measured, 0);
      defer(hits, 0);
      defer(previous_load, 0);
      defer(chase_cycle, 0);
      defer(refill_delay, 0);
      defer(load_miss_hits, 0);
      defer(store_miss_hits, 0);
      defer(retry_phase, 0);
    } else {
      if (demand_attempt && !demand_fire)
        defer(permit_demand, 1);
      defer(previous_load, load_issue);
      defer(previous_address, load_address);
      if (load_hit) {
        CHECK(previous_load);
        CHECK(previous_address < UINT64_C(32768));
        defer(hits, hits + 1);
        // A miss remains outstanding during RetryAck/PCrdGrant, not just DAT delay.
        if (refill_active && line_address == UINT64_C(5120))
          defer(load_miss_hits, load_miss_hits + 1);
        if (refill_active && line_address == UINT64_C(5184))
          defer(store_miss_hits, store_miss_hits + 1);
      }
      if (load_issue && load_address == UINT64_C(4096) && refills == 1)
        defer(chase_cycle, cycle);
      if (load_issue && load_address == UINT64_C(4104) && signatures == 0) {
        CHECK(cycle - chase_cycle == 2);
      }
      if (load_issue && signatures == 1 && refills >= 4 &&
          load_address >= UINT64_C(4352) && load_address < UINT64_C(4672)) {
        if (measured >= 8)
          CHECK(cycle - previous_issue == (bit_slice(measured, 0, 1) ? 1 : 7));
        defer(measured, measured + 1);
        defer(previous_issue, cycle);
      }
      if (instruction_out.pflush) {
        // A transferred restart is the sole entry in the replacement epoch.
        defer(instruction_count,
              instruction_request_fire ? UINT64_C(1) : UINT64_C(0));
        defer(instruction_head, 0);
        defer(instruction_tail,
              instruction_request_fire ? UINT64_C(1) : UINT64_C(0));
        if (instruction_request_fire)
          defer(instruction_words[0],
                instruction_at(instruction_out.prequest.pbits.paddress));
      } else {
        switch ((((uint128(instruction_request_fire) & mask128(1)) << 1) |
                 ((uint128(instruction_response_fire) & mask128(1)) << 0))) {
        case UINT64_C(2): {
          defer(instruction_count, instruction_count + 1);
        } break;
        case UINT64_C(1): {
          defer(instruction_count, instruction_count - 1);
        } break;
        default: {
          ;
        } break;
        }
        if (instruction_response_fire)
          defer(instruction_head, (instruction_head + 1) & 7);
        if (instruction_request_fire) {
          defer(instruction_words[instruction_tail],
                instruction_at(instruction_out.prequest.pbits.paddress));
          defer(instruction_tail, (instruction_tail + 1) & 7);
        }
      }
      if (chi_out.prequests.pvalid && chi_in.prequests.pready) {
        CHECK(!refill_pending &&
              (chi_out.prequests.pbits.popcode == UINT64_C(2) ||
               chi_out.prequests.pbits.popcode == UINT64_C(7)));
        defer(line_address,
              ((chi_out.prequests.pbits.paddress) & low_mask(64)));
        defer(transaction_id, chi_out.prequests.pbits.ptxn_uid);
        defer(refill_active, 1);
        if (chi_out.prequests.pbits.pallow_uretry)
          defer(retry_phase, 1);
        else {
          CHECK(chi_out.prequests.pbits.ppcrd_utype == 2);
          defer(refill_pending, 1);
          defer(beat, 0);
          defer(refills, refills + 1);
          defer(refill_delay,
                (chi_out.prequests.pbits.paddress == UINT64_C(5120) ||
                 chi_out.prequests.pbits.paddress == UINT64_C(5184))
                    ? 32
                    : 0);
        }
      }
      if (chi_in.presponses.pvalid && chi_out.presponses.pready)
        defer(retry_phase, retry_phase == 1 ? 2 : 0);
      if (refill_delay > 0)
        defer(refill_delay, refill_delay - 1);
      if (chi_in.presponse_udata.pvalid && chi_out.presponse_udata.pready) {
        if (beat == 3) {
          defer(refill_pending, 0);
          defer(refill_active, 0);
        } else
          defer(beat, beat + 1);
      }
      defer(uncached_pending, 0);
      if (uncached_out.prequest.pvalid && uncached_in.prequest.pready) {
        defer(uncached_pending, 1);
        defer(uncached_response,
              std::remove_cvref_t<decltype(uncached_response)>{
                  .paccess_ufault = UINT64_C(0),
                  .pdata = UINT64_C(4277009102),
                  .pcontext = {.pwriteback = uncached_out.prequest.pbits
                                                 .prequest.pcontext.pwriteback,
                               .porigin = uncached_out.prequest.pbits.prequest
                                              .pcontext.porigin}});
        if (uncached_out.prequest.pbits.prequest.paccess == UINT64_C(1))
          defer(device_reads, device_reads + 1);
      }
      if (transaction_fire && transaction.paccess == UINT64_C(2) &&
          transaction.paddress < UINT64_C(32768) &&
          !(transaction.paddress >= UINT64_C(4864) &&
            transaction.paddress < UINT64_C(4928))) {
        CHECK((transaction.paddress == UINT64_C(4128) &&
               transaction.pdata == 66) ||
              (transaction.paddress == UINT64_C(5184) &&
               transaction.pdata == UINT64_C(42949672953)));
        defer(ram_stores, ram_stores + 1);
      }
      if (transaction_fire && transaction.paccess == UINT64_C(2) &&
          transaction.paddress >= UINT64_C(32768)) {
        CHECK(transaction.paddress ==
              UINT64_C(32768) + ((signatures * 8) & low_mask(64)));
        switch (signatures) {
        case 0: {
          CHECK(transaction.pdata == 7);
        } break;
        case 1: {
          {
            CHECK(transaction.pdata == 2 && measured == 32 && refills == 4);
            ;
          }
        } break;
        case 2: {
          CHECK(transaction.pdata == 254);
        } break;
        case 3: {
          CHECK(transaction.pdata == 510);
        } break;
        case 4: {
          CHECK(transaction.pdata == UINT64_C(4294967282));
        } break;
        case 5: {
          CHECK(transaction.pdata == UINT64_C(18364758544493084927));
        } break;
        case 6: {
          CHECK(transaction.pdata == UINT64_C(18446744073709551609));
        } break;
        case 7: {
          CHECK(transaction.pdata == UINT64_C(18364758544493084927));
        } break;
        case 8: {
          CHECK(transaction.pdata == UINT64_C(4277009102) && device_reads == 1);
        } break;
        case 9: {
          CHECK(transaction.pdata == 66);
        } break;
        case 10: {
          CHECK(transaction.pdata ==
                    (UINT64_C(1229782938247303441) + UINT64_C(42949672953)) &&
                load_miss_hits > 0);
        } break;
        case 11: {
          CHECK(transaction.pdata == (UINT64_C(42949672953) * 2) &&
                store_miss_hits > 0 && refills == 6 && ram_stores == 1);
        } break;
        case 12: {
          {
            CHECK(transaction.pdata == 123);
            ;
            defer(done, 1);
          }
        } break;
        default: {
          fail(1, "extra signature");
        } break;
        }
        defer(signatures, signatures + 1);
      }
      CHECK(cycle < 10000);
    }
  }
}

void falling_update() {
  {
    demand_check(int(done));
    if (done)
      throw Finished{};
  }
}

void stimulus() {
  reset = 1;
  permit_demand = 0;
  {
    demand_init();
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
      falling();
    reset = 0;
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
