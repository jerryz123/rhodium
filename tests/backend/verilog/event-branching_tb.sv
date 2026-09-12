// Changes legal crossbar grants under backpressure and checks traced lanes against a reference.
module event_branching_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock=0, reset=1;
  logic [2:0][2:0] grants;
  forward_t source[2], sink[6];
  reverse_t source_ready[6], sink_ready[2];
  EventFeedbackCrossbar dut(.clock(clock), .reset(reset), .grants(grants),
    .ingress_0_in(source[0]), .ingress_0_out(source_ready[0]),
    .ingress_1_in(source[1]), .ingress_1_out(source_ready[1]),
    .ingress_2_in(source[0]), .ingress_2_out(source_ready[2]),
    .ingress_3_in(source[1]), .ingress_3_out(source_ready[3]),
    .ingress_4_in(source[0]), .ingress_4_out(source_ready[4]),
    .ingress_5_in(source[1]), .ingress_5_out(source_ready[5]),
    .egress_0_out(sink[0]), .egress_0_in(sink_ready[0]),
    .egress_1_out(sink[1]), .egress_1_in(sink_ready[1]),
    .egress_2_out(sink[2]), .egress_2_in(sink_ready[0]),
    .egress_3_out(sink[3]), .egress_3_in(sink_ready[1]),
    .egress_4_out(sink[4]), .egress_4_in(sink_ready[0]),
    .egress_5_out(sink[5]), .egress_5_in(sink_ready[1]));
  always #5 clock=~clock;
  import "DPI-C" function void branching_bind();
  import "DPI-C" function void branching_sample(input int unsigned lane, rst, valid, ready, payloads,
    out_valid, out_ready, out_payloads, grants);
  import "DPI-C" function void branching_check();
  import "DPI-C" function void branching_finish();
  initial begin
    branching_bind();
    for(int step=0; step<520; ++step) begin
      reset=step==0 || step==130 || step==350;
      grants='0;
      for(int i=0;i<3;++i)
        if(step>=460 || (step%13!=0 && step%9!=i)) grants[i][(i+step)%3]=1;
      foreach(source[i]) begin
        source[i].valid=step<460;
        source[i].bits=8'h2a;
        sink_ready[i].ready=step>=460 || (step%(5+i)<3 && !(step>=124 && step<=130));
      end
      if(step>=1 && step<=6) begin
        grants='0;
        foreach(source[i]) begin
          source[i].valid=step==1;
          sink_ready[i].ready=step==6;
        end
        if(step==2) begin grants[0][2]=1; grants[1][0]=1; end
        if(step==3) grants[2][2]=1;
        if(step==4) grants[2][1]=1;
      end
      @(posedge clock);
      if(!reset) for(int lane=0;lane<2;++lane) for(int i=0;i<2;++i) begin
        assert(source_ready[lane*2+i]==source_ready[4+i]) else $fatal(1,"trace changed readiness");
        assert(sink[lane*2+i].valid==sink[4+i].valid && (!sink[4+i].valid || sink[lane*2+i].bits==sink[4+i].bits))
          else $fatal(1,"trace changed buffered exit");
      end
      for(int lane=0;lane<2;++lane) begin
        int unsigned valid_mask, ready_mask, payloads, out_valid, out_ready, out_payloads;
        valid_mask=0; ready_mask=0; payloads=0; out_valid=0; out_ready=0; out_payloads=0;
        for(int i=0;i<2;++i) begin
          valid_mask|=int'(source[i].valid)<<i;
          ready_mask|=int'(source_ready[lane*2+i].ready)<<i;
          payloads|=int'(source[i].bits)<<(8*i);
          out_valid|=int'(sink[lane*2+i].valid)<<i;
          out_ready|=int'(sink_ready[i].ready)<<i;
          out_payloads|=int'(sink[lane*2+i].bits)<<(8*i);
        end
        branching_sample(32'(lane),32'(reset),valid_mask,ready_mask,payloads,out_valid,out_ready,out_payloads,32'(grants));
      end
      #1; branching_check();
      @(negedge clock);
    end
    branching_finish();
    $finish;
  end
endmodule
