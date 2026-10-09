// Preserves the rv5stage-vector-config cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
extern "C" void vector_core_trace_bind();
extern "C" void vector_core_trace_finish();
// Executes vector configuration and packed integer macros through the real core, observing only public ports.

bool fault;

bool response_valid = 0;

bool reject_store = 1;

std::uint32_t response_word;

bool scalar_during_vector = 0;

int cycles = 0, stores = 0, vector_writes = 0, rejected_stores = 0;

// Configure VS, exercise all three vset forms, consume scalar results, and
// squash a younger configuration before trapping on masked ADD into v0.
std::uint32_t instruction_at(std::uint64_t address) {
  switch (address) {
  case 0: {
    return UINT64_C(268435603);
  } break;
  case 4: {
    return UINT64_C(810586227);
  } break;
  case 8: {
    return UINT64_C(536871059);
  } break;
  case 12: {
    return UINT64_C(805347443);
  } break;
  case 16: {
    return UINT64_C(3145875);
  } break;
  case 20: {
    return UINT64_C(16838999);
  } break;
  case 24: {
    return UINT64_C(
        16871895); // vsetvli x3,x2,e8,m1 consumes the preceding vset result
  } break;
  case 28: {
    return UINT64_C(3158051);
  } break;
  case 32: {
    return UINT64_C(26214931);
  } break;
  case 36: {
    return UINT64_C(2151706711);
  } break;
  case 40: {
    return UINT64_C(3254788595);
  } break;
  case 44: {
    return UINT64_C(3159075);
  } break;
  case 48: {
    return UINT64_C(3221418327);
  } break;
  case 52: {
    return UINT64_C(2111523);
  } break;
  case 56: {
    return UINT64_C(8388719);
  } break;
  case 60: {
    return UINT64_C(3222270295);
  } break;
  case 64: {
    return UINT64_C(3254788595);
  } break;
  case 68: {
    return UINT64_C(3161123);
  } break;
  case 72: {
    return UINT64_C(8397299);
  } break;
  case 76: {
    return UINT64_C(36712483);
  } break;
  case 80: {
    return UINT64_C(3221778775); // vsetivli x2,16,e8,m1
  } break;
  case 84: {
    return UINT64_C(536871059); // VS=Initial
  } break;
  case 88: {
    return UINT64_C(805343347); // csrw mstatus,x1
  } break;
  case 92: {
    return UINT64_C(780403799); // vxor.vv v8,v8,v8
  } break;
  case 96: {
    return UINT64_C(7340691); // dependent VX scalar
  } break;
  case 100: {
    return UINT64_C(42124375); // vadd.vx v8,v8,x5
  } break;
  case 104: {
    return UINT64_C(1719926871); // vmsne.vv v0,v8,v8
  } break;
  case 108: {
    return UINT64_C(1719677015); // vmsne.vi v0,v8,0
  } break;
  case 112: {
    return UINT64_C(9417815); // masked vadd.vi v8,v8,-1
  } break;
  case 116: {
    return UINT64_C(8507507); // vstart=3
  } break;
  case 120: {
    return UINT64_C(41989207); // vadd.vi v8,v8,1
  } break;
  case 124: {
    return UINT64_C(2954896243); // read minstret before one macro
  } break;
  case 128: {
    return UINT64_C(41989207); // one macro, two beats
  } break;
  case 132: {
    return UINT64_C(2954896371); // read minstret after macro
  } break;
  case 136: {
    return UINT64_C(1080263603); // sub x7,x7,x6
  } break;
  case 140: {
    return UINT64_C(40907811); // macro retirement delta
  } break;
  case 144: {
    return UINT64_C(8397299); // vstart after nonempty macro
  } break;
  case 148: {
    return UINT64_C(36714531); // vstart=0
  } break;
  case 152: {
    return UINT64_C(805315059); // mstatus after integer vector writes
  } break;
  case 156: {
    return UINT64_C(36715555); // VS=Dirty
  } break;
  case 160: {
    return UINT64_C(147); // AVL=0
  } break;
  case 164: {
    return UINT64_C(61527); // vsetvli x0,x1,e8,m1
  } break;
  case 168: {
    return UINT64_C(8638579); // vstart=7
  } break;
  case 172: {
    return UINT64_C(42124375); // empty macro, no VRF writes
  } break;
  case 176: {
    return UINT64_C(8397299); // vstart after empty macro
  } break;
  case 180: {
    return UINT64_C(70266915); // vstart=0
  } break;
  case 184: {
    return UINT64_C(8388719); // squash younger vector instruction
  } break;
  case 188: {
    return UINT64_C(42447959); // must not write
  } break;
  case 192: {
    return UINT64_C(3174743); // vsetvli x2,x0,e8,m8: 128 elements
  } break;
  case 196: {
    return UINT64_C(
        780403799); // sixteen packed beats through all private stages
  } break;
  case 200: {
    return UINT64_C(
        3175767); // vsetvli x10,x0,e8,m8 while the older vector still writes
  } break;
  case 204: {
    return UINT64_C(
        77607971); // sd x10,72(x0), without draining the older vector
  } break;
  case 208: {
    return UINT64_C(
        8388719); // younger redirect must preserve the active vector
  } break;
  case 212: {
    return UINT64_C(42447959); // squashed
  } break;
  case 216: {
    return UINT64_C(41989207); // in-place vadd.vi v8,v8,1
  } break;
  case 220: {
    return UINT64_C(
        4294967295); // younger scalar exception must drain the active vector
  } break;
  case 224: {
    return UINT64_C(2195543); // illegal masked destination v0, after returning
  } break;
  case 256: {
    return UINT64_C(874521075); // mcause
  } break;
  case 260: {
    return UINT64_C(873472627); // mepc -> x4
  } break;
  case 264: {
    return UINT64_C(4085383827); // addi x5,x4,-200
  } break;
  case 268: {
    return UINT64_C(2265747); // cause signature address = (mepc-200)*4
  } break;
  case 272: {
    return UINT64_C(3321891); // sd x3,0(x5)
  } break;
  case 276: {
    return UINT64_C(4371491); // sd x4,8(x5)
  } break;
  case 280: {
    return UINT64_C(4325907); // skip faulting instruction
  } break;
  case 284: {
    return UINT64_C(873599091); // csrw mepc,x4
  } break;
  case 288: {
    return UINT64_C(807403635); // mret
  } break;
  default: {
    return UINT64_C(111);
  } break;
  }
}

