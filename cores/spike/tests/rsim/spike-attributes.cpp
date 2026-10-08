// SPDX-License-Identifier: Apache-2.0
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_access(std::uint64_t address, int size, bool mapped, bool grant,
                  std::uint64_t base, std::uint64_t limit, bool writable,
                  bool executable, bool cached, bool device) {
  for (int mode = 0; mode < 3; mode++) {
    request = {address, std::uint8_t(size & 7), mode == 1, mode == 2};
    eval();
    if (response.pgrant_uvalid != grant ||
        response.paccess_ufault !=
            (!mapped || (mode == 1 && !writable) || (mode == 2 && !executable)))
      fail(1, "classification address=%h size=%0d mode=%0d: %h", address, size,
           mode, response);
    if (grant &&
        (response.pgrant_ubase != base || response.pgrant_ulimit != limit))
      fail(1, "incorrect certified interval at %h", address);
    if (mapped &&
        (response.preadable != 1 || response.pwritable != writable ||
         response.pexecutable != executable || response.pcacheable != cached ||
         response.pcache_ublock_uzero != cached ||
         response.pinstruction_ucacheable != cached ||
         response.patomic != cached || response.pdevice != device))
      fail(1, "incorrect attributes at %h", address);
    if (address < UINT64_C(65536) &&
        (response.pgrant_uvalid != narrow.pgrant_uvalid ||
         response.pgrant_ubase != narrow.pgrant_ubase ||
         response.pgrant_ulimit != narrow.pgrant_ulimit ||
         response.preadable != narrow.preadable ||
         response.pwritable != narrow.pwritable ||
         response.pexecutable != narrow.pexecutable ||
         response.pcacheable != narrow.pcacheable ||
         response.pcache_ublock_uzero != narrow.pcache_ublock_uzero ||
         response.pinstruction_ucacheable != narrow.pinstruction_ucacheable ||
         response.pdevice != narrow.pdevice ||
         response.patomic != narrow.patomic ||
         response.paccess_ufault != narrow.paccess_ufault))
      fail(1, "narrow map changed a fitting access");
    if (address >= UINT64_C(65536) &&
        (narrow.pgrant_uvalid || !narrow.paccess_ufault))
      fail(1, "narrow map accepted out-of-width address");
  }
}

int main() {
  return run_test([] {
    for (int size = 0; size <= 6; size++) {
      check_access(UINT64_C(4096), size, 1, 1, UINT64_C(4096), UINT64_C(4351),
                   1, 1, 1, 0);
      check_access(UINT64_C(8192), size, 1, 1, UINT64_C(8192), UINT64_C(8447),
                   0, 0, 0, 0);
    }
    check_access(UINT64_C(4351), 0, 1, 1, UINT64_C(4096), UINT64_C(4351), 1, 1,
                 1, 0);
    check_access(UINT64_C(4351), 1, 0, 0, 0, 0, 0, 0, 0, 0);
    check_access(UINT64_C(12288), 3, 1, 1, UINT64_C(12288), UINT64_C(12295), 1,
                 0, 0, 1);
    check_access(UINT64_C(12292), 3, 0, 0, 0, 0, 0, 0, 0, 0);
    check_access(UINT64_C(16384), 2, 1, 1, UINT64_C(16384), UINT64_C(16387), 1,
                 1, 1, 0);
    check_access(UINT64_C(16392), 2, 1, 1, UINT64_C(16392), UINT64_C(16395), 1,
                 1, 1, 0);
    check_access(UINT64_C(16388), 0, 0, 0, 0, 0, 0, 0, 0, 0);

    check_access(UINT64_C(16386), 3, 1, 0, 0, 0, 1, 1, 1, 0);
    check_access(UINT64_C(65535), 0, 0, 0, 0, 0, 0, 0, 0, 0);
    check_access(UINT64_C(18446744073709551608), 3, 1, 1,
                 UINT64_C(18446744073709551608), UINT64_C(18446744073709551615),
                 1, 1, 1, 0);
    check_access(UINT64_C(18446744073709551615), 1, 0, 0, 0, 0, 0, 0, 0, 0);
  });
}
