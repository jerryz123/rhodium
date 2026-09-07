// Checks fixed-delay event lineage with pulses, bursts, filtering, and in-flight reset.
module event_pipeline_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } channel_t;
  logic clock = 0, reset = 1;
  channel_t source_in, sink_out;
  EventPipeline dut(.*);
  import "DPI-C" function void event_pipeline_sample(input int unsigned rst,
      input int unsigned valid, input int unsigned payload,
      input int unsigned out_valid, input int unsigned out_payload);
  import "DPI-C" function void event_pipeline_check();
  always #5 clock = ~clock;

  initial begin
    for (int step = 0; step < 80; step++) begin
      reset = (step == 0 || step == 8 || step == 9 || step == 34);
      source_in.valid = step < 72 && (step % 7 != 2) && (step % 7 != 3);
      source_in.bits = 8'((step * 13) % 17);
      @(posedge clock);
      // Sample the transfer edge before sequential outputs advance; compare
      // the graph after every DPI effect on that edge has completed.
      event_pipeline_sample(32'(reset), 32'(source_in.valid), 32'(source_in.bits),
                            32'(sink_out.valid), 32'(sink_out.bits));
      #1;
      event_pipeline_check();
      @(negedge clock);
    end
    $display("event pipeline simulation passed");
    $finish;
  end
endmodule
