// Compares real loop/indirect-branch execution, wrong-path stores, flushes, and cycles with BTB off/on.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_branch_prediction_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic [63:0] address; } instruction_req_bits_t;
  typedef struct packed { logic valid; instruction_req_bits_t bits; } instruction_req_t;
  typedef struct packed { logic [31:0] word; logic page_fault, access_fault; } instruction_resp_bits_t;
  typedef struct packed { logic valid; instruction_resp_bits_t bits; } instruction_resp_t;
  typedef struct packed { ready_t request; instruction_resp_t response; } instruction_in_t;
  typedef struct packed { logic flush, invalidate_all; instruction_req_t request; ready_t response; } instruction_out_t;
  typedef struct packed {
    logic [63:0] address;
    logic [3:0] access, atomic;
    logic [1:0] width;
    logic unsigned_0;
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
    logic [2:0] locality;
  } data_req_bits_t;
  typedef struct packed { logic valid; data_req_bits_t bits; } data_req_t;
  typedef struct packed { logic access_fault; logic [63:0] data; logic [1:0] destination; logic [4:0] rd; logic [1:0] floating_point_precision; } data_resp_bits_t;
  typedef struct packed { logic valid; data_resp_bits_t bits; } data_resp_t;
  typedef struct packed { ready_t request; logic request_fault, request_access_fault; data_resp_t response; logic drained, reservation_valid; } data_in_t;
  typedef struct packed { data_req_t request; } data_out_t;
  instruction_in_t instruction_in[2];
  instruction_out_t instruction_out[2];
  data_in_t data_in[2];
  data_out_t data_out[2];
  instruction_resp_t response[2];
  logic clock = 0, reset = 1;
  int cycles = 0, completed[2], stores[2], flushes[2], warm_pairs[2], last_request_cycle[2];
  logic [63:0] last_request[2];
  BranchPredictionFixture dut (
    .clock(clock), .reset(reset),
    .instruction_0_in(instruction_in[0]), .instruction_0_out(instruction_out[0]),
    .instruction_1_in(instruction_in[1]), .instruction_1_out(instruction_out[1]),
    .data_0_in(data_in[0]), .data_0_out(data_out[0]),
    .data_1_in(data_in[1]), .data_1_out(data_out[1])
  );
  always #5 clock = ~clock;
  function automatic logic [31:0] branch(input int offset);
    logic [12:0] immediate;
    immediate = 13'(offset);
    return {immediate[12], immediate[10:5], 5'd0, 5'd1, 3'b001, immediate[4:1], immediate[11], 7'h63};
  endfunction
  function automatic logic [31:0] jump(input int offset);
    logic [20:0] immediate;
    immediate = 21'(offset);
    return {immediate[20], immediate[10:1], immediate[11], immediate[19:12], 5'd0, 7'h6f};
  endfunction
  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      0: return 32'h01400093; // x1 = 20
      4: return 32'h00000113; // x2 = 0
      8: return 32'h00110113; // x2++
      12: return 32'hfff08093; // x1--
      16: return branch(-8);
      20: return 32'h00203023; // sd x2, 0(x0)
      24: return 32'h02800193; // x3 = 40
      28: return 32'h00018267; // jalr x4, 0(x3), revisited with three targets
      32, 52, 68: return 32'h10003023; // Wrong-path store, must never issue.
      40: return 32'h03800193; // x3 = 56
      44: return 32'h00000013;
      48: return jump(-20);
      56: return 32'h00403423; // sd x4, 8(x0), link must be 32
      60: return 32'h04800193; // x3 = 72
      64: return jump(-36);
      72: return 32'h00203823; // sd x2, 16(x0)
      76: return 32'h0000100f; // fence.i
      80: return 32'h00203c23; // sd x2, 24(x0)
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
        completed[i] = 0; stores[i] = 0; flushes[i] = 0; warm_pairs[i] = 0;
        last_request[i] = '1; last_request_cycle[i] = -1;
      end else if (completed[i] == 0) begin
        if (instruction_out[i].flush) begin response[i] <= '0; flushes[i]++; last_request[i] = '1; end
        else if (instruction_out[i].response.ready) response[i].valid <= 0;
        if (instruction_out[i].request.valid && instruction_in[i].request.ready) begin
          response[i] <= '{1'b1, '{instruction_at(instruction_out[i].request.bits.address), 1'b0, 1'b0}};
          if (last_request[i] == 16 && instruction_out[i].request.bits.address == 8 && cycles == last_request_cycle[i] + 1) warm_pairs[i]++;
          last_request[i] = instruction_out[i].request.bits.address;
          last_request_cycle[i] = cycles;
        end
        if (data_out[i].request.valid) begin
          assert (data_out[i].request.bits.address == 64'(stores[i] * 8)) else $fatal(1, "core %0d wrong-path/duplicate store %h", i, data_out[i].request.bits.address);
          assert (data_out[i].request.bits.data == (stores[i] == 1 ? 64'd32 : 64'd20)) else $fatal(1, "core %0d incorrect architectural result", i);
          stores[i]++;
          if (stores[i] == 4) completed[i] = cycles;
        end
      end
    end
    if (completed[0] != 0 && completed[1] != 0) begin
      assert (warm_pairs[0] >= 10 && warm_pairs[1] == 0) else $fatal(1, "missing bubbleless trained backedges");
      assert (completed[0] < completed[1] && flushes[0] + 10 < flushes[1]) else $fatal(1, "BTB failed to reduce cycles/recoveries");
      $display("RV5Stage BTB: enabled %0d cycles/%0d flushes; disabled %0d cycles/%0d flushes", completed[0], flushes[0], completed[1], flushes[1]);
      $finish;
    end
  end
  initial begin repeat (3) @(negedge clock); reset = 0; end
  initial begin #50000; $fatal(1, "branch prediction core timeout"); end
endmodule
