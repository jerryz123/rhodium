// Checks representative Zca and profile-dependent full-C expansions.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_both(std::uint16_t encoding, std::uint32_t expected) {
  compressed = encoding;
  eval();
  CHECK(rv32f.pvalid && rv32f.pinstruction == expected);
  CHECK(rv32d.pvalid && rv32d.pinstruction == expected);
  CHECK(rv64d.pvalid && rv64d.pinstruction == expected);
  CHECK(rv64_zca.pvalid && rv64_zca.pinstruction == expected);
  CHECK(rv64_zcb.pvalid && rv64_zcb.pinstruction == expected);
}

int main() {
  return run_test([] {
    check_both(UINT64_C(133), UINT64_C(1081491));
    check_both(UINT64_C(32898), UINT64_C(32871));
    check_both(UINT64_C(36994), UINT64_C(32999));
    check_both(UINT64_C(33434), UINT64_C(6292147));
    check_both(UINT64_C(37530), UINT64_C(6455987));
    check_both(UINT64_C(36866), UINT64_C(1048691));
    check_both(UINT64_C(24581), UINT64_C(4151));

    compressed = UINT64_C(0);
    eval();
    CHECK(!rv32f.pvalid && !rv32d.pvalid && !rv64d.pvalid && !rv64_zca.pvalid &&
          !rv64_zcb.pvalid);

    compressed = UINT64_C(8325);
    eval();
    CHECK(rv32f.pvalid && rv32f.pinstruction == UINT64_C(100663535));
    CHECK(rv64d.pvalid && rv64d.pinstruction == UINT64_C(1081499));
    CHECK(rv64_zca.pvalid && rv64_zca.pinstruction == UINT64_C(1081499));

    compressed = UINT64_C(24576);
    eval();
    CHECK(rv32f.pvalid && rv32f.pinstruction == UINT64_C(271367));
    CHECK(rv64d.pvalid && rv64d.pinstruction == UINT64_C(275459));
    CHECK(rv64_zca.pvalid && rv64_zca.pinstruction == UINT64_C(275459));

    compressed = UINT64_C(8192);
    eval();
    CHECK(!rv32f.pvalid);
    CHECK(rv32d.pvalid && rv32d.pinstruction == UINT64_C(275463));
    CHECK(rv64d.pvalid && rv64d.pinstruction == UINT64_C(275463));
    CHECK(!rv64_zca.pvalid);
    CHECK(!rv64_zcb.pvalid);

    compressed = UINT64_C(32864);
    eval();
    CHECK(!rv64_zca.pvalid && rv64_zcb.pvalid &&
          rv64_zcb.pinstruction == UINT64_C(3425283));

    compressed = UINT64_C(40001);
    eval();
    CHECK(!rv64_zca.pvalid && rv64_zcb.pvalid &&
          rv64_zcb.pinstruction == UINT64_C(42206259));

    compressed = UINT64_C(40049);
    eval();
    CHECK(!rv64_zca.pvalid && rv64_zcb.pvalid &&
          rv64_zcb.pinstruction == UINT64_C(134480955));
  });
}
