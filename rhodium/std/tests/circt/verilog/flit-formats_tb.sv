// Checks that fixed-flit framing advances on transfers rather than clock cycles.
// SPDX-License-Identifier: Apache-2.0
module flit_formats_tb;
  typedef struct packed { logic [7:0] payload; } fixed_flit_t;
  typedef struct packed {
    logic first;
    logic last;
    logic [7:0] payload;
  } framed_fixed_flit_t;
  typedef struct packed { logic valid; fixed_flit_t bits; } ingress_forward_t;
  typedef struct packed { logic ready; } ingress_reverse_t;
  typedef struct packed { logic ready; } egress_reverse_t;
  typedef struct packed { logic valid; framed_fixed_flit_t bits; } egress_forward_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  ingress_forward_t ingress_in;
  egress_reverse_t egress_in;
  ingress_reverse_t ingress_out;
  egress_forward_t egress_out;

  FixedFlitFramer dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  task automatic expect_flit(
    input logic expected_first,
    input logic expected_last,
    input logic [7:0] expected_payload
  );
    assert (egress_out.valid &&
            egress_out.bits.first == expected_first &&
            egress_out.bits.last == expected_last &&
            egress_out.bits.payload == expected_payload)
      else $fatal(1, "unexpected framed flit");
  endtask

  initial begin
    ingress_in = '{valid: 1'b0, bits: '{payload: 8'h00}};
    egress_in = '{ready: 1'b0};
    tick();
    reset = 1'b0;

    ingress_in = '{valid: 1'b1, bits: '{payload: 8'hA1}};
    #1;
    expect_flit(1'b1, 1'b0, 8'hA1);
    tick();
    expect_flit(1'b1, 1'b0, 8'hA1);

    egress_in.ready = 1'b1;
    tick();
    ingress_in.bits.payload = 8'hB2;
    #1;
    expect_flit(1'b0, 1'b0, 8'hB2);

    tick();
    ingress_in.bits.payload = 8'hC3;
    #1;
    expect_flit(1'b0, 1'b1, 8'hC3);

    egress_in.ready = 1'b0;
    tick();
    expect_flit(1'b0, 1'b1, 8'hC3);

    egress_in.ready = 1'b1;
    tick();
    ingress_in.bits.payload = 8'hD4;
    #1;
    expect_flit(1'b1, 1'b0, 8'hD4);

    $display("Flit format simulation passed");
    $finish;
  end
endmodule
