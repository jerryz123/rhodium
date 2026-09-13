// Checks the public retained-owner circuit against an independent state/handshake model.
// SPDX-License-Identifier: Apache-2.0
module event_retained_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock=0, reset=1, emit, finish_owner;
  forward_t source, unrelated, sink;
  reverse_t source_ready, unrelated_ready, sink_ready;
  logic model_active=0;
  logic [7:0] model_bits=0;
  EventRetained dut(.clock(clock),.reset(reset),.emit(emit),.finish_owner(finish_owner),
    .source_in(source),.source_out(source_ready),.unrelated_in(unrelated),
    .unrelated_out(unrelated_ready),.sink_in(sink_ready),.sink_out(sink));
  always #5 clock=~clock;
  import "DPI-C" function void retained_bind();
  import "DPI-C" function void retained_sample(input int unsigned rst, capture, release_owner,
    source_bits, transfer, unrelated_transfer, output_bits);
  import "DPI-C" function void retained_check();
  import "DPI-C" function void retained_finish();
  initial begin
    retained_bind();
    for(int step=0; step<120; ++step) begin
      reset=step==0 || step==29;
      source.valid=step%11!=7;
      source.bits=8'h2a; // Equal payloads must still have distinct occurrence identities.
      unrelated.valid=step%3!=0;
      unrelated.bits=8'h2a;
      emit=step%5!=2;
      finish_owner=step%9==7;
      sink_ready.ready=step%4!=1;
      @(posedge clock);
      if(!reset) begin
        assert(source_ready.ready==(!model_active || finish_owner)) else $fatal(1,"command readiness");
        assert(sink.valid==((model_active && emit) || unrelated.valid)) else $fatal(1,"attempt validity");
        if(sink.valid) assert(sink.bits==((model_active && emit) ? 8'(model_bits+1) : unrelated.bits)) else $fatal(1,"attempt payload");
        if(unrelated.valid) assert(unrelated_ready.ready==(sink_ready.ready && !(model_active && emit))) else $fatal(1,"arbitration changed");
      end
      retained_sample(32'(reset),32'(source.valid && source_ready.ready),32'(finish_owner),
        32'(source.bits),32'(sink.valid && sink_ready.ready),
        32'(unrelated.valid && unrelated_ready.ready),32'(sink.bits));
      if(reset) model_active=0;
      else if(source.valid && source_ready.ready) begin model_active=1; model_bits=source.bits; end
      else if(finish_owner) model_active=0;
      #1; retained_check();
      @(negedge clock);
    end
    retained_finish();
    $finish;
  end
endmodule
