// Checks completed one-bit and two-bit iterative HardFloat division and square-root behavior.
// SPDX-License-Identifier: BSD-3-Clause
module hardfloat_divide_sqrt_tb;
  logic clock;
  logic reset;
  logic input_valid;
  logic input_ready;
  logic square_root;
  logic [15:0] a;
  logic [15:0] b;
  logic [2:0] rounding_mode;
  logic tininess_mode;
  logic one_bit_division_valid;
  logic one_bit_square_root_valid;
  logic [15:0] one_bit_out;
  logic [4:0] one_bit_flags;
  logic two_bit_division_valid;
  logic two_bit_square_root_valid;
  logic [15:0] two_bit_out;
  logic [4:0] two_bit_flags;

  HardFloatDivideSqrt dut (
    .clock(clock),
    .reset(reset),
    .input_valid(input_valid),
    .input_ready(input_ready),
    .square_root(square_root),
    .a(a),
    .b(b),
    .rounding_mode(rounding_mode),
    .tininess_mode(tininess_mode),
    .one_bit_division_valid(one_bit_division_valid),
    .one_bit_square_root_valid(one_bit_square_root_valid),
    .one_bit_out(one_bit_out),
    .one_bit_flags(one_bit_flags),
    .two_bit_division_valid(two_bit_division_valid),
    .two_bit_square_root_valid(two_bit_square_root_valid),
    .two_bit_out(two_bit_out),
    .two_bit_flags(two_bit_flags)
  );

  always #5 clock = ~clock;

  task automatic check_operation(
    input logic next_square_root,
    input logic [15:0] next_a,
    input logic [15:0] next_b,
    input logic [2:0] next_rounding_mode,
    input logic [15:0] expected_out,
    input logic [4:0] expected_flags,
    input logic expect_two_bit_faster
  );
    integer elapsed_cycles;
    integer one_bit_cycles;
    integer two_bit_cycles;
    logic saw_one_bit;
    logic saw_two_bit;
    logic one_bit_event;
    logic two_bit_event;
    begin
      while (!input_ready)
        @(posedge clock);
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
      one_bit_cycles = -1;
      two_bit_cycles = -1;
      saw_one_bit = 1'b0;
      saw_two_bit = 1'b0;
      one_bit_event = next_square_root ? one_bit_square_root_valid : one_bit_division_valid;
      two_bit_event = next_square_root ? two_bit_square_root_valid : two_bit_division_valid;
      if (one_bit_event) begin
        saw_one_bit = 1'b1;
        one_bit_cycles = elapsed_cycles;
        if ((one_bit_out !== expected_out) || (one_bit_flags !== expected_flags))
          $fatal(1, "one-bit divide/sqrt failed: sqrt=%b a=%h b=%h mode=%h output=%h flags=%h expected=%h/%h",
                 next_square_root, next_a, next_b, next_rounding_mode, one_bit_out, one_bit_flags,
                 expected_out, expected_flags);
      end
      if (two_bit_event) begin
        saw_two_bit = 1'b1;
        two_bit_cycles = elapsed_cycles;
        if ((two_bit_out !== expected_out) || (two_bit_flags !== expected_flags))
          $fatal(1, "two-bit divide/sqrt failed: sqrt=%b a=%h b=%h mode=%h output=%h flags=%h expected=%h/%h",
                 next_square_root, next_a, next_b, next_rounding_mode, two_bit_out, two_bit_flags,
                 expected_out, expected_flags);
      end
      while (!(saw_one_bit && saw_two_bit) && (elapsed_cycles < 80)) begin
        @(posedge clock);
        #1;
        elapsed_cycles = elapsed_cycles + 1;
        one_bit_event = next_square_root ? one_bit_square_root_valid : one_bit_division_valid;
        two_bit_event = next_square_root ? two_bit_square_root_valid : two_bit_division_valid;
        if (one_bit_event && !saw_one_bit) begin
          saw_one_bit = 1'b1;
          one_bit_cycles = elapsed_cycles;
          if ((one_bit_out !== expected_out) || (one_bit_flags !== expected_flags))
            $fatal(1, "one-bit divide/sqrt failed: sqrt=%b a=%h b=%h mode=%h output=%h flags=%h expected=%h/%h",
                   next_square_root, next_a, next_b, next_rounding_mode, one_bit_out, one_bit_flags,
                   expected_out, expected_flags);
        end
        if (two_bit_event && !saw_two_bit) begin
          saw_two_bit = 1'b1;
          two_bit_cycles = elapsed_cycles;
          if ((two_bit_out !== expected_out) || (two_bit_flags !== expected_flags))
            $fatal(1, "two-bit divide/sqrt failed: sqrt=%b a=%h b=%h mode=%h output=%h flags=%h expected=%h/%h",
                   next_square_root, next_a, next_b, next_rounding_mode, two_bit_out, two_bit_flags,
                   expected_out, expected_flags);
        end
      end
      if (!(saw_one_bit && saw_two_bit))
        $fatal(1, "divide/sqrt timed out: sqrt=%b a=%h b=%h", next_square_root, next_a, next_b);
      if (expect_two_bit_faster && !(two_bit_cycles < one_bit_cycles))
        $fatal(1, "two-bit configuration did not complete sooner: one=%0d two=%0d",
               one_bit_cycles, two_bit_cycles);
      if (next_square_root && (one_bit_division_valid || two_bit_division_valid))
        $fatal(1, "square root asserted a division completion");
      if (!next_square_root && (one_bit_square_root_valid || two_bit_square_root_valid))
        $fatal(1, "division asserted a square-root completion");
    end
  endtask

  initial begin
    clock = 1'b0;
    reset = 1'b1;
    input_valid = 1'b0;
    square_root = 1'b0;
    a = 16'h0000;
    b = 16'h0000;
    rounding_mode = 3'b000;
    tininess_mode = 1'b1;
    repeat (2) @(posedge clock);
    @(negedge clock);
    reset = 1'b0;

    check_operation(1'b0, 16'h3c00, 16'h4000, 3'b000, 16'h3800, 5'b00000, 1'b1);
    check_operation(1'b0, 16'h4200, 16'h4000, 3'b000, 16'h3e00, 5'b00000, 1'b1);
    check_operation(1'b0, 16'hbc00, 16'h4000, 3'b000, 16'hb800, 5'b00000, 1'b1);
    check_operation(1'b0, 16'h3c00, 16'h4200, 3'b000, 16'h3555, 5'b00001, 1'b1);
    check_operation(1'b0, 16'h3c00, 16'h4200, 3'b011, 16'h3556, 5'b00001, 1'b1);
    check_operation(1'b0, 16'h0000, 16'h0000, 3'b000, 16'h7e00, 5'b10000, 1'b0);
    check_operation(1'b0, 16'h7c00, 16'h7c00, 3'b000, 16'h7e00, 5'b10000, 1'b0);
    check_operation(1'b0, 16'h3c00, 16'h0000, 3'b000, 16'h7c00, 5'b01000, 1'b0);
    check_operation(1'b0, 16'h8000, 16'h4000, 3'b000, 16'h8000, 5'b00000, 1'b0);
    check_operation(1'b0, 16'h7bff, 16'h3800, 3'b000, 16'h7c00, 5'b00101, 1'b1);
    check_operation(1'b0, 16'h0001, 16'h4000, 3'b000, 16'h0000, 5'b00011, 1'b1);

    check_operation(1'b1, 16'h3c00, 16'h0000, 3'b000, 16'h3c00, 5'b00000, 1'b1);
    check_operation(1'b1, 16'h4400, 16'h0000, 3'b000, 16'h4000, 5'b00000, 1'b1);
    check_operation(1'b1, 16'h4000, 16'h0000, 3'b000, 16'h3da8, 5'b00001, 1'b1);
    check_operation(1'b1, 16'h4000, 16'h0000, 3'b011, 16'h3da9, 5'b00001, 1'b1);
    check_operation(1'b1, 16'hbc00, 16'h0000, 3'b000, 16'h7e00, 5'b10000, 1'b0);
    check_operation(1'b1, 16'h8000, 16'h0000, 3'b000, 16'h8000, 5'b00000, 1'b0);
    check_operation(1'b1, 16'h7c00, 16'h0000, 3'b000, 16'h7c00, 5'b00000, 1'b0);
    check_operation(1'b1, 16'h7d00, 16'h0000, 3'b000, 16'h7e00, 5'b10000, 1'b0);
    check_operation(1'b1, 16'h0001, 16'h0000, 3'b000, 16'h0c00, 5'b00000, 1'b1);

    $display("hardfloat iterative division and square-root simulation passed");
    $finish;
  end
endmodule
