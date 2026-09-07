// Exercises WB replay and fault isolation across memory, FP registers, flags, prefetches, and atomics.
typedef struct packed { logic ss, ms, st, mt, se, me; } interrupts_t;
typedef struct packed { logic ready; } ready_t;
typedef struct packed { logic valid; } release_t;
typedef struct packed { logic valid; logic [XLEN-1:0] address; } ireq_t;
typedef struct packed { logic valid; logic [31:0] word; logic page_fault, access_fault; } iresp_t;
typedef struct packed { ready_t request; iresp_t response; } iin_t;
typedef struct packed { logic flush, invalidate_all; ireq_t request; ready_t response; } iout_t;
typedef struct packed {
  logic [XLEN-1:0] address;
  logic [3:0] access;
  logic [3:0] atomic;
  logic [1:0] width;
  logic unsigned_load;
  logic [XLEN-1:0] data;
  logic [1:0] destination;
  logic [4:0] rd;
  logic [1:0] precision;
} dreq_bits_t;
typedef struct packed { logic valid; dreq_bits_t bits; } dreq_t;
typedef struct packed {
  logic access_fault;
  logic [XLEN-1:0] data;
  logic [1:0] destination;
  logic [4:0] rd;
  logic [1:0] precision;
} dresp_bits_t;
typedef struct packed { logic valid; dresp_bits_t bits; } dresp_t;
typedef struct packed { ready_t request; logic request_fault, request_access_fault; dresp_t response; logic drained; } din_t;
typedef struct packed { dreq_t request; } dout_t;
logic clock = 0, reset = 1;
logic [63:0] time_counter = 0;
logic [XLEN-1:0] hart_id = 0, mstatus, satp;
logic [1:0] privilege;
logic translation_flush;
interrupts_t interrupts = '0;
release_t release_in = '0;
ready_t release_out;
iin_t instruction_access_in;
iout_t instruction_access_out;
din_t data_access_in;
dout_t data_access_out;
struct packed { logic valid; logic [XLEN-1:0] address; logic [1:0] operation; } prefetch_out;
logic instruction_pending = 0;
logic [31:0] instruction_word;
integer scenario, attempts, accepted, stores, response_delay, prefetches;
logic done;
dresp_bits_t pending_response;
RV5StageCore dut (.*);
always #5 clock = ~clock;

function automatic logic [31:0] addi(input int rd, rs, imm);
  return {12'(imm), 5'(rs), 3'b000, 5'(rd), 7'h13};
endfunction
function automatic logic [31:0] csr(input int rd, rs, address, op);
  return {12'(address), 5'(rs), 3'(op), 5'(rd), 7'h73};
endfunction
function automatic logic [31:0] fp(input int funct7, rd, rs1, rs2);
  return {7'(funct7), 5'(rs2), 5'(rs1), 3'b000, 5'(rd), 7'h53};
endfunction
function automatic logic [31:0] store_word(input int rs, address, opcode = 'h23);
  return {7'(address >> 5), 5'(rs), 5'b0, 3'b010, 5'(address), 7'(opcode)};
endfunction
function automatic logic [31:0] atomic_word(input int operation, rd, rs);
  return {5'(operation), 2'b0, 5'(rs), 5'd3, 3'b010, 5'(rd), 7'h2f};
endfunction
function automatic logic [31:0] instruction_at(input logic [XLEN-1:0] address);
  case (address)
    'h100: return 32'h000020b7;                  // lui x1, 2: FS=Initial
    'h104: return csr(0, 1, 'h300, 2);
    'h108: return addi(1, 0, 'h200);
    'h10c: return csr(0, 1, 'h305, 1);           // mtvec
    'h110: return 32'h3f8000b7;                  // 1.0f
    'h114: return fp('h78, 1, 1, 0);            // fmv.w.x f1, x1
    'h118: return fp('h78, 0, 0, 0);            // fmv.w.x f0, x0
    'h11c: return 32'h40000137;                  // x2 = 2.0f
    'h120: return 32'h40002283;                  // lw x5, 0x400(x0)
    'h124: return 32'h50106013;                  // prefetch.r 0x500(x0)
    'h128: return fp('h78, 1, 2, 0);            // younger FP overwrite
    'h12c: return fp('h0c, 2, 1, 0);            // fdiv.s f2, f1, f0: DZ
    'h130: return fp('h70, 6, 1, 0);            // fmv.x.w x6, f1
    'h134: return store_word(6, 'h440);
    'h138: return store_word(2, 'h444, 'h27);    // fsw f2
    'h13c: return csr(7, 0, 'h001, 2);           // fflags
    'h140: return store_word(7, 'h448);
    'h144: return addi(3, 0, 'h450);
    'h148: return atomic_word(2, 8, 0);         // lr.w
    'h14c: return atomic_word(3, 9, 2);         // sc.w
    'h150: return atomic_word(1, 10, 2);        // amoswap.w
    'h154: return 32'h48002187;                  // flw f3, 0x480(x0)
    'h158: return fp('h70, 11, 3, 0);
    'h15c: return store_word(11, 'h44c);
    'h160: return store_word(10, 'h458);
    'h200: return fp('h70, 6, 1, 0);            // fault: old f1 must survive
    'h204: return store_word(6, 'h460);
    'h208: return csr(7, 0, 'h001, 2);
    'h20c: return store_word(7, 'h464);          // fault: no younger DZ
    'h210: return csr(7, 0, 'h342, 2);
    'h214: return store_word(7, 'h468);
    'h218: return csr(7, 0, 'h343, 2);
    'h21c: return store_word(7, 'h46c);
    default: return 32'h0000006f;
  endcase
