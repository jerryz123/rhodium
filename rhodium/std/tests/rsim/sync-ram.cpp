// Verifies one-cycle read validity, write silence, and lane-masked updates.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void request_write(std::uint8_t address, std::uint32_t data,
                   std::uint8_t mask) {
  port_in.prequest = {
      .pvalid = UINT64_C(1),
      .pbits = {.paddress = address,
                .pwrite = UINT64_C(1),
                .pdata = {std::uint8_t(data), std::uint8_t(data >> 8),
                          std::uint8_t(data >> 16), std::uint8_t(data >> 24)},
                .pmask = mask}};
  tick_model();
  port_in.prequest.pvalid = UINT64_C(0);
  CHECK(!port_out.presponse.pvalid);
}

void request_read(std::uint8_t address) {
  port_in.prequest = {.pvalid = UINT64_C(1),
                      .pbits = {.paddress = address,
                                .pwrite = UINT64_C(0),
                                .pdata = {},
                                .pmask = {}}};
  tick_model();
  port_in.prequest.pvalid = UINT64_C(0);
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    port_in = {};
    tick_model();
    CHECK(!port_out.presponse.pvalid);
    reset = UINT64_C(0);

    request_write(UINT64_C(1), UINT64_C(287454020), UINT64_C(15));
    request_read(UINT64_C(1));
    CHECK(port_out.presponse.pvalid &&
          (port_out.presponse.pbits ==
           std::array<std::uint8_t, 4>{0x44, 0x33, 0x22, 0x11}));

    tick_model();
    CHECK(!port_out.presponse.pvalid);

    request_write(UINT64_C(1), UINT64_C(2864434397), UINT64_C(5));
    request_read(UINT64_C(1));
    CHECK(port_out.presponse.pvalid &&
          (port_out.presponse.pbits ==
           std::array<std::uint8_t, 4>{0xdd, 0x33, 0xbb, 0x11}));
  });
}
