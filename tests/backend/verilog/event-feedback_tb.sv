// Drives registered recirculation and compares instrumentation against an untraced lane.
module event_feedback_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock=0, reset=1, select_feedback=0, route_feedback=0;
  forward_t source, sink[2];
  reverse_t source_ready[2], sink_ready;
  EventFeedback dut(.clock(clock), .reset(reset), .select_feedback(select_feedback), .route_feedback(route_feedback),
    .ingress_0_in(source), .ingress_0_out(source_ready[0]),
    .ingress_1_in(source), .ingress_1_out(source_ready[1]),
    .egress_0_out(sink[0]), .egress_0_in(sink_ready),
    .egress_1_out(sink[1]), .egress_1_in(sink_ready));
  always #5 clock=~clock;
  import "DPI-C" function void feedback_bind();
  import "DPI-C" function void feedback_sample(input int unsigned rst, select_feedback, route_feedback,
    valid, ready, payload, out_valid, out_ready, out_payload);
  import "DPI-C" function void feedback_check();
  import "DPI-C" function void feedback_finish();
  initial begin
    feedback_bind();
    for(int step=0; step<330; ++step) begin
      reset=step==0 || step==9 || step==60 || step==180;
      source.valid=step<300;
      source.bits=8'h2a; // Identical payloads deliberately cannot identify parents.
      select_feedback=step<300 && step%7<3;
      route_feedback=step<300 && step%11<5;
      sink_ready.ready=step>=300 || step%5<3;
      if(step>=1 && step<=8) begin
        select_feedback=step==2 || step==3 || step==8;
        route_feedback=select_feedback;
        sink_ready.ready=step==5;
        source.valid=step!=4;
      end
      @(posedge clock);
      if(!reset) begin
        assert(source_ready[0]==source_ready[1]) else $fatal(1,"instrumentation changed readiness");
        assert(sink[0].valid==sink[1].valid && (!sink[0].valid || sink[0].bits==sink[1].bits))
          else $fatal(1,"instrumentation changed output");
      end
      feedback_sample(32'(reset),32'(select_feedback),32'(route_feedback),32'(source.valid),32'(source_ready[0].ready),
        32'(source.bits),32'(sink[0].valid),32'(sink_ready.ready),32'(sink[0].bits));
      #1; feedback_check();
      @(negedge clock);
    end
    feedback_finish();
    $finish;
  end
endmodule
