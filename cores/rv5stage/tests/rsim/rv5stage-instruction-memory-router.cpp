// Checks replay, cancellation, and exactly-once instruction routing.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
bool cache_replay = 0;
int transactions = 0;
void tick() {
  rising();
  settle();
}
void attempt(std::uint64_t address, std::uint8_t cached, std::uint8_t replay,
             std::uint32_t word = 0) {
  core_in.prequest = {
      .pvalid = 1,
      .pbits = {.paddress = address, .pcacheable = cached, .pdevice = 0}};
  tick();
  core_in.prequest.pvalid = 0;
  CHECK(core_out.presponse.pvalid &&
        core_out.presponse.pbits.preplay == replay);
  if (!replay)
    CHECK(core_out.presponse.pbits.presponse.pword == word);
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
                .pdata = std::uint32_t(cache_out.prequest.pbits.paddress),
                .paccess_ufault = 0,
                .preplay = cache_replay});
    }
    if (!reset && uncached_out.prequest.pvalid && uncached_in.prequest.pready)
      transactions++;
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
          core_out.presponse.pbits.presponse.pword == UINT64_C(4096));
    core_in.prequest.pbits.paddress = UINT64_C(4100);
    tick();
    CHECK(core_out.presponse.pvalid && !core_out.presponse.pbits.preplay &&
          core_out.presponse.pbits.presponse.pword == UINT64_C(4100));
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
                             .pbits = {.pword = UINT64_C(0x12345678),
                                       .ppage_ufault = 0,
                                       .paccess_ufault = 0}};
    tick();
    uncached_in.presponse.pvalid = 0;
    attempt(UINT64_C(49152), 0, 0, UINT64_C(0x12345678));
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
          core_out.presponse.pbits.presponse.pword == UINT64_C(4104));
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
          uncached_out.pflush && uncached_out.pinvalidate_uall);
    CHECK(!core_out.presponse.pvalid && !uncached_out.prequest.pvalid &&
          !uncached_out.presponse.pready);
    tick();
    core_in.pflush = 0;
    core_in.pinvalidate_uall = 0;
    attempt(UINT64_C(4112), 1, 0, UINT64_C(4112));
    CHECK(transactions == 2);

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
