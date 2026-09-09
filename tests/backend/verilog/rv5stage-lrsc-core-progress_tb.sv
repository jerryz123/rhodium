// Qualifies word/doubleword constrained loops, prediction, read pressure, and competing LR/SC progress.
module rv5stage_lrsc_core_progress_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; RV5StageDataReq bits; } request_t;
  typedef struct packed { logic valid; RV5StageDataResp bits; } response_t;
  typedef struct packed { request_t request; } host_in_t;
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; response_t response; logic drained; logic reservation_valid; } host_out_t;
  logic clock = 0, reset = 1, run = 0;
  host_in_t host_in;
  host_out_t host_out;
  logic instruction_requests, data_requests, eviction_waiting;
  logic rv32, predicted;
  byte unsigned image[65536];
  int completions = 0, fetches = 0, demands = 0, blocked_snoops = 0;
  int failures = 0;
  logic [63:0] result;
  RV5StageLRSCCoreProgress dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    if (!reset) begin
      if (host_out.response.valid) begin
        assert (!host_out.response.bits.access_fault) else $fatal(1, "host access fault");
        completions++;
        result = host_out.response.bits.data;
      end
      if (run) begin
        fetches += int'(instruction_requests);
        demands += int'(data_requests);
        blocked_snoops += int'(eviction_waiting);
      end
    end
    @(posedge clock);
    #1;
  endtask

  task automatic access(input bit write, input int address, input logic [63:0] data = 0,
                        input int operation = 0, input int data_width = 3);
    int target_count;
    target_count = completions + 1;
    for (int cycle = 0; !host_out.request.ready && cycle < 10000; cycle++) tick();
    assert (host_out.request.ready) else $fatal(1, "host admission timeout");
    host_in.request.bits = '0;
    host_in.request.bits.address = 64'(address);
    host_in.request.bits.access = operation != 0 ? 4'(operation) : write ? 2 : 1;
    host_in.request.bits.width = 2'(data_width);
    host_in.request.bits.data = data;
    host_in.request.bits.destination = write ? 0 : 1;
    host_in.request.valid = 1;
    tick();
    host_in.request.valid = 0;
    for (int cycle = 0; completions < target_count && cycle < 10000; cycle++) tick();
    assert (completions == target_count) else $fatal(1, "host completion timeout at %h (run=%b)", address, run);
  endtask

  function automatic logic [31:0] addi(input int rd, input int rs, input int imm);
    return {12'(imm), 5'(rs), 3'b000, 5'(rd), 7'h13};
  endfunction
  function automatic logic [31:0] branch(input int rs, input bit nonzero, input int offset);
    return {1'(offset >> 12), 6'(offset >> 5), 5'd0, 5'(rs), 3'(nonzero), 4'(offset >> 1), 1'(offset >> 11), 7'h63};
  endfunction
  function automatic logic [31:0] jal(input int offset);
    return {1'(offset >> 20), 10'(offset >> 1), 1'(offset >> 11), 8'(offset >> 12), 5'd0, 7'h6f};
  endfunction
  function automatic logic [31:0] csr_write(input int csr, input int rs);
    return {12'(csr), 5'(rs), 3'b001, 5'd0, 7'h73};
  endfunction
  task automatic word(input int address, input logic [31:0] data);
    for (int lane = 0; lane < 4; lane++) image[address + lane] = 8'(data >> (lane * 8));
  endtask
  task automatic initialize_program(input int loop_pc, input bit translated, input bit redirects,
                                    input bit word_access = 0);
    int pc;
    logic [63:0] data;
    for (int address = 'h1000; address < 'h2100; address += 4) word(address, 32'h00000013);
    pc = 'h1000;
    word(pc, 32'h000042b7); pc += 4; // lui x5,4: reserved word
    word(pc, addi(11, 0, 8)); pc += 4;
    if (translated) begin
      word(pc, addi(10, 0, 1)); pc += 4;
      word(pc, 32'h03f51513); pc += 4; // slli x10,x10,63
      word(pc, addi(10, 10, 8)); pc += 4;
      word(pc, csr_write('h180, 10)); pc += 4; // satp: Sv39, root at 0x8000
      word(pc, 32'h12000073); pc += 4; // sfence.vma
      word(pc, 32'h00001537); pc += 4;
      word(pc, 32'h00155513); pc += 4; // srli x10,x10,1: MPP=S
      word(pc, csr_write('h300, 10)); pc += 4;
      word(pc, 32'h00002537); pc += 4;
      word(pc, addi(10, 10, loop_pc - 'h2000)); pc += 4;
      word(pc, csr_write('h341, 10)); pc += 4;
      word(pc, 32'h30200073); // mret to loop in S mode
    end else word(pc, jal(loop_pc - pc));
    // Exactly sixteen sequential instructions, including the retry branch:
    // LR, twelve branch/NOP slots, ADDI, SC, BNE retry. Pressure cases take
    // six forward branches over NOPs, stressing cold and disabled prediction.
    word(loop_pc, word_access ? 32'h1002a32f : 32'h1002b32f); // lr.w/d x6,(x5)
    for (int index = 1; index <= 12; index++)
      word(loop_pc + index * 4, !redirects ? branch(0, 0, 4) : index % 2 == 1 ? branch(0, 0, 8) : 32'h00000013);
    word(loop_pc + 52, addi(6, 6, 1));
    word(loop_pc + 56, word_access ? 32'h1862a3af : 32'h1862b3af); // sc.w/d x7,x6,(x5)
    word(loop_pc + 60, branch(7, 1, -60));
    word(loop_pc + 64, addi(11, 11, -1));
    word(loop_pc + 68, branch(11, 1, -68));
    word(loop_pc + 72, rv32 ? 32'h0462a023 : 32'h0462b023); // coherent signature
    word(loop_pc + 76, 32'h10500073); // wfi
    word(loop_pc + 80, jal(0));
    for (int address = 'h1000; address < 'h2100; address += 8) begin
      data = 0;
      for (int lane = 0; lane < 8; lane++) data |= 64'(image[address + lane]) << (lane * 8);
      access(1, address, data);
    end
    access(1, 'h4000, 0);
    access(1, 'h4040, 0);
    // Three-level identity mapping with 4-KiB leaves and V/R/W/X/A/D.
    // Crossing 0x2000 exercises a distinct ITLB entry and page-table walk.
    access(1, 'h8000, 'h2401);
    access(1, 'h9000, 'h2801);
    access(1, 'ha008, 'h4cf);
    access(1, 'ha010, 'h8cf);
    access(1, 'ha020, 'h10cf);
  endtask

  initial begin
    int loop_pc;
    int rival_successes, passed = 0;
    logic [63:0] rival_value;
    bit done;
    host_in = '0;
    // Warm a function in L1I, patch it through the core's write-back D-cache,
    // execute FENCE.I, and call it again. Only the new instruction returns 2.
    reset = 1; run = 0; tick(); tick(); reset = 0; tick();
    initialize_program('h1fc0, 0, 0, rv32);
    access(1, 'h1000, {jal('h2000-'h1004) | 32'h80, 32'h000022b7});
    access(1, 'h1008, {addi(7, 7, 'h313), 32'h002003b7});
    access(1, 'h1010, {32'h0000100f, 32'h0072a023}); // fence.i; sw x7,0(x5)
    access(1, 'h1018, {32'h00004437, jal('h2000-'h1018) | 32'h80});
    access(1, 'h1020, {32'h10500073, rv32 ? 32'h04642023 : 32'h04643023});
    access(1, 'h2000, {32'h00008067, addi(6, 0, 1)});
    run = 1; done = 0;
    for (int poll = 0; poll < 128 && !done; poll++) begin
      repeat (128) tick();
      access(0, 'h4040);
      done = result == 2;
    end
    assert (done) else $fatal(1, "full-core FENCE.I did not execute the dirty patched instruction");
    $display("Full-core FENCE.I self-modifying-code test passed");
    for (int word_access = int'(rv32); word_access < 2; word_access++) begin
    for (int translated = 0; translated < (rv32 ? 1 : 2); translated++) begin
      for (int alignment = 0; alignment < 3; alignment++) begin
        for (int pressure = 0; pressure < 4; pressure++) begin
          reset = 1; run = 0; tick(); tick(); reset = 0; tick();
          loop_pc = alignment == 0 ? 'h1fc0 : alignment == 1 ? 'h1fe0 : 'h1ffe;
          initialize_program(loop_pc, 1'(translated), pressure != 0, 1'(word_access));
          fetches = 0; demands = 0; blocked_snoops = 0;
          rival_successes = 0;
          run = 1; done = 0;
          // Rival wins count only after the core participates; cold fetch
          // must not let the host finish the test before any core data traffic.
          if (pressure == 3) begin
            for (int startup = 0; startup < 10000 && demands == 0; startup++) tick();
            assert (demands > 0) else $fatal(1, "core did not enter LR/SC phase: RV32=%0b predictor=%0b word=%0d Sv39=%0d pc=%h", rv32, predicted, word_access, translated, loop_pc);
          end
          for (int poll = 0; poll < 512 && !done; poll++) begin
            if (pressure == 1) begin
              // Repeated read-only replacement in the reserved line's LLC
              // set. Never write the reserved word while the core is running.
              for (int read_index = 0; read_index < 8; read_index++)
                access(0, 'h5000 + ((poll * 8 + read_index) % 32) * 128);
            end else if (pressure >= 2) begin
              // Other LRs may not starve the core. Competing SCs may win:
              // eventuality guarantees system progress, not per-hart fairness.
              access(0, 'h4000, 0, 3, word_access != 0 ? 2 : 3);
              rival_value = result;
              if (pressure == 3) begin
                access(0, 'h4000, rival_value, 4, word_access != 0 ? 2 : 3);
                if (result == 0) rival_successes++;
              end
            end else repeat (128) tick();
            access(0, 'h4040);
            done = result == 8 || (pressure == 3 && rival_successes >= 8);
          end
          if (!done) begin
            failures++;
            $display("Constrained LR/SC FAILED: RV32=%0b predictor=%0b word=%0d Sv39=%0d pc=%h pressure=%0d fetches=%0d demands=%0d blocked=%0d", rv32, predicted, word_access, translated, loop_pc, pressure, fetches, demands, blocked_snoops);
            // Diagnostic recovery is not a pass: record whether stopping the
            // competing traffic alone lets the same unmodified program finish.
            for (int poll = 0; poll < 64 && !done; poll++) begin
              repeat (128) tick();
              access(0, 'h4040);
              done = result == 8;
            end
            $display("Recovery after stopping pressure: %b", done);
            continue;
          end
          access(0, 'h4000);
          assert ((result == 8 || (pressure == 3 && rival_successes >= 8 && result <= 8)) && fetches > 0 && demands > 0)
            else $fatal(1, "incorrect architectural LR/SC result: RV32=%0b predictor=%0b word=%0d Sv39=%0d pc=%h pressure=%0d result=%h rival_successes=%0d fetches=%0d demands=%0d", rv32, predicted, word_access, translated, loop_pc, pressure, result, rival_successes, fetches, demands);
          passed++;
          $display("Constrained LR/SC passed: RV32=%0b predictor=%0b word=%0d Sv39=%0d pc=%h pressure=%0d rival_successes=%0d", rv32, predicted, word_access, translated, loop_pc, pressure, rival_successes);
        end
      end
    end
    end
    $display("LR/SC qualification: %0d passed, %0d failed", passed, failures);
    assert (failures == 0) else $fatal(1, "%0d constrained LR/SC cases failed", failures);
    $finish;
  end
  initial begin
    #500000000;
    $fatal(1, "full-core LR/SC watchdog expired");
  end
endmodule
