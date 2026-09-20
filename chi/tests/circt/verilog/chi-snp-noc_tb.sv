// Simulates NodeID-selected CHI SNP delivery through an independently routed plane.
// SPDX-License-Identifier: Apache-2.0
module chi_snp_noc_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHISnoopDispatch bits; } dispatch_t;
  typedef struct packed { logic valid; CHISnpFlit bits; } snoop_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  dispatch_t dispatch_in;
  ready_t snoops_0_in;
  ready_t snoops_1_in;
  ready_t dispatch_out;
  snoop_t snoops_0_out;
  snoop_t snoops_1_out;

  CHISnpNoCFixture dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  task automatic send_snoop(
    input logic [6:0] target_id,
    input logic target_lane,
    input logic [11:0] txn_id
  );
    integer cycles;
    begin
      dispatch_in = '0;
      dispatch_in.bits.target_id = target_id;
      dispatch_in.bits.flit.src_id = 7'd5;
      dispatch_in.bits.flit.txn_id = txn_id;
      dispatch_in.bits.flit.opcode = 5'h0a;
      dispatch_in.bits.flit.address = 41'h12345;
      dispatch_in.valid = 1'b1;
      #1;
      while (!dispatch_out.ready)
        tick();
      tick();
      dispatch_in = '0;

      for (cycles = 0; cycles < 8; cycles = cycles + 1) begin
        #1;
        if ((target_lane ? snoops_1_out.valid : snoops_0_out.valid))
          break;
        tick();
      end
      assert (cycles < 8)
        else $fatal(1, "SNP did not reach the selected RN-F");
      assert (!(target_lane ? snoops_0_out.valid : snoops_1_out.valid))
        else $fatal(1, "SNP appeared at an unselected RN-F");
      if (target_lane) begin
        assert (snoops_1_out.bits.src_id == 7'd5 &&
                snoops_1_out.bits.txn_id == txn_id &&
                snoops_1_out.bits.opcode == 5'h0a &&
                snoops_1_out.bits.address == 41'h12345)
          else $fatal(1, "second RN-F SNP payload changed in transport");
      end else begin
        assert (snoops_0_out.bits.src_id == 7'd5 &&
                snoops_0_out.bits.txn_id == txn_id &&
                snoops_0_out.bits.opcode == 5'h0a &&
                snoops_0_out.bits.address == 41'h12345)
          else $fatal(1, "first RN-F SNP payload changed in transport");
      end
      tick();
    end
  endtask

  initial begin
    dispatch_in = '0;
    snoops_0_in.ready = 1'b1;
    snoops_1_in.ready = 1'b1;
    tick();
    reset = 1'b0;

    send_snoop(7'd2, 1'b0, 12'h123);
    send_snoop(7'd3, 1'b1, 12'h456);

    $display("chi SNP NoC simulation passed");
    $finish;
  end
endmodule
