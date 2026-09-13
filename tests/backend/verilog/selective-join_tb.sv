// Verifies selective joining consumes the control and exactly the selected data lanes atomically.
// SPDX-License-Identifier: Apache-2.0
module selective_join_tb;
  typedef struct packed {
    logic       valid;
    logic [2:0] bits;
  } selection_forward_t;
  typedef struct packed {
    logic       valid;
    logic [7:0] bits;
  } data_forward_t;
  typedef struct packed {
    logic [2:0] selection;
    logic [2:0][7:0] values;
  } result_payload_t;
  typedef struct packed {
    logic            valid;
    result_payload_t bits;
  } result_forward_t;
  typedef struct packed {
    logic ready;
  } reverse_t;

  selection_forward_t selection_in;
  data_forward_t ingress_0_in;
  data_forward_t ingress_1_in;
  data_forward_t ingress_2_in;
  reverse_t egress_in;
  reverse_t selection_out;
  reverse_t ingress_0_out;
  reverse_t ingress_1_out;
  reverse_t ingress_2_out;
  result_forward_t egress_out;

  SelectiveRendezvous dut (
    .selection_in (selection_in),
    .ingress_0_in (ingress_0_in),
    .ingress_1_in (ingress_1_in),
    .ingress_2_in (ingress_2_in),
    .egress_in    (egress_in),
    .selection_out(selection_out),
    .ingress_0_out(ingress_0_out),
    .ingress_1_out(ingress_1_out),
    .ingress_2_out(ingress_2_out),
    .egress_out   (egress_out)
  );

  initial begin
    selection_in = '{valid: 1'b1, bits: 3'b101};
    ingress_0_in = '{valid: 1'b1, bits: 8'ha0};
    ingress_1_in = '{valid: 1'b0, bits: 8'hb1};
    ingress_2_in = '{valid: 1'b0, bits: 8'hc2};
    egress_in = '{ready: 1'b1};
    #1;
    assert (!egress_out.valid && !selection_out.ready &&
            !ingress_0_out.ready && !ingress_1_out.ready && ingress_2_out.ready)
      else $fatal(1, "selective join partially consumed an incomplete selection");

    ingress_2_in.valid = 1'b1;
    egress_in.ready = 1'b0;
    #1;
    assert (egress_out.valid && !selection_out.ready &&
            !ingress_0_out.ready && !ingress_1_out.ready && !ingress_2_out.ready &&
            egress_out.bits.selection == 3'b101 &&
            egress_out.bits.values[0] == 8'ha0 && egress_out.bits.values[2] == 8'hc2)
      else $fatal(1, "selective join did not hold a complete output under backpressure");

    egress_in.ready = 1'b1;
    #1;
    assert (selection_out.ready && ingress_0_out.ready &&
            !ingress_1_out.ready && ingress_2_out.ready)
      else $fatal(1, "selective join did not release the selected sources together");

    selection_in.bits = 3'b010;
    ingress_0_in.valid = 1'b0;
    ingress_1_in = '{valid: 1'b1, bits: 8'hd3};
    ingress_2_in.valid = 1'b0;
    egress_in.ready = 1'b0;
    #1;
    assert (egress_out.valid && egress_out.bits.selection == 3'b010 &&
            egress_out.bits.values[1] == 8'hd3 && !selection_out.ready)
      else $fatal(1, "selective join did not track a changed Decoupled selection");

    selection_in.bits = 3'b000;
    egress_in.ready = 1'b1;
    #1;
    assert (egress_out.valid && selection_out.ready &&
            !ingress_0_out.ready && !ingress_1_out.ready && !ingress_2_out.ready &&
            egress_out.bits.selection == 3'b000)
      else $fatal(1, "empty selection did not produce an explicitly empty result");

    selection_in.valid = 1'b0;
    #1;
    assert (!egress_out.valid && !ingress_0_out.ready &&
            !ingress_1_out.ready && !ingress_2_out.ready)
      else $fatal(1, "selective join transferred without a selection token");

    $finish;
  end
endmodule