endfunction

assign instruction_access_in.request.ready = !instruction_pending;
assign instruction_access_in.response.valid = instruction_pending;
assign instruction_access_in.response.word = instruction_word;
assign instruction_access_in.response.page_fault = 0;
assign instruction_access_in.response.access_fault = 0;
assign data_access_in.request.ready = response_delay == 0 && (data_access_out.request.bits.address != 'h400 || (scenario == 0 && attempts >= 3));
assign data_access_in.request_fault = 0;
assign data_access_in.request_access_fault = scenario == 1 && data_access_out.request.valid && data_access_out.request.bits.address == 'h400;
assign data_access_in.response.valid = response_delay == 1;
assign data_access_in.response.bits = pending_response;
assign data_access_in.drained = response_delay == 0;

always_ff @(posedge clock) begin
  if (reset) begin
    instruction_pending <= 0;
    attempts <= 0;
    accepted <= 0;
    stores <= 0;
    prefetches <= 0;
    response_delay <= 0;
    pending_response <= '0;
    done <= 0;
  end else begin
    if (instruction_access_out.flush) instruction_pending <= 0;
    else begin
      if (instruction_pending && instruction_access_out.response.ready) instruction_pending <= 0;
      if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
        instruction_pending <= 1;
        instruction_word <= instruction_at(instruction_access_out.request.address);
      end
    end
    if (response_delay != 0) response_delay <= response_delay - 1;
    if (prefetch_out.valid) begin
      assert(scenario == 0 && accepted == 1 && prefetches == 0 && prefetch_out.address == 'h500 && prefetch_out.operation == 2) else $fatal(1, "prefetch escaped WB authorization or replayed after acceptance");
      prefetches <= prefetches + 1;
    end
    if (data_access_out.request.valid && data_access_out.request.bits.address == 'h400) attempts <= attempts + 1;
    if (data_access_out.request.valid && data_access_in.request.ready) begin
      assert (!data_access_in.request_access_fault) else $fatal(1, "fault accepted");
      if (data_access_out.request.bits.access == 2) begin
        stores <= stores + 1;
        if (scenario == 0) begin
          case (stores)
            0: assert(data_access_out.request.bits.address == 'h440 && data_access_out.request.bits.data == 'h40000000 && accepted == 1 && attempts == 4) else $fatal(1, "FP replay/RAW failed");
            1: assert(data_access_out.request.bits.address == 'h444 && data_access_out.request.bits.data == 'h7f800000) else $fatal(1, "FP store/completion failed");
            2: assert(data_access_out.request.bits.address == 'h448 && data_access_out.request.bits.data == 8) else $fatal(1, "FP flags failed");
            3: assert(data_access_out.request.bits.address == 'h44c && data_access_out.request.bits.data == 'h40400000) else $fatal(1, "FP load reservation/completion failed");
            4: begin
              assert(data_access_out.request.bits.address == 'h458 && data_access_out.request.bits.data == 42) else $fatal(1, "atomic completion failed");
              done <= 1;
            end
            default: $fatal(1, "duplicated store");
          endcase
        end else begin
          case (stores)
            0: assert(data_access_out.request.bits.address == 'h460 && data_access_out.request.bits.data == 'h3f800000) else $fatal(1, "younger FP register mutation escaped fault");
            1: assert(data_access_out.request.bits.address == 'h464 && data_access_out.request.bits.data == 0) else $fatal(1, "younger FP flags escaped fault");
            2: assert(data_access_out.request.bits.address == 'h468 && data_access_out.request.bits.data == 5) else $fatal(1, "wrong fault cause");
            3: begin
              assert(data_access_out.request.bits.address == 'h46c && data_access_out.request.bits.data == 'h400 && accepted == 0) else $fatal(1, "wrong fault address");
              done <= 1;
            end
            default: $fatal(1, "younger store escaped fault");
          endcase
        end
      end else begin
        if (data_access_out.request.bits.address == 'h400) accepted <= accepted + 1;
        else assert(scenario == 0 && ((data_access_out.request.bits.address == 'h450 && data_access_out.request.bits.access inside {3, 4, 5}) || (data_access_out.request.bits.address == 'h480 && data_access_out.request.bits.access == 1 && data_access_out.request.bits.destination == 2))) else $fatal(1, "unexpected memory effect");
        response_delay <= data_access_out.request.bits.address == 'h400 ? 20 : 4;
        pending_response.data <= data_access_out.request.bits.address == 'h480 ? 'h40400000 : 42;
        pending_response.destination <= data_access_out.request.bits.destination;
        pending_response.rd <= data_access_out.request.bits.rd;
        pending_response.precision <= data_access_out.request.bits.precision;
      end
    end

  end
end

initial begin
  for (scenario = 0; scenario < 2; scenario++) begin
    reset = 1;
    repeat (3) @(negedge clock);
    reset = 0;
    release_in.valid = 1'b1;
    @(negedge clock);
    release_in.valid = 0;
    for (int cycles = 0; cycles < 2500 && !done; cycles++) @(negedge clock);
    assert(done) else $fatal(1, "WB scenario %0d timed out", scenario);
    assert(prefetches == (scenario == 0 ? 1 : 0)) else $fatal(1, "WB prefetch count mismatch");
  end
  $display("RV%0d WB memory/FP authorization, replay, and fault isolation passed", XLEN);
  $finish;
end
