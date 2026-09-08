// Checks real EX/MEM/WB hit latency, dependent forwarding, signed lanes, and precise squash.
module rv5stage_load_hit_tb;
  typedef struct packed {logic ready;} ready_t;
  typedef struct packed {logic [63:0] address;} ireq_bits_t;
  typedef struct packed {logic valid; ireq_bits_t bits;} ireq_t;
  typedef struct packed {logic [31:0] word; logic page_fault, access_fault;} iresp_bits_t;
  typedef struct packed {logic valid; iresp_bits_t bits;} iresp_t;
  typedef struct packed {ready_t request; iresp_t response;} instruction_in_t;
  typedef struct packed {logic flush, invalidate_all; ireq_t request; ready_t response;} instruction_out_t;
  typedef struct packed {
    logic [63:0] address;
    logic [3:0] access, atomic;
    logic [1:0] width;
    logic unsigned_0;
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
    logic [2:0] locality;
  } request_bits_t;
  typedef struct packed {request_bits_t request; logic device;} ureq_bits_t;
  typedef struct packed {logic valid; ureq_bits_t bits;} ureq_t;
  typedef struct packed {logic access_fault; logic [63:0] data; logic [1:0] destination; logic [4:0] rd; logic [1:0] floating_point_precision;} response_bits_t;
  typedef struct packed {logic valid; response_bits_t bits;} response_t;
  typedef struct packed {ready_t request; logic request_fault, request_access_fault; response_t response; logic drained;} uncached_in_t;
  typedef struct packed {ureq_t request;} uncached_out_t;
  typedef struct packed {logic valid; CHIReqFlit bits;} req_t;
  typedef struct packed {logic valid; CHIRspFlit bits;} rsp_t;
  typedef struct packed {logic valid; CHIDatFlit bits;} dat_t;
  typedef struct packed {logic valid; CHISnpFlit bits;} snp_t;
  typedef struct packed {ready_t requests, requester_responses, request_data; rsp_t responses; dat_t response_data; snp_t snoops;} chi_in_t;
  typedef struct packed {req_t requests; rsp_t requester_responses; dat_t request_data; ready_t responses, response_data, snoops;} chi_out_t;

  logic clock=0, reset=1;
  instruction_in_t instruction_in;
  instruction_out_t instruction_out;
  uncached_in_t uncached_in;
  uncached_out_t uncached_out;
  chi_in_t chi_in;
  chi_out_t chi_out;
  logic load_issue, load_hit, transaction_valid, transaction_fire;
  logic [63:0] load_address;
  request_bits_t transaction;
  RV5StageLoadHit dut(.*);
  always #5 clock=~clock;

  logic refill_pending=0, uncached_pending=0;
  logic [31:0] instruction_words[8];
  logic [2:0] instruction_head=0, instruction_tail=0;
  logic [3:0] instruction_count=0;
  logic [63:0] line_address;
  logic [11:0] transaction_id;
  response_bits_t uncached_response;
  integer cycle=0, beat=0, refills=0, signatures=0, hits=0, device_reads=0, ram_stores=0;
  integer measured=0, previous_issue=0, chase_cycle=0;
  logic previous_load=0;
  logic [63:0] previous_address;

  function automatic logic [31:0] load_insn(input int rd, rs1, offset, size);
    return {12'(offset),5'(rs1),3'(size),5'(rd),7'h03};
  endfunction
  function automatic logic [31:0] add_insn(input int rd, rs1, rs2, input bit word_op=0);
    return {7'b0,5'(rs2),5'(rs1),3'b0,5'(rd),word_op ? 7'h3b : 7'h33};
  endfunction
  function automatic logic [31:0] store_insn(input int rs2, offset, base=20);
    return {7'(offset>>5),5'(rs2),5'(base),3'b011,5'(offset),7'h23};
  endfunction
  function automatic logic [31:0] instruction_at(input logic [63:0] address);
    int n;
    n=int'(address/4);
    if(n>=16 && n<96) begin
      case((n-16)%5)
        0: return load_insn(5,8,16,2);
        1: return load_insn(6,8,20,2);
        2: return 32'h00448493; // addi x9,x9,4
        3: return 32'h00450513; // addi x10,x10,4
        4: return add_insn(7,5,6,1);
      endcase
    end
    case(n)
      0: return 32'h00001437; // lui x8,1
      1: return load_insn(5,8,0,3); // cold line fill
      2: return add_insn(6,5,0); // wait for the cold load
      3: return 32'h00008a37; // lui x20,8: signature device
      8: return load_insn(5,8,0,3);
      9: return load_insn(6,5,0,3); // immediately dependent address
      10: return add_insn(7,6,0);
      11: return store_insn(7,0);
      12: return 32'h0ff0000f; // fence
      96: return store_insn(7,8);
      97: return 32'h0ff0000f;
      100: return load_insn(5,8,24,0); // lb -1
      101: return load_insn(6,8,24,4); // lbu 255
      102: return add_insn(7,5,6);
      103: return store_insn(7,16);
      104: return 32'h0ff0000f;
      108: return load_insn(5,8,24,1); // lh -32513
      109: return load_insn(6,8,24,5); // lhu 33023
      110: return add_insn(7,5,6);
      111: return store_insn(7,24);
      112: return 32'h0ff0000f;
      116: return load_insn(5,8,16,6); // lwu 4294967289
      117: return load_insn(6,8,16,2); // lw -7
      118: return add_insn(7,5,6);
      119: return store_insn(7,32);
      120: return 32'h0ff0000f;
      124: return load_insn(7,8,24,3);
      125: return store_insn(7,40);
      126: return 32'h0ff0000f;
      128: return 32'h000062b7; // enable FS
      129: return 32'h3002a073; // csrs mstatus,x5
      130: return 32'h01042087; // flw f1,16(x8)
      131: return 32'he00083d3; // fmv.x.w x7,f1
      132: return store_insn(7,48);
      133: return 32'h0ff0000f;
      136: return 32'h01843107; // fld f2,24(x8)
      137: return 32'he20103d3; // fmv.x.d x7,f2
      138: return store_insn(7,56);
      139: return 32'h0ff0000f;
      140: return load_insn(7,20,0,3); // side-effecting device read: WB only
      141: return store_insn(7,64);
      142: return 32'h0ff0000f;
      144: return 32'h04200393;
      145: return store_insn(7,32,8); // update the resident cache line
      146: return load_insn(7,8,32,3); // must not bypass the older store
      147: return store_insn(7,72);
      148: return 32'h0ff0000f;
      152: return 32'h07b00393; // x7=123, must survive the younger squashed lookup
      153: return 32'h40000293; // x5=0x400
      154: return 32'h30529073; // csrw mtvec,x5
      155: return 32'h000102b7; // x5=0x10000, unmapped
      156: return load_insn(6,5,0,3); // access fault at WB
      157: return load_insn(7,8,0,3); // younger warm lookup must not commit
      158: return store_insn(0,80); // wrong path
      256: return store_insn(7,80); // trap handler: precise preserved value
      257: return 32'h0000006f;
      default: return 32'h00000013;
    endcase
  endfunction
  function automatic logic [63:0] data_at(input logic [63:0] address);
    case(address)
      64'h1000: return 64'h1008;
      64'h1008: return 64'd7;
      64'h1010: return 64'h00000009fffffff9;
      64'h1018: return 64'hfedcba98765480ff;
      default: return 64'h1111111111111111;
    endcase
  endfunction
  always_comb begin
    instruction_in='0;
    instruction_in.request.ready=instruction_count<8;
    instruction_in.response.valid=instruction_count!=0;
    instruction_in.response.bits.word=instruction_words[instruction_head];
  end
  always_comb begin
    uncached_in='0;
    uncached_in.request.ready=!uncached_pending;
    uncached_in.response.valid=uncached_pending;
    uncached_in.response.bits=uncached_response;
    uncached_in.drained=!uncached_pending;
  end
  always_comb begin
    chi_in='0;
    chi_in.requests.ready=1;
    chi_in.requester_responses.ready=1;
    chi_in.request_data.ready=1;
    chi_in.response_data.valid=refill_pending;
    chi_in.response_data.bits.opcode=4'h4;
    chi_in.response_data.bits.resp=3'b010; // clean unique line permits the later local store
    chi_in.response_data.bits.byte_enable=16'hffff;
    chi_in.response_data.bits.data_id=2'(beat);
    chi_in.response_data.bits.home_nid_or_pbha_or_mismatched_mecid=7'd1;
    chi_in.response_data.bits.dbid_or_mecid=16'h55;
    chi_in.response_data.bits.txn_id=transaction_id;
    chi_in.response_data.bits.src_id=7'd1;
    chi_in.response_data.bits.tgt_id=7'd3;
    chi_in.response_data.bits.data={data_at(line_address+64'(16*beat+8)),data_at(line_address+64'(16*beat))};
  end
  always @(posedge clock) begin
    cycle<=cycle+1;
    if(reset) begin
      instruction_count<=0; instruction_head<=0; instruction_tail<=0;
      refill_pending<=0; uncached_pending<=0;
      refills<=0; beat<=0; signatures<=0; measured<=0; hits<=0;
      previous_load<=0; chase_cycle<=0;
    end else begin
      previous_load<=load_issue;
      previous_address<=load_address;
      if(load_hit) begin
        assert(previous_load) else $fatal(1,"hit did not follow EX issue by exactly one cycle");
        assert(previous_address<64'h8000) else $fatal(1,"device or unmapped access completed speculatively");
        hits<=hits+1;
      end
      if(load_issue && load_address==64'h1000 && refills==1) chase_cycle<=cycle;
      if(load_issue && load_address==64'h1008 && signatures==0) begin
        assert(cycle-chase_cycle==2) else $fatal(1,"dependent load needs more than one bubble: %0d",cycle-chase_cycle);
      end
      if(load_issue && signatures==1 && (load_address==64'h1010 || load_address==64'h1014)) begin
        if(measured!=0) assert(cycle-previous_issue==(measured[0] ? 1 : 4))
          else $fatal(1,"vvadd-style warm hit bubble: issue=%0d gap=%0d",measured,cycle-previous_issue);
        measured<=measured+1;
        previous_issue<=cycle;
      end
      if(instruction_out.flush) begin instruction_count<=0; instruction_head<=0; instruction_tail<=0; end
      else begin
        case({instruction_out.request.valid && instruction_in.request.ready,instruction_in.response.valid && instruction_out.response.ready})
          2'b10: instruction_count<=instruction_count+1;
          2'b01: instruction_count<=instruction_count-1;
          default: ;
        endcase
        if(instruction_in.response.valid && instruction_out.response.ready) instruction_head<=instruction_head+1;
        if(instruction_out.request.valid && instruction_in.request.ready) begin
          instruction_words[instruction_tail]<=instruction_at(instruction_out.request.bits.address);
          instruction_tail<=instruction_tail+1;
        end
      end
      if(chi_out.requests.valid && chi_in.requests.ready) begin
        assert(!refill_pending && chi_out.requests.bits.opcode==7'h02) else $fatal(1,"unexpected CHI transaction");
        line_address<=64'(chi_out.requests.bits.address);
        transaction_id<=chi_out.requests.bits.txn_id;
        refill_pending<=1; beat<=0; refills<=refills+1;
      end
      if(refill_pending && chi_out.response_data.ready) begin
        if(beat==3) refill_pending<=0;
        else beat<=beat+1;
      end
      uncached_pending<=0;
      if(uncached_out.request.valid && uncached_in.request.ready) begin
        uncached_pending<=1;
        uncached_response<='{access_fault:1'b0,data:64'hfeedface,destination:uncached_out.request.bits.request.destination,rd:uncached_out.request.bits.request.rd,floating_point_precision:uncached_out.request.bits.request.floating_point_precision};
        if(uncached_out.request.bits.request.access==4'd1) device_reads<=device_reads+1;
      end
      if(transaction_fire && transaction.access==4'd2 && transaction.address<64'h8000) begin
        assert(transaction.address==64'h1020 && transaction.data==66) else $fatal(1,"unexpected cache mutation");
        ram_stores<=ram_stores+1;
      end
      if(transaction_fire && transaction.access==4'd2 && transaction.address>=64'h8000) begin
        assert(transaction.address==64'h8000+64'(signatures*8)) else $fatal(1,"unexpected signature address %h",transaction.address);
        case(signatures)
          0: assert(transaction.data==7) else $fatal(1,"dependent hit forwarded wrong value");
          1: begin
            assert(transaction.data==2 && measured==32 && refills==1)
              else $fatal(1,"warm loop result=%h loads=%0d refills=%0d",transaction.data,measured,refills);
            $display("warm vvadd-style stream: 80 instructions, 32 hits, no load-use bubbles");
          end
          2: assert(transaction.data==254) else $fatal(1,"byte extension");
          3: assert(transaction.data==510) else $fatal(1,"halfword extension");
          4: assert(transaction.data==64'hfffffff2) else $fatal(1,"word extension");
          5: assert(transaction.data==64'hfedcba98765480ff) else $fatal(1,"doubleword data");
          6: assert(transaction.data==64'hfffffffffffffff9) else $fatal(1,"FP single load-hit writeback");
          7: assert(transaction.data==64'hfedcba98765480ff) else $fatal(1,"FP double load-hit writeback");
          8: assert(transaction.data==64'hfeedface && device_reads==1) else $fatal(1,"device load did not execute exactly once");
          9: assert(transaction.data==66 && ram_stores==1) else $fatal(1,"load bypassed an older store");
          10: begin
            assert(transaction.data==123) else $fatal(1,"squashed hit overwrote x7");
            $display("RV5Stage EX-issued load-hit latency, forwarding, lanes, and squash passed (%0d hits)",hits);
            $finish;
          end
          default: $fatal(1,"extra signature");
        endcase
        signatures<=signatures+1;
      end
      assert(cycle<10000) else $fatal(1,"load-hit test timeout: signatures=%0d hits=%0d",signatures,hits);
    end
  end
  initial begin
    repeat(4) @(negedge clock);
    reset=0;
  end
endmodule
