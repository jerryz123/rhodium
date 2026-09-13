// Exercises offer replacement while stalled, old-owner delivery, drain, and pending reset.
// SPDX-License-Identifier: Apache-2.0
module event_offer_register_tb;
  typedef struct packed {logic valid; logic [7:0] bits;} forward_t;
  typedef struct packed {logic ready;} ready_t;
  logic clock=0,reset=1;
  forward_t source_in,sink_out;
  ready_t sink_in;
  EventOfferRegister dut(.*);
  always #5 clock=~clock;
  import "DPI-C" function void offer_register_bind();
  import "DPI-C" function void offer_register_sample(int unsigned reset, update, data, valid, ready, payload);
  import "DPI-C" function void offer_register_check();
  import "DPI-C" function void offer_register_finish();
  always @(posedge clock) begin
    offer_register_sample(int'(reset),int'(source_in.valid),int'(source_in.bits),int'(sink_out.valid),int'(sink_in.ready),int'(sink_out.bits));
    #1; offer_register_check();
  end
  task automatic tick; @(posedge clock); #2; endtask
  int unsigned rng=32'hc7651234;
  function automatic int unsigned random_word();
    rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng;
  endfunction
  initial begin
    offer_register_bind(); source_in='0; sink_in='0; tick(); reset=0;
    source_in.valid=1; source_in.bits=8'h55; tick();
    source_in.bits=8'haa; tick(); // Replace a stalled owner.
    sink_in.ready=1; tick(); // Deliver the old owner while capturing the new one.
    source_in.valid=0; tick(); // Drain without replacement.
    sink_in.ready=0; source_in.valid=1; tick();
    reset=1; tick(); reset=0; // Reset a pending owner explicitly.
    repeat(1000) begin
      automatic int unsigned r=random_word();
      reset=r[7:0]==0; source_in.valid=r[0]; source_in.bits=r[1]?8'h55:8'haa; sink_in.ready=r[2]; tick();
    end
    reset=0; source_in.valid=0; sink_in.ready=1; repeat(2) tick();
    offer_register_finish(); $finish;
  end
  initial begin #20000; $fatal(1,"offer register timeout"); end
endmodule
