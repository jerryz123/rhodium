// Verifies block payload mapping and unchanged ready-valid control.
// SPDX-License-Identifier: Apache-2.0
module flow_map_tb;
  typedef struct packed {
    logic       valid;
    logic [7:0] bits;
  } input_forward_t;
  typedef struct packed {
    logic [7:0] data;
    logic [3:0] tag;
  } mapped_t;
  typedef struct packed {
    logic    valid;
    mapped_t bits;
  } output_forward_t;
  typedef struct packed {
    logic ready;
  } reverse_t;

  input_forward_t ingress_in;
  logic [3:0] tag;
  reverse_t egress_in;
  reverse_t ingress_out;
  output_forward_t egress_out;

  MapFlowExample dut (
    .ingress_in (ingress_in),
    .tag        (tag),
    .egress_in  (egress_in),
    .ingress_out(ingress_out),
    .egress_out (egress_out)
  );

  initial begin
    ingress_in = '{valid: 1'b1, bits: 8'ha5};
    tag = 4'hc;
    egress_in = '{ready: 1'b0};
    #1;
    assert (!ingress_out.ready && egress_out.valid &&
            egress_out.bits.data == 8'ha5 &&
            egress_out.bits.tag == 4'hc)
      else $fatal(1, "flow map changed blocked control or mapped payload");

    egress_in.ready = 1'b1;
    #1;
    assert (ingress_out.ready && egress_out.valid &&
            egress_out.bits.data == 8'ha5 &&
            egress_out.bits.tag == 4'hc)
      else $fatal(1, "flow map did not forward ready-valid control");

    ingress_in.valid = 1'b0;
    tag = 4'h3;
    #1;
    assert (ingress_out.ready && !egress_out.valid &&
            egress_out.bits.data == 8'ha5 &&
            egress_out.bits.tag == 4'h3)
      else $fatal(1, "flow map did not preserve invalid control");

    $finish;
  end
endmodule
