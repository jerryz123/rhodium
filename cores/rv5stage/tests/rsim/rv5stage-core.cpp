// Preserves the rv5stage-core cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using RiscvSplitResult =
    std::remove_cvref_t<decltype(split_completion_in.pbits)>;
using RiscvSplitResult =
    std::remove_cvref_t<decltype(split_completion_in.pbits)>;
// Checks forwarding, replay, branch-over-deferred-load hazards, and retained split completion.

bool split_pending, split_load_done, split_store_done;

std::uint8_t split_delay;

bool instruction_response_valid;

std::uint32_t instruction_response_bits;

bool data_response_valid;

std::uint64_t data_response_bits;

std::uint8_t data_response_rd;

std::uint8_t load_requests;

bool first_response_sent;

std::uint8_t first_response_delay;

bool second_response_sent;

std::uint8_t second_response_delay;

std::uint8_t stores_seen;

bool rejected_first_load;

bool saw_replay_refetch;

bool saw_fetch_flush;

bool saw_redirect;

bool saw_fence_i_invalidate;

bool saw_fence_i_refetch;

std::uint8_t fetch_flushes;
constexpr std::uint8_t MEMORY_LOAD = UINT64_C(1);
constexpr std::uint8_t WRITEBACK_ACK_KIND = UINT64_C(0);
constexpr std::uint8_t WRITEBACK_INTEGER_KIND = UINT64_C(1);

std::uint32_t instruction_at(std::uint64_t address) {
  switch (address) {
  case UINT64_C(0x100000000): {
    return UINT64_C(12931); // ld x5, 0(x0)
  } break;
  case UINT64_C(0x100000004): {
    return UINT64_C(0x28533); // add x10, x5, x0
  } break;
  case UINT64_C(0x100000008): {
    return UINT64_C(0x1003403); // ld x8, 16(x0), deferred past branch
  } break;
  case UINT64_C(0x10000000c): {
    return UINT64_C(0x100313); // addi x6, x0, 1
  } break;
  case UINT64_C(0x100000010): {
    return UINT64_C(0x2031863); // bne x6, x0, +48
  } break;
  case UINT64_C(0x100000014): {
    return UINT64_C(0x2603023); // sd x6, 32(x0), must be squashed
  } break;
  case UINT64_C(0x100000040): {
    return UINT64_C(0x6503b3); // add x7, x10, x6
  } break;
  case UINT64_C(0x100000044): {
    return UINT64_C(0x900413); // addi x8, x0, 9
  } break;
  case UINT64_C(0x100000048): {
    return UINT64_C(4111); // fence.i
  } break;
  case UINT64_C(0x10000004c): {
    return UINT64_C(0x8004ef); // jal x9, +8
  } break;
  case UINT64_C(0x100000050): {
    return UINT64_C(0x2603023); // sd x6, 32(x0), must be squashed
  } break;
  case UINT64_C(0x100000054): {
    return UINT64_C(0x703423); // sd x7, 8(x0)
  } break;
  case UINT64_C(0x100000058): {
    return UINT64_C(0x803823); // sd x8, 16(x0)
  } break;
  case UINT64_C(0x10000005c): {
    return UINT64_C(0x903c23); // sd x9, 24(x0)
  } break;
  case UINT64_C(0x100000060): {
    return UINT64_C(0x100593); // addi x11, x0, 1
  } break;
  case UINT64_C(0x100000064): {
    return UINT64_C(0x258593); // addi x11, x11, 2
  } break;
  case UINT64_C(0x100000068): {
    return UINT64_C(0xb58633); // add x12, x11, x11; newest writer wins
  } break;
  case UINT64_C(0x10000006c): {
    return UINT64_C(0x400693); // addi x13, x0, 4
  } break;
  case UINT64_C(0x100000070): {
    return UINT64_C(0xd60733); // add x14, x12, x13; two forwarding sources
  } break;
  case UINT64_C(0x100000074): {
    return UINT64_C(0x970013); // addi x0, x14, 9; must not forward to x0
  } break;
  case UINT64_C(0x100000078): {
    return UINT64_C(0xe007b3); // add x15, x0, x14
  } break;
  case UINT64_C(0x10000007c): {
    return UINT64_C(0x2c03423); // sd x12, 40(x0)
  } break;
  case UINT64_C(0x100000080): {
    return UINT64_C(0x2e03823); // sd x14, 48(x0)
  } break;
  case UINT64_C(0x100000084): {
    return UINT64_C(0x2f03c23); // sd x15, 56(x0)
  } break;
  case UINT64_C(0x100000088): {
    return UINT64_C(0x1400813); // addi x16, x0, 20
  } break;
  case UINT64_C(0x10000008c): {
    return UINT64_C(0x100893); // addi x17, x0, 1
  } break;
  case UINT64_C(0x100000090): {
    return UINT64_C(0x200913); // addi x18, x0, 2
  } break;
  case UINT64_C(0x100000094): {
    return UINT64_C(0x11809b3); // add x19, x16, x17; WB-to-ID capture
  } break;
  case UINT64_C(0x100000098): {
    return UINT64_C(0x5303023); // sd x19, 64(x0)
  } break;
  case UINT64_C(0x10000009c): {
    return UINT64_C(0x4003423); // sd x0, 72(x0)
  } break;
  case UINT64_C(0x1000000a0): {
    return UINT64_C(
        17840643); // ld x20, 17(x0), retained until split completion
  } break;
  case UINT64_C(0x1000000a4): {
    return UINT64_C(0x5403c23); // sd x20, 88(x0), checks split load writeback
  } break;
  case UINT64_C(0x1000000a8): {
    return UINT64_C(
        78657699); // sd x11, 81(x0), retained until split completion
  } break;
  default: {
    return UINT64_C(19);
  } break;
  }
}

