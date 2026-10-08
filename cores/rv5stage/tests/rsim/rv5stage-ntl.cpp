// Preserves the rv5stage-ntl cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using PhysicalMemoryResp =
    std::remove_cvref_t<decltype(data_access_in.presponse.pbits)>;
// Checks WB NTL association, replay, FP memory, squash, trap entry, and interrupt entry.

int scenario, cycles, attempts, accepted, irq_age, load_delay;
bool done, hint_fetched, i_valid, d_valid;
std::uint32_t i_word;
PhysicalMemoryResp d_bits;

std::uint32_t instruction_at(std::uint64_t pc) {
  switch (pc) {
  case 0: {
    return UINT64_C(0x10000093); // mtvec = 256
  } break;
  case 4: {
    return UINT64_C(0x30509073);
  } break;
  case 8: {
    return UINT64_C(8375); // FS = Initial
  } break;
  case 12: {
    return scenario == 12 ? UINT64_C(0x808093) : UINT64_C(32915);
  } break;
  case 16: {
    return UINT64_C(0x30009073);
  } break;
  case 20: {
    return UINT64_C(0x8000093); // enable MTIP locally
  } break;
  case 24: {
    return UINT64_C(0x30409073);
  } break;
  case 28: {
    return UINT64_C(0x2a00113); // x2 = 42
  } break;
  case 32: {
    return UINT64_C(0xf2000053); // fmv.d.x f0,x0
  } break;
  case 36: {
    return scenario == 13 ? UINT64_C(0x180006f) : UINT64_C(0x1c0006f);
  } break;
  case 60: {
    return UINT64_C(0x30003503); // older delayed ld x10,768(x0)
  } break;
  case 64: {
    return scenario < 4
               ? (UINT64_C(0x200033) + (((scenario)&low_mask(32)) << 20))
               : UINT64_C(0x200033);
  } break;
  case 68: {
    switch (scenario) {
    case 4: {
      return UINT64_C(19); // non-memory successor consumes the hint
    } break;
    case 5: {
      return UINT64_C(0x500033); // replacement NTL.ALL
    } break;
    case 6: {
      return UINT64_C(0x20003183); // ld x3,512(x0)
    } break;
    case 7: {
      return UINT64_C(0x20003087); // fld f1,512(x0)
    } break;
    case 8: {
      return UINT64_C(0x20003027); // fsd f0,512(x0)
    } break;
    case 9: {
      return UINT64_C(115); // ecall
    } break;
    case 10: {
      return UINT64_C(0x80006f); // branch consumes; squash younger NTL
    } break;
    case 12: {
      return UINT64_C(19); // withheld until interrupt entry
    } break;
    default: {
      return UINT64_C(0x20203023); // sd x2,512(x0)
    } break;
    }
  } break;
  case 72: {
    switch (scenario) {
    case 6: {
      return UINT64_C(0x20303423); // sd x3,520(x0), no locality
    } break;
    case 7: {
      return UINT64_C(0x20103427); // fsd f1,520(x0), no locality
    } break;
    case 10: {
      return UINT64_C(0x500033);
    } break;
    case 14: {
      return UINT64_C(0x500033); // younger hint squashed by the target's fault
    } break;
    default: {
      return UINT64_C(0x20203423);
    } break;
    }
  } break;
  case 76: {
    return UINT64_C(0x20203823); // done store
  } break;
  case 80: {
    return UINT64_C(111);
  } break;
  case 256: {
    return UINT64_C(0x22203023); // handler writes a distinct signature at 544
  } break;
  default: {
    return UINT64_C(111);
  } break;
  }
}

void tick() {
  falling();
  settle();
}

