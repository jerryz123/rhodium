// Checks PMA routing, IO admission, and memory response ordering.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
constexpr unsigned LOAD = 1, STORE = 2, LOAD_RESERVED = 3, ATOMIC = 5;
void tick() {
  settle();
  rising();
  settle();
  falling();
  settle();
}

void check_request(std::uint32_t address, std::uint8_t access,
                   std::uint8_t expected_cache, std::uint8_t expected_device,
                   std::uint8_t expected_access_fault) {
  core_in.prequest.pvalid = UINT64_C(1);
  core_in.prequest.pbits.pmemory.paddress = address;
  core_in.prequest.pbits.pmemory.pbyte_umask =
      ((((1 << (1 << core_in.prequest.pbits.pmemory.pwidth)) - 1)
        << (address % 4)) &
       low_mask(4));
  core_in.prequest.pbits.pmemory.paccess = access;
  core_in.prequest.pbits.pmemory.plocality = UINT64_C(4);
  core_in.prequest.pbits.pmemory.pcontext.pwriteback =
      memory_vector(UINT64_C(5));
  settle();
  CHECK(
      core_out.prequest.pready && cache_out.prequest.pvalid == expected_cache &&
      (!expected_cache || (cache_out.prequest.pbits.plocality == UINT64_C(4) &&
                           cache_out.prequest.pbits.pcontext.pwriteback ==
                               memory_vector(UINT64_C(5)))) &&
      !uncached_out.prequest.pvalid &&
      core_out.prequest_uaccess_ufault == expected_access_fault);
  if (expected_device) {
    tick();
    core_in.prequest.pvalid = UINT64_C(0);
    settle();
    CHECK(uncached_out.prequest.pvalid && !core_out.pdrained &&
          uncached_out.prequest.pbits.prequest.paddress == address &&
          uncached_out.prequest.pbits.prequest.paccess == access &&
          uncached_out.prequest.pbits.prequest.plocality == UINT64_C(4) &&
          uncached_out.prequest.pbits.prequest.pcontext.pwriteback ==
              memory_vector(UINT64_C(5)));
    tick();
    uncached_in.presponse.pvalid = UINT64_C(1);
    uncached_in.presponse.pbits.pdata = UINT64_C(0x12345678);
    settle();
    CHECK(core_out.presponse.pvalid &&
          core_out.presponse.pbits.pdata == UINT64_C(0x12345678));
    tick();
    uncached_in.presponse.pvalid = UINT64_C(0);
  }
}

