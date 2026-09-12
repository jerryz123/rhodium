// Exercises retained selections, prefix releases, replacement, flush, and downstream stalls.
module event_window_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } channel_t;
  typedef struct packed { logic ready; } ready_t;
  logic clock = 0, reset = 1, flush = 0, emit = 0;
  logic [1:0] release_count = 0, selected = 1, occupancy;
  channel_t source_in, sink_out;
  ready_t sink_in;
  EventWindow dut(.remove_count(release_count), .*);
  import "DPI-C" function void event_window_sample(input int unsigned rst, clear, offer,
      remove_count, selection, valid, payload, ready, out_valid, out_payload, count);
  import "DPI-C" function void event_window_check();
  import "DPI-C" function void event_window_finish();
  always #5 clock = ~clock;
  initial begin
    for (int step = 0; step < 160; ++step) begin
      reset = step == 0 || step == 79;
      flush = step == 19 || step == 20 || step == 55 || step == 111;
      release_count = 2'((step % 4 > int'(occupancy)) ? int'(occupancy) : step % 4);
      if (step % 8 < 4) release_count = 0;
      if (step % 16 == 12) release_count = occupancy;
      selected = occupancy > 1 && step % 3 == 0 ? 3 : 1;
      emit = occupancy != 0 && step % 5 != 0;
      source_in.valid = step < 150 && int'(occupancy) - int'(release_count) < 3;
      source_in.bits = 8'(step % 3);
      sink_in.ready = step % 7 > 2;
      @(posedge clock);
      event_window_sample(32'(reset), 32'(flush), 32'(emit), 32'(release_count), 32'(selected),
          32'(source_in.valid), 32'(source_in.bits), 32'(sink_in.ready),
          32'(sink_out.valid), 32'(sink_out.bits), 32'(occupancy));
      #1 event_window_check();
      @(negedge clock);
    end
    event_window_finish();
    $display("event window simulation passed");
    $finish;
  end
endmodule