void drive() {
  {
    instruction_access_in = {};
    instruction_access_in.prequest.pready =
        (instruction_access_out.pflush || !i_valid ||
         instruction_access_out.presponse.pready) &&
        !(scenario == 12 && hint_fetched &&
          instruction_access_out.prequest.pbits.paddress < 256);
    instruction_access_in.presponse.pvalid = i_valid;
    instruction_access_in.presponse.pbits.pword = i_word;
    data_access_in = {};
    data_access_in.prequest.pready = !(scenario == 11 && attempts < 2);
    data_access_in.prequest_ufault =
        scenario == 14 && data_access_out.prequest.pvalid &&
        data_access_out.prequest.pbits.paddress == 512;
    data_access_in.pdrained = !d_valid && load_delay == 0;
    data_access_in.presponse.pvalid = d_valid;
    data_access_in.presponse.pbits = d_bits;
    interrupts = {};
    interrupts.pmachine_utimer = scenario == 12 && irq_age > 25;
  }
}

void observe() {
  {
    if (reset) {
      defer(i_valid, 0);
      defer(d_valid, 0);
      defer(d_bits, std::remove_cvref_t<decltype(d_bits)>{});
      defer(hint_fetched, 0);
      defer(irq_age, 0);
      defer(load_delay, 0);
      defer(attempts, 0);
      defer(accepted, 0);
      defer(done, 0);
    } else {
      if (hint_fetched)
        defer(irq_age, irq_age + 1);
      if (instruction_access_out.pflush ||
          instruction_access_out.presponse.pready)
        defer(i_valid, 0);
      if (instruction_access_out.prequest.pvalid &&
          instruction_access_in.prequest.pready) {
        CHECK(instruction_access_out.pflush || !i_valid ||
              instruction_access_out.presponse.pready);
        defer(i_valid, 1);
        defer(i_word,
              instruction_at(instruction_access_out.prequest.pbits.paddress));
        if (instruction_access_out.prequest.pbits.paddress == 64)
          defer(hint_fetched, 1);
      }
      if (d_valid && data_access_out.presponse.pready)
        defer(d_valid, 0);
      if (load_delay > 0) {
        defer(load_delay, load_delay - 1);
        if (load_delay == 1)
          defer(d_valid, 1);
      }
      if (data_access_out.prequest.pvalid) {
        defer(attempts, attempts + 1);
        if (data_access_out.prequest.pbits.paddress == 512) {
          if (scenario == 13)
            CHECK(load_delay > 0);
          CHECK(data_access_out.prequest.pbits.plocality ==
                (scenario < 4 ? ((scenario + 1) & low_mask(3)) : UINT64_C(1)));
        } else if (data_access_out.prequest.pbits.paddress == 520) {
          CHECK(data_access_out.prequest.pbits.plocality ==
                (scenario == 5 ? UINT64_C(4) : UINT64_C(0)));
        } else {
          CHECK(data_access_out.prequest.pbits.plocality == 0);
        }
        if (data_access_in.prequest.pready && !data_access_in.prequest_ufault) {
          defer(accepted, accepted + 1);
          if (data_access_out.prequest.pbits.paccess == 1) {
            defer(
                d_bits,
                std::remove_cvref_t<decltype(d_bits)>{
                    .paccess_ufault = UINT64_C(0),
                    .pdata = UINT64_C(42),
                    .pcontext = {
                        .pwriteback =
                            data_access_out.prequest.pbits.pcontext.pwriteback,
                        .porigin =
                            data_access_out.prequest.pbits.pcontext.porigin}});
            defer(load_delay, scenario == 13 ? 50 : 8);
          }
          if (data_access_out.prequest.pbits.paddress == 520 &&
              (scenario == 6 || scenario == 7))
            CHECK(data_access_out.prequest.pbits.pdata == 42);
          if (data_access_out.prequest.pbits.paddress == 528 ||
              data_access_out.prequest.pbits.paddress == 544) {
            CHECK((data_access_out.prequest.pbits.paddress == 544) ==
                  (scenario == 9 || scenario == 12 || scenario == 14));
            defer(done, 1);
          }
        }
      }
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  time_counter = 0;
  hart_id = 0;
  {
    for (scenario = 0; scenario < 15; scenario++) {
      reset = 1;
      for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
        tick();
      reset = 0;
      cycles = 0;
      while (!done && cycles < 700) {
        tick();
        cycles++;
      }
      CHECK(done);
      if (scenario == 11)
        CHECK(attempts == accepted + 2);
      if (scenario == 9 || scenario == 12 || scenario == 14)
        CHECK(accepted == 1);
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
