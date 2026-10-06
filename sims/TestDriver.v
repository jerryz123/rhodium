// Clocks a generated SoCHarness and bounds execution with an optional max-cycles plusarg.
// SPDX-License-Identifier: Apache-2.0
module TestDriver;
  reg clock;
  reg reset;
  wire [31:0] exit;
  integer max_cycles;
  reg [31:0] pending_exit;
  integer drain_status;
  import "DPI-C" function int rhodium_sim_open();
  import "DPI-C" function int rhodium_sim_begin(input bit reset_active);
  import "DPI-C" function int rhodium_sim_end();
  import "DPI-C" function int rhodium_sim_drain();
  import "DPI-C" function int rhodium_sim_finish();

  // Compile passes may add unused observation outputs, never functional inputs.
  /* verilator lint_off PINMISSING */
  SoCHarness dut (
    .clock(clock),
    .reset(reset),
    .exit(exit)
  );
  /* verilator lint_on PINMISSING */

  task automatic check_runtime(input integer status);
    if (status != 0) begin
      void'(rhodium_sim_finish());
      $fatal(1, "simulation runtime failed");
    end
  endtask

  always begin
    #1;
    // Bracket rising-edge evaluation; end only after every callback has settled.
    if (!clock) check_runtime(rhodium_sim_begin(reset));
    else check_runtime(rhodium_sim_end());
    clock = ~clock;
  end

  initial begin
    clock = 1'b0;
    reset = 1'b1;
    pending_exit = 0;
    max_cycles = 1000000;
    if ($value$plusargs("max-cycles=%d", max_cycles)) begin
      if (max_cycles <= 0) $fatal(1, "max-cycles must be positive");
    end
    check_runtime(rhodium_sim_open());
    repeat (3) @(posedge clock);
    // Release reset away from the sampled edge, consistently in both variants.
    @(negedge clock);
    reset = 1'b0;

    repeat (max_cycles) begin
      @(posedge clock);
      // Observe completion after all rising-edge DPI callbacks have settled.
      @(negedge clock);
      if (pending_exit == 0 && exit != 0) pending_exit = exit;
      drain_status = 0;
      if (pending_exit != 0) begin
        drain_status = rhodium_sim_drain();
        if (drain_status < 0) check_runtime(1);
      end
      if (drain_status == 1) begin
        check_runtime(rhodium_sim_finish());
        if (pending_exit == 1) begin
          $display("SoC harness simulation passed");
          $finish;
        end else begin
          $fatal(1, "SoC harness reported target failure: exit word %0d", pending_exit);
        end
      end
    end

    check_runtime(rhodium_sim_finish());
    $fatal(1, "SoC harness simulation timed out");
  end
endmodule
