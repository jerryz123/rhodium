// Compares functional lanes and public-transfer lineage through partial tracing and stalls.
// SPDX-License-Identifier: Apache-2.0
module event_partial_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock=0, reset=1;
  logic [2:0] grants;
  forward_t source[5], sink[2], gold_sink[2];
  reverse_t source_ready[5], gold_ready[5], sink_ready[2];
  logic selected_fire, joined_fire, middle_fire, middle_stall;
  EventPartial dut(.clock(clock), .reset(reset), .grants(grants),
    .ingress_0_in(source[0]), .ingress_0_out(source_ready[0]), .gold_ingress_0_in(source[0]), .gold_ingress_0_out(gold_ready[0]),
    .ingress_1_in(source[1]), .ingress_1_out(source_ready[1]), .gold_ingress_1_in(source[1]), .gold_ingress_1_out(gold_ready[1]),
    .ingress_2_in(source[2]), .ingress_2_out(source_ready[2]), .gold_ingress_2_in(source[2]), .gold_ingress_2_out(gold_ready[2]),
    .ingress_3_in(source[3]), .ingress_3_out(source_ready[3]), .gold_ingress_3_in(source[3]), .gold_ingress_3_out(gold_ready[3]),
    .ingress_4_in(source[4]), .ingress_4_out(source_ready[4]), .gold_ingress_4_in(source[4]), .gold_ingress_4_out(gold_ready[4]),
    .egress_0_out(sink[0]), .egress_0_in(sink_ready[0]), .gold_egress_0_out(gold_sink[0]), .gold_egress_0_in(sink_ready[0]),
    .egress_1_out(sink[1]), .egress_1_in(sink_ready[1]), .gold_egress_1_out(gold_sink[1]), .gold_egress_1_in(sink_ready[1]),
    .selected_fire(selected_fire), .joined_fire(joined_fire), .middle_fire(middle_fire), .middle_stall(middle_stall));
  always #5 clock=~clock;
  import "DPI-C" function void partial_bind();
  import "DPI-C" function void partial_sample(int unsigned rst, inputs, choice, selected, joined, middle, after_fire, isolated, stalled, middle_stalled, isolated_stalled);
  import "DPI-C" function void partial_check();
  import "DPI-C" function void partial_finish();
  initial begin
    partial_bind();
    for (int step=0; step<270; ++step) begin
      int mask;
      reset=step==0 || step==120;
      grants=3'(1 << (step%3));
      for (int i=0; i<5; ++i) begin
        source[i].valid=(step<240 || i==3) && (step%7!=i);
        source[i].bits=8'h2a;
      end
      sink_ready[0].ready=step>=240 || step%9<5;
      sink_ready[1].ready=step>=240 || step%5<3;
      @(posedge clock);
      mask=0;
      for (int i=0; i<5; ++i) begin
        if (!reset) assert(source_ready[i]==gold_ready[i]) else $fatal(1,"partial tracing changed readiness");
        if (source[i].valid && source_ready[i].ready) mask |= 1 << i;
      end
      for (int i=0; i<2; ++i)
        if (!reset) assert(sink[i].valid==gold_sink[i].valid && (!sink[i].valid || sink[i].bits==gold_sink[i].bits))
          else $fatal(1,"partial tracing changed output");
      partial_sample(32'(reset), mask, 32'(step%3), 32'(selected_fire), 32'(joined_fire), 32'(middle_fire),
        32'(sink[0].valid && sink_ready[0].ready), 32'(sink[1].valid && sink_ready[1].ready),
        32'(sink[0].valid && !sink_ready[0].ready),32'(middle_stall),32'(sink[1].valid && !sink_ready[1].ready));
      #1; partial_check();
      @(negedge clock);
    end
    partial_finish();
    $finish;
  end
endmodule
