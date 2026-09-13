// Checks atomic replication and exact lineage with independent branch stalls and reset.
// SPDX-License-Identifier: Apache-2.0
module event_atomic_fork_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock = 0, reset = 1;
  forward_t source[3], sink[9], gold_sink[9];
  reverse_t source_response[3], gold_source_response[3], sink_ready[9];
  logic fork_valid[3], fork_ready[3];
  logic [2:0] transfers[3];
  bit accepted[3];
  int unsigned random_state = 32'hadbcec31;
  EventAtomicFork dut(
    .clock(clock),
    .reset(reset),
    .sources_0_in(source[0]),
    .sources_0_out(source_response[0]),
    .gold_sources_0_in(source[0]),
    .gold_sources_0_out(gold_source_response[0]),
    .fork_valid0(fork_valid[0]),
    .fork_ready0(fork_ready[0]),
    .transfers0(transfers[0]),
    .sources_1_in(source[1]),
    .sources_1_out(source_response[1]),
    .gold_sources_1_in(source[1]),
    .gold_sources_1_out(gold_source_response[1]),
    .fork_valid1(fork_valid[1]),
    .fork_ready1(fork_ready[1]),
    .transfers1(transfers[1]),
    .sources_2_in(source[2]),
    .sources_2_out(source_response[2]),
    .gold_sources_2_in(source[2]),
    .gold_sources_2_out(gold_source_response[2]),
    .fork_valid2(fork_valid[2]),
    .fork_ready2(fork_ready[2]),
    .transfers2(transfers[2]),
    .sinks_0_out(sink[0]),
    .sinks_0_in(sink_ready[0]),
    .gold_sinks_0_out(gold_sink[0]),
    .gold_sinks_0_in(sink_ready[0]),
    .sinks_1_out(sink[1]),
    .sinks_1_in(sink_ready[1]),
    .gold_sinks_1_out(gold_sink[1]),
    .gold_sinks_1_in(sink_ready[1]),
    .sinks_2_out(sink[2]),
    .sinks_2_in(sink_ready[2]),
    .gold_sinks_2_out(gold_sink[2]),
    .gold_sinks_2_in(sink_ready[2]),
    .sinks_3_out(sink[3]),
    .sinks_3_in(sink_ready[3]),
    .gold_sinks_3_out(gold_sink[3]),
    .gold_sinks_3_in(sink_ready[3]),
    .sinks_4_out(sink[4]),
    .sinks_4_in(sink_ready[4]),
    .gold_sinks_4_out(gold_sink[4]),
    .gold_sinks_4_in(sink_ready[4]),
    .sinks_5_out(sink[5]),
    .sinks_5_in(sink_ready[5]),
    .gold_sinks_5_out(gold_sink[5]),
    .gold_sinks_5_in(sink_ready[5]),
    .sinks_6_out(sink[6]),
    .sinks_6_in(sink_ready[6]),
    .gold_sinks_6_out(gold_sink[6]),
    .gold_sinks_6_in(sink_ready[6]),
    .sinks_7_out(sink[7]),
    .sinks_7_in(sink_ready[7]),
    .gold_sinks_7_out(gold_sink[7]),
    .gold_sinks_7_in(sink_ready[7]),
    .sinks_8_out(sink[8]),
    .sinks_8_in(sink_ready[8]),
    .gold_sinks_8_out(gold_sink[8]),
    .gold_sinks_8_in(sink_ready[8])
  );
  always #5 clock = ~clock;
  import "DPI-C" function void event_atomic_fork_sample(input int unsigned lane,
      input int unsigned rst, input int unsigned in_valid, input int unsigned in_ready,
      input int unsigned payload, input int unsigned fork_offer, input int unsigned fork_accept,
      input int unsigned branch_transfers, input int unsigned out_valid,
      input int unsigned out_ready, input int unsigned payloads);
  import "DPI-C" function void event_atomic_fork_check();
  import "DPI-C" function void event_atomic_fork_finish();
  function automatic int unsigned random_word();
    random_state = random_state * 32'd1664525 + 32'd1013904223;
    return random_state ^ (random_state >> 16);
  endfunction
  initial begin
    foreach (source[i]) begin source[i] = '0; accepted[i] = 1; end
    for (int step = 0; step < 500; step++) begin
      reset = step == 0 || step == 100 || step == 151 || step == 152;
      foreach (source[i]) begin
        if (accepted[i] || !source[i].valid || reset) begin
          source[i].valid = step < 380 && ((step >= 20 && step < 46) ||
              (step >= 70 && step <= 100) || random_word() % 4 != 0);
          // Repeated payloads force the checker to distinguish occurrence IDs.
          source[i].bits = 8'(random_word() % 4);
        end
      end
      foreach (sink_ready[i]) begin
        sink_ready[i].ready = step >= 380 || ((step >= 20 && step < 46) ||
            (!(step >= 70 && step <= 100) && random_word() % (i % 3 + 2) == 0));
      end
      @(posedge clock);
      foreach (source[i]) begin
        int unsigned valid_mask, ready_mask, payloads;
        valid_mask = 0; ready_mask = 0; payloads = 0;
        assert (source_response[i] == gold_source_response[i]);
        accepted[i] = source[i].valid && source_response[i].ready;
        for (int j = 0; j < 3; j++) begin
          assert (sink[3*i+j].valid == gold_sink[3*i+j].valid);
          if (sink[3*i+j].valid) assert (sink[3*i+j].bits == gold_sink[3*i+j].bits);
          valid_mask |= 32'(sink[3*i+j].valid) << j;
          ready_mask |= 32'(sink_ready[3*i+j].ready) << j;
          payloads |= 32'(sink[3*i+j].bits) << (8*j);
        end
        event_atomic_fork_sample(32'(i), 32'(reset), 32'(source[i].valid),
            32'(source_response[i].ready), 32'(source[i].bits), 32'(fork_valid[i]),
            32'(fork_ready[i]), 32'(transfers[i]), valid_mask, ready_mask, payloads);
      end
      #1;
      event_atomic_fork_check();
      @(negedge clock);
    end
    event_atomic_fork_finish();
    $display("event atomic fork simulation passed");
    $finish;
  end
endmodule
