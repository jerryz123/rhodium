// Verifies WB-only CMO dispatch, precise completion, privilege policy, and squash.
module rv5stage_zicbom_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; RV5StageInstructionReq bits; } ireq_t;
  typedef struct packed { logic valid; RV5StageInstructionResp bits; } iresp_t;
  typedef struct packed { ready_t request; iresp_t response; } iin_t;
  typedef struct packed { logic flush; logic invalidate_all; ireq_t request; ready_t response; } iout_t;
  typedef struct packed { logic valid; RV5StageDataReq bits; } dreq_t;
  typedef struct packed { logic valid; RV5StageDataResp bits; } dresp_t;
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; dresp_t response; logic drained; logic reservation_valid; } din_t;
  typedef struct packed { dreq_t request; } dout_t;
  logic clock = 0, reset = 1;
  logic [63:0] time_counter = 0, hart_id = 0;
  RV5StageInterrupts interrupts;
  iin_t instruction_access_in;
  iout_t instruction_access_out;
  din_t data_access_in;
  dout_t data_access_out;
  logic translation_flush;
  logic [1:0] privilege;
  logic [63:0] mstatus, satp;
  logic [66:0] prefetch_out;
  logic instruction_response_valid;
  logic [31:0] instruction_response_bits;
  integer scenario, attempts, accepted, pending_cycles, stores;
  logic done;
  RV5StageCore dut (.*);
  always #5 clock = ~clock;

  function automatic logic [31:0] cbo_instruction();
    if (scenario == 0) return 32'h0010a00f; // clean
    if (scenario == 2) return 32'h0020a00f; // flush
    return 32'h0000a00f;                   // invalidate
  endfunction
  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      64'h0: return 32'h342021f3;    // csrr x3, mcause
      64'h4: return 32'h00303423;    // sd x3, 8(x0)
      64'h8: return 32'h34302273;    // csrr x4, mtval
      64'hc: return 32'h00403823;    // sd x4, 16(x0)
      64'h10: return 32'h0000006f;
      64'h100: return 32'h03f00093;  // x1 = unaligned address 63
      64'h104: return scenario == 10 ? 32'h0340006f : 32'h00000013;
      64'h108: return scenario == 7 ? 32'h01000113 : (scenario >= 8 ? 32'h03000113 : 32'h00000113);
      64'h10c: return 32'h30a11073;  // csrw menvcfg, x2
      64'h110: return scenario >= 6 ? 32'h0080006f : scenario < 3 ? 32'h0100006f : 32'h0200006f;
      64'h118: return 32'h13000113;  // x2 = lower-privilege entry
      64'h11c: return 32'h34111073;  // csrw mepc, x2
      64'h120: return scenario < 3 ? 32'hb02022f3 : scenario == 9 ? 32'h00000113 : 32'h00001137;
      64'h124: return scenario < 3 ? 32'h00c0006f : 32'h00115113; // skip to CMO, or MPP = S/U
      64'h128: return 32'h30011073;  // csrw mstatus, x2
      64'h12c: return 32'h30200073;
      64'h130: return cbo_instruction();
      64'h134: return scenario < 3 ? 32'hb0202373 : 32'h00000013; // read retired count again
      64'h138: return scenario < 3 ? 32'h40530333 : 32'h02003023;
      64'h13c: return 32'h02603023; // observable counter delta, no fence
      default: return 32'h0000006f;
    endcase
  endfunction
  always_comb begin
    instruction_access_in = '0;
    instruction_access_in.request.ready = !instruction_response_valid;
    instruction_access_in.response.valid = instruction_response_valid;
    instruction_access_in.response.bits.word = instruction_response_bits;
  end
  assign data_access_in.request.ready = data_access_out.request.bits.access < 7 || attempts >= 3;
  assign data_access_in.request_fault = data_access_out.request.valid && data_access_out.request.bits.access >= 7 && scenario == 5;
  assign data_access_in.request_access_fault = data_access_out.request.valid && data_access_out.request.bits.access >= 7 && scenario == 4;
  assign data_access_in.response.valid = pending_cycles == 1;
  assign data_access_in.response.bits = {scenario == 3, {($bits(RV5StageDataResp)-1){1'b0}}};
  assign data_access_in.drained = pending_cycles == 0;
  assign data_access_in.reservation_valid = 1'b0;
  always_ff @(posedge clock) begin
    if (reset) begin
      instruction_response_valid <= 0;
      instruction_response_bits <= 0;
      attempts <= 0; accepted <= 0; pending_cycles <= 0; stores <= 0; done <= 0;
    end else begin
      if (instruction_access_out.flush) instruction_response_valid <= 0;
      else begin
        if (instruction_response_valid && instruction_access_out.response.ready) instruction_response_valid <= 0;
        if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
          instruction_response_valid <= 1;
          instruction_response_bits <= instruction_at(instruction_access_out.request.bits.address);
        end
      end
      if (pending_cycles != 0) pending_cycles <= pending_cycles - 1;
      if (data_access_out.request.valid && data_access_out.request.bits.access >= 7) begin
        assert (scenario != 6 && scenario != 9 && scenario != 10 && data_access_out.request.bits.address == 63 && data_access_out.request.bits.destination == 0)
          else $fatal(1, "CMO permission, squash, or original address violated");
        assert (data_access_out.request.bits.access == (scenario == 0 ? 8 : scenario == 2 || scenario == 7 ? 9 : 7))
          else $fatal(1, "wrong decoded/converted CMO operation");
        attempts <= attempts + 1;
        if (data_access_in.request.ready && !data_access_in.request_fault && !data_access_in.request_access_fault) begin
          assert (accepted == 0) else $fatal(1, "accepted CMO reissued");
          accepted <= accepted + 1;
          pending_cycles <= 24;
        end
      end
      if (data_access_out.request.valid && data_access_out.request.bits.access == 2) begin
        assert (pending_cycles == 0) else $fatal(1, "younger store/trap escaped pending CMO");
        if (scenario < 3 || scenario == 7 || scenario == 8 || scenario == 10) begin
          assert (data_access_out.request.bits.address == 32 && accepted == (scenario == 10 ? 0 : 1) && attempts == (scenario == 10 ? 0 : 4))
            else $fatal(1, "CMO replay, squash or ordered completion failed");
          if (scenario < 3) assert (data_access_out.request.bits.data == 3)
            else $fatal(1, "CMO replay or completion retired the wrong number of instructions");
          done <= 1;
        end else if (stores == 0) begin
          assert (data_access_out.request.bits.address == 8 && data_access_out.request.bits.data == (scenario == 3 || scenario == 4 ? 7 : scenario == 5 ? 15 : 2) && accepted == (scenario == 3 ? 1 : 0))
            else $fatal(1, "CMO wrong precise trap, scenario %0d", scenario);
          stores <= 1;
        end else begin
          assert (data_access_out.request.bits.address == 16 && data_access_out.request.bits.data == (scenario == 6 || scenario == 9 ? {32'b0,cbo_instruction()} : 64'd63))
            else $fatal(1, "CMO lost original tval");
          done <= 1;
        end
      end

    end
  end
  initial begin
    interrupts = '0;
    for (scenario = 0; scenario <= 10; scenario++) begin
      reset = 1;
      repeat (2) @(posedge clock);
      @(negedge clock); reset = 0;
      @(posedge clock);
      @(negedge clock);
      for (int cycles = 0; cycles < 1500 && !done; cycles++) begin @(posedge clock); #1; end
      assert (done) else $fatal(1, "CMO scenario %0d timed out", scenario);
    end
    $display("RV5Stage CMO WB authorization, retirement, permissions, and precise faults passed");
    $finish;
  end
endmodule
