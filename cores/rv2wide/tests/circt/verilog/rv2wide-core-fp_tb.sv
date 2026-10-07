// SPDX-License-Identifier: Apache-2.0
module rv2wide_core_fp_tb;
  typedef struct packed { logic [63:0] cause, value; } fetch_fault_t;
  typedef struct packed { logic valid; fetch_fault_t bits; } fetch_fault_flow_t;
  typedef struct packed { logic valid; logic [63:0] pc, target; logic compressed; logic [1:0] ras_action; } prediction_t;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction, raw_instruction; logic [63:0] sequential_pc; logic compressed_illegal; fetch_fault_flow_t fault; prediction_t prediction; logic [1:0] speculated_ras_action; } instruction_t;
  typedef struct packed { logic [1:0] count; instruction_t [1:0] entries; } packet_t;
  typedef struct packed { logic valid; packet_t bits; } packet_flow_t;
  typedef struct packed { logic [1:0] disposition; logic [63:0] cause, value; } resolution_t;
  typedef struct packed { logic valid; resolution_t bits; } resolution_flow_t;
  typedef struct packed { logic valid; instruction_t bits; } instruction_flow_t;
  typedef struct packed { instruction_t fetched; logic [4:0] rd; logic write; logic [63:0] data; logic deferred; } retirement_t;
  typedef struct packed { logic valid; retirement_t bits; } retirement_flow_t;
  typedef struct packed { logic [63:0] pc, target; resolution_t resolution; } redirect_t;
  typedef struct packed { logic valid; redirect_t bits; } redirect_flow_t;
  typedef struct packed { logic [63:0] address; logic [3:0] access, atomic; logic [1:0] width; logic [63:0] data; logic [7:0] mask; } memory_req_t;
  typedef struct packed { logic valid; memory_req_t bits; } memory_req_flow_t;
  typedef struct packed { logic valid; logic [63:0] bits; } memory_resp_flow_t;
  typedef struct packed { logic request_ready; resolution_flow_t fault; memory_resp_flow_t response; logic drained, ordered_busy; } memory_in_t;
  typedef struct packed { memory_req_flow_t request; logic response_ready; } memory_out_t;
  typedef struct packed { logic [2:0] outcome; logic [63:0] data; } lookup_t;
  typedef struct packed { logic valid; lookup_t bits; } lookup_flow_t;
  typedef struct packed { lookup_flow_t response; logic commit_ready; } pipeline_in_t;
  typedef struct packed { memory_req_flow_t request; logic commit; } pipeline_out_t;
  typedef struct packed { logic [7:0] byte_mask; logic [63:0] address; logic [3:0] access, atomic; logic [1:0] width; logic unsigned_load; logic [63:0] data; logic context_bit; logic [2:0] locality; } split_req_t;
  typedef struct packed { logic access_fault; logic [63:0] data; logic context_bit; } split_physical_resp_t;
  typedef struct packed { split_physical_resp_t response; logic page_fault; logic [63:0] fault_address; logic [66:0] guest; } split_result_t;
  typedef struct packed { logic valid; split_req_t bits; } split_request_flow_t;

  typedef struct packed { logic valid; split_result_t bits; } split_response_flow_t;
  typedef struct packed { logic request_ready; split_response_flow_t response; } split_in_t;
  logic clock=0, reset=1;
  always #5 clock=~clock;
  packet_flow_t instructions='0;
  logic ready;
  retirement_flow_t retired[2], completed;
  instruction_flow_t memory_stage[2];
  resolution_flow_t resolution[2];
  redirect_flow_t redirect;
  logic [1:0] issued, retired_count;
  memory_in_t memory_in='0;
  memory_out_t memory_out;
  pipeline_in_t pipeline_in='0;
  pipeline_out_t pipeline_out;
  split_in_t split_in='0;
  split_request_flow_t split_out;
  int cycles=0, commits=0;
  logic [63:0] gprs[32];
  logic [63:0] next_pc=64'h8000;
  int expected_traps=0, traps=0, fp_pairs=0;
  logic [63:0] load_data=64'h4008000000000000;
  logic [63:0] stored_data=0;
  int stores=0, response_due=0;
  bit slow_load=0, pending_load=0, inject_fault=0, pending_split=0;
  int split_due=0, split_count=0;
  logic [63:0] inject_pc=0;
  RV2WideCore dut(
    .clock(clock),.reset(reset),.hart_id(64'd0),.time_counter(64'd0),.interrupts(6'd0),
    .instructions_in(instructions),.instructions_out(ready),
    .resolution_0_in(resolution[0]),.resolution_1_in(resolution[1]),
    .memory_stage_0_out(memory_stage[0]),.memory_stage_1_out(memory_stage[1]),
    .retired_0_out(retired[0]),.retired_1_out(retired[1]),.completed_out(completed),
    .redirect_out(redirect),.issued(issued),.retired_count(retired_count),
    .memory_in(memory_in),.memory_out(memory_out),.pipeline_in(pipeline_in),.pipeline_out(pipeline_out),
    .split_in(split_in),.split_out(split_out),
    .branch_update_out(),.predictor_restore_out(),.predictor_clear_out(),
    .translation_state(),.translation_flush(),.instruction_invalidate_out(),.fetch_flush_out(),.instruction_capacity(),.sleeping()
  );
  always_comb begin
    resolution[0]='0; resolution[1]='0;
    memory_in.request_ready=!pending_load; memory_in.drained=!pending_load;
    memory_in.response.valid=pending_load && cycles>=response_due;
    memory_in.response.bits=load_data;
    for(int lane=0;lane<2;lane++) if(inject_fault && memory_stage[lane].valid && memory_stage[lane].bits.pc==inject_pc) begin
      resolution[lane].valid=1; resolution[lane].bits='{disposition:2'd1,cause:64'd2,value:64'd0};
    end
    pipeline_in.commit_ready=1;
    split_in.request_ready=!pending_split;
    split_in.response.valid=pending_split && cycles>=split_due;
    split_in.response.bits='0;
    split_in.response.bits.response.data=load_data;
  end
  always @(negedge clock) if(!reset) cycles++;
  always @(posedge clock) if(!reset) begin
    if(cycles>3000) $fatal(1,"FP core timeout");
    pipeline_in.response.valid <= pipeline_out.request.valid;
    pipeline_in.response.bits.outcome <= slow_load ? 3'd0 : pipeline_out.request.bits.access==1 ? 3'd1 : 3'd2;
    pipeline_in.response.bits.data <= load_data;
    if(pipeline_out.request.valid && pipeline_out.request.bits.access==2) stored_data= pipeline_out.request.bits.data;
    if(pipeline_out.commit) stores++;
    if(split_out.valid && split_in.request_ready) begin
      pending_split<=1; split_due=cycles+4; split_count++;
      if(split_out.bits.access==2) stored_data=split_out.bits.data;
    end
    if(split_in.response.valid) pending_split<=0;
    if(memory_out.request.valid && memory_in.request_ready) begin
      assert(slow_load && memory_out.request.bits.access==1) else $fatal(1,"unexpected slow request");
      pending_load<=1; response_due=cycles+12;
    end
    if(memory_in.response.valid && memory_out.response_ready) pending_load<=0;
    if(retired_count==2 && (retired[0].bits.fetched.instruction[6:0]==7'h53 || retired[1].bits.fetched.instruction[6:0]==7'h53)) fp_pairs++;
    for(int i=0;i<2;i++) if(retired[i].valid) begin
      commits++;
      if(retired[i].bits.write && !retired[i].bits.deferred) gprs[retired[i].bits.rd]=retired[i].bits.data;
    end
    if(completed.valid && completed.bits.write) gprs[completed.bits.rd]=completed.bits.data;
    if(redirect.valid && redirect.bits.resolution.disposition==1) begin
      traps++;
      assert(traps<=expected_traps && redirect.bits.resolution.cause==2) else $fatal(1,"unexpected FP trap pc=%h",redirect.bits.pc);
    end
  end
  function automatic logic [31:0] fp(int funct7,int rd,int rs1,int rs2,int rm=0);
    return (32'(funct7)<<25)|(32'(rs2)<<20)|(32'(rs1)<<15)|(32'(rm)<<12)|(32'(rd)<<7)|32'h53;
  endfunction
  task automatic send(logic [31:0] a,logic [31:0] b=32'h13,bit paired=0);
    @(negedge clock);
    instructions.valid=1; instructions.bits='0; instructions.bits.count=paired?2:1;
    instructions.bits.entries[0]='{pc:next_pc,instruction:a,raw_instruction:a,sequential_pc:next_pc+4,default:'0};
    instructions.bits.entries[1]='{pc:next_pc+4,instruction:b,raw_instruction:b,sequential_pc:next_pc+8,default:'0};
    do @(posedge clock); while(!ready);
    next_pc+=paired?8:4;
    @(negedge clock); instructions.valid=0;
  endtask
  task automatic settle;
    repeat(100) @(negedge clock);
  endtask
  task automatic expect_gpr(int rd,logic [63:0] value);
    assert(gprs[rd]===value) else $fatal(1,"x%0d got %h expected %h",rd,gprs[rd],value);
  endtask
  initial begin
    for(int i=0;i<32;i++) gprs[i]=0;
    repeat(4) @(negedge clock); reset=0;
    // FS disabled must trap before any numerical request or FPR update.
    expected_traps=1;
    send(fp('h69,1,0,0)); settle();
    assert(traps==1) else $fatal(1,"FS-off did not trap");
    send(32'h000020b7); // lui x1,2: FS=Initial
    send(32'h30009073); // csrw mstatus,x1
    settle();
    send(32'h01000093,32'h00900313,1); // x1=16, x6=9
    send(fp('h69,1,1,0),32'h00700393,1); // fcvt.d.w f1,x1 + integer
    settle();
    send(32'h00b00413,fp('h01,2,1,1),1); // integer + fadd.d f2,f1,f1
    send(fp('h61,3,2,0)); // fcvt.w.d x3,f2
    send(fp('h0d,3,2,1)); // fdiv.d f3,f2,f1 (WB)
    send(fp('h61,4,3,0));
    settle(); expect_gpr(3,32); expect_gpr(4,2); expect_gpr(7,7); expect_gpr(8,11);
    assert(fp_pairs>=2) else $fatal(1,"FP did not pair from both age slots");

    // Same-destination pairing preserves age when FP returns at WB or later.
    send(fp('h61,21,2,0),32'h06300a93,1); // older FP x21=32, younger integer x21=99
    send(32'h000a8b13); // x22=x21 must forward the younger value
    settle(); expect_gpr(21,99); expect_gpr(22,99);
    send(32'h00700b93,fp('h61,23,2,0),1); // older integer x23=7, younger FP x23=32
    send(32'h000b8c13); // x24=x23 waits for the younger FP value
    settle(); expect_gpr(23,32); expect_gpr(24,32);

    // f0 is writable, and FPR forwarding must cover the load port too.
    send(fp('h69,0,1,0));
    send(fp('h61,5,0,0)); settle(); expect_gpr(5,16);
    send(32'h10003407); // fld f8,256(x0): 3.0
    send(fp('h61,9,8,0)); settle(); expect_gpr(9,3);
    send(32'h10803427); // fsd f8,264(x0)
    settle(); assert(stores==1 && stored_data==load_data) else $fatal(1,"FP store lost FPR source");

    slow_load=1;
    send(32'h10003607); // fld f12,256(x0): accepted delayed load
    send(fp('h01,14,1,1)); // independent arithmetic while the load is pending
    send(fp('h61,15,12,0)); settle(); expect_gpr(15,3);
    slow_load=0;

    send(32'h10303c07); // misaligned fld f24,259(x0)
    settle();
    send(fp('h61,24,24,0)); settle(); expect_gpr(24,3);
    send((32'd8<<25)|(32'd24<<20)|(32'd3<<12)|(32'd11<<7)|32'h27); // fsd f24,267(x0)
    settle();
    assert(split_count==2 && stored_data==load_data) else $fatal(1,"split FP load/store lost its owner or data");

    // Kill already-launched younger arithmetic with an older same-group fault.
    send(fp('h69,20,1,0)); settle();
    inject_fault=1; inject_pc=next_pc; expected_traps=2;
    send(32'h00100893,fp('h01,20,1,1),1); settle();
    inject_fault=0;
    assert(traps==2) else $fatal(1,"older fault did not trap");
    send(fp('h61,21,20,0)); settle(); expect_gpr(21,16);

    // Single precision boxing and mixed fixed-latency return ordering.
    send(fp('h68,10,1,0)); // fcvt.s.w f10,x1
    send(fp('h00,11,10,10));
    send(fp('h60,12,11,0)); settle(); expect_gpr(12,32);
    send(fp('h71,13,10,0)); settle(); expect_gpr(13,64'hffffffff41800000);
    send(32'h00302573); // csrr x10,fcsr
    settle(); expect_gpr(10,0);
    send((32'd1<<27)|(32'd1<<25)|(32'd3<<20)|(32'd1<<15)|(32'd22<<7)|32'h43); // fmadd.d f22,f1,f3,f1
    send(fp('h61,22,22,0)); settle(); expect_gpr(22,48);
    send(fp('h79,0,0,0)); // fmv.d.x f0,x0
    send(fp('h0d,16,1,0)); // 16/0: DZ, +infinity
    send(fp('h71,17,16,0));
    send(32'h00302973); // csrr x18,fcsr drains prior arithmetic
    settle(); expect_gpr(17,64'h7ff0000000000000); expect_gpr(18,8);
    send(32'h00101073); // csrw fflags,x0
    settle();
    expected_traps=3;
    send(fp('h01,20,1,1,5)); settle();
    assert(traps==3) else $fatal(1,"reserved rounding mode did not trap");
    $display("RV2Wide FP mixed-latency, pairing, load/store, CSR and legality checks passed");
    $finish;
  end
endmodule
