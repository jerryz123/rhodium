// Clocks a generated SoCHarness and bounds execution with an optional max-cycles plusarg.
module TestDriver;
  reg clock;
  reg reset;
  wire [31:0] exit;
  wire [1:0] uart_out;
  integer max_cycles;

  SoCHarness dut (
    .clock(clock),
    .reset(reset),
    .uart_in(1'b1),
    .uart_out(uart_out),
    .exit(exit)
  );

  always #1 clock = ~clock;

  initial begin
    clock = 1'b0;
    reset = 1'b1;
    max_cycles = 1000000;
    if ($value$plusargs("max-cycles=%d", max_cycles)) begin
      if (max_cycles <= 0) $fatal(1, "max-cycles must be positive");
    end
    repeat (3) @(posedge clock);
    reset = 1'b0;

    repeat (max_cycles) begin
      @(posedge clock);
      if (exit != 0) begin
        if (exit == 1) begin
          $display("SoC harness simulation passed");
          $finish;
        end else begin
          $fatal(1, "SoC harness reported target failure: exit word %0d", exit);
        end
      end
    end

    $fatal(1, "SoC harness simulation timed out");
  end
endmodule
