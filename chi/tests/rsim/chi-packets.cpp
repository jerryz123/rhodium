// Checks address-dependent CHI packet sets, physical IDs, and monitored write
// constructors for 128/256/512-bit buses.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <bit>

int main() {
  return run_test([] {
    reset = 1;
    valid = 0;
    int bytes_per_packet, transfer_bytes, first_byte, expected_set, expected_id;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    reset = 0;
    valid = 1;

    for (int sz = 0; sz <= 6; sz++) {
      for (int addr = 0; addr < 256; addr += (1 << sz)) {
        address = ((addr)&low_mask(8));
        size = ((sz)&low_mask(3));
        for (int id = 0; id < 4; id++) {
          data_id = ((id)&low_mask(2));
          eval();
          for (int w = 0; w < 3; w++) {
            bytes_per_packet = 16 << w;
            transfer_bytes = 1 << sz;
            first_byte = (addr / transfer_bytes) * transfer_bytes;
            expected_set = 0;

            for (int b = first_byte; b < first_byte + transfer_bytes; b++)
              expected_set |= 1 << (((b % 64) / bytes_per_packet) *
                                    (bytes_per_packet / 16));
            expected_id =
                ((addr % 64) / bytes_per_packet) * (bytes_per_packet / 16);
            CHECK(packet_sets[w] == ((expected_set)&low_mask(4)));
            CHECK(packet_counts[w] ==
                  ((std::popcount(unsigned(expected_set)) - 1) & low_mask(2)));
            CHECK(write_ids[w] == ((expected_id)&low_mask(2)) &&
                  critical_chunks[w] == (((addr % 64) / 16) & low_mask(2)));
            CHECK(valid_ids[w] == ((id % (bytes_per_packet / 16)) == 0));
            CHECK(packet_addresses[w] ==
                  (((addr / 64) * 64 +
                    ((id * 16) / bytes_per_packet) * bytes_per_packet) &
                   low_mask(8)));
          }
        }
        eval();
      }
    }
  });
}
