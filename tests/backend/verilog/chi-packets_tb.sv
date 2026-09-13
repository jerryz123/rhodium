// Checks address-dependent CHI packet sets, physical IDs, and monitored write constructors for 128/256/512-bit buses.
// SPDX-License-Identifier: Apache-2.0
module chi_packets_tb;
  logic clock = 0;
  logic reset = 1;
  logic [7:0] address = 0;
  logic [2:0] size = 0;
  logic [1:0] data_id = 0;
  logic valid = 0;
  wire [11:0] packet_sets;
  wire [5:0] packet_counts, write_ids, critical_chunks;
  wire [2:0] valid_ids;
  wire [23:0] packet_addresses;
  CHIPacketFixture dut (.*);
  always #5 clock = ~clock;

  initial begin
    int bytes_per_packet, transfer_bytes, first_byte, expected_set, expected_id;
    repeat (2) @(negedge clock);
    reset = 0;
    valid = 1;
    // Exhaust all naturally aligned sizes and positions across multiple lines.
    for (int sz = 0; sz <= 6; sz++) begin
      for (int addr = 0; addr < 256; addr += (1 << sz)) begin
        address = 8'(addr);
        size = 3'(sz);
        for (int id = 0; id < 4; id++) begin
          data_id = 2'(id);
          #1;
          for (int w = 0; w < 3; w++) begin
            bytes_per_packet = 16 << w;
            transfer_bytes = 1 << sz;
            first_byte = (addr / transfer_bytes) * transfer_bytes;
            expected_set = 0;
            // Independent reference: enumerate bytes, then collect their
            // physical packet positions instead of mirroring the RTL size table.
            for (int b = first_byte; b < first_byte + transfer_bytes; b++)
              expected_set |= 1 << (((b % 64) / bytes_per_packet) * (bytes_per_packet / 16));
            expected_id = ((addr % 64) / bytes_per_packet) * (bytes_per_packet / 16);
            assert (packet_sets[w*4 +: 4] == 4'(expected_set))
              else $fatal(1, "packet set width=%0d address=%0h size=%0d", bytes_per_packet*8, addr, sz);
            assert (packet_counts[w*2 +: 2] == 2'($countones(expected_set)-1))
              else $fatal(1, "packet count disagrees with transfer coverage");
            assert (write_ids[w*2 +: 2] == 2'(expected_id) && critical_chunks[w*2 +: 2] == 2'((addr % 64)/16))
              else $fatal(1, "DataID/CCID width=%0d address=%0h", bytes_per_packet*8, addr);
            assert (valid_ids[w] == ((id % (bytes_per_packet / 16)) == 0))
              else $fatal(1, "reserved DataID legality mismatch");
            assert (packet_addresses[w*8 +: 8] == 8'((addr / 64)*64 + ((id*16)/bytes_per_packet)*bytes_per_packet))
              else $fatal(1, "physical packet address counted request offset twice");
          end
        end
        @(negedge clock);
      end
    end
    $display("CHI cross-width packet layout and constructor monitoring passed");
    $finish;
  end
endmodule
