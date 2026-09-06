// Verifies routed time beats, interrupt updates, and stalled destination stability.

module tiled_distribution_tb;
  logic clock = 0;
  logic reset = 1;
  always #5 clock = ~clock;

  struct packed {
    logic valid;
    TiledTimeBeat bits;
  } time_ingress_in;
  struct packed {
    logic valid;
    IndexedState bits;
  } interrupt_ingress_in;
  struct packed { logic ready; } time_egress_0_in;
  struct packed { logic ready; } time_egress_1_in;
  struct packed { logic ready; } interrupt_egress_0_in;
  struct packed { logic ready; } interrupt_egress_1_in;
  struct packed { logic ready; } time_ingress_out;
  struct packed { logic ready; } interrupt_ingress_out;
  struct packed { logic valid; FramedFixedFlit bits; } time_egress_0_out;
  struct packed { logic valid; FramedFixedFlit bits; } time_egress_1_out;
  struct packed { logic valid; TiledHartInterrupts bits; } interrupt_egress_0_out;
  struct packed { logic valid; TiledHartInterrupts bits; } interrupt_egress_1_out;

  TiledDistributionFixture dut (.*);

  task automatic send_interrupt(input logic hart,
                                input logic machine_software,
                                input logic machine_timer);
    interrupt_ingress_in.valid = 1;
    interrupt_ingress_in.bits.index = hart;
    interrupt_ingress_in.bits.value.machine_software = machine_software;
    interrupt_ingress_in.bits.value.machine_timer = machine_timer;
    do @(posedge clock); while (!interrupt_ingress_out.ready);
    @(negedge clock);
    interrupt_ingress_in.valid = 0;
  endtask

  task automatic send_time(input logic hart,
                           input logic first,
                           input logic last,
                           input logic [15:0] payload);
    time_ingress_in.valid = 1;
    time_ingress_in.bits.hart = hart;
    time_ingress_in.bits.flit.first = first;
    time_ingress_in.bits.flit.last = last;
    time_ingress_in.bits.flit.payload = payload;
    do @(posedge clock); while (!time_ingress_out.ready);
    @(negedge clock);
    time_ingress_in.valid = 0;
  endtask

  initial begin
    time_ingress_in = '0;
    interrupt_ingress_in = '0;
    time_egress_0_in.ready = 1;
    time_egress_1_in.ready = 1;
    interrupt_egress_0_in.ready = 1;
    interrupt_egress_1_in.ready = 0;
    repeat (3) @(posedge clock);
    reset = 0;

    send_interrupt(1, 1, 0);
    repeat (40) begin
      @(negedge clock);
      if (interrupt_egress_1_out.valid) begin
        assert (interrupt_egress_1_out.bits.machine_software == 1);
        assert (interrupt_egress_1_out.bits.machine_timer == 0);
        break;
      end
    end
    assert (interrupt_egress_1_out.valid);
    assert (!interrupt_egress_0_out.valid);
    interrupt_egress_1_in.ready = 1;
    @(posedge clock);

    time_egress_0_in.ready = 0;
    time_egress_1_in.ready = 0;
    send_time(0, 1, 0, 16'hCAFE);
    repeat (40) begin
      @(negedge clock);
      if (time_egress_0_out.valid) begin
        assert (time_egress_0_out.bits.first == 1);
        assert (time_egress_0_out.bits.last == 0);
        assert (time_egress_0_out.bits.payload == 16'hCAFE);
        break;
      end
    end
    assert (time_egress_0_out.valid);
    assert (!time_egress_1_out.valid);
    time_egress_0_in.ready = 1;
    time_egress_1_in.ready = 1;
    @(posedge clock);

    interrupt_egress_1_in.ready = 0;
    send_interrupt(1, 0, 1);
    repeat (40) begin
      @(negedge clock);
      if (interrupt_egress_1_out.valid) break;
    end
    assert (interrupt_egress_1_out.valid);
    assert (interrupt_egress_1_out.bits.machine_software == 0);
    assert (interrupt_egress_1_out.bits.machine_timer == 1);
    repeat (3) begin
      @(negedge clock);
      assert (interrupt_egress_1_out.valid);
      assert (interrupt_egress_1_out.bits.machine_software == 0);
      assert (interrupt_egress_1_out.bits.machine_timer == 1);
    end
    interrupt_egress_1_in.ready = 1;
    @(posedge clock);

    $finish;
  end
endmodule
