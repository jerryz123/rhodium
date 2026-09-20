// Simulates both directions of a recursively nested interface adapter.
// SPDX-License-Identifier: Apache-2.0
module nested_interface_tb;
  typedef struct packed {
    logic valid;
    logic [7:0] bits;
  } command_forward_t;

  typedef struct packed {
    logic ready;
  } command_backward_t;

  typedef struct packed {
    command_forward_t command;
    logic enable;
    command_backward_t response;
  } control_forward_t;

  typedef struct packed {
    command_backward_t command;
    command_forward_t response;
    logic [7:0] status;
  } control_backward_t;

  control_forward_t ingress_in;
  control_backward_t egress_in;
  control_backward_t ingress_out;
  control_forward_t egress_out;

  NestedInterfaceAdapter dut (
    .ingress_in(ingress_in),
    .egress_in(egress_in),
    .ingress_out(ingress_out),
    .egress_out(egress_out)
  );

  initial begin
    ingress_in = '{command: '{valid: 1'b1, bits: 8'hA5},
                   enable: 1'b1,
                   response: '{ready: 1'b1}};
    egress_in = '{command: '{ready: 1'b1},
                  response: '{valid: 1'b1, bits: 8'h5A},
                  status: 8'h3C};
    #1;

    if (egress_out.command.valid !== 1'b1 ||
        egress_out.command.bits !== 8'hA5 ||
        egress_out.enable !== 1'b1 ||
        egress_out.response.ready !== 1'b1)
      $fatal(1, "nested forward interface flow failed");
    if (ingress_out.command.ready !== 1'b1 ||
        ingress_out.response.valid !== 1'b1 ||
        ingress_out.response.bits !== 8'h5A ||
        ingress_out.status !== 8'h3C)
      $fatal(1, "nested backward interface flow failed");

    $display("nested interface simulation passed");
    $finish;
  end
endmodule
