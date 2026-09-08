// Checks WB NTL association, replay, FP memory, squash, trap entry, and interrupt entry.
module rv5stage_ntl_tb;
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
  logic [63:0] time_counter = 0, hart_id = 0, mstatus, satp;
  logic [1:0] privilege;
  logic translation_flush;
  RV5StageInterrupts interrupts;
  iin_t instruction_access_in;
  iout_t instruction_access_out;
  din_t data_access_in;
  dout_t data_access_out;
  RV5StageCore dut (.load_access_in('0), .load_access_out(), .prefetch_out(), .*);
  always #5 clock = ~clock;
  integer scenario, cycles, attempts, accepted, irq_age, load_delay;
  logic done, hint_fetched, i_valid, d_valid;
  logic [31:0] i_word;
  RV5StageDataResp d_bits;

  function automatic logic [31:0] instruction_at(input logic [63:0] pc);
    case (pc)
      0: return 32'h10000093;   // mtvec = 256
      4: return 32'h30509073;
      8: return 32'h000020b7;   // FS = Initial
      12: return scenario == 12 ? 32'h00808093 : 32'h00008093;
      16: return 32'h30009073;
      20: return 32'h08000093;  // enable MTIP locally
      24: return 32'h30409073;
      28: return 32'h02a00113;  // x2 = 42
      32: return 32'hf2000053;  // fmv.d.x f0,x0
      36: return scenario == 13 ? 32'h0180006f : 32'h01c0006f;
      60: return 32'h30003503;  // older delayed ld x10,768(x0)
      64: return scenario < 4 ? (32'h00200033 + (32'(scenario) << 20)) : 32'h00200033;
      68: case (scenario)
        4: return 32'h00000013; // non-memory successor consumes the hint
        5: return 32'h00500033; // replacement NTL.ALL
        6: return 32'h20003183; // ld x3,512(x0)
        7: return 32'h20003087; // fld f1,512(x0)
        8: return 32'h20003027; // fsd f0,512(x0)
        9: return 32'h00000073; // ecall
        10: return 32'h0080006f;// branch consumes; squash younger NTL
        12: return 32'h00000013;// withheld until interrupt entry
        default: return 32'h20203023; // sd x2,512(x0)
      endcase
      72: case (scenario)
        6: return 32'h20303423; // sd x3,520(x0), no locality
        7: return 32'h20103427; // fsd f1,520(x0), no locality
        10: return 32'h00500033;
        14: return 32'h00500033; // younger hint squashed by the target's fault
        default: return 32'h20203423;
      endcase
      76: return 32'h20203823; // done store
      80: return 32'h0000006f;
      256: return 32'h22203023;// handler writes a distinct signature at 544
      default: return 32'h0000006f;
    endcase
  endfunction

  always_comb begin
    instruction_access_in = '0;
    instruction_access_in.request.ready = (!i_valid || instruction_access_out.response.ready) && !(scenario == 12 && hint_fetched && instruction_access_out.request.bits.address < 256);
    instruction_access_in.response.valid = i_valid;
    instruction_access_in.response.bits.word = i_word;
    data_access_in = '0;
    data_access_in.request.ready = !(scenario == 11 && attempts < 2);
    data_access_in.request_fault = scenario == 14 && data_access_out.request.valid && data_access_out.request.bits.address == 512;
    data_access_in.drained = !d_valid && load_delay == 0;
    data_access_in.response.valid = d_valid;
    data_access_in.response.bits = d_bits;
    interrupts = '0;
    interrupts.machine_timer = scenario == 12 && irq_age > 25;
  end

  task automatic tick;
    @(negedge clock);
    #1;
  endtask
  always @(posedge clock) begin
    if (reset) begin
      i_valid <= 0;
      d_valid <= 0;
      d_bits <= '0;
      hint_fetched <= 0;
      irq_age <= 0;
      load_delay <= 0;
      attempts <= 0;
      accepted <= 0;
      done <= 0;
    end else begin
      if (hint_fetched) irq_age <= irq_age + 1;
      if (instruction_access_out.flush) i_valid <= 0;
      else begin
        if (instruction_access_out.response.ready) i_valid <= 0;
        if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
          assert (!i_valid || instruction_access_out.response.ready) else $fatal(1, "instruction response overflow");
          i_valid <= 1;
          i_word <= instruction_at(instruction_access_out.request.bits.address);
          if (instruction_access_out.request.bits.address == 64) hint_fetched <= 1;
        end
      end
      d_valid <= 0;
      if (load_delay > 0) begin
        load_delay <= load_delay - 1;
        if (load_delay == 1) d_valid <= 1;
      end
      if (data_access_out.request.valid) begin
        attempts <= attempts + 1;
        if (data_access_out.request.bits.address == 512) begin
          if (scenario == 13) assert (load_delay > 0) else $fatal(1, "NTL serialized behind older load");
          assert (data_access_out.request.bits.locality == (scenario < 4 ? 3'(scenario + 1) : 3'd1))
            else $fatal(1, "scenario %0d target lost locality on attempt %0d", scenario, attempts);
        end else if (data_access_out.request.bits.address == 520) begin
          assert (data_access_out.request.bits.locality == (scenario == 5 ? 3'd4 : 3'd0))
            else $fatal(1, "scenario %0d replacement/consumption failed", scenario);
        end else begin
          assert (data_access_out.request.bits.locality == 0)
            else $fatal(1, "scenario %0d leaked locality across control flow", scenario);
        end
        if (data_access_in.request.ready && !data_access_in.request_fault) begin
          accepted <= accepted + 1;
          if (data_access_out.request.bits.access == 1) begin
            d_bits <= '{access_fault: 1'b0, data: 64'd42,
                        destination: data_access_out.request.bits.destination,
                        rd: data_access_out.request.bits.rd,
                        floating_point_precision: data_access_out.request.bits.floating_point_precision};
            load_delay <= scenario == 13 ? 50 : 8;
          end
          if (data_access_out.request.bits.address == 520 && (scenario == 6 || scenario == 7))
            assert (data_access_out.request.bits.data == 42) else $fatal(1, "load/store data changed");
          if (data_access_out.request.bits.address == 528 || data_access_out.request.bits.address == 544) begin
            assert ((data_access_out.request.bits.address == 544) == (scenario == 9 || scenario == 12 || scenario == 14))
              else $fatal(1, "scenario %0d took the wrong trap path", scenario);
            done <= 1;
          end
        end
      end
    end
  end
  initial begin
    for (scenario = 0; scenario < 15; scenario++) begin
      reset = 1;
      repeat (4) tick();
      reset = 0;
      cycles = 0;
      while (!done && cycles < 700) begin tick(); cycles++; end
      assert (done) else $fatal(1, "NTL scenario %0d timed out", scenario);
      if (scenario == 11) assert (attempts == accepted + 2) else $fatal(1, "replay count mismatch");
      if (scenario == 9 || scenario == 12 || scenario == 14) assert (accepted == 1) else $fatal(1, "trap allowed younger memory");
      $display("NTL scenario %0d passed", scenario);
    end
    $finish;
  end
endmodule
