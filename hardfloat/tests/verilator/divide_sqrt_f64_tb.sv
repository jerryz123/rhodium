// Checks completed multiply-assisted binary64 HardFloat division and square-root behavior.
// SPDX-License-Identifier: BSD-3-Clause
module hardfloat_divide_sqrt_f64_tb;
  logic clock;
  logic reset;
  logic input_valid;
  logic square_root;
  logic [63:0] a;
  logic [63:0] b;
  logic [2:0] rounding_mode;
  logic tininess_mode;
  logic division_ready;
  logic square_root_ready;
  logic division_valid;
  logic square_root_valid;
  logic [63:0] out;
  logic [4:0] exception_flags;

  HardFloatDivideSqrtF64 dut (
    .clock(clock),
    .reset(reset),
    .input_valid(input_valid),
    .square_root(square_root),
    .a(a),
    .b(b),
    .rounding_mode(rounding_mode),
    .tininess_mode(tininess_mode),
    .division_ready(division_ready),
    .square_root_ready(square_root_ready),
    .division_valid(division_valid),
    .square_root_valid(square_root_valid),
    .out(out),
    .exception_flags(exception_flags)
  );

  always #5 clock = ~clock;

  task automatic check_operation(
    input logic next_square_root,
    input logic [63:0] next_a,
    input logic [63:0] next_b,
    input logic [2:0] next_rounding_mode,
    input logic [63:0] expected_out,
    input logic [4:0] expected_flags
  );
    integer elapsed_cycles;
    logic selected_ready;
    logic selected_valid;
    begin
      selected_ready = next_square_root ? square_root_ready : division_ready;
      while (!selected_ready) begin
        @(posedge clock);
        #1;
        selected_ready = next_square_root ? square_root_ready : division_ready;
      end
      @(negedge clock);
      square_root = next_square_root;
      a = next_a;
      b = next_b;
      rounding_mode = next_rounding_mode;
      input_valid = 1'b1;
      @(posedge clock);
      #1;
      input_valid = 1'b0;
      elapsed_cycles = 0;
      selected_valid = next_square_root ? square_root_valid : division_valid;
      while (!selected_valid && (elapsed_cycles < 40)) begin
        @(posedge clock);
        #1;
        elapsed_cycles = elapsed_cycles + 1;
        selected_valid = next_square_root ? square_root_valid : division_valid;
      end
      if (!selected_valid)
        $fatal(1, "binary64 divide/sqrt timed out: sqrt=%b a=%h b=%h", next_square_root, next_a, next_b);
      if ((out !== expected_out) || (exception_flags !== expected_flags))
        $fatal(1, "binary64 divide/sqrt failed: sqrt=%b a=%h b=%h mode=%h output=%h flags=%h expected=%h/%h",
               next_square_root, next_a, next_b, next_rounding_mode, out, exception_flags,
               expected_out, expected_flags);
      if (next_square_root && division_valid)
        $fatal(1, "binary64 square root asserted a division completion");
      if (!next_square_root && square_root_valid)
        $fatal(1, "binary64 division asserted a square-root completion");
    end
  endtask

  task automatic check_division_pipeline;
    integer result_count;
    integer wait_cycles;
    logic first_completed_before_second_start;
    begin
      while (!division_ready)
        @(posedge clock);
      @(negedge clock);
      square_root = 1'b0;
      a = 64'h4020000000000000;
      b = 64'h4000000000000000;
      rounding_mode = 3'b000;
      input_valid = 1'b1;
      @(posedge clock);
      #1;
      input_valid = 1'b0;
      first_completed_before_second_start = division_valid;

      wait_cycles = 0;
      while (!division_ready && (wait_cycles < 6)) begin
        @(posedge clock);
        #1;
        wait_cycles = wait_cycles + 1;
        first_completed_before_second_start = first_completed_before_second_start || division_valid;
      end
      if (!division_ready)
        $fatal(1, "binary64 division pipeline did not reopen within six cycles");
      if (first_completed_before_second_start)
        $fatal(1, "binary64 first division completed before the second was admitted");

      @(negedge clock);
      a = 64'h4022000000000000;
      b = 64'h4008000000000000;
      input_valid = 1'b1;
      @(posedge clock);
      #1;
      input_valid = 1'b0;

      result_count = 0;
      wait_cycles = 0;
      while ((result_count < 2) && (wait_cycles < 50)) begin
        if (division_valid) begin
          if ((result_count == 0) && ((out !== 64'h4010000000000000) || (exception_flags !== 5'b00000)))
            $fatal(1, "first pipelined binary64 division was not returned first: output=%h flags=%h",
                   out, exception_flags);
          if ((result_count == 1) && ((out !== 64'h4008000000000000) || (exception_flags !== 5'b00000)))
            $fatal(1, "second pipelined binary64 division was incorrect: output=%h flags=%h",
                   out, exception_flags);
          result_count = result_count + 1;
        end
        if (square_root_valid)
          $fatal(1, "pipelined divisions asserted a square-root completion");
        @(posedge clock);
        #1;
        wait_cycles = wait_cycles + 1;
      end
      if (result_count != 2)
        $fatal(1, "binary64 division pipeline did not return both results");
    end
  endtask

  initial begin
    clock = 1'b0;
    reset = 1'b1;
    input_valid = 1'b0;
    square_root = 1'b0;
    a = 64'h0000000000000000;
    b = 64'h0000000000000000;
    rounding_mode = 3'b000;
    tininess_mode = 1'b1;
    repeat (2) @(posedge clock);
    @(negedge clock);
    reset = 1'b0;

    check_operation(1'b0, 64'h3ff0000000000000, 64'h4000000000000000, 3'b000,
                    64'h3fe0000000000000, 5'b00000);
    check_operation(1'b0, 64'h4008000000000000, 64'h4000000000000000, 3'b000,
                    64'h3ff8000000000000, 5'b00000);
    check_operation(1'b0, 64'hbff0000000000000, 64'h4000000000000000, 3'b000,
                    64'hbfe0000000000000, 5'b00000);
    check_operation(1'b0, 64'h3ff0000000000000, 64'h4008000000000000, 3'b000,
                    64'h3fd5555555555555, 5'b00001);
    check_operation(1'b0, 64'h3ff0000000000000, 64'h4008000000000000, 3'b011,
                    64'h3fd5555555555556, 5'b00001);
    check_operation(1'b0, 64'h0000000000000000, 64'h0000000000000000, 3'b000,
                    64'h7ff8000000000000, 5'b10000);
    check_operation(1'b0, 64'h3ff0000000000000, 64'h0000000000000000, 3'b000,
                    64'h7ff0000000000000, 5'b01000);
    check_operation(1'b0, 64'h7fefffffffffffff, 64'h3fe0000000000000, 3'b000,
                    64'h7ff0000000000000, 5'b00101);
    check_operation(1'b0, 64'h0000000000000001, 64'h4000000000000000, 3'b000,
                    64'h0000000000000000, 5'b00011);

    check_operation(1'b1, 64'h0000000000000000, 64'h3ff0000000000000, 3'b000,
                    64'h3ff0000000000000, 5'b00000);
    check_operation(1'b1, 64'h0000000000000000, 64'h4010000000000000, 3'b000,
                    64'h4000000000000000, 5'b00000);
    check_operation(1'b1, 64'h0000000000000000, 64'h4000000000000000, 3'b000,
                    64'h3ff6a09e667f3bcd, 5'b00001);
    check_operation(1'b1, 64'h0000000000000000, 64'h4000000000000000, 3'b010,
                    64'h3ff6a09e667f3bcc, 5'b00001);
    check_operation(1'b1, 64'h0000000000000000, 64'hbff0000000000000, 3'b000,
                    64'h7ff8000000000000, 5'b10000);
    check_operation(1'b1, 64'h0000000000000000, 64'h8000000000000000, 3'b000,
                    64'h8000000000000000, 5'b00000);
    check_operation(1'b1, 64'h0000000000000000, 64'h7ff0000000000000, 3'b000,
                    64'h7ff0000000000000, 5'b00000);
    check_operation(1'b1, 64'h0000000000000000, 64'h7ff4000000000000, 3'b000,
                    64'h7ff8000000000000, 5'b10000);
    check_operation(1'b1, 64'h0000000000000000, 64'h0000000000000001, 3'b000,
                    64'h1e60000000000000, 5'b00000);

    check_division_pipeline();

    $display("hardfloat multiply-assisted binary64 division and square-root simulation passed");
    $finish;
  end
endmodule