void drive() {
  {
    instruction_access_in = {};
    instruction_access_in.prequest.pready =
        instruction_access_out.pflush || !response_valid;
    instruction_access_in.presponse.pvalid = response_valid;
    instruction_access_in.presponse.pbits.pword = response_word;
    data_access_in = {};
    // Reject each store once, then retain readiness until its retry transfers.
    // Periodic readiness can phase-lock against the fixed replay latency.
    data_access_in.prequest.pready = !reject_store;
    data_access_in.pdrained = 1;
  }
}

void observe() {
  {
    if (reset) {
      defer(response_valid, 0);
      defer(cycles, 0);
      defer(stores, 0);
      defer(reject_store, 1);
      defer(rejected_stores, 0);
    } else {
      defer(cycles, cycles + 1);
      if (data_access_out.prequest.pvalid) {
        defer(reject_store, data_access_in.prequest.pready);
        if (!data_access_in.prequest.pready)
          defer(rejected_stores, rejected_stores + 1);
      }
      if (instruction_access_out.pflush)
        defer(response_valid, 0);
      else if (response_valid && instruction_access_out.presponse.pready)
        defer(response_valid, 0);
      // A transferred restart replaces the old response in the flush cycle.
      if (instruction_access_out.prequest.pvalid &&
          instruction_access_in.prequest.pready) {
        defer(response_valid, 1);
        defer(response_word,
              instruction_at(instruction_access_out.prequest.pbits.paddress));
      }
      if (data_access_out.prequest.pvalid && data_access_in.prequest.pready) {
        CHECK(data_access_out.prequest.pbits.paccess == 2 &&
              data_access_out.prequest.pbits.paddress ==
                  ((stores)&low_mask(64)) * 8);
        switch (stores) {
        case 0: {
          CHECK(data_access_out.prequest.pbits.pdata == 3);
        } break;
        case 1: {
          CHECK(data_access_out.prequest.pbits.pdata == 3);
        } break;
        case 2:
        case 3: {
          CHECK(data_access_out.prequest.pbits.pdata == 5);
        } break;
        case 4: {
          CHECK(data_access_out.prequest.pbits.pdata == 0);
        } break;
        case 5: {
          CHECK(data_access_out.prequest.pbits.pdata == 2);
        } break;
        case 6:
        case 8: {
          CHECK(data_access_out.prequest.pbits.pdata == 0);
        } break;
        case 7: {
          CHECK(bit_slice(data_access_out.prequest.pbits.pdata, 9,
                          (10) - (9) + 1) == 3 &&
                bit_slice(data_access_out.prequest.pbits.pdata, 63, 1));
        } break;
        case 9: {
          {
            CHECK(data_access_out.prequest.pbits.pdata == 128 &&
                  scalar_during_vector);
          }
        } break;
        case 10:
        case 12: {
          CHECK(data_access_out.prequest.pbits.pdata == 2 &&
                vector_writes == 46);
        } break;
        case 11: {
          {
            CHECK(data_access_out.prequest.pbits.pdata == 220);
            CHECK(vector_writes == 46);
          }
        } break;
        case 13: {
          {
            CHECK(data_access_out.prequest.pbits.pdata == 224);
            CHECK(vector_writes == 46 && rejected_stores == 14);
            settle();
            vector_core_trace_finish();
            ;
            throw Finished{};
          }
        } break;
        }
        defer(stores, stores + 1);
      }
      if (cycles > 5000)
        fail(1, "vector config pipeline timeout, stores=%0d", stores);
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  hart_id = 0;
  time_counter = 0;
  {
    vector_core_trace_bind();
    interrupts = {};
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
      rising();
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
extern "C" void test_scalar_write(std::uint64_t rst, std::uint64_t valid,
                                  std::uint64_t address, std::uint64_t data) {
  if (!rst && valid && address == 10 && data == 128) {
    CHECK(vector_writes >= 14 && vector_writes < 30);
    scalar_during_vector = true;
  }
}
extern "C" void test_vector_write(std::uint64_t rst, std::uint64_t valid,
                                  std::uint64_t address, std::uint64_t data,
                                  std::uint64_t mask) {
  static std::uint64_t previous_write = 0;
  if (rst || !valid)
    return;
  auto n = vector_writes;
  std::uint64_t expected_mask = UINT64_MAX, expected_data = 0;
  unsigned expected_address = 16 + n % 2;
  switch (n) {
  case 0:
  case 1:
    expected_data = 0;
    break;
  case 2:
  case 3:
    expected_data = UINT64_C(0x0707070707070707);
    break;
  case 4:
  case 5:
  case 6:
  case 7:
    expected_address = 0;
    expected_mask = n % 2 == 0 ? 0xff : 0xff00;
    expected_data = n < 6 ? 0 : expected_mask;
    break;
  case 8:
  case 9:
    expected_data = UINT64_C(0x0606060606060606);
    break;
  case 10:
    expected_data = UINT64_C(0x0707070707000000);
    expected_mask = UINT64_C(0xffffffffff000000);
    break;
  case 11:
    expected_data = UINT64_C(0x0707070707070707);
    break;
  case 12:
    expected_data = UINT64_C(0x0808080808070707);
    break;
  case 13:
    expected_data = UINT64_C(0x0808080808080808);
    break;
  default:
    CHECK(n < 46);
    expected_address = 16 + ((n - 14) % 16);
    expected_data = n < 30 ? 0 : UINT64_C(0x0101010101010101);
    if (n != 14 && n != 30)
      CHECK(test_cycles - previous_write == 1);
  }
  CHECK(address == expected_address && mask == expected_mask &&
        (data & mask) == expected_data);
  ++vector_writes;
  previous_write = test_cycles;
}
