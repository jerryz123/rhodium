// Checks zero-cycle Decode bypass, parcel retention, stalls, precise faults, and clear priority.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_instruction_buffer_tb;
  typedef struct packed { logic valid; logic [63:0] pc, target; logic compressed; } prediction_t;
  typedef struct packed {
    logic [63:0] pc;
    logic [31:0] word;
    logic [1:0] mask;
    logic page_fault, access_fault;
    prediction_t prediction;
  } packet_t;
  typedef struct packed { logic valid; packet_t bits; } packets_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; } pulse_t;
  typedef struct packed {
    logic [63:0] pc;
    logic [31:0] instruction, raw_instruction;
    logic [63:0] sequential_pc, predicted_next_pc;
    logic compressed_illegal, instruction_page_fault, instruction_access_fault;
    logic [63:0] instruction_fault_address;
  } instruction_t;
  typedef struct packed { logic valid; instruction_t bits; } fetched_t;
  logic clock = 0, reset = 1;
  packets_t packets_in = '0;
  ready_t packets_out;
  ready_t fetched_in = '{1'b1};
  fetched_t fetched_out;
  pulse_t clear_in = '0;
  RV5StageInstructionBufferFixture dut (.*);
  always #5 clock = ~clock;

  task automatic offer(input logic [63:0] pc, input logic [31:0] word, input logic [1:0] mask = 2'b11);
    @(negedge clock);
    packets_in = '{1'b1, '{pc, word, mask, 1'b0, 1'b0, '0}};
    #1;
  endtask
  task automatic expect_instruction(input logic [63:0] pc, input logic [31:0] raw);
    assert (fetched_out.valid && fetched_out.bits.pc == pc && fetched_out.bits.raw_instruction == raw)
      else $fatal(1, "IBuf mismatch pc=%h raw=%h valid=%b", fetched_out.bits.pc, fetched_out.bits.raw_instruction, fetched_out.valid);
  endtask
  task automatic edge_step;
    @(posedge clock); #1;
  endtask

  initial begin
    repeat (2) edge_step();
    @(negedge clock); reset = 0;
    offer('h100, 'h00100093);
    expect_instruction('h100, 'h00100093); // No capture edge before Decode.
    assert (packets_out.ready);
    edge_step();
    offer('h104, 'h00850085);
    expect_instruction('h104, 'h85);
    edge_step();
    @(negedge clock); packets_in.valid = 0; fetched_in.ready = 0; #1;
    repeat (3) begin
      expect_instruction('h106, 'h85);
      assert (!packets_out.ready);
      edge_step();
    end
    @(negedge clock); fetched_in.ready = 1; #1;
    expect_instruction('h106, 'h85);
    edge_step();

    // A straddling first half enters residual storage even with Decode stalled.
    @(negedge clock); fetched_in.ready = 0;
    offer('hffe, 'h03130001, 2'b10);
    assert (!fetched_out.valid && packets_out.ready);
    edge_step();
    offer('h1000, 'h00850010);
    expect_instruction('hffe, 'h00100313);
    assert (!packets_out.ready);
    edge_step();
    @(negedge clock); fetched_in.ready = 1; #1;
    expect_instruction('hffe, 'h00100313);
    edge_step();
    @(negedge clock); packets_in.valid = 0; #1;
    expect_instruction('h1002, 'h85);
    edge_step();

    offer('h1ffe, 'h03130001, 2'b10);
    edge_step();
    offer('h2000, 0);
    packets_in.bits.page_fault = 1; #1;
    assert (fetched_out.valid && fetched_out.bits.pc == 'h1ffe && fetched_out.bits.instruction_page_fault && fetched_out.bits.instruction_fault_address == 'h2000);
    edge_step();
    offer('h3000, 'h00850085);
    edge_step();
    @(negedge clock); clear_in.valid = 1; #1;
    assert (!fetched_out.valid && !packets_out.ready);
    edge_step();
    @(negedge clock); clear_in.valid = 0; packets_in.valid = 0; #1;
    assert (!fetched_out.valid);
    $display("RV5Stage fall-through instruction buffer passed");
    $finish;
  end
  initial begin
    #2000; $fatal(1, "IBuf timeout");
  end
endmodule
