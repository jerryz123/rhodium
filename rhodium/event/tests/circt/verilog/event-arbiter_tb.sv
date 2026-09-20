// Checks grant-sensitive traces against unannotated networks with independent input traffic.
// SPDX-License-Identifier: Apache-2.0
module event_arbiter_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock = 0, reset = 1;
  forward_t source[15], sink[5], gold_sink[5];
  reverse_t source_response[15], sink_ready[5], gold_source_response[15];
  logic [2:0] taken[5];
  logic joined[5];
  bit accepted[15];
  int unsigned random_state = 32'hcedab123;
  EventArbiter dut(
    .clock(clock), .reset(reset),
    .sources_0_in(source[0]), .sources_0_out(source_response[0]),
    .gold_sources_0_in(source[0]), .gold_sources_0_out(gold_source_response[0]),
    .sources_1_in(source[1]), .sources_1_out(source_response[1]),
    .gold_sources_1_in(source[1]), .gold_sources_1_out(gold_source_response[1]),
    .sources_2_in(source[2]), .sources_2_out(source_response[2]),
    .gold_sources_2_in(source[2]), .gold_sources_2_out(gold_source_response[2]),
    .sources_3_in(source[3]), .sources_3_out(source_response[3]),
    .gold_sources_3_in(source[3]), .gold_sources_3_out(gold_source_response[3]),
    .sources_4_in(source[4]), .sources_4_out(source_response[4]),
    .gold_sources_4_in(source[4]), .gold_sources_4_out(gold_source_response[4]),
    .sources_5_in(source[5]), .sources_5_out(source_response[5]),
    .gold_sources_5_in(source[5]), .gold_sources_5_out(gold_source_response[5]),
    .sources_6_in(source[6]), .sources_6_out(source_response[6]),
    .gold_sources_6_in(source[6]), .gold_sources_6_out(gold_source_response[6]),
    .sources_7_in(source[7]), .sources_7_out(source_response[7]),
    .gold_sources_7_in(source[7]), .gold_sources_7_out(gold_source_response[7]),
    .sources_8_in(source[8]), .sources_8_out(source_response[8]),
    .gold_sources_8_in(source[8]), .gold_sources_8_out(gold_source_response[8]),
    .sources_9_in(source[9]), .sources_9_out(source_response[9]),
    .gold_sources_9_in(source[9]), .gold_sources_9_out(gold_source_response[9]),
    .sources_10_in(source[10]), .sources_10_out(source_response[10]),
    .gold_sources_10_in(source[10]), .gold_sources_10_out(gold_source_response[10]),
    .sources_11_in(source[11]), .sources_11_out(source_response[11]),
    .gold_sources_11_in(source[11]), .gold_sources_11_out(gold_source_response[11]),
    .sources_12_in(source[12]), .sources_12_out(source_response[12]),
    .gold_sources_12_in(source[12]), .gold_sources_12_out(gold_source_response[12]),
    .sources_13_in(source[13]), .sources_13_out(source_response[13]),
    .gold_sources_13_in(source[13]), .gold_sources_13_out(gold_source_response[13]),
    .sources_14_in(source[14]), .sources_14_out(source_response[14]),
    .gold_sources_14_in(source[14]), .gold_sources_14_out(gold_source_response[14]),
    .sinks_0_out(sink[0]), .sinks_0_in(sink_ready[0]),
    .gold_sinks_0_out(gold_sink[0]), .gold_sinks_0_in(sink_ready[0]),
    .take0_0(taken[0][0]), .take0_1(taken[0][1]), .take0_2(taken[0][2]), .join0(joined[0]),
    .sinks_1_out(sink[1]), .sinks_1_in(sink_ready[1]),
    .gold_sinks_1_out(gold_sink[1]), .gold_sinks_1_in(sink_ready[1]),
    .take1_0(taken[1][0]), .take1_1(taken[1][1]), .take1_2(taken[1][2]), .join1(joined[1]),
    .sinks_2_out(sink[2]), .sinks_2_in(sink_ready[2]),
    .gold_sinks_2_out(gold_sink[2]), .gold_sinks_2_in(sink_ready[2]),
    .take2_0(taken[2][0]), .take2_1(taken[2][1]), .take2_2(taken[2][2]), .join2(joined[2]),
    .sinks_3_out(sink[3]), .sinks_3_in(sink_ready[3]),
    .gold_sinks_3_out(gold_sink[3]), .gold_sinks_3_in(sink_ready[3]),
    .take3_0(taken[3][0]), .take3_1(taken[3][1]), .take3_2(taken[3][2]), .join3(joined[3]),
    .sinks_4_out(sink[4]), .sinks_4_in(sink_ready[4]),
    .gold_sinks_4_out(gold_sink[4]), .gold_sinks_4_in(sink_ready[4]),
    .take4_0(taken[4][0]), .take4_1(taken[4][1]), .take4_2(taken[4][2]), .join4(joined[4])
  );
  always #5 clock = ~clock;
  import "DPI-C" function void event_arbiter_sample(input int unsigned lane,
      input int unsigned rst, input int unsigned valid_mask, input int unsigned ready_mask,
      input int unsigned payloads, input int unsigned take_mask, input int unsigned join_fire,
      input int unsigned out_valid, input int unsigned out_ready, input int unsigned out_payload);
  import "DPI-C" function void event_arbiter_check();
  import "DPI-C" function void event_arbiter_finish();
  function automatic int unsigned random_word();
    random_state = random_state * 32'd1664525 + 32'd1013904223;
    return random_state ^ (random_state >> 16);
  endfunction

  initial begin
    foreach (source[i]) begin source[i] = '0; accepted[i] = 1; end
    for (int step = 0; step < 450; step++) begin
      reset = step == 0 || step == 100 || step == 151 || step == 152;
      foreach (source[i]) begin
        if (accepted[i] || !source[i].valid || reset) begin
          source[i].valid = step < 350 && (step < 8 ? (i % 3 == 2 || (i % 3 == 0 && step >= 3)) :
              (step >= 240 && step < 300 ? i % 3 == (step - 240) / 20 :
              (step >= 20 && step < 46) || random_word() % 3 != 0));
          source[i].bits = step < 8 ? 8'(i % 3 + 1) : 8'(random_word() >> 24) % 4;
        end
      end
      foreach (sink_ready[i]) begin
        sink_ready[i].ready = step >= 350 || (step >= 8 && !(step >= 70 && step <= 100) &&
            ((step >= 20 && step < 46) || (step >= 240 && step < 300) || random_word() % (i % 3 + 2) == 0));
      end
      @(posedge clock);
      foreach (source[i]) begin
        assert (source_response[i] == gold_source_response[i]);
        accepted[i] = source[i].valid && source_response[i].ready;
      end
      foreach (sink[i]) begin
        int unsigned valid_mask, ready_mask, payloads;
        valid_mask = 0; ready_mask = 0; payloads = 0;
        for (int j = 0; j < 3; j++) begin
          valid_mask |= 32'(source[i * 3 + j].valid) << j;
          ready_mask |= 32'(source_response[i * 3 + j].ready) << j;
          payloads |= 32'(source[i * 3 + j].bits) << (8 * j);
        end
        assert (sink[i].valid == gold_sink[i].valid);
        if (sink[i].valid) assert (sink[i].bits == gold_sink[i].bits);
        event_arbiter_sample(32'(i), 32'(reset), valid_mask, ready_mask, payloads,
            32'(taken[i]), 32'(joined[i]), 32'(sink[i].valid), 32'(sink_ready[i].ready), 32'(sink[i].bits));
      end
      #1;
      event_arbiter_check();
      @(negedge clock);
    end
    event_arbiter_finish();
    $display("event arbiter simulation passed");
    $finish;
  end
endmodule
