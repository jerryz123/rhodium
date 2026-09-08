// Clocks a generated SoCHarness and bounds execution with an optional max-cycles plusarg.
module TestDriver;
  reg clock;
  reg reset;
  wire [31:0] exit;
  integer max_cycles;
`ifdef RHEG_TRACE
  import "DPI-C" function int rheg_sim_open(input string path);
  import "DPI-C" function int rheg_sim_cycle(input longint unsigned cycle);
  import "DPI-C" function int rheg_sim_close();
  string trace_path;
  longint unsigned event_cycle;
`endif

  SoCHarness dut (
    .clock(clock),
    .reset(reset),
    .exit(exit)
  );

  always #1 clock = ~clock;

  initial begin
    clock = 1'b0;
    reset = 1'b1;
    max_cycles = 1000000;
`ifdef RHEG_TRACE
    event_cycle = 0;
    if (!$value$plusargs("rheg-trace=%s", trace_path)) $fatal(1, "+rheg-trace=PATH is required");
    if (rheg_sim_open(trace_path) != 0) $fatal(1, "RHEG initialization failed");
`endif
    if ($value$plusargs("max-cycles=%d", max_cycles)) begin
      if (max_cycles <= 0) $fatal(1, "max-cycles must be positive");
    end
    repeat (3) @(posedge clock);
    // Release reset away from the sampled edge, consistently in both variants.
    @(negedge clock);
    reset = 1'b0;

    repeat (max_cycles) begin
      @(posedge clock);
      // Observe completion after all rising-edge DPI callbacks have settled.
      @(negedge clock);
`ifdef RHEG_TRACE
      if (rheg_sim_cycle(event_cycle) != 0) $fatal(1, "RHEG cycle export failed");
      event_cycle = event_cycle + 1;
`endif
      if (exit != 0) begin
`ifdef RHEG_TRACE
        if (rheg_sim_close() != 0) $fatal(1, "RHEG finalization failed");
`endif
        if (exit == 1) begin
          $display("SoC harness simulation passed");
          $finish;
        end else begin
          $fatal(1, "SoC harness reported target failure: exit word %0d", exit);
        end
      end
    end

`ifdef RHEG_TRACE
    if (rheg_sim_close() != 0) $fatal(1, "RHEG finalization failed");
`endif
    $fatal(1, "SoC harness simulation timed out");
  end
endmodule
