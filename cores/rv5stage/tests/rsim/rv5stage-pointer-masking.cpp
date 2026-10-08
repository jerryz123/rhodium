// Preserves the rv5stage-pointer-masking cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using RiscvSplitResult =
    std::remove_cvref_t<decltype(split_completion_in.pbits)>;
using RiscvSplitResult =
    std::remove_cvref_t<decltype(split_completion_in.pbits)>;
using instruction_resp_t =
    std::remove_cvref_t<decltype(instruction_access_in.presponse)>;
using data_resp_t = std::remove_cvref_t<decltype(data_access_in.presponse)>;
// Runs tagged load/store/prefetch traffic through PMM changes, replay, and precise fault reporting.

instruction_resp_t instruction_response;

data_resp_t data_response;

int stores, loads, faults, prefetches, flushes;

bool rejected;
constexpr std::uint64_t LOAD_VALUE = UINT64_C(0x123456789abcdef0);

std::uint32_t addi(int rd, int rs1, int imm) {
  return (field(((imm)&low_mask(12)), 12, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(UINT64_C(0), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(19), 7, 0));
}
std::uint32_t slli(int rd, int rs1, int amount) {
  return (field(UINT64_C(0), 6, 26) | field(((amount)&low_mask(6)), 6, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(UINT64_C(1), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(19), 7, 0));
}
std::uint32_t csrw(int csr, int rs1) {
  return (field(((csr)&low_mask(12)), 12, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(UINT64_C(1), 3, 12) |
          field(UINT64_C(0), 5, 7) | field(UINT64_C(115), 7, 0));
}
std::uint32_t csrr(int rd, int csr) {
  return (field(((csr)&low_mask(12)), 12, 20) | field(UINT64_C(0), 5, 15) |
          field(UINT64_C(2), 3, 12) | field(((rd)&low_mask(5)), 5, 7) |
          field(UINT64_C(115), 7, 0));
}
std::uint32_t sd(int rs2, int rs1, int imm) {
  return (field(((imm >> 5) & low_mask(7)), 7, 25) |
          field(((rs2)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(UINT64_C(3), 3, 12) |
          field(((imm)&low_mask(5)), 5, 7) | field(UINT64_C(35), 7, 0));
}
std::uint32_t ld(int rd, int rs1, int imm) {
  return (field(((imm)&low_mask(12)), 12, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(UINT64_C(3), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(3), 7, 0));
}
std::uint32_t instruction_at(std::uint64_t pc) {
  switch (pc) {
  case UINT64_C(4096): {
    return addi(1, 0, 2);
  } break;
  case UINT64_C(4100): {
    return slli(1, 1, 32);
  } break;
  case UINT64_C(4104): {
    return csrw(UINT64_C(266), 1); // PMM7, then refetch with the new policy.
  } break;
  case UINT64_C(4108): {
    return addi(1, 0, 1);
  } break;
  case UINT64_C(4112): {
    return slli(1, 1, 17);
  } break;
  case UINT64_C(4116): {
    return csrw(UINT64_C(768), 1); // MPRV, MPP=U, Bare.
  } break;
  case UINT64_C(4120): {
    return addi(2, 0, -1);
  } break;
  case UINT64_C(4124): {
    return slli(2, 2, 57);
  } break;
  case UINT64_C(4128): {
    return addi(2, 2, UINT64_C(256));
  } break;
  case UINT64_C(4132): {
    return sd(2, 2, 0); // Address untagged, stored register value tagged.
  } break;
  case UINT64_C(4136): {
    return ld(3, 2, 8); // Rejected once, then replayed.
  } break;
  case UINT64_C(4140): {
    return sd(3, 2, 16);
  } break;
  case UINT64_C(4144): {
    return addi(1, 0, 3);
  } break;
  case UINT64_C(4148): {
    return slli(1, 1, 32);
  } break;
  case UINT64_C(4152): {
    return csrw(UINT64_C(266), 1); // PMM16 must apply to the next memory op.
  } break;
  case UINT64_C(4156): {
    return addi(2, 0, -1);
  } break;
  case UINT64_C(4160): {
    return slli(2, 2, 48);
  } break;
  case UINT64_C(4164): {
    return addi(2, 2, UINT64_C(384));
  } break;
  case UINT64_C(4168): {
    return sd(2, 2, 0);
  } break;
  case UINT64_C(4172): {
    return UINT64_C(0x16013); // prefetch.i 0(x2): explicit access, not fetch.
  } break;
  case UINT64_C(4176): {
    return UINT64_C(0x116013); // prefetch.r 0(x2).
  } break;
  case UINT64_C(4180): {
    return UINT64_C(0x316013); // prefetch.w 0(x2).
  } break;
  case UINT64_C(4184): {
    return addi(4, 0, UINT64_C(1024));
  } break;
  case UINT64_C(4188): {
    return csrw(UINT64_C(773), 4);
  } break;
  case UINT64_C(4192): {
    return ld(3, 2, 1); // Misaligned scalar load is legal under Zicclsm.
  } break;
  case UINT64_C(4196): {
    return ld(3, 2, 9); // Inject an access fault; mtval must contain 0x189.
  } break;
  case UINT64_C(1024): {
    return csrr(5, UINT64_C(835));
  } break;
  case UINT64_C(1028): {
    return csrr(6, UINT64_C(834));
  } break;
  case UINT64_C(1032): {
    return addi(7, 0, UINT64_C(512));
  } break;
  case UINT64_C(1036): {
    return sd(5, 7, 0);
  } break;
  case UINT64_C(1040): {
    return sd(6, 7, 8);
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
        instruction_access_out.pflush || !instruction_response.pvalid;
    instruction_access_in.presponse = instruction_response;
    data_access_in = {};
    data_access_in.prequest.pready =
        rejected || !data_access_out.prequest.pvalid ||
        data_access_out.prequest.pbits.paddress != UINT64_C(264);
    data_access_in.prequest_uaccess_ufault =
        data_access_out.prequest.pvalid &&
        data_access_out.prequest.pbits.paddress == UINT64_C(393);
    data_access_in.presponse = data_response;
    data_access_in.pdrained =
        !data_response.pvalid && !split_completion_in.pvalid;
  }
}

void observe() {
  {
    if (reset) {
      defer(instruction_response,
            std::remove_cvref_t<decltype(instruction_response)>{});
      defer(data_response, std::remove_cvref_t<decltype(data_response)>{});
      defer(split_completion_in,
            std::remove_cvref_t<decltype(split_completion_in)>{});
      defer(stores, 0);
      defer(loads, 0);
      defer(faults, 0);
      defer(prefetches, 0);
      defer(flushes, 0);
      defer(rejected, 0);
    } else {
      defer(split_completion_in.pvalid, 0);
      CHECK(!translation_flush && !instruction_access_out.pinvalidate_uall);
      if (instruction_access_out.pflush) {
        defer(instruction_response.pvalid, 0);
        defer(flushes, flushes + 1);
      } else if (instruction_response.pvalid &&
                 instruction_access_out.presponse.pready)
        defer(instruction_response.pvalid, 0);
      if (instruction_access_out.prequest.pvalid &&
          instruction_access_in.prequest.pready) {
        CHECK(instruction_access_out.prequest.pbits.paddress < UINT64_C(8192));
        defer(
            instruction_response,
            instruction_resp_t{
                1,
                {instruction_at(instruction_access_out.prequest.pbits.paddress),
                 0, 0}});
      }
      if (data_response.pvalid && data_access_out.presponse.pready)
        defer(data_response.pvalid, 0);
      if (prefetch_out.pvalid) {
        CHECK(prefetch_out.pbits.paddress == UINT64_C(384) &&
              prefetch_out.pbits.poperation ==
                  ((prefetches + 1) & low_mask(2)));
        defer(prefetches, prefetches + 1);
      }
      if (data_access_out.prequest.pvalid) {
        if (!data_access_in.prequest.pready)
          defer(rejected, 1);
        else if (data_access_in.prequest_uaccess_ufault) {
          CHECK(data_access_out.prequest.pbits.paccess == 1 && loads == 2 &&
                stores == 3 && faults == 0);
          defer(faults, faults + 1);
        } else if (data_access_out.prequest.pbits.paccess == 1) {
          CHECK((data_access_out.prequest.pbits.paddress == UINT64_C(264) &&
                 loads == 0) ||
                (data_access_out.prequest.pbits.paddress == UINT64_C(385) &&
                 loads == 1));
          defer(loads, loads + 1);
          if ((data_access_out.prequest.pbits.paddress &
               ((UINT64_C(1) << data_access_out.prequest.pbits.pwidth) - 1)) !=
              0) {
            defer(split_completion_in.pvalid, 1);
            defer(
                split_completion_in.pbits,
                std::remove_cvref_t<decltype(split_completion_in.pbits)>{
                    .presponse =
                        {.paccess_ufault = UINT64_C(0),
                         .pdata = LOAD_VALUE,
                         .pcontext = {.pwriteback =
                                          data_access_out.prequest.pbits
                                              .pcontext.pwriteback,
                                      .porigin = data_access_out.prequest.pbits
                                                     .pcontext.porigin}},
                    .ppage_ufault = UINT64_C(0),
                    .pfault_uaddress = UINT64_C(0),
                    .pguest = {}});
          } else {
            defer(data_response.pvalid, 1);
            defer(data_response.pbits,
                  std::remove_cvref_t<decltype(data_response.pbits)>{
                      .paccess_ufault = UINT64_C(0),
                      .pdata = LOAD_VALUE,
                      .pcontext = data_access_out.prequest.pbits.pcontext});
          }
        } else {
          CHECK(data_access_out.prequest.pbits.paccess == 2);
          switch (stores) {
          case 0: {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(256) &&
                  data_access_out.prequest.pbits.pdata ==
                      UINT64_C(0xfe00000000000100));
          } break;
          case 1: {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(272) &&
                  data_access_out.prequest.pbits.pdata == LOAD_VALUE);
          } break;
          case 2: {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(384) &&
                  data_access_out.prequest.pbits.pdata ==
                      UINT64_C(0xffff000000000180));
          } break;
          case 3: {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(512) &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(393));
          } break;
          case 4: {
            {
              CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(520) &&
                    data_access_out.prequest.pbits.pdata == 5);
              CHECK(loads == 2 && faults == 1 && rejected && prefetches == 3 &&
                    flushes >= 4);

              throw Finished{};
            }
          } break;
          default: {
            fail(1, "unexpected store");
          } break;
          }
          defer(stores, stores + 1);
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
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
    for (int repeat_index = 0; repeat_index < (1500); ++repeat_index)
      falling();
    fail(1,
         "pointer-masking program timed out: stores=%0d loads=%0d flushes=%0d",
         stores, loads, flushes);
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
