// Runs tagged load/store/prefetch traffic through PMM changes, replay, and precise fault reporting.
module rv5stage_pointer_masking_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; logic [63:0] address; } instruction_req_t;
  typedef struct packed { logic valid; logic [31:0] word; logic page_fault; logic access_fault; } instruction_resp_t;
  typedef struct packed { ready_t request; instruction_resp_t response; } instruction_in_t;
  typedef struct packed { logic flush; logic invalidate_all; instruction_req_t request; ready_t response; } instruction_out_t;
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
  typedef struct packed { logic access_fault; logic [63:0] data; logic [1:0] destination; logic [4:0] rd; logic [1:0] floating_point_precision; } data_resp_bits_t;
  typedef struct packed { logic valid; data_resp_bits_t bits; } data_resp_t;
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; data_resp_t response; logic drained; logic reservation_valid; } data_in_t;
  typedef struct packed { data_req_t request; } data_out_t;
  typedef struct packed { logic valid; logic [63:0] address; logic [1:0] operation; } prefetch_t;
  logic clock = 0, reset = 1;
  logic [63:0] time_counter = 0, hart_id = 0;
  logic [5:0] interrupts = 0;
  logic [1:0] privilege;
  logic [63:0] mstatus, satp;
  logic translation_flush;
  instruction_in_t instruction_access_in;
  instruction_out_t instruction_access_out;
  data_in_t data_access_in;
  data_out_t data_access_out;
  prefetch_t prefetch_out;
  instruction_resp_t instruction_response;
  data_resp_t data_response;
  int stores, loads, prefetches, flushes;
  bit rejected;
  localparam logic [63:0] LOAD_VALUE = 64'h123456789abcdef0;

  RV5StageCoreFixture dut (.pipeline_access_in('0), .pipeline_access_out(), .*);
  always #5 clock = ~clock;

  function automatic logic [31:0] addi(input int rd, rs1, imm);
    return {12'(imm), 5'(rs1), 3'b000, 5'(rd), 7'h13};
  endfunction
  function automatic logic [31:0] slli(input int rd, rs1, amount);
    return {6'b0, 6'(amount), 5'(rs1), 3'b001, 5'(rd), 7'h13};
  endfunction
  function automatic logic [31:0] csrw(input int csr, rs1);
    return {12'(csr), 5'(rs1), 3'b001, 5'b0, 7'h73};
  endfunction
  function automatic logic [31:0] csrr(input int rd, csr);
    return {12'(csr), 5'b0, 3'b010, 5'(rd), 7'h73};
  endfunction
  function automatic logic [31:0] sd(input int rs2, rs1, imm);
    return {7'(imm >> 5), 5'(rs2), 5'(rs1), 3'b011, 5'(imm), 7'h23};
  endfunction
  function automatic logic [31:0] ld(input int rd, rs1, imm);
    return {12'(imm), 5'(rs1), 3'b011, 5'(rd), 7'h03};
  endfunction
  function automatic logic [31:0] instruction_at(input logic [63:0] pc);
    case (pc)
      'h1000: return addi(1, 0, 2);
      'h1004: return slli(1, 1, 32);
      'h1008: return csrw('h10a, 1); // PMM7, then refetch with the new policy.
      'h100c: return addi(1, 0, 1);
      'h1010: return slli(1, 1, 17);
      'h1014: return csrw('h300, 1); // MPRV, MPP=U, Bare.
      'h1018: return addi(2, 0, -1);
      'h101c: return slli(2, 2, 57);
      'h1020: return addi(2, 2, 'h100);
      'h1024: return sd(2, 2, 0); // Address untagged, stored register value tagged.
      'h1028: return ld(3, 2, 8); // Rejected once, then replayed.
      'h102c: return sd(3, 2, 16);
      'h1030: return addi(1, 0, 3);
      'h1034: return slli(1, 1, 32);
      'h1038: return csrw('h10a, 1); // PMM16 must apply to the next memory op.
      'h103c: return addi(2, 0, -1);
      'h1040: return slli(2, 2, 48);
      'h1044: return addi(2, 2, 'h180);
      'h1048: return sd(2, 2, 0);
      'h104c: return 32'h00016013; // prefetch.i 0(x2): explicit access, not fetch.
      'h1050: return 32'h00116013; // prefetch.r 0(x2).
      'h1054: return 32'h00316013; // prefetch.w 0(x2).
      'h1058: return addi(4, 0, 'h400);
      'h105c: return csrw('h305, 4);
      'h1060: return ld(3, 2, 1); // Misaligned; hardware mtval must contain 0x181.
      'h0400: return csrr(5, 'h343);
      'h0404: return csrr(6, 'h342);
      'h0408: return addi(7, 0, 'h200);
      'h040c: return sd(5, 7, 0);
      'h0410: return sd(6, 7, 8);
      default: return 32'h0000006f;
    endcase
  endfunction

  always_comb begin
    instruction_access_in = '0;
    instruction_access_in.request.ready = !instruction_response.valid;
    instruction_access_in.response = instruction_response;
    data_access_in = '0;
    data_access_in.request.ready = rejected || !data_access_out.request.valid || data_access_out.request.bits.address != 'h108;
    data_access_in.response = data_response;
    data_access_in.drained = !data_response.valid;
  end

  always @(posedge clock) begin
    if (reset) begin
      instruction_response <= '0;
      data_response <= '0;
      stores <= 0;
      loads <= 0;
      prefetches <= 0;
      flushes <= 0;
      rejected <= 0;
    end else begin
      assert (!translation_flush && !instruction_access_out.invalidate_all)
        else $fatal(1, "PMM change incorrectly invalidated translations or I-cache");
      if (instruction_access_out.flush) begin
        instruction_response.valid <= 0;
        flushes <= flushes + 1;
      end else begin
        if (instruction_response.valid && instruction_access_out.response.ready) instruction_response.valid <= 0;
        if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
          assert (instruction_access_out.request.address < 'h2000) else $fatal(1, "fetch address changed");
          instruction_response <= {1'b1, instruction_at(instruction_access_out.request.address), 2'b0};
        end
      end
      data_response.valid <= 0;
      if (prefetch_out.valid) begin
        assert (prefetch_out.address == 'h180 && prefetch_out.operation == 2'(prefetches + 1))
          else $fatal(1, "explicit prefetch pointer was not normalized");
        prefetches <= prefetches + 1;
      end
      if (data_access_out.request.valid) begin
        if (!data_access_in.request.ready) rejected <= 1;
        else if (data_access_out.request.bits.access == 1) begin
          assert (data_access_out.request.bits.address == 'h108 && loads == 0) else $fatal(1, "load replay address");
          loads <= loads + 1;
          data_response.valid <= 1;
          data_response.bits <= {1'b0, LOAD_VALUE, 2'd1, data_access_out.request.bits.rd, 2'b01};
        end else begin
          assert (data_access_out.request.bits.access == 2) else $fatal(1, "unexpected memory operation");
          case (stores)
            0: assert (data_access_out.request.bits.address == 'h100 && data_access_out.request.bits.data == 64'hfe00000000000100) else $fatal(1, "PMM7 store changed the register value or failed to mask");
            1: assert (data_access_out.request.bits.address == 'h110 && data_access_out.request.bits.data == LOAD_VALUE) else $fatal(1, "load replay/result");
            2: assert (data_access_out.request.bits.address == 'h180 && data_access_out.request.bits.data == 64'hffff000000000180) else $fatal(1, "PMM16 store/serialization");
            3: assert (data_access_out.request.bits.address == 'h200 && data_access_out.request.bits.data == 'h181) else $fatal(1, "mtval did not preserve transformed fault address");
            4: begin
              assert (data_access_out.request.bits.address == 'h208 && data_access_out.request.bits.data == 4) else $fatal(1, "misaligned cause");
              assert (loads == 1 && rejected && prefetches == 3 && flushes >= 4) else $fatal(1, "missing replay/restart/prefetch coverage");
              $display("Ssnpm core serialization, replay, explicit prefetch, and transformed mtval passed");
              $finish;
            end
            default: $fatal(1, "unexpected store");
          endcase
          stores <= stores + 1;
        end
      end
    end
  end

  initial begin
    repeat (3) @(negedge clock);
    reset = 0;
    repeat (1500) @(negedge clock);
    $fatal(1, "pointer-masking program timed out: stores=%0d loads=%0d flushes=%0d", stores, loads, flushes);
  end
endmodule
