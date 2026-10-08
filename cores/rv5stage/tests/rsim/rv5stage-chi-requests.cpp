// Checks complete requester packets, response defaults, and snoop address widths.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../chi/tests/rsim/request.hpp"
#include <random>

template <class T>
void check_request(const T &actual, std::uint64_t addr, unsigned transaction,
                   unsigned controls) {
  T expected{};
  expected.pexp_ucomp_uack = controls & 1;
  expected.psnp_uattr_uor_udo_udwt = opcode != 0x1c && opcode != 0x1d;
  expected.pmem_uattr.pallocate = (controls >> 1) & 1;
  expected.pmem_uattr.pcacheable = (controls >> 3) & 1;
  expected.pmem_uattr.pdevice = (controls >> 4) & 1;
  expected.pmem_uattr.pearly_uwrite_uacknowledge = (controls >> 5) & 1;
  expected.ppcrd_utype = pcrd;
  expected.pallow_uretry = (controls >> 2) & 1;
  expected.paddress = addr & low_mask(52);
  expected.psize_uor_unum_ureq = size;
  expected.popcode = opcode;
  expected.preturn_unid_uor_ustash_unid_uor_udata_utarget = 0x1234;
  expected.ptxn_uid = transaction & 0xfff;
  expected.psrc_uid = 0x1234;
  expected.ptgt_uid = 0x4321;
  CHECK(same_request(actual, expected));
}
int main() {
  return run_test([] {
    std::mt19937 random(0x562010);
    for (unsigned op : {0x02, 0x07, 0x18, 0x04, 0x1d, 0x1c}) {
      opcode = op;
      for (unsigned control = 0; control < 64; ++control)
        for (unsigned sz = 0; sz <= 6; ++sz) {
          address = (std::uint64_t(random()) << 32) | random();
          txn = random() & 0xfff;
          pcrd = random() & 15;
          flags = control;
          size = sz;
          eval();
          std::remove_cvref_t<decltype(response)> expected{};
          expected.popcode = 2;
          expected.ptxn_uid = txn;
          expected.psrc_uid = 0x1234;
          expected.ptgt_uid = 0x4321;
          expected.presp = flags & 7;
          CHECK(same_response(response, expected));
          expected.popcode = 1;
          expected.ptxn_uid = (~txn) & 0xfff;
          expected.psrc_uid = 0x4321;
          expected.ptgt_uid = 0x1234;
          expected.presp = (~flags) & 7;
          CHECK(same_response(other_response, expected));
          CHECK(snoop_32 == (address & 0xfffffff8));
          CHECK(snoop_52 == (address & (low_mask(52) & ~7ull)));
          CHECK(snoop_64 == snoop_52);
          check_request(w128, address, txn, flags);
          check_request(other_w128, ~address, ~txn, ~flags);
          check_request(o128, address, txn, flags);
          check_request(other_o128, ~address, ~txn, ~flags);
          check_request(w256, address, txn, flags);
          check_request(other_w256, ~address, ~txn, ~flags);
          check_request(o256, address, txn, flags);
          check_request(other_o256, ~address, ~txn, ~flags);
          check_request(w512, address, txn, flags);
          check_request(other_w512, ~address, ~txn, ~flags);
          check_request(o512, address, txn, flags);
          check_request(other_o512, ~address, ~txn, ~flags);
        }
    }
  });
}