void drive() {
  {
    instruction_access_in.prequest.pready =
        instruction_access_out.pflush || !instruction_response_valid;
    instruction_access_in.presponse.pvalid = instruction_response_valid;
    instruction_access_in.presponse.pbits.pword = instruction_response_bits;
    instruction_access_in.presponse.pbits.ppage_ufault = UINT64_C(0);
    instruction_access_in.presponse.pbits.paccess_ufault = UINT64_C(0);
    data_access_in.prequest.pready =
        rejected_first_load || !data_access_out.prequest.pvalid ||
        data_access_out.prequest.pbits.paddress != UINT64_C(0);
    data_access_in.prequest_ufault = UINT64_C(0);
    data_access_in.prequest_uaccess_ufault = UINT64_C(0);
    data_access_in.presponse.pvalid = data_response_valid;
    data_access_in.presponse.pbits.pdata = data_response_bits;
    data_access_in.presponse.pbits.pcontext.pwriteback =
        memory_integer(data_response_rd);
    data_access_in.pdrained = UINT64_C(1);
    data_access_in.preservation_uvalid = UINT64_C(0);
  }
}

void observe() {
  {
    if (reset) {
      defer(instruction_response_valid, UINT64_C(0));
      defer(instruction_response_bits,
            std::remove_cvref_t<decltype(instruction_response_bits)>{});
      defer(data_response_valid, UINT64_C(0));
      defer(data_response_bits,
            std::remove_cvref_t<decltype(data_response_bits)>{});
      defer(data_response_rd,
            std::remove_cvref_t<decltype(data_response_rd)>{});
      defer(load_requests, std::remove_cvref_t<decltype(load_requests)>{});
      defer(first_response_sent, UINT64_C(0));
      defer(first_response_delay,
            std::remove_cvref_t<decltype(first_response_delay)>{});
      defer(second_response_sent, UINT64_C(0));
      defer(second_response_delay,
            std::remove_cvref_t<decltype(second_response_delay)>{});
      defer(stores_seen, std::remove_cvref_t<decltype(stores_seen)>{});
      defer(rejected_first_load, UINT64_C(0));
      defer(saw_replay_refetch, UINT64_C(0));
      defer(saw_fetch_flush, UINT64_C(0));
      defer(saw_redirect, UINT64_C(0));
      defer(saw_fence_i_invalidate, UINT64_C(0));
      defer(saw_fence_i_refetch, UINT64_C(0));
      defer(fetch_flushes, std::remove_cvref_t<decltype(fetch_flushes)>{});
      defer(split_completion_in,
            std::remove_cvref_t<decltype(split_completion_in)>{});
      defer(split_pending, 0);
      defer(split_delay, std::remove_cvref_t<decltype(split_delay)>{});
      defer(split_load_done, 0);
      defer(split_store_done, 0);
    } else {
      defer(split_completion_in.pvalid, 0);
      if (split_completion_in.pvalid) {
        defer(split_pending, 0);
        if (split_completion_in.pbits.presponse.pcontext.pwriteback ==
            memory_integer(UINT64_C(20)))
          defer(split_load_done, 1);
        else
          defer(split_store_done, 1);
      } else if (split_pending) {
        if (split_delay != 0)
          defer(split_delay, split_delay - UINT64_C(1));
        else
          defer(split_completion_in.pvalid, 1);
      }
      if (instruction_access_out.pflush) {
        defer(instruction_response_valid, UINT64_C(0));
        defer(saw_fetch_flush, UINT64_C(1));
        defer(fetch_flushes, fetch_flushes + UINT64_C(1));
        if (instruction_access_out.pinvalidate_uall)
          defer(saw_fence_i_invalidate, UINT64_C(1));
      } else if (instruction_response_valid &&
                 instruction_access_out.presponse.pready) {
        defer(instruction_response_valid, UINT64_C(0));
      }
      if (instruction_access_out.prequest.pvalid &&
          instruction_access_in.prequest.pready) {
        defer(instruction_response_valid, UINT64_C(1));
        defer(instruction_response_bits,
              instruction_at(instruction_access_out.prequest.pbits.paddress));
        if (instruction_access_out.prequest.pbits.paddress ==
                UINT64_C(0x100000000) &&
            (rejected_first_load || (data_access_out.prequest.pvalid &&
                                     !data_access_in.prequest.pready))) {
          CHECK(instruction_access_out.pflush || fetch_flushes >= 1);
          defer(saw_replay_refetch, UINT64_C(1));
        }
        if (instruction_access_out.prequest.pbits.paddress ==
            UINT64_C(0x100000040)) {
          CHECK(instruction_access_out.pflush);
          CHECK(first_response_sent && !second_response_sent);
          defer(saw_redirect, UINT64_C(1));
        }
        if (instruction_access_out.prequest.pbits.paddress ==
                UINT64_C(0x10000004c) &&
            (saw_fence_i_invalidate || instruction_access_out.pinvalidate_uall))
          defer(saw_fence_i_refetch, UINT64_C(1));
        if (instruction_access_out.prequest.pbits.paddress ==
                UINT64_C(0x1000000ac) &&
            (split_store_done ||
             (split_completion_in.pvalid &&
              bit_slice(split_completion_in.pbits.presponse.pcontext.pwriteback,
                        7, 2) == WRITEBACK_ACK_KIND))) {
          CHECK(instruction_access_out.pflush && split_load_done &&
                stores_seen == 10 && load_requests == 3);

          throw Finished{};
        }
      }

      if (data_access_out.prequest.pvalid && !data_access_in.prequest.pready) {
        CHECK(!rejected_first_load && load_requests == 0 &&
              data_access_out.prequest.pbits.paccess == MEMORY_LOAD &&
              data_access_out.prequest.pbits.paddress == UINT64_C(0) &&
              bit_slice(data_access_out.prequest.pbits.pcontext.pwriteback, 7,
                        2) == WRITEBACK_INTEGER_KIND &&
              memory_rd(data_access_out.prequest.pbits.pcontext.pwriteback) ==
                  UINT64_C(5));
        defer(rejected_first_load, UINT64_C(1));
      }

      if (data_response_valid && data_access_out.presponse.pready) {
        defer(data_response_valid, UINT64_C(0));
        if (data_response_rd == UINT64_C(5)) {
          defer(first_response_sent, UINT64_C(1));
          defer(second_response_delay, UINT64_C(3));
        } else {
          CHECK(data_response_rd == UINT64_C(8));
          defer(second_response_sent, UINT64_C(1));
        }
      } else if (!data_response_valid) {
        if (load_requests != 0 && !first_response_sent) {
          if (first_response_delay != 0)
            defer(first_response_delay, first_response_delay - UINT64_C(1));
          else {
            defer(data_response_valid, UINT64_C(1));
            defer(data_response_bits, UINT64_C(42));
            defer(data_response_rd, UINT64_C(5));
          }
        } else if (saw_redirect && first_response_sent &&
                   !second_response_sent) {
          if (second_response_delay != 0)
            defer(second_response_delay, second_response_delay - UINT64_C(1));
          else {
            defer(data_response_valid, UINT64_C(1));
            defer(data_response_bits, UINT64_C(100));
            defer(data_response_rd, UINT64_C(8));
          }
        }
      }

      if (data_access_out.prequest.pvalid && data_access_in.prequest.pready) {
        if (data_access_out.prequest.pbits.paccess == MEMORY_LOAD) {
          if (load_requests == 0) {
            CHECK(rejected_first_load &&
                  (saw_replay_refetch ||
                   (instruction_access_out.pflush &&
                    instruction_access_out.prequest.pvalid &&
                    instruction_access_in.prequest.pready &&
                    instruction_access_out.prequest.pbits.paddress ==
                        UINT64_C(0x100000000))));
            CHECK(
                data_access_out.prequest.pbits.paddress == UINT64_C(0) &&
                bit_slice(data_access_out.prequest.pbits.pcontext.pwriteback, 7,
                          2) == WRITEBACK_INTEGER_KIND &&
                memory_rd(data_access_out.prequest.pbits.pcontext.pwriteback) ==
                    UINT64_C(5));
          } else if (load_requests == 1)
            CHECK(
                load_requests == 1 &&
                data_access_out.prequest.pbits.paddress == UINT64_C(16) &&
                bit_slice(data_access_out.prequest.pbits.pcontext.pwriteback, 7,
                          2) == WRITEBACK_INTEGER_KIND &&
                memory_rd(data_access_out.prequest.pbits.pcontext.pwriteback) ==
                    UINT64_C(8));
          else {
            CHECK(load_requests == 2 && stores_seen == 8 && !split_pending &&
                  !split_load_done &&
                  data_access_out.prequest.pbits.paddress == 17 &&
                  data_access_out.prequest.pbits.pcontext.pwriteback ==
                      memory_integer(UINT64_C(20)));
            defer(split_pending, 1);
            defer(split_delay, 6);
            defer(split_completion_in.pbits,
                  std::remove_cvref_t<decltype(split_completion_in.pbits)>{});
            defer(split_completion_in.pbits.presponse.pdata, 100);
            defer(split_completion_in.pbits.presponse.pcontext.pwriteback,
                  memory_integer(UINT64_C(20)));
          }
          if (load_requests == 0)
            defer(first_response_delay, UINT64_C(1));
          defer(load_requests, load_requests + UINT64_C(1));
        } else {
          CHECK(data_access_out.prequest.pbits.paddress != UINT64_C(32));
          CHECK(second_response_sent);
          if (stores_seen == 0) {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(8) &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(43) &&
                  bit_slice(data_access_out.prequest.pbits.pcontext.pwriteback,
                            7, 2) == WRITEBACK_ACK_KIND);
            defer(stores_seen, 1);
          } else if (stores_seen == 1) {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(16) &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(9) &&
                  bit_slice(data_access_out.prequest.pbits.pcontext.pwriteback,
                            7, 2) == WRITEBACK_ACK_KIND);
            defer(stores_seen, 2);
          } else if (stores_seen == 2) {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(24) &&
                  data_access_out.prequest.pbits.pdata ==
                      UINT64_C(0x100000050) &&
                  bit_slice(data_access_out.prequest.pbits.pcontext.pwriteback,
                            7, 2) == WRITEBACK_ACK_KIND);
            CHECK(saw_fence_i_invalidate && saw_fence_i_refetch);
            CHECK(rejected_first_load && saw_replay_refetch);
            defer(stores_seen, 3);
          } else {
            CHECK(bit_slice(data_access_out.prequest.pbits.pcontext.pwriteback,
                            7, 2) == WRITEBACK_ACK_KIND);
            switch (stores_seen) {
            case 3: {
              CHECK(data_access_out.prequest.pbits.paddress == 40 &&
                    data_access_out.prequest.pbits.pdata == 6);
            } break;
            case 4: {
              CHECK(data_access_out.prequest.pbits.paddress == 48 &&
                    data_access_out.prequest.pbits.pdata == 10);
            } break;
            case 5: {
              CHECK(data_access_out.prequest.pbits.paddress == 56 &&
                    data_access_out.prequest.pbits.pdata == 10);
            } break;
            case 6: {
              CHECK(data_access_out.prequest.pbits.paddress == 64 &&
                    data_access_out.prequest.pbits.pdata == 21);
            } break;
            case 7: {
              CHECK(data_access_out.prequest.pbits.paddress == 72 &&
                    data_access_out.prequest.pbits.pdata == 0);
            } break;
            case 8: {
              {
                CHECK(split_load_done && !split_pending &&
                      data_access_out.prequest.pbits.paddress == 88 &&
                      data_access_out.prequest.pbits.pdata == 100);
              }
            } break;
            case 9: {
              {
                CHECK(data_access_out.prequest.pbits.paddress == 81 &&
                      data_access_out.prequest.pbits.pdata == 3);
                CHECK(!split_pending && !split_store_done);
                defer(split_pending, 1);
                defer(split_delay, 6);
                defer(
                    split_completion_in.pbits,
                    std::remove_cvref_t<decltype(split_completion_in.pbits)>{});
                defer(split_completion_in.pbits.presponse.pcontext.pwriteback,
                      data_access_out.prequest.pbits.pcontext.pwriteback);
              }
            } break;
            default: {
              fail(1, "unexpected forwarding test store");
            } break;
            }
            defer(stores_seen, stores_seen + UINT64_C(1));
          }
        }
      }
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = UINT64_C(1);
  time_counter = {};
  hart_id = {};
  {
    interrupts = {};

    for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
      rising();
    settle();
    reset = UINT64_C(0);
    rising();
    settle();
    for (int repeat_index = 0; repeat_index < (200); ++repeat_index)
      rising();
    fail(1, "core did not complete the scoreboard scenario");
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
