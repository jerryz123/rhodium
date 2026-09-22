// Checks retained record mapping against live captures during stalls and invalid cycles.
// SPDX-License-Identifier: Apache-2.0
module retained_flow_map_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } input_forward_t;
  typedef struct packed { logic [7:0] data; logic [3:0] tag; } mapped_t;
  typedef struct packed { logic valid; mapped_t bits; } output_forward_t;
  typedef struct packed { logic ready; } reverse_t;
  input_forward_t ingress_in;
  output_forward_t egress_out;
  reverse_t egress_in, ingress_out;
  logic [3:0] tag;
  MapFlowExample dut(.ingress_in(ingress_in), .tag(tag), .egress_in(egress_in),
                     .ingress_out(ingress_out), .egress_out(egress_out));
  initial begin
    for (int cycle = 0; cycle < 512; cycle++) begin
      ingress_in.valid = 1'(cycle >> 2);
      ingress_in.bits = 8'(cycle * 37 + 5);
      egress_in.ready = 1'(cycle >> 4);
      tag = 4'(cycle * 7 + 3);
      #1;
      assert (ingress_out.ready == egress_in.ready && egress_out.valid == ingress_in.valid &&
              egress_out.bits.data == ingress_in.bits && egress_out.bits.tag == tag)
        else $fatal(1, "retained map mismatch at cycle %0d", cycle);
      // Change only the captured signal, including while valid and blocked.
      tag = tag ^ 4'hf;
      #1;
      assert (egress_out.bits.data == ingress_in.bits && egress_out.bits.tag == tag &&
              ingress_out.ready == egress_in.ready && egress_out.valid == ingress_in.valid)
        else $fatal(1, "retained map froze a live capture at cycle %0d", cycle);
    end
    $finish;
  end
endmodule