void drive() {}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = UINT64_C(1);
  {
    core_in = {};
    core_in.presponse.pready = UINT64_C(1);
    cache_in = {};
    uncached_in = {};
    tick();
    reset = UINT64_C(0);
    core_in.prequest.pvalid = UINT64_C(1);
    cache_in.prequest.pready = UINT64_C(1);
    cache_in.pdrained = UINT64_C(1);
    uncached_in.prequest.pready = UINT64_C(1);
    uncached_in.pdrained = UINT64_C(1);
    cache_in.preservation_uvalid = UINT64_C(1);
    settle();
    CHECK(core_out.preservation_uvalid);
    cache_in.preservation_uvalid = UINT64_C(0);
    settle();
    CHECK(!core_out.preservation_uvalid);

    check_request(UINT64_C(4096), LOAD, UINT64_C(1), UINT64_C(0), UINT64_C(0));
    check_request(UINT64_C(4096), STORE, UINT64_C(1), UINT64_C(0), UINT64_C(0));
    // Ssccptr admission: walker-origin doubleword reads use the ordinary
    // coherent cache path at every aligned offset, without losing ownership.
    // This fixture checks routing; the core/MMU fixture consumes actual PTEs.
    core_in.prequest.pbits.pmemory.pcontext.porigin = 1;
    core_in.prequest.pbits.pmemory.pwidth = 3;
    for (int address = UINT64_C(4096); address < UINT64_C(8192); address += 8) {
      check_request(((address)&low_mask(32)), LOAD, 1, 0, 0);
      CHECK(cache_out.prequest.pbits.paddress == ((address)&low_mask(32)) &&
            cache_out.prequest.pbits.pcontext.porigin);
    }
    cache_in.presponse.pvalid = 1;
    cache_in.presponse.pbits.pcontext.porigin = 1;
    cache_in.presponse.pbits.pdata = UINT64_C(0x12345678);
    settle();
    CHECK(core_out.presponse.pvalid &&
          core_out.presponse.pbits.pcontext.porigin &&
          core_out.presponse.pbits.pdata == UINT64_C(0x12345678));
    cache_in.presponse = {};
    core_in.prequest.pbits.pmemory.pcontext.porigin = 0;
    core_in.prequest.pbits.pmemory.pwidth = 0;
    // Attribute overrides alter routing and ordering, never physical permission.
    for (int kind = 1; kind <= 2; kind++) {
      core_in.prequest.pbits.ppbmt = ((kind)&low_mask(2));
      check_request(UINT64_C(4096), LOAD, 0, 1, 0);
      CHECK(uncached_out.prequest.pbits.pdevice == (kind == 2));
      check_request(UINT64_C(4096), STORE, 0, 1, 0);
      check_request(UINT64_C(8192), LOAD, 0, 1, 0);
      CHECK(uncached_out.prequest.pbits.pdevice == (kind == 2));
      check_request(UINT64_C(12288), STORE, 0, 0, 1);
      check_request(UINT64_C(24576), LOAD, 0, 0, 1);
      for (int op = 7; op <= 9; op++)
        check_request(UINT64_C(4096), ((op)&low_mask(4)), 1, 0, 0);
    }
    core_in.prequest.pbits.ppbmt = 0;
    // Every AMOArithmetic operation must reach coherent RAM, including its
    // first and last naturally aligned word, but not a non-atomic/device PMA.
    core_in.prequest.pbits.pmemory.pwidth = UINT64_C(2);
    for (int operation = 0; operation < 9; operation++) {
      core_in.prequest.pbits.pmemory.patomic = ((operation)&low_mask(4));
      check_request(UINT64_C(4096), ATOMIC, 1, 0, 0);
      check_request(UINT64_C(8188), ATOMIC, 1, 0, 0);
      check_request(UINT64_C(8192), ATOMIC, 0, 0, 1);
      check_request(UINT64_C(12288), ATOMIC, 0, 0, 1);
    }
    core_in.prequest.pbits.pmemory.patomic = 0;
    core_in.prequest.pbits.pmemory.pwidth = 0;
    check_request(UINT64_C(8192), LOAD, UINT64_C(0), UINT64_C(1), UINT64_C(0));
    CHECK(uncached_out.prequest.pbits.pdevice);
    check_request(UINT64_C(8192), ATOMIC, UINT64_C(0), UINT64_C(0),
                  UINT64_C(1));
    check_request(UINT64_C(8192), UINT64_C(0), UINT64_C(0), UINT64_C(0),
                  UINT64_C(1));
    check_request(UINT64_C(12288), LOAD_RESERVED, UINT64_C(0), UINT64_C(0),
                  UINT64_C(1));
    check_request(UINT64_C(12288), STORE, UINT64_C(0), UINT64_C(0),
                  UINT64_C(1));
    check_request(UINT64_C(20480), LOAD, UINT64_C(0), UINT64_C(1), UINT64_C(0));
    CHECK(!uncached_out.prequest.pbits.pdevice);
    check_request(UINT64_C(24576), LOAD, UINT64_C(0), UINT64_C(0), UINT64_C(1));
    check_request(UINT64_C(0x11000), LOAD, UINT64_C(0), UINT64_C(0),
                  UINT64_C(1));
    core_in.prequest.pbits.pmemory.pwidth = UINT64_C(3);
    check_request(UINT64_C(16384), LOAD, UINT64_C(0), UINT64_C(0), UINT64_C(1));
    core_in.prequest.pbits.pmemory.pwidth = UINT64_C(0);

    // Block permission is checked at both ends, independent of rs1 alignment
    // and scalar width. Uncached RAM is legal; devices and partial blocks are not.
    for (int offset = 0; offset < 64; offset++) {
      check_request(UINT64_C(4096) + offset, UINT64_C(6), UINT64_C(1),
                    UINT64_C(0), UINT64_C(0));
      check_request(UINT64_C(28672) + offset, UINT64_C(6), UINT64_C(0),
                    UINT64_C(1), UINT64_C(0));
    }
    check_request(UINT64_C(8193), UINT64_C(6), UINT64_C(0), UINT64_C(0),
                  UINT64_C(1));
    check_request(UINT64_C(12289), UINT64_C(6), UINT64_C(0), UINT64_C(0),
                  UINT64_C(1));
    check_request(UINT64_C(20481), UINT64_C(6), UINT64_C(0), UINT64_C(0),
                  UINT64_C(1));
    check_request(UINT64_C(32769), UINT64_C(6), UINT64_C(0), UINT64_C(0),
                  UINT64_C(1));
    check_request(UINT64_C(65535), UINT64_C(6), UINT64_C(0), UINT64_C(0),
                  UINT64_C(1));

    // Management is permitted by either read or write access, not CBZE/atomic
    // capability; static uncached regions complete without device accesses.
    for (int operation = 7; operation <= 9; operation++) {
      check_request(UINT64_C(4159), ((operation)&low_mask(4)), 1, 0, 0);
      check_request(UINT64_C(12289), ((operation)&low_mask(4)), 1, 0, 0);
      check_request(UINT64_C(8193), ((operation)&low_mask(4)), 0, 0, 0);
      check_request(UINT64_C(20481), ((operation)&low_mask(4)), 0, 0, 0);
      check_request(UINT64_C(32769), ((operation)&low_mask(4)), 0, 0, 1);
      check_request(UINT64_C(24577), ((operation)&low_mask(4)), 0, 0, 1);
    }
    check_request(UINT64_C(20481), UINT64_C(8), 0, 0, 0);
    core_in.presponse.pready = UINT64_C(0);
    rising();
    settle();
    falling();
    core_in.prequest.pvalid = 0;
    settle();
    CHECK(core_out.presponse.pvalid &&
          !core_out.presponse.pbits.paccess_ufault &&
          !cache_out.prequest.pvalid && !uncached_out.prequest.pvalid);
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick();
      CHECK(core_out.presponse.pvalid && !core_out.pdrained);
    }
    core_in.presponse.pready = UINT64_C(1);
    rising();
    settle();
    falling();
    core_in.prequest.pvalid = 1;

    // Older cached work prevents IO admission, but instruction-owned RN-I
    // activity alone does not prevent a cached access or occupy the IO-MSHR.
    uncached_in.pdrained = UINT64_C(0);
    uncached_in.prequest.pready = UINT64_C(0);
    check_request(UINT64_C(4096), LOAD, UINT64_C(1), UINT64_C(0), UINT64_C(0));
    cache_in.pdrained = UINT64_C(0);
    core_in.prequest.pbits.pmemory.paddress = UINT64_C(8192);
    settle();
    CHECK(!core_out.prequest.pready && !uncached_out.prequest.pvalid);
    cache_in.pdrained = UINT64_C(1);
    settle();
    CHECK(core_out.prequest.pready);
    tick();
    core_in.prequest.pbits.pmemory.paddress = UINT64_C(4096);
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
      settle();
      CHECK(!core_out.prequest.pready && !cache_out.prequest.pvalid &&
            !core_out.pdrained && uncached_out.prequest.pvalid &&
            uncached_out.prequest.pbits.prequest.paddress == UINT64_C(8192));
      core_in.prequest.pbits.pmemory.paddress = UINT64_C(20481);
      core_in.prequest.pbits.pmemory.paccess = UINT64_C(8);
      settle();
      CHECK(!core_out.prequest.pready && !core_out.presponse.pvalid);
      core_in.prequest.pbits.pmemory.paddress = UINT64_C(4096);
      core_in.prequest.pbits.pmemory.paccess = LOAD;
      tick();
    }
    uncached_in.prequest.pready = UINT64_C(1);
    tick();
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
      CHECK(!uncached_out.prequest.pvalid && !core_out.prequest.pready &&
            !core_out.pdrained);
      tick();
    }
    uncached_in.presponse.pvalid = UINT64_C(1);
    tick();
    uncached_in.presponse.pvalid = UINT64_C(0);
    check_request(UINT64_C(4096), LOAD, UINT64_C(1), UINT64_C(0), UINT64_C(0));

    cache_in.prequest_uaccess_ufault = UINT64_C(1);
    check_request(UINT64_C(4096), LOAD, UINT64_C(1), UINT64_C(0), UINT64_C(1));
    cache_in.prequest_uaccess_ufault = UINT64_C(0);
    cache_in.pdrained = UINT64_C(0);
    uncached_in.pdrained = UINT64_C(0);
    check_request(UINT64_C(24576), LOAD, UINT64_C(0), UINT64_C(0), UINT64_C(1));

    core_in.prequest.pvalid = UINT64_C(0);
    cache_in.presponse.pvalid = UINT64_C(1);
    cache_in.presponse.pbits = {.paccess_ufault = UINT64_C(0),
                                .pdata = UINT64_C(0xabcdef01),
                                .pcontext = {memory_integer(UINT64_C(7)), 0}};
    settle();
    CHECK(core_out.presponse.pvalid == cache_in.presponse.pvalid &&
          same_memory_response(core_out.presponse.pbits,
                               cache_in.presponse.pbits));
    cache_in.presponse.pvalid = UINT64_C(0);
    settle();
    CHECK(!core_out.prequest_uaccess_ufault);

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
