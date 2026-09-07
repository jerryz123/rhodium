// Exercises same-cycle lineage, hierarchy, stalls, drops, wide payloads, and reset.
module event_runtime_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  typedef struct packed { logic valid; logic [64:0] bits; } wide_t;
  logic clock = 0, reset = 1, enabled = 1;
  logic [7:0] heartbeat;
  forward_t sources_0_in, sources_1_in, sinks_0_out, sinks_1_out;
  reverse_t sources_0_out, sources_1_out, sinks_0_in, sinks_1_in;
  wide_t wide_source_in, wide_sink_out;
  EventRuntime dut(.*);
  import "DPI-C" function void event_runtime_check(input int unsigned phase);
  import "DPI-C" function void event_runtime_order_test();
  always #5 clock = ~clock;

  task automatic tick(input int unsigned phase);
    #1;
    assert (sinks_0_out.bits == sources_0_in.bits + 8'd1);
    assert (sinks_1_out.bits == sources_1_in.bits + 8'd1);
    assert (sinks_0_out.valid == (sources_0_in.valid && enabled && sources_0_in.bits != 8'd2));
    assert (sinks_1_out.valid == (sources_1_in.valid && sources_1_in.bits != 8'd2));
    assert (sources_0_out.ready == (enabled && (sources_0_in.bits == 8'd2 || sinks_0_in.ready)));
    assert (sources_1_out.ready == (sources_1_in.bits == 8'd2 || sinks_1_in.ready));
    assert (wide_sink_out.bits == wide_source_in.bits);
    assert (wide_sink_out.valid == (wide_source_in.valid && wide_source_in.bits != 0));
    @(posedge clock);
    #1;
    event_runtime_check(phase);
    assert (heartbeat == (phase == 0 || phase == 5 ? 8'd0 : phase == 6 ? 8'd1 : 8'(phase)));
    @(negedge clock);
  endtask

  initial begin
    event_runtime_order_test();
    sources_0_in = '{1, 8'd10}; sources_1_in = '{1, 8'd20};
    sinks_0_in = '{1}; sinks_1_in = '{1};
    wide_source_in = '{1, 65'h1_0123456789abcdef};
    tick(0);
    reset = 0;
    tick(1);
    sources_0_in.bits = 12;
    sinks_0_in.ready = 0; sinks_1_in.ready = 0; wide_source_in.valid = 0;
    tick(2);
    enabled = 0; sources_1_in.bits = 2; sinks_1_in.ready = 1;
    wide_source_in = '{1, 65'd0};
    tick(3);
    enabled = 1; sources_0_in.bits = 2; sources_1_in.valid = 0;
    wide_source_in = '{1, 65'h1_0000000000000009};
    tick(4);
    reset = 1;
    tick(5);
    reset = 0; sources_0_in = '{1, 8'd7}; sources_1_in = '{1, 8'd8};
    sinks_0_in.ready = 1; wide_source_in = '{1, 65'd1};
    tick(6);
    $display("event runtime simulation passed");
    $finish;
  end
endmodule
