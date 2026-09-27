// Scores bank outputs independently while exercising concurrent residency and slot reuse.
// SPDX-License-Identifier: Apache-2.0
module event_retained_bank_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock=0, reset=1;
  logic [1:0] allocation;
  logic [2:0] release_entries;
  logic [1:0][1:0] selection;
  logic [1:0] emit;
  forward_t source;
  forward_t [1:0] sinks;
  reverse_t source_ready;
  logic [2:0] live=0;
  logic [2:0][7:0] payload=0;
  EventRetainedBank dut(.clock(clock),.reset(reset),.allocation(allocation),
    .release_0(release_entries),.selection(selection),.emit(emit),
    .source_in(source),.source_out(source_ready),.sinks_0_out(sinks[0]),.sinks_1_out(sinks[1]));
  always #5 clock=~clock;
  import "DPI-C" function void bank_bind();
  import "DPI-C" function void bank_sample(input int unsigned reset, capture, allocation,
    input int unsigned release_mask, payload, emit0, select0, emit1, select1);
  import "DPI-C" function void bank_check();
  import "DPI-C" function void bank_finish();
  initial begin
    bank_bind();
    for(int step=0; step<180; ++step) begin
      reset=step==0 || step==71;
      allocation=2'(step%3);
      release_entries=3'(step%8);
      // Repeated equal payloads cannot be used as transaction identities.
      source.valid=step%7!=4;
      source.bits=8'h2a;
      selection[0]=2'((step/2)%3);
      selection[1]=2'((step/5)%3);
      emit=2'(step%4);
      @(posedge clock);
      if(!reset) begin
        assert(source_ready.ready==(!live[allocation] || release_entries[allocation])) else $fatal(1,"bank readiness");
        for(int lane=0;lane<2;++lane) begin
          assert(sinks[lane].valid==(emit[lane] && live[selection[lane]])) else $fatal(1,"bank output validity");
          if(sinks[lane].valid) assert(sinks[lane].bits==payload[selection[lane]]) else $fatal(1,"bank output payload");
        end
      end
      bank_sample(32'(reset),32'(source.valid && source_ready.ready),32'(allocation),
        32'(release_entries),32'(source.bits),32'(sinks[0].valid),32'(selection[0]),
        32'(sinks[1].valid),32'(selection[1]));
      if(reset) live=0;
      else begin
        live=live & ~release_entries;
        if(source.valid && source_ready.ready) begin live[allocation]=1; payload[allocation]=source.bits; end
      end
      #1; bank_check();
      @(negedge clock);
    end
    bank_finish();
    $finish;
  end
endmodule
