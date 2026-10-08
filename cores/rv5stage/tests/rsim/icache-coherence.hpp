// Shares the public-port icache-coherence oracle across hardware specializations.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Checks dirty-code visibility, fence synchronization, and snapshot retention under outer replacement.

int requests = 0, completions = 0;

std::uint64_t data_result;

void tick() {
  if (!reset) {
    if (instruction_request)
      requests++;
    if (host_out.presponse.pvalid) {
      CHECK(!host_out.presponse.pbits.paccess_ufault);
      completions++;
      data_result = host_out.presponse.pbits.pdata;
    }
  }
  rising();
  settle();
}
void access(std::uint8_t write, int address, std::uint64_t value = 0) {
  int previous;
  previous = completions;
  host_in.prequest.pbits = {};
  host_in.prequest.pbits.paddress = ((address)&low_mask(64));
  host_in.prequest.pbits.paccess = write ? 2 : 1;
  host_in.prequest.pbits.pwidth = 3;
  host_in.prequest.pbits.pbyte_umask = UINT64_C(255);
  host_in.prequest.pbits.pdata = value;
  host_in.prequest.pbits.pcontext.pwriteback =
      write ? UINT64_C(0) : UINT64_C(128);
  host_in.prequest.pvalid = 1;
  settle();
  for (int c = 0; !host_out.prequest.pready && c < 10000; c++)
    tick();
  CHECK(host_out.prequest.pready);
  tick();
  host_in.prequest.pvalid = 0;
  for (int c = 0; completions == previous && c < 10000; c++)
    tick();
  CHECK(completions == previous + 1);
}
void fetch_word(int address, std::uint32_t expected) {
  bool done;
  done = 0;
  for (int attempt = 0; !done && attempt < 1000; attempt++) {
    fetch_in.prequest.pbits.paddress = ((address)&low_mask(64));
    virtual_lookup_in = {.pvalid = 1, .pbits = ((address)&low_mask(64))};
    settle();
    for (int c = 0; !virtual_lookup_out.pready && c < 10000; c++)
      tick();
    CHECK(virtual_lookup_out.pready);
    tick();
    virtual_lookup_in.pvalid = 0;
    fetch_in.prequest.pvalid = 1;
    tick();
    fetch_in.prequest.pvalid = 0;
    done = fetch_out.presponse.pvalid && !fetch_out.presponse.pbits.preplay;
  }
  CHECK(fetch_out.presponse.pvalid &&
        !fetch_out.presponse.pbits.paccess_ufault &&
        fetch_out.presponse.pbits.pdata == expected);
  tick();
}
void fence_i() {
  for (int c = 0; !host_out.pdrained && c < 10000; c++)
    tick();
  CHECK(host_out.pdrained);
  fetch_in.pinvalidate_uall = 1;
  tick();
  fetch_in.pinvalidate_uall = 0;
}

void drive() {}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = 1;
  {
    host_in = {};
    host_in.presponse.pready = 1;
    fetch_in = {};
    virtual_lookup_in = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick();
    reset = 0;
    // Backing RAM still contains its old value: the instruction read must intervene on L1D.
    access(1, UINT64_C(16384), UINT64_C(0x1300100293));
    fetch_word(UINT64_C(16384), UINT64_C(0x100293));
    access(1, UINT64_C(16384), UINT64_C(0x1300200293));
    // This implementation deliberately retains the old snapshot until synchronization.
    fetch_word(UINT64_C(16384), UINT64_C(0x100293));
    fence_i();
    fetch_word(UINT64_C(16384), UINT64_C(0x200293));
    // Repeated unrelated data allocations replace this line in the tiny inclusive LLC.
    for (int i = 0; i < 32; i++)
      access(1, UINT64_C(20480) + 128 * i, ((i)&low_mask(64)));
    {
      int before_fetch;
      before_fetch = requests;
      fetch_word(UINT64_C(16384), UINT64_C(0x200293));
      CHECK(requests == before_fetch);
    }
    fence_i();
    fetch_word(UINT64_C(16384), UINT64_C(0x200293));
    access(0, UINT64_C(16384));
    CHECK(data_result == UINT64_C(0x1300200293));

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
