// Simulates both directions of a ready-valid interface adapter.
// SPDX-License-Identifier: Apache-2.0
module interface_tb;
  typedef struct packed {
    logic valid;
    logic [7:0] bits;
  } forward_t;

  typedef struct packed {
    logic ready;
  } backward_t;

  forward_t ingress_in;
  backward_t egress_in;
  backward_t ingress_out;
  forward_t egress_out;

  ReadyValidAdapter dut (
    .ingress_in(ingress_in),
    .egress_in(egress_in),
    .ingress_out(ingress_out),
    .egress_out(egress_out)
  );

  initial begin
    ingress_in = '{valid: 1'b1, bits: 8'hA5};
    egress_in = '{ready: 1'b1};
    #1;

    if (egress_out.valid !== 1'b1 ||
        egress_out.bits !== 8'hA5)
      $fatal(1, "forward interface flow failed");
    if (ingress_out.ready !== 1'b1)
      $fatal(1, "backward interface flow failed");

    $display("interface simulation passed");
    $finish;
  end
endmodule
