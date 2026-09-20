// Checks loss, replay, qualification, and reset against the adapter's public wiring.
// SPDX-License-Identifier: Apache-2.0
module event_offer_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock = 0, reset = 1, qualify;
  logic offer_ready;
  forward_t source, sink;
  reverse_t ready;
  EventOffer dut(.clock(clock), .reset(reset), .source_in(source),
    .sink_out(sink), .sink_in(ready), .qualify(qualify), .offer_ready(offer_ready));
  always #5 clock = ~clock;
  import "DPI-C" function void event_offer_bind();
  import "DPI-C" function void event_offer_sample(input int unsigned rst,
    input int unsigned valid, input int unsigned ready, input int unsigned qualify,
    input int unsigned payload);
  import "DPI-C" function void event_offer_check();
  import "DPI-C" function void event_offer_finish();
  initial begin
    event_offer_bind();
    for (int step = 0; step < 100; ++step) begin
      reset = step == 0 || step == 39;
      source.valid = step % 10 != 5;
      source.bits = 8'((step / 2) % 4);
      ready.ready = step % 4 >= 2;
      qualify = step % 7 >= 2;
      // Every decade starts with an identical-payload rejected/replayed pair.
      if (step % 10 < 2) begin
        source.valid = 1;
        source.bits = 8'h2a;
        qualify = 1;
        ready.ready = step % 10 == 1;
      end
      @(posedge clock);
      // Check every cycle, even invalid/faulting ones: there is no holding slot
      // and qualification never feeds either of these functional outputs.
      assert(sink == source) else $fatal(1, "offer wiring changed at step %0d", step);
      assert(offer_ready == ready.ready) else $fatal(1, "qualification gated readiness");
      event_offer_sample(32'(reset), 32'(source.valid), 32'(ready.ready),
        32'(qualify), 32'(source.bits));
      #1; event_offer_check();
      @(negedge clock);
    end
    event_offer_finish();
    $display("event offer simulation passed");
    $finish;
  end
endmodule
