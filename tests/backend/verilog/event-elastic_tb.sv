// Differentially checks repeated elastic lanes under deterministic randomized stalls.
module event_elastic_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock = 0, reset = 1;
  forward_t sources_0_in, sources_1_in, sinks_0_out, sinks_1_out;
  reverse_t sources_0_out, sources_1_out, sinks_0_in, sinks_1_in;
  forward_t gold_sources_0_in, gold_sources_1_in, gold_sinks_0_out, gold_sinks_1_out;
  reverse_t gold_sources_0_out, gold_sources_1_out, gold_sinks_0_in, gold_sinks_1_in;
  logic left_middle_fire, right_middle_fire;
  logic [7:0] left_middle_payload, right_middle_payload;
  bit accepted_left = 1, accepted_right = 1;
  int unsigned random_state = 32'hbadc0ffe;
  EventElastic dut(.*);
  assign gold_sources_0_in = sources_0_in;
  assign gold_sources_1_in = sources_1_in;
  assign gold_sinks_0_in = sinks_0_in;
  assign gold_sinks_1_in = sinks_1_in;
  always #5 clock = ~clock;
  import "DPI-C" function void event_elastic_sample(input int unsigned lane,
      input int unsigned rst, input int unsigned valid, input int unsigned ready,
      input int unsigned payload, input int unsigned middle_fire,
      input int unsigned middle_payload, input int unsigned out_valid,
      input int unsigned out_ready, input int unsigned out_payload);
  import "DPI-C" function void event_elastic_check();
  import "DPI-C" function void event_elastic_finish();

  function automatic int unsigned random_word();
    random_state = random_state * 32'd1664525 + 32'd1013904223;
    return random_state ^ (random_state >> 16);
  endfunction

  initial begin
    sources_0_in = '0; sources_1_in = '0;
    for (int step = 0; step < 260; step++) begin
      reset = step == 0 || step == 19 || step == 97 || step == 98;
      if (accepted_left || !sources_0_in.valid || reset) begin
        sources_0_in.valid = step < 230 && (random_word() % 5 != 0);
        sources_0_in.bits = 8'(random_word() >> 24) % 8;
      end
      if (accepted_right || !sources_1_in.valid || reset) begin
        sources_1_in.valid = step < 230 && (random_word() % 4 != 0);
        sources_1_in.bits = 8'(random_word() >> 24) % 8;
      end
      sinks_0_in.ready = step >= 230 || (!(step >= 10 && step <= 25) && random_word() % 3 != 0);
      sinks_1_in.ready = step >= 230 || (!(step >= 75 && step <= 100) && random_word() % 4 == 0);
      @(posedge clock);
      assert (sources_0_out == gold_sources_0_out && sources_1_out == gold_sources_1_out);
      assert (sinks_0_out.valid == gold_sinks_0_out.valid && sinks_1_out.valid == gold_sinks_1_out.valid);
      if (sinks_0_out.valid) assert (sinks_0_out.bits == gold_sinks_0_out.bits);
      if (sinks_1_out.valid) assert (sinks_1_out.bits == gold_sinks_1_out.bits);
      accepted_left = sources_0_in.valid && sources_0_out.ready;
      accepted_right = sources_1_in.valid && sources_1_out.ready;
      event_elastic_sample(0, 32'(reset), 32'(sources_0_in.valid), 32'(sources_0_out.ready),
          32'(sources_0_in.bits), 32'(left_middle_fire), 32'(left_middle_payload),
          32'(sinks_0_out.valid), 32'(sinks_0_in.ready), 32'(sinks_0_out.bits));
      event_elastic_sample(1, 32'(reset), 32'(sources_1_in.valid), 32'(sources_1_out.ready),
          32'(sources_1_in.bits), 32'(right_middle_fire), 32'(right_middle_payload),
          32'(sinks_1_out.valid), 32'(sinks_1_in.ready), 32'(sinks_1_out.bits));
      #1;
      event_elastic_check();
      @(negedge clock);
    end
    event_elastic_finish();
    $display("event elastic simulation passed");
    $finish;
  end
endmodule
