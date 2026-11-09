// Checks replay, cancellation, and exactly-once instruction routing.
// SPDX-License-Identifier: Apache-2.0
#include "../../../tests/rsim/driver.hpp"
using fetch_word = std::remove_cvref_t<decltype(core_out.presponse.pbits.pdata)>;
bool cache_replay = 0;
int transactions = 0;
std::uint64_t accepted_address = 0;
bool accepted_device = false;
void tick() {
  rising();
  settle();
}
void attempt(std::uint64_t address, std::uint8_t cached, std::uint8_t replay,
             std::uint64_t word = 0, std::uint8_t device = 0,
             std::uint8_t access_fault = 0) {
  core_in.prequest = {
      .pvalid = 1,
      .pbits = {.paddress = address, .pcacheable = cached, .pdevice = device}};
  tick();
  core_in.prequest.pvalid = 0;
  CHECK(core_out.presponse.pvalid &&
        core_out.presponse.pbits.preplay == replay);
  CHECK(core_out.presponse.pbits.paccess_ufault == access_fault);
  if (!replay)
    CHECK(core_out.presponse.pbits.pdata == fetch_word(word));
  tick();
}

void drive() {}

void observe() {
  {
    if (reset || cache_out.pflush)
      defer(cache_in.presponse,
            std::remove_cvref_t<decltype(cache_in.presponse)>{});
    else {
      defer(cache_in.presponse.pvalid, cache_out.prequest.pvalid);
      defer(cache_in.presponse.pbits,
            std::remove_cvref_t<decltype(cache_in.presponse.pbits)>{
                .pdata = fetch_word(cache_out.prequest.pbits.paddress),
                .paccess_ufault = 0,
                .preplay = cache_replay});
    }
    if (!reset && uncached_out.prequest.pvalid && uncached_in.prequest.pready) {
      transactions++;
      accepted_address = uncached_out.prequest.pbits.paddress;
      accepted_device = uncached_out.prequest.pbits.pdevice;
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  core_in = {};
  uncached_in = {};
  {
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick();
    reset = 0;
    uncached_in.prequest.pready = 1;

    // Back-to-back cached words have no response ownership queue.
    core_in.prequest = {
        .pvalid = 1,
        .pbits = {.paddress = UINT64_C(4096), .pcacheable = 1, .pdevice = 0}};
    tick();
    CHECK(core_out.presponse.pvalid && !core_out.presponse.pbits.preplay &&
          core_out.presponse.pbits.pdata == UINT64_C(4096));
    core_in.prequest.pbits.paddress = UINT64_C(4100);
    tick();
    CHECK(core_out.presponse.pvalid && !core_out.presponse.pbits.preplay &&
          core_out.presponse.pbits.pdata == UINT64_C(4100));
    core_in.prequest.pvalid = 0;
    tick();
    cache_replay = 1;
    attempt(UINT64_C(8192), 1, 1);
    cache_replay = 0;
    attempt(UINT64_C(8192), 1, 0, UINT64_C(8192));

    attempt(UINT64_C(49152), 0, 1);
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
      attempt(UINT64_C(49152), 0, 1);
    CHECK(transactions == 1);
    uncached_in.presponse = {.pvalid = 1,
                             .pbits = {.pdata = fetch_word(UINT64_C(0x1234567812345678)),
                                       .paccess_ufault = 0}};
    tick();
    uncached_in.presponse.pvalid = 0;
    attempt(UINT64_C(49152), 0, 0, UINT64_C(0x1234567812345678));
    CHECK(transactions == 1);

    // Killing S1 must not issue IO or suppress the preceding cached S2 word.
    core_in.prequest = {
        .pvalid = 1,
        .pbits = {.paddress = UINT64_C(4104), .pcacheable = 1, .pdevice = 0}};
    tick();
    core_in.prequest.pbits = {
        .paddress = UINT64_C(53248), .pcacheable = 0, .pdevice = 1};
    core_in.ps1_ukill = 1;
    settle();
    CHECK(core_out.presponse.pvalid &&
          core_out.presponse.pbits.pdata == UINT64_C(4104));
    tick();
    core_in.prequest.pvalid = 0;
    core_in.ps1_ukill = 0;
    CHECK(!core_out.presponse.pvalid && !uncached_out.prequest.pvalid);
    CHECK(transactions == 1);

    // A stalled uncached request may be retried, but only acceptance owns work.
    uncached_in.prequest.pready = 0;
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      attempt(UINT64_C(53248), 0, 1);
    CHECK(transactions == 1);
    uncached_in.prequest.pready = 1;
    attempt(UINT64_C(53248), 0, 1);
    CHECK(transactions == 2);
    core_in.pflush = 1;
    core_in.pinvalidate_uall = 1;
    settle();
    CHECK(cache_out.pflush && cache_out.pinvalidate_uall &&
          uncached_out.pflush);
    CHECK(!core_out.presponse.pvalid && !uncached_out.prequest.pvalid &&
          !uncached_out.presponse.pready);
    tick();
    core_in.pflush = 0;
    core_in.pinvalidate_uall = 0;
    attempt(UINT64_C(4112), 1, 0, UINT64_C(4112));
    CHECK(transactions == 2);

    // Completed data cannot satisfy a retry with different physical attributes.
    attempt(UINT64_C(57344), 0, 1);
    CHECK(transactions == 3 && !accepted_device);
    uncached_in.presponse = {
        .pvalid = 1,
        .pbits = {.pdata = fetch_word(UINT64_C(0xdeadbeef01234567)),
                  .paccess_ufault = 1}};
    tick();
    uncached_in.presponse.pvalid = 0;
    attempt(UINT64_C(57344), 0, 1, 0, 1);
    CHECK(transactions == 3);
    attempt(UINT64_C(57344), 0, 1, 0, 1);
    CHECK(transactions == 4 && accepted_device && accepted_address == UINT64_C(57344));
    uncached_in.presponse.pvalid = 1;
    tick();
    uncached_in.presponse.pvalid = 0;

    // Invalidation alone is not consumer cancellation and retains completed IO.
    core_in.pinvalidate_uall = 1;
    settle();
    CHECK(cache_out.pinvalidate_uall && !cache_out.pflush && !uncached_out.pflush);
    tick();
    core_in.pinvalidate_uall = 0;
    attempt(UINT64_C(57344), 0, 0, UINT64_C(0xdeadbeef01234567), 1, 1);
    CHECK(transactions == 4);

    // The executable high RAM region can be uncached via PBMT. Bit 32 is the
    // only difference from the low ROM request; a stale completion must not hit.
    constexpr std::uint64_t low = 0xf000, high = (UINT64_C(1) << 32) | low;
    attempt(low, 0, 1);
    CHECK(transactions == 5 && accepted_address == low);
    uncached_in.presponse.pbits.paccess_ufault = 0;
    uncached_in.presponse.pbits.pdata = fetch_word(0x11112222);
    uncached_in.presponse.pvalid = 1;
    tick();
    uncached_in.presponse.pvalid = 0;
    attempt(high, 0, 1);
    CHECK(transactions == 5);
    attempt(high, 0, 1);
    CHECK(transactions == 6 && accepted_address == high && !accepted_device);
    uncached_in.presponse.pbits.pdata = fetch_word(0x33334444);
    uncached_in.presponse.pvalid = 1;
    tick();
    uncached_in.presponse.pvalid = 0;
    attempt(high, 0, 0, 0x33334444);
    CHECK(transactions == 6);

    // Keep the lowest block-number bit as well as the highest address bit.
    attempt(high, 0, 1);
    uncached_in.presponse.pvalid = 1;
    tick();
    uncached_in.presponse.pvalid = 0;
    const auto adjacent = high + sizeof(fetch_word);
    attempt(adjacent, 0, 1);
    CHECK(transactions == 7);
    attempt(adjacent, 0, 1);
    CHECK(transactions == 8 && accepted_address == adjacent);

    throw Finished{};
  }
}

int main() {
  return run_test([] {
    cycle_limit = 10002;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
