// Differentially checks direct, held, bypassed, replaced, and reset stall observations.
// SPDX-License-Identifier: Apache-2.0
module event_stall_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock = 0, reset = 1;
  forward_t source[3], sink[3], gold_sink[3];
  reverse_t ready[3], source_ready[3], gold_ready[3];
  EventStall dut(
    .clock(clock), .reset(reset),
    .sources_0_in(source[0]), .sources_1_in(source[1]), .sources_2_in(source[2]),
    .sources_0_out(source_ready[0]), .sources_1_out(source_ready[1]), .sources_2_out(source_ready[2]),
    .sinks_0_out(sink[0]), .sinks_1_out(sink[1]), .sinks_2_out(sink[2]),
    .sinks_0_in(ready[0]), .sinks_1_in(ready[1]), .sinks_2_in(ready[2]),
    .gold_sources_0_in(source[0]), .gold_sources_1_in(source[1]), .gold_sources_2_in(source[2]),
    .gold_sources_0_out(gold_ready[0]), .gold_sources_1_out(gold_ready[1]), .gold_sources_2_out(gold_ready[2]),
    .gold_sinks_0_out(gold_sink[0]), .gold_sinks_1_out(gold_sink[1]), .gold_sinks_2_out(gold_sink[2]),
    .gold_sinks_0_in(ready[0]), .gold_sinks_1_in(ready[1]), .gold_sinks_2_in(ready[2])
  );
  always #5 clock = ~clock;
  import "DPI-C" function void event_stall_bind();
  import "DPI-C" function void event_stall_sample(input int unsigned lane, input int unsigned rst,
      input int unsigned valid, input int unsigned ready, input int unsigned payload,
      input int unsigned out_valid, input int unsigned out_ready, input int unsigned out_payload);
  import "DPI-C" function void event_stall_check();
  import "DPI-C" function void event_stall_finish();
  initial begin
    event_stall_bind();
    for (int step = 0; step < 160; ++step) begin
      reset = step == 0 || step == 19 || step == 20 || step == 77;
      for (int lane = 0; lane < 3; ++lane) begin
        // Decoupled may change or withdraw an unaccepted offer. Repeat payloads
        // deliberately so payload equality cannot stand in for event identity.
        source[lane].valid = step < 140 && (step + lane) % 5 != 0;
        source[lane].bits = 8'((step + lane) % 4);
        ready[lane].ready = step >= 140 || (step >= 25 && (step + lane) % 7 >= 3);
      end
      @(posedge clock);
      for (int lane = 0; lane < 3; ++lane) begin
        assert(source_ready[lane] == gold_ready[lane]);
        assert(sink[lane].valid == gold_sink[lane].valid);
        if (sink[lane].valid) assert(sink[lane].bits == gold_sink[lane].bits);
        event_stall_sample(32'(lane), 32'(reset), 32'(source[lane].valid), 32'(source_ready[lane].ready),
          32'(source[lane].bits), 32'(sink[lane].valid), 32'(ready[lane].ready), 32'(sink[lane].bits));
      end
      #1; event_stall_check();
      @(negedge clock);
    end
    event_stall_finish();
    $display("event stall simulation passed");
    $finish;
  end
endmodule
