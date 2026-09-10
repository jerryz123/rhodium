// Checks bounded PAUSE throttling, retirement, squash, interrupt exit, and older-load independence.
module rv5stage_pause_tb;
  typedef struct packed {
    logic supervisor_software;
    logic machine_software;
    logic supervisor_timer;
    logic machine_timer;
    logic supervisor_external;
    logic machine_external;
  } interrupts_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic [63:0] address; } instruction_req_bits_t;
  typedef struct packed { logic valid; instruction_req_bits_t bits; } instruction_req_t;
  typedef struct packed { logic [31:0] word; logic page_fault; logic access_fault; } instruction_resp_bits_t;
  typedef struct packed { logic valid; instruction_resp_bits_t bits; } instruction_resp_t;
  typedef struct packed { ready_t request; instruction_resp_t response; } instruction_in_t;
  typedef struct packed {
    logic flush;
    logic invalidate_all;
    instruction_req_t request;
    ready_t response;
  } instruction_out_t;
  typedef struct packed {
    logic [63:0] address;
    logic [3:0] access;
    logic [3:0] atomic;
    logic [1:0] width;
    logic unsigned_0;
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
    logic [2:0] locality;
  } data_req_bits_t;
  typedef struct packed { logic valid; data_req_bits_t bits; } data_req_t;
  typedef struct packed {
    logic access_fault;
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
  } data_resp_bits_t;
  typedef struct packed { logic valid; data_resp_bits_t bits; } data_resp_t;
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; data_resp_t response; logic drained; logic reservation_valid; } data_in_t;

  typedef struct packed { data_req_t request; } data_out_t;
  logic clock = 0, reset = 1;
  interrupts_t interrupts;
  instruction_in_t instruction_access_in;
  instruction_out_t instruction_access_out;
  data_in_t data_access_in;
  data_out_t data_access_out;
  logic fault, translation_flush;
  logic [1:0] privilege;
  logic [63:0] mstatus, satp, hart_id = 0, time_counter = 0;
  logic response_valid = 0;
  logic [31:0] response_word;
  integer scenario, cycles, stores, baseline_cycles, single_cycles, load_age;
  logic done, load_pending;
  RV5StageCoreFixture dut (.pipeline_access_in('0), .pipeline_access_out(), .prefetch_out(), .*);
  always #5 clock = ~clock;

  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    if (scenario == 7) begin
      case (address)
        0: instruction_at = 32'h002000b7; // mstatus.TW must not restrict PAUSE
        4: instruction_at = 32'h30009073;
        8: instruction_at = 32'h02000093;
        12: instruction_at = 32'h34109073; // mepc = 32, MPP = U
        16: instruction_at = 32'h30200073;
        32: instruction_at = 32'h0100000f;
        36: instruction_at = 32'h00000013;
        40: instruction_at = 32'h00300313;
        44: instruction_at = 32'h00000013;
        48: instruction_at = 32'h00603023;
        default: instruction_at = 32'h0000006f;
      endcase
      return instruction_at;
    end
    case (address)
      0: instruction_at = 32'h10000093; // mtvec = 0x100
      4: instruction_at = 32'h30509073;
      8: instruction_at = 32'h08000093; // locally enable MTIP
      12: instruction_at = 32'h30409073;
      16: instruction_at = scenario == 3 ? 32'h00800093 : 32'h00000093;
      20: instruction_at = 32'h30009073;
      24: instruction_at = (scenario == 6 || scenario == 8) ? 32'h20003a03 : 32'h00000013; // delayed ld x20, 512(x0)
      28: instruction_at = (scenario == 6 || scenario == 8) ? 32'h00000013 : 32'hc02022f3; // sample instret without draining the delayed load
      32: instruction_at = scenario == 0 ? 32'h00000013 :
                           scenario == 4 ? 32'h0080006f :
                           scenario == 5 ? 32'hffffffff : 32'h0100000f;
      36: instruction_at = (scenario == 2 || scenario == 4 || scenario == 5) ? 32'h0100000f : 32'h00000013;
      40: instruction_at = scenario == 8 ? 32'h000a0313 : scenario == 6 ? 32'h00300313 : 32'hc0202373;
      44: instruction_at = (scenario == 6 || scenario == 8) ? 32'h00000013 : 32'h40530333; // retirement delta
      48: instruction_at = 32'h00603023;
      52: instruction_at = 32'h0000006f;
      256: instruction_at = 32'hb0202373; // handler retirement delta
      260: instruction_at = 32'h40530333;
      264: instruction_at = 32'h00603423;
      268: instruction_at = 32'h34202373;
      272: instruction_at = 32'h00603823;
      276: instruction_at = 32'h34102373;
      280: instruction_at = 32'h00603c23;
      284: instruction_at = 32'h0000006f;
      default: instruction_at = 32'h00000013;
    endcase
  endfunction

  always_comb begin
    instruction_access_in = '0;
    instruction_access_in.request.ready = !response_valid;
    instruction_access_in.response.valid = response_valid;
    instruction_access_in.response.bits.word = response_word;
    data_access_in = '0;
    data_access_in.request.ready = 1;
    data_access_in.drained = !load_pending;
    data_access_in.response.valid = load_pending && load_age == (scenario == 8 ? 12 : 120);
    data_access_in.response.bits.rd = 20;
    data_access_in.response.bits.destination = 1; // integer register completion
    data_access_in.response.bits.data = 64'h1234;
  end

  always @(posedge clock) begin
    if (reset) begin
      response_valid <= 0;
      cycles <= 0;
      stores <= 0;
      done <= 0;
      load_pending <= 0;
      load_age <= 0;
    end else begin
      cycles <= cycles + 1;
      if (instruction_access_out.flush) response_valid <= 0;
      else begin
        if (response_valid && instruction_access_out.response.ready) response_valid <= 0;
        if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
          response_valid <= 1;
          response_word <= instruction_at(instruction_access_out.request.bits.address);
        end
      end
      if (load_pending) begin
        load_age <= load_age + 1;
        if (data_access_in.response.valid) load_pending <= 0;
      end
      if (data_access_out.request.valid) begin
        if (data_access_out.request.bits.access == 1) begin
          assert ((scenario == 6 || scenario == 8) && data_access_out.request.bits.address == 512 && !load_pending)
            else $fatal(1, "unexpected load");
          load_pending <= 1;
          load_age <= 0;
        end else begin
          assert (data_access_out.request.bits.access == 2) else $fatal(1, "unexpected memory operation");
          stores <= stores + 1;
          case (data_access_out.request.bits.address)
            0: begin
              assert (scenario != 3 && scenario != 5) else $fatal(1, "younger store escaped trap");
              assert (data_access_out.request.bits.data == (scenario == 8 ? 64'h1234 : scenario == 4 ? 2 : 3))
                else $fatal(1, "PAUSE retirement count");
              if (scenario == 0) baseline_cycles = cycles;
              if (scenario == 1) begin
                single_cycles = cycles;
                assert (cycles >= baseline_cycles + 16 && cycles <= baseline_cycles + 28)
                  else $fatal(1, "single PAUSE duration: %0d baseline %0d", cycles, baseline_cycles);
              end
              if (scenario == 2)
                assert (cycles >= single_cycles + 16 && cycles <= single_cycles + 28)
                  else $fatal(1, "consecutive PAUSE duration");
              if (scenario == 4)
                assert (cycles < baseline_cycles + 16) else $fatal(1, "squashed PAUSE throttled");
              if (scenario == 6)
                assert (load_pending && load_age < 120) else $fatal(1, "PAUSE drained older load");
              if (scenario == 7)
                assert (privilege == 0 && mstatus[21]) else $fatal(1, "U-mode/TW PAUSE");
              done <= 1;
            end
            8: assert (data_access_out.request.bits.data == (scenario == 5 ? 1 : 2))
                 else $fatal(1, "trap retirement delta");
            16: assert (data_access_out.request.bits.data == (scenario == 5 ? 64'd2 : 64'h8000000000000007))
                  else $fatal(1, "trap cause");
            24: begin
              assert (data_access_out.request.bits.data == (scenario == 5 ? 32 : 36)) else $fatal(1, "trap EPC");
              assert (stores == 2) else $fatal(1, "handler store count");
              done <= 1;
            end
            default: $fatal(1, "unexpected signature address");
          endcase
        end
      end
      assert (!fault && cycles < 500) else $fatal(1, "scenario %0d hung/faulted", scenario);
    end
  end

  initial begin
    interrupts = '0;
    for (scenario = 0; scenario < 9; scenario++) begin
      reset = 1;
      interrupts = '0;
      repeat (3) @(negedge clock);
      reset = 0;
      if (scenario == 3) begin
        wait (cycles == baseline_cycles + 1);
        @(negedge clock);
        interrupts.machine_timer = 1;
      end
      wait (done);
      @(negedge clock);
      $display("PAUSE scenario %0d passed (%0d cycles)", scenario, cycles);
    end
    $finish;
  end
endmodule
