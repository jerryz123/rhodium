// Checks joined lineage and manifest-bound snapshots under stalls, reset, and reconvergence.
module event_join_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock = 0, reset = 1;
  forward_t source[13], sink[9], gold_sink[9];
  reverse_t source_response[13], gold_source_response[13], sink_ready[9];
  logic [3:0] first_fire, rejoin_fire, combine_fire;
  logic [7:0] replicas_fire, combine_inputs, routes_fire;
  bit accepted[13];
  int unsigned taken[13], random_state = 32'hd0cc2211;
  EventJoin dut(
    .sequence_source_in(source[12]),
    .sequence_source_out(source_response[12]),
    .gold_sequence_source_in(source[12]),
    .gold_sequence_source_out(gold_source_response[12]),
    .sequence_sink_out(sink[8]),
    .sequence_sink_in(sink_ready[8]),
    .gold_sequence_sink_out(gold_sink[8]),
    .gold_sequence_sink_in(sink_ready[8]),
    .clock(clock),
    .reset(reset),
    .sources_0_in(source[0]),
    .sources_0_out(source_response[0]),
    .gold_sources_0_in(source[0]),
    .gold_sources_0_out(gold_source_response[0]),
    .sources_1_in(source[1]),
    .sources_1_out(source_response[1]),
    .gold_sources_1_in(source[1]),
    .gold_sources_1_out(gold_source_response[1]),
    .sources_2_in(source[2]),
    .sources_2_out(source_response[2]),
    .gold_sources_2_in(source[2]),
    .gold_sources_2_out(gold_source_response[2]),
    .sources_3_in(source[3]),
    .sources_3_out(source_response[3]),
    .gold_sources_3_in(source[3]),
    .gold_sources_3_out(gold_source_response[3]),
    .sources_4_in(source[4]),
    .sources_4_out(source_response[4]),
    .gold_sources_4_in(source[4]),
    .gold_sources_4_out(gold_source_response[4]),
    .sources_5_in(source[5]),
    .sources_5_out(source_response[5]),
    .gold_sources_5_in(source[5]),
    .gold_sources_5_out(gold_source_response[5]),
    .sources_6_in(source[6]),
    .sources_6_out(source_response[6]),
    .gold_sources_6_in(source[6]),
    .gold_sources_6_out(gold_source_response[6]),
    .sources_7_in(source[7]),
    .sources_7_out(source_response[7]),
    .gold_sources_7_in(source[7]),
    .gold_sources_7_out(gold_source_response[7]),
    .sources_8_in(source[8]),
    .sources_8_out(source_response[8]),
    .gold_sources_8_in(source[8]),
    .gold_sources_8_out(gold_source_response[8]),
    .sources_9_in(source[9]),
    .sources_9_out(source_response[9]),
    .gold_sources_9_in(source[9]),
    .gold_sources_9_out(gold_source_response[9]),
    .sources_10_in(source[10]),
    .sources_10_out(source_response[10]),
    .gold_sources_10_in(source[10]),
    .gold_sources_10_out(gold_source_response[10]),
    .sources_11_in(source[11]),
    .sources_11_out(source_response[11]),
    .gold_sources_11_in(source[11]),
    .gold_sources_11_out(gold_source_response[11]),
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
    .first_fire(first_fire),
    .replicas_fire(replicas_fire),
    .rejoin_fire(rejoin_fire),
    .combine_inputs(combine_inputs),
    .combine_fire(combine_fire),
    .routes_fire(routes_fire)
  );
  always #5 clock = ~clock;
  import "DPI-C" function void event_join_sample(input int unsigned lane,
      input int unsigned rst, input int unsigned in_valid, input int unsigned in_ready,
      input int unsigned payloads, input int unsigned first_transfer,
      input int unsigned replica_transfers, input int unsigned rejoin_transfer,
      input int unsigned combine_transfers, input int unsigned combined_transfer,
      input int unsigned route_transfers, input int unsigned out_valid,
      input int unsigned out_ready, input int unsigned out_payloads);
  import "DPI-C" function void event_join_check();
  import "DPI-C" function void event_join_occurrences(input int unsigned rst,
      input int unsigned input_fire, input int unsigned payload,
      input int unsigned output_fire, input int unsigned result);
  import "DPI-C" function void event_join_finish();
  import "DPI-C" function void event_join_bind();
  function automatic int unsigned random_word();
    random_state = random_state * 32'd1664525 + 32'd1013904223;
    return random_state ^ (random_state >> 16);
  endfunction
  initial begin
    event_join_bind();
    foreach (source[i]) begin source[i] = '0; accepted[i] = 1; taken[i] = 0; end
    for (int step = 0; step < 900; step++) begin
      reset = step == 0 || step == 100 || step == 301 || step == 302;
      foreach (source[i]) begin
        if (reset) taken[i] = 0;
        if (accepted[i] || !source[i].valid || reset) begin
          source[i].valid = taken[i] < 60 && (step >= 750 || random_word() % 4 != 0);
          source[i].bits = 8'(random_word() % 8);
          if (i == 12) source[i].bits[0] = 1'(taken[i] & 1);
        end
      end
      foreach (sink_ready[i])
        sink_ready[i].ready = step >= 750 || (!(step >= 70 && step <= 100) && random_word() % (i % 2 + 2) == 0);
      @(posedge clock);
      foreach (source[i]) begin
        assert (source_response[i] == gold_source_response[i]);
        accepted[i] = source[i].valid && source_response[i].ready;
        if (accepted[i] && !reset) taken[i]++;
      end
      for (int lane = 0; lane < 4; lane++) begin
        int unsigned vi, ri, pi, vo, ro, po;
        vi = 0; ri = 0; pi = 0; vo = 0; ro = 0; po = 0;
        for (int j = 0; j < 3; j++) begin
          vi |= 32'(source[3*lane+j].valid) << j;
          ri |= 32'(source_response[3*lane+j].ready) << j;
          pi |= 32'(source[3*lane+j].bits) << (8*j);
        end
        for (int j = 0; j < 2; j++) begin
          assert (sink[2*lane+j].valid == gold_sink[2*lane+j].valid);
          if (sink[2*lane+j].valid) assert (sink[2*lane+j].bits == gold_sink[2*lane+j].bits);
          vo |= 32'(sink[2*lane+j].valid) << j;
          ro |= 32'(sink_ready[2*lane+j].ready) << j;
          po |= 32'(sink[2*lane+j].bits) << (8*j);
        end
        event_join_sample(32'(lane), 32'(reset), vi, ri, pi, 32'(first_fire[lane]),
            (32'(replicas_fire) >> (2*lane)) & 3, 32'(rejoin_fire[lane]),
            (32'(combine_inputs) >> (2*lane)) & 3, 32'(combine_fire[lane]),
            (32'(routes_fire) >> (2*lane)) & 3, vo, ro, po);
      end
      assert (sink[8].valid == gold_sink[8].valid);
      if (sink[8].valid) assert (sink[8].bits == gold_sink[8].bits);
      event_join_occurrences(32'(reset), 32'(accepted[12]), 32'(source[12].bits),
          32'(sink[8].valid && sink_ready[8].ready), 32'(sink[8].bits));
      #1;
      event_join_check();
      @(negedge clock);
    end
    event_join_finish();
    $display("event join simulation passed");
    $finish;
  end
endmodule
