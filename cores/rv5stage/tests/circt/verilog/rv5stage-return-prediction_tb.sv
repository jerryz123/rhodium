// Compares alternating-call-site return prediction under the same two-entry BTB pressure.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_return_prediction_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic [63:0] address; } instruction_req_bits_t;
  typedef struct packed { logic valid; instruction_req_bits_t bits; } instruction_req_t;
  typedef struct packed { logic [31:0] word; logic page_fault, access_fault; } instruction_resp_bits_t;
  typedef struct packed { logic valid; instruction_resp_bits_t bits; } instruction_resp_t;
  typedef struct packed { ready_t request; instruction_resp_t response; } instruction_in_t;
  typedef struct packed { logic flush, invalidate_all; instruction_req_t request; ready_t response; } instruction_out_t;
  typedef struct packed {
    logic [7:0] byte_mask;
    logic [63:0] address;
    logic [3:0] access, atomic;
    logic [1:0] width;
    logic unsigned_0;
    logic [63:0] data;
    logic [8:0] writeback;
    logic origin;
    logic [2:0] locality;
  } data_req_bits_t;
  typedef struct packed { logic valid; data_req_bits_t bits; } data_req_t;
  typedef struct packed { logic access_fault; logic [63:0] data; logic [8:0] writeback; logic origin; } data_resp_bits_t;
  typedef struct packed { logic valid; data_resp_bits_t bits; } data_resp_t;
  typedef struct packed { ready_t request; logic request_fault, request_access_fault; data_resp_t response; logic drained, reservation_valid; } data_in_t;
  typedef struct packed { data_req_t request; } data_out_t;
  instruction_in_t instruction_in[2];
  instruction_out_t instruction_out[2];
  data_in_t data_in[2];
  data_out_t data_out[2];
  instruction_resp_t response[2];
  logic clock = 0, reset = 1;
  int cycles = 0, completed[2], flushes[2];
  ReturnPredictionFixture dut (
    .clock(clock), .reset(reset),
    .instruction_0_in(instruction_in[0]), .instruction_0_out(instruction_out[0]),
    .instruction_1_in(instruction_in[1]), .instruction_1_out(instruction_out[1]),
    .data_0_in(data_in[0]), .data_0_out(data_out[0]),
    .data_1_in(data_in[1]), .data_1_out(data_out[1])
  );
  always #5 clock = ~clock;

  function automatic logic [31:0] branch_not_equal(input int rs1, offset);
    logic [12:0] immediate;
    immediate = 13'(offset);
    return {immediate[12], immediate[10:5], 5'd0, 5'(rs1), 3'b001, immediate[4:1], immediate[11], 7'h63};
  endfunction
  function automatic logic [31:0] jump_and_link(input int rd, offset);
    logic [20:0] immediate;
    immediate = 21'(offset);
    return {immediate[20], immediate[10:1], immediate[11], immediate[19:12], 5'(rd), 7'h6f};
  endfunction
  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      0: return 32'h01000113;                 // x2 = 16 loop iterations.
      4: return 32'h00000193;                 // x3 = 0 completed calls.
      8: return jump_and_link(1, 32);          // Call function, return to 12.
      12: return jump_and_link(1, 28);         // Same function, return to 16.
      16: return 32'hfff10113;                 // x2--.
      20: return branch_not_equal(2, -12);     // Repeat at the first call site.
      24: return 32'h00303023;                 // sd x3, 0(x0): completion.
      40: return 32'h00118193;                 // Function body: x3++.
      44: return 32'h00008067;                 // ret: jalr x0, 0(x1).
      default: return 32'h00000013;
    endcase
  endfunction

  always_comb begin
    for (int i = 0; i < 2; i++) begin
      instruction_in[i] = '0;
      instruction_in[i].request.ready = instruction_out[i].flush || !response[i].valid || instruction_out[i].response.ready;
      instruction_in[i].response = response[i];
      data_in[i] = '0;
      data_in[i].request.ready = 1;
      data_in[i].drained = 1;
    end
  end
  always @(posedge clock) begin
    cycles = cycles + 1;
    for (int i = 0; i < 2; i++) begin
      if (reset) begin
        response[i] <= '0;
        completed[i] = 0;
        flushes[i] = 0;
      end else if (completed[i] == 0) begin
        if (instruction_out[i].flush) begin
          response[i] <= '0;
          flushes[i]++;
        end else if (instruction_out[i].response.ready) begin
          response[i].valid <= 0;
        end
        if (instruction_out[i].request.valid && instruction_in[i].request.ready)
          response[i] <= '{1'b1, '{instruction_at(instruction_out[i].request.bits.address), 1'b0, 1'b0}};
        if (data_out[i].request.valid) begin
          assert (data_out[i].request.bits.address == 0 && data_out[i].request.bits.data == 32)
            else $fatal(1, "core %0d returned incorrect architectural result address=%h data=%h", i, data_out[i].request.bits.address, data_out[i].request.bits.data);
          completed[i] = cycles;
        end
      end
    end
    if (completed[0] != 0 && completed[1] != 0) begin
      assert (completed[0] + 20 < completed[1]) else $fatal(1, "RAS did not reduce alternating-return cycles: enabled=%0d disabled=%0d", completed[0], completed[1]);
      assert (flushes[0] + 20 < flushes[1]) else $fatal(1, "RAS did not reduce alternating-return recoveries: enabled=%0d disabled=%0d", flushes[0], flushes[1]);
      $display("RV5Stage RAS: enabled %0d cycles/%0d flushes; disabled %0d cycles/%0d flushes", completed[0], flushes[0], completed[1], flushes[1]);
      $finish;
    end
  end
  initial begin repeat (3) @(negedge clock); reset = 0; end
  initial begin #100000; $fatal(1, "return prediction timeout"); end
endmodule
