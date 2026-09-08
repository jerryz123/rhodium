// Compares public RV5Stage timing for identical programs and distinct operand data.
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic [W-1:0] address; } iqbits_t;
  typedef struct packed { logic valid; iqbits_t bits; } iq_t;
  typedef struct packed { logic [31:0] word; logic page_fault; logic access_fault; } irbits_t;
  typedef struct packed { logic valid; irbits_t bits; } ir_t;
  typedef struct packed { ready_t request; ir_t response; } ii_t;
  typedef struct packed { logic flush; logic invalidate_all; iq_t request; ready_t response; } io_t;
  typedef struct packed {
    logic [W-1:0] address;
    logic [3:0] access;
    logic [3:0] atomic;
    logic [1:0] width;
    logic unsigned_0;
    logic [W-1:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
    logic [2:0] locality;
  } dqbits_t;
  typedef struct packed { logic valid; dqbits_t bits; } dq_t;
  typedef struct packed { logic access_fault; logic [W-1:0] data; logic [1:0] destination; logic [4:0] rd; logic [1:0] floating_point_precision; } drbits_t;
  typedef struct packed { logic valid; drbits_t bits; } dr_t;
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; dr_t response; logic drained; logic reservation_valid; } di_t;
  typedef struct packed { dq_t request; } do_t;
  logic clock = 0;
  logic reset = 1;
  logic run_core = 0;
  logic multiply_issue = 0, load_valid = 0, completion_ready = 0;
  logic [3:0] multiply_control = 0;
  logic [W-1:0] multiply_left[2], multiply_right[2], load_value[2], completion_value[2];
  logic multiply_available[2], multiply_valid[2], completion_valid[2];
  logic [1:0] completion_chosen[2];
  int contended_cycles = 0;
  ii_t ii[2];
  io_t io[2];
  di_t di[2];
  do_t dout[2];
  logic [1:0] privilege[2];
  logic [W-1:0] mstatus[2], satp[2];
  logic translation_flush[2];
  logic [31:0] program_word[2], probe_count[2];
  logic instruction_pending;
  logic [31:0] instruction_word;
  int response_delay;
  logic [4:0] response_rd;
  logic response_second;
  int cycle, stores, trial, schedule, total_probes = 0, differing_results = 0;
  longint unsigned total_cycles = 0;
  logic [W-1:0] operand_a[2], operand_b[2];
  for (genvar g=0; g<2; g++) begin: pair
    RV5StageZkt dut(.clock, .reset, .program_word(program_word[g]), .probe_count(probe_count[g]),
      .multiply_issue, .multiply_left(multiply_left[g]), .multiply_right(multiply_right[g]), .multiply_control,
      .load_valid, .load_value(load_value[g]), .completion_ready,
      .multiply_available(multiply_available[g]), .multiply_valid(multiply_valid[g]),
      .completion_valid(completion_valid[g]), .completion_value(completion_value[g]), .completion_chosen(completion_chosen[g]),
      .instruction_access_in(ii[g]), .instruction_access_out(io[g]),
      .data_access_in(di[g]), .data_access_out(dout[g]),
      .privilege(privilege[g]), .mstatus(mstatus[g]), .satp(satp[g]), .translation_flush(translation_flush[g]));
  end
  always #5 clock = ~clock;

  function automatic logic [W-1:0] varied_operand(input int index);
    case (index)
      0: return '0;
      1: return W'(1);
      2: return '1;
      3: return W'(1) << (W-1);
      4: return (W'(1) << (W-1)) - W'(1);
      5: return W'(64'haaaaaaaaaaaaaaaa);
      6: return W'(64'h5555555555555555);
      7: return W'(32'h80000000);
      8: return W'(32'h7fffffff);
      9: return W'(32'hffffffff);
      default: begin
        if (index < 10 + W) return W'(1) << (index - 10);
        return W'(64'h9e3779b97f4a7c15 * (64'(index) + 64'd1)) ^ W'(64'hd1b54a32d192ed03);
      end
    endcase
  endfunction

  always_comb begin
    for (int g=0; g<2; g++) begin
      ii[g] = '0;
      ii[g].request.ready = run_core && !instruction_pending && (schedule == 0 || cycle % (schedule == 1 ? 5 : 3) != 0);
      ii[g].response.valid = instruction_pending;
      ii[g].response.bits.word = instruction_word;
      di[g] = '0;
      di[g].request.ready = response_delay == 0 && (schedule == 0 || cycle % (schedule == 1 ? 7 : 5) > 1);
      di[g].drained = response_delay == 0;
      di[g].response.valid = response_delay == 1;
      di[g].response.bits.destination = 1;
      di[g].response.bits.rd = response_rd;
      di[g].response.bits.data = response_second ? operand_b[g] : operand_a[g];
    end
  end

  // Sample pre-edge interface events. Both environments use exactly the same
  // delays and addresses; only the data returned by operand loads differs.
  always @(posedge clock) begin
    if (reset) begin
      instruction_pending <= 0;
      instruction_word <= 32'h13;
      response_delay <= 0;
      response_rd <= 0;
      response_second <= 0;
      cycle <= 0;
      stores <= 0;
    end else begin
      cycle <= cycle + 1;
      assert ({io[0].request.valid, io[0].response.ready, io[0].flush, io[0].invalidate_all} ===
              {io[1].request.valid, io[1].response.ready, io[1].flush, io[1].invalidate_all})
        else $fatal(1, "fetch timing diverged: W=%0d trial=%0d schedule=%0d cycle=%0d probe=%0d", W, trial, schedule, cycle, stores);
      if (io[0].request.valid) begin
        assert (io[0].request.bits.address === io[1].request.bits.address)
          else $fatal(1, "fetch address diverged");
        assert (io[0].request.bits.address >= 'h10000 && io[0].request.bits.address < 'h20000)
          else $fatal(1, "unexpected trap/fetch address %h, probe=%0d", io[0].request.bits.address, stores);
      end
      assert (dout[0].request.valid === dout[1].request.valid)
        else $fatal(1, "data/completion timing diverged: W=%0d trial=%0d schedule=%0d cycle=%0d probe=%0d", W, trial, schedule, cycle, stores);
      assert ({privilege[0],mstatus[0],satp[0],translation_flush[0]} === {privilege[1],mstatus[1],satp[1],translation_flush[1]})
        else $fatal(1, "architectural control state diverged");
      if (dout[0].request.valid) begin
        assert (dout[0].request.bits.address === dout[1].request.bits.address &&
                dout[0].request.bits.access === dout[1].request.bits.access &&
                dout[0].request.bits.width === dout[1].request.bits.width &&
                dout[0].request.bits.destination === dout[1].request.bits.destination &&
                dout[0].request.bits.rd === dout[1].request.bits.rd)
          else $fatal(1, "data request control diverged");
      end
      if (instruction_pending && io[0].response.ready) instruction_pending <= 0;
      if (io[0].request.valid && ii[0].request.ready) begin
        instruction_pending <= 1;
        instruction_word <= program_word[0];
      end
      if (io[0].flush) instruction_pending <= 0;
      if (response_delay > 0) response_delay <= response_delay - 1;
      if (dout[0].request.valid && di[0].request.ready) begin
        case (dout[0].request.bits.access)
          1: begin
            assert (dout[0].request.bits.address == 0 || dout[0].request.bits.address == 8)
              else $fatal(1, "unexpected operand load address");
            response_delay <= schedule == 0 ? 2 : (3 + cycle % (schedule == 1 ? 5 : 11));
            response_rd <= dout[0].request.bits.rd;
            response_second <= dout[0].request.bits.address == 8;
          end
          2: begin
            assert (dout[0].request.bits.address == 16) else $fatal(1, "unexpected result store address");
            stores <= stores + 1;
            total_probes <= total_probes + 1;
            if (dout[0].request.bits.data != dout[1].request.bits.data) differing_results <= differing_results + 1;
          end
          default: $fatal(1, "unexpected memory operation");
        endcase
      end
    end
  end

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  // The same public multiplier adapter and priority topology as core WB.
  // Force valid overlap and sink stalls; check latency, priority, held data,
  // and exactly-once consumption without naming any generated internal wire.
  task automatic check_completion_contention;
    logic [2*W-1:0] left_extended, right_extended, product;
    logic [W-1:0] expected[2];
    repeat (3) tick();
    @(negedge clock);
    reset = 0;
    for (int mode=0; mode<(W == 64 ? 5 : 4); mode++) begin
      for (int sample=0; sample<96; sample++) begin
        @(negedge clock);
        // low, signed high, signed/unsigned high, unsigned high, word low
        case (mode)
          0: multiply_control = 4'b0000;
          1: multiply_control = 4'b1110;
          2: multiply_control = 4'b1010;
          3: multiply_control = 4'b0010;
          4: multiply_control = 4'b0001;
        endcase
        multiply_left[0] = 0;
        multiply_right[0] = 0;
        multiply_left[1] = varied_operand(sample);
        multiply_right[1] = varied_operand((sample * 17 + mode) % 96);
        for (int g=0; g<2; g++) begin
          left_extended = {{W{multiply_control[3] && multiply_left[g][W-1]}}, multiply_left[g]};
          right_extended = {{W{multiply_control[2] && multiply_right[g][W-1]}}, multiply_right[g]};
          product = left_extended * right_extended;
          expected[g] = multiply_control[1] ? product[W+:W] : product[0+:W];
          if (multiply_control[0]) expected[g] = W'($signed(product[31:0]));
          load_value[g] = varied_operand(sample + g);
          assert (multiply_available[g]) else $fatal(1, "multiplier did not release its previous result");
        end
        multiply_issue = 1;
        tick();
        @(negedge clock);
        multiply_issue = 0;
        load_valid = 1;
        completion_ready = 0;
        repeat (W+1) begin
          assert (!multiply_valid[0] && !multiply_valid[1]) else $fatal(1, "operand-dependent/early multiply completion");
          tick();
        end
        repeat (3) begin
          for (int g=0; g<2; g++) begin
            assert (multiply_valid[g] && !multiply_available[g] && completion_valid[g] && completion_chosen[g] == 0 && completion_value[g] == load_value[g])
              else $fatal(1, "stalled load/multiply completion priority or stability failed");
          end
          contended_cycles++;
          tick();
        end
        @(negedge clock);
        completion_ready = 1;
        repeat (4) begin
          tick();
          assert (multiply_valid[0] && multiply_valid[1] && completion_chosen[0] == 0 && completion_chosen[1] == 0)
            else $fatal(1, "higher-priority load did not retain multiply completion");
          contended_cycles++;
        end
        @(negedge clock);
        load_valid = 0;
        #1;
        for (int g=0; g<2; g++) begin
          assert (completion_valid[g] && completion_chosen[g] == 1 && completion_value[g] == expected[g])
            else $fatal(1, "retained multiply result mismatch: mode=%0d sample=%0d", mode, sample);
        end
        tick();
        assert (!multiply_valid[0] && !multiply_valid[1] && !completion_valid[0] && !completion_valid[1])
          else $fatal(1, "multiply completion consumed more than once");
      end
    end
    $display("Zkt RV%0d completion contention PASS: %0d forced overlap cycles", W, contended_cycles);
  endtask

  initial begin
    check_completion_contention();
    run_core = 1;
    for (schedule=0; schedule<3; schedule++) begin
      for (trial=0; trial<96; trial++) begin
        @(negedge clock);
        reset = 1;
        operand_a[0] = 0;
        operand_b[0] = 0;
        operand_a[1] = varied_operand(trial);
        operand_b[1] = varied_operand((trial * 17 + schedule) % 96);
        repeat (3) @(negedge clock);
        reset = 0;
        while (stores < int'(probe_count[0]) && cycle < 100000) @(negedge clock);
        assert (stores == int'(probe_count[0])) else $fatal(1, "timeout W=%0d trial=%0d schedule=%0d stores=%0d", W, trial, schedule, stores);
        total_cycles += 64'(cycle);
      end
    end
    assert (differing_results > 0) else $fatal(1, "vacuous comparison: operands did not change any result");
    $display("Zkt timing regression RV%0d PASS: %0d forms, 96 operand pairs x 3 schedules, %0d compared probes, %0d cycles, %0d differing results", W, int'(probe_count[0]), total_probes, total_cycles, differing_results);
    $finish;
  end
