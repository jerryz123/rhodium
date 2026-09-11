// Drives legal changing matchings and compares direct/configured crossbars under backpressure.
module event_crossbar_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock=0, reset=1;
  logic [2:0][1:0] grants;
  forward_t source[6], sink[4];
  reverse_t source_ready[6], sink_ready[4];
  bit accepted[6];
  EventCrossbar dut(.clock(clock), .reset(reset), .grants(grants),
    .sources_0_in(source[0]), .sources_0_out(source_ready[0]),
    .sources_1_in(source[1]), .sources_1_out(source_ready[1]),
    .sources_2_in(source[2]), .sources_2_out(source_ready[2]),
    .sources_3_in(source[3]), .sources_3_out(source_ready[3]),
    .sources_4_in(source[4]), .sources_4_out(source_ready[4]),
    .sources_5_in(source[5]), .sources_5_out(source_ready[5]),
    .sinks_0_out(sink[0]), .sinks_0_in(sink_ready[0]),
    .sinks_1_out(sink[1]), .sinks_1_in(sink_ready[1]),
    .sinks_2_out(sink[2]), .sinks_2_in(sink_ready[2]),
    .sinks_3_out(sink[3]), .sinks_3_in(sink_ready[3]));
  always #5 clock=~clock;
  import "DPI-C" function void crossbar_bind();
  import "DPI-C" function void crossbar_sample(input int unsigned lane, rst,
    valid_mask, ready_mask, payloads, out_valid, out_ready, out_payloads, grants);
  import "DPI-C" function void crossbar_check();
  import "DPI-C" function void crossbar_finish();
  initial begin
    crossbar_bind();
    foreach(source[i]) begin source[i]='0; accepted[i]=1; end
    for(int step=0; step<220; ++step) begin
      reset=step==0 || step==60;
      grants='0;
      if(step%11!=0) begin
        grants[step%3][0]=1;
        if(step%7!=0) grants[(step+1)%3][1]=1;
      end
      if(step==1) begin
        grants='0;
        grants[0][0]=1;
        grants[1][1]=1;
      end
      foreach(source[i]) begin
        if(accepted[i] || !source[i].valid || reset) begin
          source[i].valid=step<180 && (step<4 || step%9!=(i%3) || (step>=48 && step<=60));
          // Frequent identical values prevent payload-based occurrence matching.
          source[i].bits=step%4==0 ? 8'(step+i%3) : 8'h2a;
        end
      end
      foreach(sink_ready[i])
        sink_ready[i].ready=step==1 || step>=180 || ((step% (4+i%2)!=1) && !(step>=48 && step<=60));
      @(posedge clock);
      if(!reset) begin
        for(int i=0; i<3; ++i)
          assert(source_ready[i]==source_ready[i+3]) else $fatal(1,"crossbar readiness differs");
        for(int i=0; i<2; ++i)
          assert(sink[i].valid==sink[i+2].valid && (!sink[i].valid || sink[i].bits==sink[i+2].bits))
            else $fatal(1,"crossbar output differs");
      end
      for(int lane=0; lane<2; ++lane) begin
        int unsigned valid_mask, ready_mask, payloads, out_valid, out_ready, out_payloads;
        valid_mask=0; ready_mask=0; payloads=0;
        out_valid=0; out_ready=0; out_payloads=0;
        for(int i=0; i<3; ++i) begin
          valid_mask |= int'(source[lane*3+i].valid)<<i;
          ready_mask |= int'(source_ready[lane*3+i].ready)<<i;
          payloads |= int'(source[lane*3+i].bits)<<(i*8);
          accepted[lane*3+i]=source[lane*3+i].valid && source_ready[lane*3+i].ready;
        end
        for(int i=0; i<2; ++i) begin
          out_valid |= int'(sink[lane*2+i].valid)<<i;
          out_ready |= int'(sink_ready[lane*2+i].ready)<<i;
          out_payloads |= int'(sink[lane*2+i].bits)<<(i*8);
        end
        crossbar_sample(32'(lane),32'(reset),valid_mask,ready_mask,payloads,out_valid,out_ready,out_payloads,32'(grants));
      end
      #1; crossbar_check();
      @(negedge clock);
    end
    crossbar_finish();
    $finish;
  end
endmodule
