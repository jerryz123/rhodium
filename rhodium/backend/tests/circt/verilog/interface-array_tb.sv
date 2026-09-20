// Simulates two independent ready-valid lanes flattened from an interface array.
// SPDX-License-Identifier: Apache-2.0
module interface_array_tb;
  typedef struct packed {
    logic       valid;
    logic [7:0] bits;
  } forward_t;
  typedef struct packed {
    logic ready;
  } reverse_t;

  forward_t ingress_0_in;
  forward_t ingress_1_in;
  reverse_t egress_0_in;
  reverse_t egress_1_in;
  reverse_t ingress_0_out;
  reverse_t ingress_1_out;
  forward_t egress_0_out;
  forward_t egress_1_out;

  InterfaceArrayAdapter dut (
    .ingress_0_in  (ingress_0_in),
    .ingress_1_in  (ingress_1_in),
    .egress_0_in   (egress_0_in),
    .egress_1_in   (egress_1_in),
    .ingress_0_out (ingress_0_out),
    .ingress_1_out (ingress_1_out),
    .egress_0_out  (egress_0_out),
    .egress_1_out  (egress_1_out)
  );

  initial begin
    ingress_0_in = '{valid: 1'b1, bits: 8'h35};
    ingress_1_in = '{valid: 1'b0, bits: 8'hca};
    egress_0_in = '{ready: 1'b0};
    egress_1_in = '{ready: 1'b1};
    #1;

    if (egress_0_out !== ingress_0_in ||
        egress_1_out !== ingress_1_in ||
        ingress_0_out !== egress_0_in ||
        ingress_1_out !== egress_1_in)
      $fatal(1, "interface array lanes did not pass through independently");

    $finish;
  end
endmodule
