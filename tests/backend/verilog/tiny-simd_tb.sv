// Simulates program loading, generated lanes, enum decode, and host-enabled multiplication.
// SPDX-License-Identifier: Apache-2.0
module tiny_simd_tb;
  typedef struct packed {
    logic [1:0] address;
    logic [11:0] data;
    logic write;
  } loader_request_t;

  typedef struct packed {
    logic ready;
  } loader_response_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic load_seed = 1'b0;
  logic step = 1'b0;
  logic [1:0][7:0] seeds;
  loader_request_t loader_in;
  logic [1:0][7:0] results;
  logic [1:0] pc_out;
  loader_response_t loader_out;

  TinySIMD dut (
    .clock(clock),
    .reset(reset),
    .load_seed(load_seed),
    .step(step),
    .seeds(seeds),
    .loader_in(loader_in),
    .results(results),
    .pc_out(pc_out),
    .loader_out(loader_out)
  );

  always #5 clock = ~clock;

  task automatic write_instruction(
    input logic [1:0] address,
    input logic [3:0] opcode,
    input logic [7:0] operand
  );
    loader_in = '{address: address, data: {opcode, operand}, write: 1'b1};
    @(posedge clock);
    #1;
  endtask

  initial begin
    seeds[0] = 8'd3;
    seeds[1] = 8'd5;
    loader_in = '0;

    // The program is loaded while synchronous reset holds the architectural
    // registers at zero. Memory itself deliberately has no reset semantics.
    write_instruction(2'd0, 4'h1, 8'd2);   // Add
    write_instruction(2'd1, 4'h8, 8'd3);   // Multiply
    write_instruction(2'd2, 4'h2, 8'h0F);  // Xor
    write_instruction(2'd3, 4'h3, 8'd1);   // ShiftLeft
    loader_in.write = 1'b0;

    assert (loader_out.ready === 1'b1)
      else $fatal(1, "program loader was not ready");

    reset = 1'b0;
    load_seed = 1'b1;
    @(posedge clock);
    #1;
    assert (results[0] == 8'd3 && results[1] == 8'd5)
      else $fatal(1, "host-generated lanes did not load their seeds");

    load_seed = 1'b0;
    step = 1'b1;
    @(posedge clock);
    #1;
    assert (results[0] == 8'd5 && results[1] == 8'd7 && pc_out == 2'd1)
      else $fatal(1, "enum-selected add instruction failed");

    @(posedge clock);
    #1;
    assert (results[0] == 8'd15 && results[1] == 8'd21 && pc_out == 2'd2)
      else $fatal(1, "host-enabled multiply instruction failed");

    @(posedge clock);
    #1;
    assert (results[0] == 8'h00 && results[1] == 8'h1A && pc_out == 2'd3)
      else $fatal(1, "enum-selected xor instruction failed");

    @(posedge clock);
    #1;
    assert (results[0] == 8'h00 && results[1] == 8'h34 && pc_out == 2'd0)
      else $fatal(1, "enum-selected shift instruction failed");

    $display("tiny SIMD simulation passed");
    $finish;
  end
endmodule
