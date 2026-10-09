// Preserves the rv5stage-core-rv32f cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using dresp_bits_t =
    std::remove_cvref_t<decltype(data_access_in.presponse.pbits)>;
// Runs the architectural WB authorization scenarios on RV32F.

// Exercises WB isolation and FP/memory effects while honoring accepted restart fetches.

bool instruction_pending = 0;

std::uint32_t instruction_word;

int scenario, attempts, accepted, stores, response_delay, prefetches,
    restart_accepts;

int killed_fixed_launches, killed_fixed_returns;

bool done;

dresp_bits_t pending_response;

std::uint32_t addi(int rd, int rs, int imm) {
  return (((uint128(((imm)&low_mask(12))) & mask128(12)) << 20) |
          ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(19)) & mask128(7)) << 0));
}
std::uint32_t csr(int rd, int rs, int address, int op) {
  return (((uint128(((address)&low_mask(12))) & mask128(12)) << 20) |
          ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((op)&low_mask(3))) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(115)) & mask128(7)) << 0));
}
std::uint32_t fp(int funct7, int rd, int rs1, int rs2) {
  return (((uint128(((funct7)&low_mask(7))) & mask128(7)) << 25) |
          ((uint128(((rs2)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(((rs1)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(83)) & mask128(7)) << 0));
}
std::uint32_t store_word(int rs, int address, int opcode = UINT64_C(35)) {
  return (((uint128(((address >> 5) & low_mask(7))) & mask128(7)) << 25) |
          ((uint128(((rs)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
          ((uint128(((address)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(((opcode)&low_mask(7))) & mask128(7)) << 0));
}
std::uint32_t atomic_word(int operation, int rd, int rs) {
  return (((uint128(((operation)&low_mask(5))) & mask128(5)) << 27) |
          ((uint128(UINT64_C(0)) & mask128(2)) << 25) |
          ((uint128(((rs)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(UINT64_C(3)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(2)) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(47)) & mask128(7)) << 0));
}
std::uint32_t instruction_at(std::remove_cvref_t<decltype(hart_id)> address) {
  if (scenario == 2)
    switch (address) {
    case UINT64_C(280): {
      return UINT64_C(2139095351); // Build an sNaN before the faulting load.
    } break;
    case UINT64_C(284): {
      return addi(2, 2, 1);
    } break;
    case UINT64_C(288): {
      return fp(UINT64_C(120), 5, 2, 0);
    } break;
    case UINT64_C(292):
    case UINT64_C(296):
    case UINT64_C(300): {
      return addi(0, 0, 0);
    } break;
    case UINT64_C(304): {
      return UINT64_C(1073750659);
    } break;
    case UINT64_C(308): {
      return fp(UINT64_C(0), 1, 5,
                1); // EX result would overwrite f1 and raise NV.
    } break;
    default: {
      {
      }
    } break;
    }
  switch (address) {
  case UINT64_C(256): {
    return UINT64_C(8375); // lui x1, 2: FS=Initial
  } break;
  case UINT64_C(260): {
    return csr(0, 1, UINT64_C(768), 2);
  } break;
  case UINT64_C(264): {
    return addi(1, 0, UINT64_C(512));
  } break;
  case UINT64_C(268): {
    return csr(0, 1, UINT64_C(773), 1); // mtvec
  } break;
  case UINT64_C(272): {
    return UINT64_C(1065353399); // 1.0f
  } break;
  case UINT64_C(276): {
    return fp(UINT64_C(120), 1, 1, 0); // fmv.w.x f1, x1
  } break;
  case UINT64_C(280): {
    return fp(UINT64_C(120), 0, 0, 0); // fmv.w.x f0, x0
  } break;
  case UINT64_C(284): {
    return UINT64_C(1073742135); // x2 = 2.0f
  } break;
  case UINT64_C(288): {
    return UINT64_C(1073750659); // lw x5, 0x400(x0)
  } break;
  case UINT64_C(292): {
    return fp(UINT64_C(120), 1, 2, 0); // immediately younger FP overwrite
  } break;
  case UINT64_C(296): {
    return UINT64_C(1343250451); // prefetch.r 0x500(x0)
  } break;
  case UINT64_C(300): {
    return fp(UINT64_C(12), 2, 1, 0); // fdiv.s f2, f1, f0: DZ
  } break;
  case UINT64_C(304): {
    return fp(UINT64_C(112), 6, 1, 0); // fmv.x.w x6, f1
  } break;
  case UINT64_C(308): {
    return store_word(6, UINT64_C(1088));
  } break;
  case UINT64_C(312): {
    return store_word(2, UINT64_C(1092), UINT64_C(39)); // fsw f2
  } break;
  case UINT64_C(316): {
    return csr(7, 0, UINT64_C(1), 2); // fflags
  } break;
  case UINT64_C(320): {
    return store_word(7, UINT64_C(1096));
  } break;
  case UINT64_C(324): {
    return addi(3, 0, UINT64_C(1104));
  } break;
  case UINT64_C(328): {
    return atomic_word(2, 8, 0); // lr.w
  } break;
  case UINT64_C(332): {
    return atomic_word(3, 9, 2); // sc.w
  } break;
  case UINT64_C(336): {
    return atomic_word(1, 10, 2); // amoswap.w
  } break;
  case UINT64_C(340): {
    return UINT64_C(1207968135); // flw f3, 0x480(x0)
  } break;
  case UINT64_C(344): {
    return fp(UINT64_C(112), 11, 3, 0);
  } break;
  case UINT64_C(348): {
    return store_word(11, UINT64_C(1100));
  } break;
  case UINT64_C(352): {
    return store_word(10, UINT64_C(1112));
  } break;
  case UINT64_C(356): {
    return fp(UINT64_C(0), 4, 1, 3); // fadd.s f4 = 2 + 3
  } break;
  case UINT64_C(360): {
    return fp(UINT64_C(8), 5, 1, 3); // independent fmul.s f5 = 2 * 3
  } break;
  case UINT64_C(364): {
    return fp(UINT64_C(112), 12, 4, 0); // dependent FP -> GPR
  } break;
  case UINT64_C(368): {
    return store_word(12, UINT64_C(1136));
  } break;
  case UINT64_C(372): {
    return store_word(5, UINT64_C(1140), UINT64_C(39));
  } break;
  case UINT64_C(512): {
    return fp(UINT64_C(112), 6, 1, 0); // fault: old f1 must survive
  } break;
  case UINT64_C(516): {
    return store_word(6, UINT64_C(1120));
  } break;
  case UINT64_C(520): {
    return csr(7, 0, UINT64_C(1), 2);
  } break;
  case UINT64_C(524): {
    return store_word(7, UINT64_C(1124)); // fault: no younger DZ
  } break;
  case UINT64_C(528): {
    return csr(7, 0, UINT64_C(834), 2);
  } break;
  case UINT64_C(532): {
    return store_word(7, UINT64_C(1128));
  } break;
  case UINT64_C(536): {
    return csr(7, 0, UINT64_C(835), 2);
  } break;
  case UINT64_C(540): {
    return store_word(7, UINT64_C(1132));
  } break;
  default: {
    return UINT64_C(111);
  } break;
  }
}

void drive() {
  instruction_access_in.prequest.pready =
      instruction_access_out.pflush || !instruction_pending ||
      instruction_access_out.presponse.pready;
  instruction_access_in.presponse.pvalid = instruction_pending;
  instruction_access_in.presponse.pbits.pword = instruction_word;
  instruction_access_in.presponse.pbits.ppage_ufault = 0;
  instruction_access_in.presponse.pbits.paccess_ufault = 0;
  data_access_in.prequest.pready =
      response_delay == 0 &&
      (data_access_out.prequest.pbits.paddress != UINT64_C(1024) ||
       (scenario == 0 && attempts >= 3));
  data_access_in.prequest_ufault = 0;
  data_access_in.prequest_uaccess_ufault =
      scenario != 0 && data_access_out.prequest.pvalid &&
      data_access_out.prequest.pbits.paddress == UINT64_C(1024);
  data_access_in.presponse.pvalid = response_delay == 1;
  data_access_in.presponse.pbits = pending_response;
  data_access_in.pdrained = response_delay == 0;
}

void observe() {
  {
    if (reset) {
      defer(instruction_pending, 0);
      defer(attempts, 0);
      defer(accepted, 0);
      defer(stores, 0);
      defer(prefetches, 0);
      defer(restart_accepts, 0);
      defer(killed_fixed_launches, 0);
      defer(killed_fixed_returns, 0);
      defer(response_delay, 0);
      defer(pending_response,
            std::remove_cvref_t<decltype(pending_response)>{});
      defer(done, 0);
    } else {
      // Observe the scalar adapter's public ports to prove this scenario really
      // launches speculative arithmetic, rather than cancelling it before EX.

      if (instruction_access_out.pflush ||
          (instruction_pending && instruction_access_out.presponse.pready))
        defer(instruction_pending, 0);
      // Flush cancels the old response; an accepted replacement belongs to the new epoch.
      if (instruction_access_out.prequest.pvalid &&
          instruction_access_in.prequest.pready) {
        defer(instruction_pending, 1);
        defer(instruction_word,
              instruction_at(instruction_access_out.prequest.pbits.paddress));
        if (instruction_access_out.pflush)
          defer(restart_accepts, restart_accepts + 1);
      }
      if (response_delay > 1 ||
          (response_delay == 1 && data_access_out.presponse.pready))
        defer(response_delay, response_delay - 1);
      if (prefetch_out.pvalid) {
        CHECK(scenario == 0 && accepted == 1 && prefetches == 0 &&
              prefetch_out.pbits.paddress == UINT64_C(1280) &&
              prefetch_out.pbits.poperation == 2);
        defer(prefetches, prefetches + 1);
      }
      if (data_access_out.prequest.pvalid &&
          data_access_out.prequest.pbits.paddress == UINT64_C(1024))
        defer(attempts, attempts + 1);
      if (data_access_out.prequest.pvalid && data_access_in.prequest.pready) {
        CHECK(!data_access_in.prequest_uaccess_ufault);
        if (data_access_out.prequest.pbits.paccess == 2) {
          defer(stores, stores + 1);
          if (scenario == 0) {
            switch (stores) {
            case 0: {
              CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(1088) &&
                    data_access_out.prequest.pbits.pdata ==
                        UINT64_C(1073741824) &&
                    accepted == 1 && attempts == 4);
            } break;
            case 1: {
              CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(1092) &&
                    data_access_out.prequest.pbits.pdata ==
                        UINT64_C(2139095040));
            } break;
            case 2: {
              CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(1096) &&
                    data_access_out.prequest.pbits.pdata == 8);
            } break;
            case 3: {
              CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(1100) &&
                    data_access_out.prequest.pbits.pdata ==
                        UINT64_C(1077936128));
            } break;
            case 4: {
              {
                CHECK(data_access_out.prequest.pbits.paddress ==
                          UINT64_C(1112) &&
                      data_access_out.prequest.pbits.pdata == 42);
              }
            } break;
            case 5: {
              CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(1136) &&
                    data_access_out.prequest.pbits.pdata ==
                        UINT64_C(1084227584));
            } break;
            case 6: {
              {
                CHECK(data_access_out.prequest.pbits.paddress ==
                          UINT64_C(1140) &&
                      data_access_out.prequest.pbits.pdata ==
                          UINT64_C(1086324736));
                defer(done, 1);
              }
            } break;
            default: {
              fail(1, "duplicated store");
            } break;
            }
          } else {
            switch (stores) {
            case 0: {
              CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(1120) &&
                    data_access_out.prequest.pbits.pdata ==
                        UINT64_C(1065353216));
            } break;
            case 1: {
              CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(1124) &&
                    data_access_out.prequest.pbits.pdata == 0);
            } break;
            case 2: {
              CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(1128) &&
                    data_access_out.prequest.pbits.pdata == 5);
            } break;
            case 3: {
              {
                CHECK(data_access_out.prequest.pbits.paddress ==
                          UINT64_C(1132) &&
                      data_access_out.prequest.pbits.pdata == UINT64_C(1024) &&
                      accepted == 0);
                defer(done, 1);
              }
            } break;
            default: {
              fail(1, "younger store escaped fault");
            } break;
            }
          }
        } else {
          if (data_access_out.prequest.pbits.paddress == UINT64_C(1024))
            defer(accepted, accepted + 1);
          else
            CHECK(
                scenario == 0 &&
                ((data_access_out.prequest.pbits.paddress == UINT64_C(1104) &&
                  (data_access_out.prequest.pbits.paccess == 3 ||
                   data_access_out.prequest.pbits.paccess == 4 ||
                   data_access_out.prequest.pbits.paccess == 5)) ||
                 (data_access_out.prequest.pbits.paddress == UINT64_C(1152) &&
                  data_access_out.prequest.pbits.paccess == 1 &&
                  bit_slice(data_access_out.prequest.pbits.pcontext.pwriteback,
                            7, (8) - (7) + 1) == 2)));
          defer(response_delay,
                data_access_out.prequest.pbits.paddress == UINT64_C(1024) ? 20
                                                                          : 4);
          defer(pending_response.pdata,
                data_access_out.prequest.pbits.paddress == UINT64_C(1152)
                    ? UINT64_C(1077936128)
                    : 42);
          defer(pending_response.pcontext.pwriteback,
                data_access_out.prequest.pbits.pcontext.pwriteback);
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
  interrupts = {};
  {
    for (scenario = 0; scenario < 3; scenario++) {
      reset = 1;
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      falling();
      for (int cycles = 0; cycles < 2500 && !done; cycles++)
        falling();
      CHECK(done);
      CHECK(restart_accepts > 0);
      CHECK(prefetches == (scenario == 0 ? 1 : 0));
      if (scenario == 2)
        CHECK(killed_fixed_launches == 1 && killed_fixed_returns == 1);
    };
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
// Samples the same scalar-adapter boundary as the former hierarchical probe.
// Called during the edge, before deferred host state is published.
extern "C" void test_fp_observe(std::uint64_t rst, std::uint64_t issue_valid,
                                std::uint64_t issue_pc,
                                std::uint64_t result_valid,
                                std::uint64_t result_pc,
                                std::uint64_t authorize_valid,
                                std::uint64_t authorize_pc) {
  if (!rst && scenario == 2) {
    if (issue_valid && issue_pc == 0x134)
      ++killed_fixed_launches;
    if (result_valid && result_pc == 0x134)
      ++killed_fixed_returns;
    CHECK(!(authorize_valid && authorize_pc == 0x134));
  }
}
