// Executes vector configuration and packed integer macros through the real core, observing only public ports.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_config_tb;
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
  logic reject_store = 1;
  logic [31:0] response_word;

  integer cycles = 0, stores = 0, vector_writes = 0, rejected_stores = 0;
  RV5StageCoreFixture dut (.pipeline_access_in('0), .pipeline_access_out(), .prefetch_out(), .*);
  always #5 clock = ~clock;
  // Configure VS, exercise all three vset forms, consume scalar results, and
  // squash a younger configuration before trapping on masked ADD into v0.
  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    case (address)
      0: return 32'h10000093;
      4: return 32'h30509073;
      8: return 32'h20000093;
      12: return 32'h3000a073;
      16: return 32'h00300093;
      20: return 32'h0100f157;
      24: return 32'h00110193;
      28: return 32'h00303023;
      32: return 32'h01900213;
      36: return 32'h80407057;
      40: return 32'hc20021f3;
      44: return 32'h00303423;
      48: return 32'hc002f157;
      52: return 32'h00203823;
      56: return 32'h0080006f;
      60: return 32'hc00ff157;
      64: return 32'hc20021f3;
      68: return 32'h00303c23;
      72: return 32'h008021f3;
      76: return 32'h02303023;
      80: return 32'hc0087157; // vsetivli x2,16,e8,m1
      84: return 32'h20000093; // VS=Initial
      88: return 32'h30009073; // csrw mstatus,x1
      92: return 32'h2e840457; // vxor.vv v8,v8,v8
      96: return 32'h00700293; // dependent VX scalar
      100: return 32'h0282c457; // vadd.vx v8,v8,x5
      104: return 32'h66840057; // vmsne.vv v0,v8,v8
      108: return 32'h66803057; // vmsne.vi v0,v8,0
      112: return 32'h008fb457; // masked vadd.vi v8,v8,-1
      116: return 32'h0081d073; // vstart=3
      120: return 32'h0280b457; // vadd.vi v8,v8,1
      124: return 32'hb0202373; // read minstret before one macro
      128: return 32'h0280b457; // one macro, two beats
      132: return 32'hb02023f3; // read minstret after macro
      136: return 32'h406383b3; // sub x7,x7,x6
      140: return 32'h02703423; // macro retirement delta
      144: return 32'h008021f3; // vstart after nonempty macro
      148: return 32'h02303823; // vstart=0
      152: return 32'h300021f3; // mstatus after integer vector writes
      156: return 32'h02303c23; // VS=Dirty
      160: return 32'h00000093; // AVL=0
      164: return 32'h0000f057; // vsetvli x0,x1,e8,m1
      168: return 32'h0083d073; // vstart=7
      172: return 32'h0282c457; // empty macro, no VRF writes
      176: return 32'h008021f3; // vstart after empty macro
      180: return 32'h04303023; // vstart=0
      184: return 32'h0080006f; // squash younger vector instruction
      188: return 32'h0287b457; // must not write
      192: return 32'h00307157; // vsetvli x2,x0,e8,m8: 128 elements
      196: return 32'h2e840457; // sixteen packed beats through all private stages
      200: return 32'h0280b457; // in-place vadd.vi v8,v8,1
      204: return 32'h00218057; // illegal masked destination v0
      256: return 32'h342021f3; // mcause
      260: return 32'h04303423; // illegal-instruction cause
      264: return 32'h341021f3; // mepc
      268: return 32'h04303823; // precise fault PC
      default: return 32'h0000006f;
    endcase
  endfunction
  always_comb begin
    instruction_access_in = '0;
    instruction_access_in.request.ready = instruction_access_out.flush || !response_valid;
    instruction_access_in.response.valid = response_valid;
    instruction_access_in.response.bits.word = response_word;
    data_access_in = '0;
    // Reject each store once, then retain readiness until its retry transfers.
    // Periodic readiness can phase-lock against the fixed replay latency.
    data_access_in.request.ready = !reject_store;
    data_access_in.drained = 1;
  end
  always @(posedge clock) begin
    if (reset) begin
      response_valid <= 0;
      cycles <= 0;
      stores <= 0;
      reject_store <= 1;
      rejected_stores <= 0;
    end else begin
      cycles <= cycles + 1;
      if (data_access_out.request.valid) begin
        reject_store <= data_access_in.request.ready;
        if (!data_access_in.request.ready) rejected_stores <= rejected_stores + 1;
      end
      if (instruction_access_out.flush) response_valid <= 0;
      else if (response_valid && instruction_access_out.response.ready) response_valid <= 0;
      // A transferred restart replaces the old response in the flush cycle.
      if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
        response_valid <= 1;
        response_word <= instruction_at(instruction_access_out.request.bits.address);
      end
      if (data_access_out.request.valid && data_access_in.request.ready) begin
        assert (data_access_out.request.bits.access == 2 && data_access_out.request.bits.address == 64'(stores) * 8)
          else $fatal(1, "unexpected vector-program memory request, store=%0d address=%0d", stores, data_access_out.request.bits.address);
        case (stores)
          0: assert (data_access_out.request.bits.data == 4) else $fatal(1, "vset result forwarding");
          1: assert (data_access_out.request.bits.data == 3) else $fatal(1, "vsetvl preserve");
          2,3: assert (data_access_out.request.bits.data == 5) else $fatal(1, "vsetivli or squash");
          4: assert (data_access_out.request.bits.data == 0) else $fatal(1, "vstart");
          5: assert (data_access_out.request.bits.data == 2) else $fatal(1, "vector beats overcounted minstret");
          6,8: assert (data_access_out.request.bits.data == 0) else $fatal(1, "arithmetic did not clear vstart");
          7: assert (data_access_out.request.bits.data[10:9] == 3 && data_access_out.request.bits.data[63]) else $fatal(1, "vector writes did not dirty VS");
          9: assert (data_access_out.request.bits.data == 2) else $fatal(1, "illegal vector group must trap");
          10: begin
            assert (data_access_out.request.bits.data == 204) else $fatal(1, "precise vector trap");
            assert (vector_writes == 46) else $fatal(1, "lost, duplicated, or squashed vector writes: %0d", vector_writes);
            assert (rejected_stores == 11) else $fatal(1, "each signature store must exercise exactly one replay");
            $display("rv5stage vector configuration and integer pipeline passed");
            $finish;
          end
        endcase
        stores <= stores + 1;
      end
      if (cycles > 5000) $fatal(1, "vector config pipeline timeout, stores=%0d", stores);
    end
  end
  initial begin
    interrupts = '0;
    repeat (4) @(posedge clock);
    @(negedge clock); reset = 0;
  end
endmodule

// Bind an observer to the reusable VRF's public write port, never its storage.
module vector_core_write_observer(input logic clock, reset, input logic [134:0] write_in);
  wire [5:0] address = write_in[133:128];
  wire [63:0] data = write_in[127:64], mask = write_in[63:0];
  integer n;
  time previous_write;
  logic [63:0] expected_data, expected_mask;
  integer expected_address;
  always @(posedge clock) begin
    if (!reset && write_in[134]) begin
      n = rv5stage_vector_config_tb.vector_writes;
      expected_mask = '1;
      expected_address = 16 + (n % 2);
      case (n)
        0,1: expected_data = 0;
        2,3: expected_data = 64'h0707070707070707;
        4,5,6,7: begin
          expected_address = 0;
          expected_mask = n % 2 == 0 ? 64'hff : 64'hff00;
          expected_data = n < 6 ? 0 : expected_mask;
        end
        8,9: expected_data = 64'h0606060606060606;
        10: begin expected_data = 64'h0707070707000000; expected_mask = 64'hffffffffff000000; end
        11: expected_data = 64'h0707070707070707;
        12: expected_data = 64'h0808080808070707;
        13: expected_data = 64'h0808080808080808;
        default: begin
          assert (n < 46) else $fatal(1, "unexpected architectural vector write");
          expected_address = 16 + ((n - 14) % 16);
          expected_data = n < 30 ? 64'b0 : 64'h0101010101010101;
          if (n != 14 && n != 30)
            assert ($time - previous_write == 10) else $fatal(1, "bubble in packed vector WB stream");
        end
      endcase
      assert (int'(address) == expected_address && mask == expected_mask && (data & mask) == expected_data)
        else $fatal(1, "core vector write %0d row %0d data %h mask %h", n, address, data, mask);
      rv5stage_vector_config_tb.vector_writes++;
      previous_write = $time;
    end
  end
endmodule
bind RV5StageVectorRegisterFile vector_core_write_observer vector_observer(.clock(clock), .reset(reset), .write_in(write_in));
