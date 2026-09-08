// Checks shift FIFO ordering, options, occupancy, reset, and pointer-FIFO equivalence.
module shift_queue_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock = 0, reset = 1;
  forward_t source[12], sink[12];
  reverse_t source_ready[12], sink_ready[12];
  logic [11:0][2:0] counts;
  logic [11:0][4:0] masks;
  logic [11:0] reference_matches;
  byte unsigned model[12][5];
  int used[12];
  bit accepted[12];
  int unsigned random_state = 32'h517f1f0;
  int bypasses[12], replacements[12], stalls[12], shifts[12];
  ShiftQueueFixture dut(
    .clock(clock), .reset(reset), .counts(counts), .masks(masks),
    .reference_matches(reference_matches),
    .sources_0_in(source[0]), .sources_0_out(source_ready[0]),
    .sinks_0_in(sink_ready[0]), .sinks_0_out(sink[0]),
    .sources_1_in(source[1]), .sources_1_out(source_ready[1]),
    .sinks_1_in(sink_ready[1]), .sinks_1_out(sink[1]),
    .sources_2_in(source[2]), .sources_2_out(source_ready[2]),
    .sinks_2_in(sink_ready[2]), .sinks_2_out(sink[2]),
    .sources_3_in(source[3]), .sources_3_out(source_ready[3]),
    .sinks_3_in(sink_ready[3]), .sinks_3_out(sink[3]),
    .sources_4_in(source[4]), .sources_4_out(source_ready[4]),
    .sinks_4_in(sink_ready[4]), .sinks_4_out(sink[4]),
    .sources_5_in(source[5]), .sources_5_out(source_ready[5]),
    .sinks_5_in(sink_ready[5]), .sinks_5_out(sink[5]),
    .sources_6_in(source[6]), .sources_6_out(source_ready[6]),
    .sinks_6_in(sink_ready[6]), .sinks_6_out(sink[6]),
    .sources_7_in(source[7]), .sources_7_out(source_ready[7]),
    .sinks_7_in(sink_ready[7]), .sinks_7_out(sink[7]),
    .sources_8_in(source[8]), .sources_8_out(source_ready[8]),
    .sinks_8_in(sink_ready[8]), .sinks_8_out(sink[8]),
    .sources_9_in(source[9]), .sources_9_out(source_ready[9]),
    .sinks_9_in(sink_ready[9]), .sinks_9_out(sink[9]),
    .sources_10_in(source[10]), .sources_10_out(source_ready[10]),
    .sinks_10_in(sink_ready[10]), .sinks_10_out(sink[10]),
    .sources_11_in(source[11]), .sources_11_out(source_ready[11]),
    .sinks_11_in(sink_ready[11]), .sinks_11_out(sink[11])
  );
  always #5 clock = ~clock;
  function automatic int depth(input int lane);
    return lane < 4 ? 1 : lane < 8 ? 2 : 5;
  endfunction
  function automatic int unsigned random_word();
    random_state = random_state * 32'd1664525 + 32'd1013904223;
    return random_state ^ (random_state >> 16);
  endfunction
  initial begin
    foreach (source[i]) begin
      source[i] = '0; sink_ready[i] = '0; used[i] = 0; accepted[i] = 1;
    end
    for (int step = 0; step < 1200; step++) begin
      @(negedge clock);
      reset = step == 0 || step == 90 || step == 91 || step == 700;
      foreach (source[i]) begin
        if (accepted[i] || !source[i].valid || reset) begin
          source[i].valid = step < 1100 && (step < 180 || random_word() % 4 != 0);
          source[i].bits = 8'(random_word());
        end
        sink_ready[i].ready = step >= 1100 || (step < 180 ? step % 40 >= 12 : random_word() % 3 != 0);
      end
      #1;
      if (!reset) foreach (source[i]) begin
        bit piped, flowed, expected_ready, expected_valid;
        byte unsigned expected_bits;
        piped = i % 4 >= 2; flowed = i % 2 == 1;
        expected_ready = used[i] < depth(i) || (piped && sink_ready[i].ready);
        expected_valid = used[i] != 0 || (flowed && source[i].valid);
        expected_bits = used[i] != 0 ? model[i][0] : source[i].bits;
        assert (counts[i] == 3'(used[i]) && masks[i] == (5'(1 << used[i]) - 5'd1))
          else $fatal(1, "occupancy mismatch lane=%0d step=%0d", i, step);
        assert (source_ready[i].ready == expected_ready && sink[i].valid == expected_valid)
          else $fatal(1, "handshake mismatch lane=%0d step=%0d", i, step);
        if (expected_valid) assert (sink[i].bits == expected_bits)
          else $fatal(1, "order mismatch lane=%0d step=%0d", i, step);
        assert (reference_matches[i]) else $fatal(1, "pointer FIFO mismatch lane=%0d", i);
      end
      @(posedge clock);
      foreach (source[i]) begin
        bit push, pop, bypass;
        push = source[i].valid && source_ready[i].ready;
        pop = sink[i].valid && sink_ready[i].ready;
        bypass = used[i] == 0 && i % 2 == 1 && push && pop;
        accepted[i] = push;
        if (reset) used[i] = 0;
        else begin
          if (bypass) bypasses[i]++;
          if (used[i] == depth(i) && push && pop) replacements[i]++;
          if (sink[i].valid && !sink_ready[i].ready) stalls[i]++;
          if (!bypass) begin
            if (pop) begin
              if (used[i] > 1) shifts[i]++;
              for (int j = 0; j < used[i] - 1; j++) model[i][j] = model[i][j+1];
              used[i]--;
            end
            if (push) begin model[i][used[i]] = source[i].bits; used[i]++; end
          end
        end
      end
      #1;
    end
    foreach (source[i]) begin
      assert (used[i] == 0 && counts[i] == 0 && !sink[i].valid);
      assert (stalls[i] > 0);
      if (i % 2 == 1) assert (bypasses[i] > 0);
      if (i % 4 >= 2) assert (replacements[i] > 0);
      if (depth(i) > 1) assert (shifts[i] > 0);
    end
    $display("shift queue simulation passed: 12 configurations, 1200 cycles");
    $finish;
  end
endmodule
