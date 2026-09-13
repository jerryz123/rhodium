// Verifies WB-owned WRS waiting, timeout, interrupt wake, and precise retirement.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_zawrs_tb;
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
  logic reservation;
  integer scenario, cycles, wait_cycles, stores;
  logic saw_wrs, done;
  RV5StageCoreFixture dut (.pipeline_access_in('0), .pipeline_access_out(), .prefetch_out(), .*);
  always #5 clock = ~clock;

  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      0: instruction_at = 32'h10000093; // mtvec = 0x100
      4: instruction_at = 32'h30509073;
      8: instruction_at = 32'h08000093; // locally enable MTIP
      12: instruction_at = 32'h30409073;
      16: instruction_at = 32'h00700093; // allow U-mode counter reads
      20: instruction_at = 32'h30609073;
      24: instruction_at = 32'h10609073;
      28: instruction_at = (scenario == 4 || scenario == 5 || scenario == 7) ? 32'h002000b7 : 32'h00000093; // TW
      32: instruction_at = scenario == 8 ? 32'h00800093 : 32'h00008093;
      36: instruction_at = 32'h30009073; // mstatus
      40: instruction_at = 32'h03c00093; // mepc = 60
      44: instruction_at = 32'h34109073;
      48: instruction_at = (scenario >= 4 && scenario <= 6) ? 32'h30200073 : 32'h00000013;
      52: instruction_at = 32'h00000013;
      56: instruction_at = scenario == 10 ? 32'hc02022f3 : 32'h00000013;
      60: instruction_at = scenario == 10 ? 32'h0080006f : 32'hc02022f3; // skip speculative WRS or read instret
      64: instruction_at = (scenario == 3 || scenario == 5) ? 32'h01d00073 : 32'h00d00073;
      68: instruction_at = 32'hc0202373; // csrr x6, instret
      72: instruction_at = 32'h40530333; // sub x6, x6, x5
      76: instruction_at = 32'h00603023; // sd x6, 0(x0)
      80: instruction_at = 32'h0000006f;
      256: instruction_at = 32'hb0202373; // handler samples minstret first
      260: instruction_at = 32'h40530333;
      264: instruction_at = 32'h00603423; // retirement delta at 8
      268: instruction_at = 32'h34202373;
      272: instruction_at = 32'h00603823; // mcause at 16
      276: instruction_at = 32'h34102373;
      280: instruction_at = 32'h00603c23; // mepc at 24
      284: instruction_at = 32'h34302373;
      288: instruction_at = 32'h02603023; // mtval at 32
      292: instruction_at = 32'h0000006f;
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
    data_access_in.drained = 1;
    data_access_in.reservation_valid = reservation;
  end

  always @(posedge clock) begin
    if (reset) begin
      response_valid <= 0;
      cycles <= 0;
      wait_cycles <= 0;
      saw_wrs <= 0;
      stores <= 0;
      done <= 0;
    end else begin
      cycles <= cycles + 1;
      if (instruction_access_out.flush)
        response_valid <= 0;
      else begin
        if (response_valid && instruction_access_out.response.ready) response_valid <= 0;
        if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
          response_valid <= 1;
          response_word <= instruction_at(instruction_access_out.request.bits.address);
          if (instruction_access_out.request.bits.address == 64) saw_wrs <= 1;
        end
      end
      if (saw_wrs) wait_cycles <= wait_cycles + 1;
      if (data_access_out.request.valid) begin
        assert (data_access_out.request.bits.access == 2) else $fatal(1, "unexpected request");
        stores <= stores + 1;
        case (data_access_out.request.bits.address)
          0: begin
            assert (scenario != 4 && scenario != 8) else $fatal(1, "trap did not block younger store");
            assert (data_access_out.request.bits.data == 2) else $fatal(1, "WRS retirement count");
            if (scenario == 3 || scenario == 5)
              assert (wait_cycles >= 4096 && wait_cycles < 4200) else $fatal(1, "STO duration");
            done <= 1;
          end
          8: assert (data_access_out.request.bits.data == (scenario == 4 ? 1 : 2)) else $fatal(1, "fault/interrupt retirement count");
          16: assert (data_access_out.request.bits.data == (scenario == 4 ? 64'd2 : 64'h8000000000000007)) else $fatal(1, "cause");
          24: assert (data_access_out.request.bits.data == (scenario == 4 ? 64'd64 : 64'd68)) else $fatal(1, "trap PC");
          32: begin
            assert (scenario == 4 || scenario == 8) else $fatal(1, "unexpected trap");
            assert (data_access_out.request.bits.data == (scenario == 4 ? 64'h00d00073 : 0)) else $fatal(1, "trap value");
            assert (stores == 3) else $fatal(1, "handler store count");
            done <= 1;
          end
          default: $fatal(1, "unexpected store");
        endcase
      end
      assert (!fault && cycles < 5000) else $fatal(1, "scenario %0d hung/faulted", scenario);
    end
  end

  initial begin
    interrupts = '0;
    reservation = 0;
    for (scenario = 0; scenario < 11; scenario++) begin
      reset = 1;
      reservation = scenario != 0;
      interrupts = '0;
      if (scenario == 2) interrupts.machine_timer = 1;
      repeat (3) @(negedge clock);
      reset = 0;
      wait (saw_wrs || done);
      @(negedge clock);
      if (scenario == 9) reservation = 0; // invalidate before WB entry
      if (scenario == 1 || scenario == 6 || scenario == 7 || scenario == 8) begin
        repeat (scenario == 7 ? 4300 : 80) @(negedge clock);
        assert (!done && stores == 0) else $fatal(1, "WRS completed before wake");
        if (scenario == 8) interrupts.machine_timer = 1;
        else reservation = 0;
      end
      wait (done);
      @(negedge clock);
      $display("Zawrs scenario %0d passed", scenario);
    end
    $finish;
  end
endmodule
