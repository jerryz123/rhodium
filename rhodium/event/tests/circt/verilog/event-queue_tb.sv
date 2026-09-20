// Compares traced queues with reference lanes under directed and randomized traffic.
// SPDX-License-Identifier: Apache-2.0
module event_queue_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock = 0, reset = 1;
  forward_t source[10], sink[10], gold_sink[10];
  reverse_t source_response[10], sink_ready[10], gold_source_response[10];
  bit accepted[10];
  int unsigned random_state = 32'hca11ab1e;
  EventQueue dut(
    .clock(clock), .reset(reset),
    .sources_0_in(source[0]), .sources_0_out(source_response[0]),
    .sinks_0_out(sink[0]), .sinks_0_in(sink_ready[0]),
    .gold_sources_0_in(source[0]), .gold_sources_0_out(gold_source_response[0]),
    .gold_sinks_0_out(gold_sink[0]), .gold_sinks_0_in(sink_ready[0]),
    .sources_1_in(source[1]), .sources_1_out(source_response[1]),
    .sinks_1_out(sink[1]), .sinks_1_in(sink_ready[1]),
    .gold_sources_1_in(source[1]), .gold_sources_1_out(gold_source_response[1]),
    .gold_sinks_1_out(gold_sink[1]), .gold_sinks_1_in(sink_ready[1]),
    .sources_2_in(source[2]), .sources_2_out(source_response[2]),
    .sinks_2_out(sink[2]), .sinks_2_in(sink_ready[2]),
    .gold_sources_2_in(source[2]), .gold_sources_2_out(gold_source_response[2]),
    .gold_sinks_2_out(gold_sink[2]), .gold_sinks_2_in(sink_ready[2]),
    .sources_3_in(source[3]), .sources_3_out(source_response[3]),
    .sinks_3_out(sink[3]), .sinks_3_in(sink_ready[3]),
    .gold_sources_3_in(source[3]), .gold_sources_3_out(gold_source_response[3]),
    .gold_sinks_3_out(gold_sink[3]), .gold_sinks_3_in(sink_ready[3]),
    .sources_4_in(source[4]), .sources_4_out(source_response[4]),
    .sinks_4_out(sink[4]), .sinks_4_in(sink_ready[4]),
    .gold_sources_4_in(source[4]), .gold_sources_4_out(gold_source_response[4]),
    .gold_sinks_4_out(gold_sink[4]), .gold_sinks_4_in(sink_ready[4]),
    .sources_5_in(source[5]), .sources_5_out(source_response[5]),
    .sinks_5_out(sink[5]), .sinks_5_in(sink_ready[5]),
    .gold_sources_5_in(source[5]), .gold_sources_5_out(gold_source_response[5]),
    .gold_sinks_5_out(gold_sink[5]), .gold_sinks_5_in(sink_ready[5]),
    .sources_6_in(source[6]), .sources_6_out(source_response[6]),
    .sinks_6_out(sink[6]), .sinks_6_in(sink_ready[6]),
    .gold_sources_6_in(source[6]), .gold_sources_6_out(gold_source_response[6]),
    .gold_sinks_6_out(gold_sink[6]), .gold_sinks_6_in(sink_ready[6]),
    .sources_7_in(source[7]), .sources_7_out(source_response[7]),
    .sinks_7_out(sink[7]), .sinks_7_in(sink_ready[7]),
    .gold_sources_7_in(source[7]), .gold_sources_7_out(gold_source_response[7]),
    .gold_sinks_7_out(gold_sink[7]), .gold_sinks_7_in(sink_ready[7]),
    .sources_8_in(source[8]), .sources_8_out(source_response[8]),
    .sinks_8_out(sink[8]), .sinks_8_in(sink_ready[8]),
    .gold_sources_8_in(source[8]), .gold_sources_8_out(gold_source_response[8]),
    .gold_sinks_8_out(gold_sink[8]), .gold_sinks_8_in(sink_ready[8]),
    .sources_9_in(source[9]), .sources_9_out(source_response[9]),
    .sinks_9_out(sink[9]), .sinks_9_in(sink_ready[9]),
    .gold_sources_9_in(source[9]), .gold_sources_9_out(gold_source_response[9]),
    .gold_sinks_9_out(gold_sink[9]), .gold_sinks_9_in(sink_ready[9])
  );
  always #5 clock = ~clock;
  import "DPI-C" function void event_queue_sample(input int unsigned lane,
      input int unsigned rst, input int unsigned valid, input int unsigned ready,
      input int unsigned payload, input int unsigned out_valid,
      input int unsigned out_ready, input int unsigned out_payload);
  import "DPI-C" function void event_queue_check();
  import "DPI-C" function void event_queue_finish();

  function automatic int unsigned random_word();
    random_state = random_state * 32'd1664525 + 32'd1013904223;
    return random_state ^ (random_state >> 16);
  endfunction

  initial begin
    foreach (source[i]) begin source[i] = '0; accepted[i] = 1; end
    for (int step = 0; step < 600; step++) begin
      reset = step == 0 || step == 40 || step == 121 || step == 122;
      foreach (source[i]) begin
        if (accepted[i] || !source[i].valid || reset) begin
          source[i].valid = step < 500 && (step < 20 ? step % 2 == 1 :
              (step < 90 || (step >= 350 && step < 410) || random_word() % 5 != 0));
          source[i].bits = 8'(random_word() >> 24) % 8;
        end
        sink_ready[i].ready = step >= 500 || (step < 20 || (step >= 41 && step < 90) ||
            (step >= 371 && step < 410) || (random_word() % (i % 3 + 2) == 0));
        if ((step >= 21 && step <= 40) || (step >= 101 && step <= 121) ||
            (step >= 350 && step <= 370)) sink_ready[i].ready = 0;
      end
      @(posedge clock);
      foreach (source[i]) begin
        assert (source_response[i] == gold_source_response[i]);
        assert (sink[i].valid == gold_sink[i].valid);
        if (sink[i].valid) assert (sink[i].bits == gold_sink[i].bits);
        accepted[i] = source[i].valid && source_response[i].ready;
        event_queue_sample(32'(i), 32'(reset), 32'(source[i].valid), 32'(source_response[i].ready),
            32'(source[i].bits), 32'(sink[i].valid), 32'(sink_ready[i].ready), 32'(sink[i].bits));
      end
      #1;
      event_queue_check();
      @(negedge clock);
    end
    event_queue_finish();
    $display("event queue simulation passed");
    $finish;
  end
endmodule
