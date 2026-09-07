// Checks independently backpressured, exactly-once hart releases and reset rearming.
module soc_boot_release_tb;
  typedef struct packed { logic valid; } valid_t;
  typedef struct packed { logic ready; } ready_t;
  logic clock = 0, reset = 1;
  valid_t release_in;
  ready_t release_out;
  ready_t hart_release_0_in, hart_release_1_in, hart_release_2_in, hart_release_3_in;
  valid_t hart_release_0_out, hart_release_1_out, hart_release_2_out, hart_release_3_out;
  wire [3:0] offers = {hart_release_3_out.valid, hart_release_2_out.valid,
                       hart_release_1_out.valid, hart_release_0_out.valid};
  SoCBootDistributor dut(.*);
  always #5 clock = ~clock;
  task automatic tick;
    @(posedge clock); #1;
  endtask
  task automatic ready_mask(input logic [3:0] mask);
    {hart_release_3_in.ready, hart_release_2_in.ready,
     hart_release_1_in.ready, hart_release_0_in.ready} = mask;
  endtask
  task automatic restart;
    reset = 1; release_in = '0; ready_mask(0);
    repeat (2) tick();
    reset = 0;
    repeat (3) begin
      tick();
      assert(offers == 0 && release_out.ready) else $fatal(1, "release before host request");
    end
  endtask
  task automatic offer_release;
    release_in.valid = 1;
    tick(); release_in.valid = 0;
    assert(offers == 4'b1111 && !release_out.ready) else $fatal(1, "release not broadcast");
  endtask
  initial begin
    restart(); offer_release();
    for (int hart = 0; hart < 4; hart++) begin
      repeat (3) begin
        tick();
        assert(offers == (4'b1111 << hart)) else $fatal(1, "stalled release lost or repeated");
      end
      ready_mask(4'b0001 << hart); tick();
      // Leave accepted harts ready; none may receive a duplicate.
      ready_mask((4'b0001 << (hart + 1)) - 1);
    end
    repeat (5) begin
      tick();
      assert(offers == 0 && !release_out.ready) else $fatal(1, "one-shot distributor rearmed without reset");
    end
    restart(); offer_release();
    ready_mask(4'b0101); tick();
    assert(offers == 4'b1010) else $fatal(1, "independent acceptance failed");
    restart(); // Discard pending releases and permit a new cold boot.
    offer_release(); ready_mask(4'b1111); tick();
    assert(offers == 0) else $fatal(1, "reset did not rearm every hart");
    $display("SoC control-only release, independent stalls, and reset passed");
    $finish;
  end
  initial begin #10000; $fatal(1, "release test timeout"); end
endmodule
