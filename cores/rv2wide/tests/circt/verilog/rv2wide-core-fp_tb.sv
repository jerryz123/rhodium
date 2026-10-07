// Checks FP ownership and shared GPR write scheduling against integer multiply and load returns.
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
  logic [7:0] stored_mask=0;
  logic [1:0] stored_width=0;
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
    if(cycles>20000) $fatal(1,"FP core timeout");
    pipeline_in.response.valid <= pipeline_out.request.valid;
    pipeline_in.response.bits.outcome <= slow_load ? 3'd0 : pipeline_out.request.bits.access==1 ? 3'd1 : 3'd2;
    pipeline_in.response.bits.data <= load_data;
    if(pipeline_out.request.valid && pipeline_out.request.bits.access==2) begin
      stored_data=pipeline_out.request.bits.data;
      stored_mask=pipeline_out.request.bits.mask;
      stored_width=pipeline_out.request.bits.width;
    end
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
  task automatic expect_fpr(int rd,logic [63:0] value);
    send(fp('h71,30,rd,0)); settle(); expect_gpr(30,value);
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

    // Fixed multiply and FP-to-GPR returns share a calendar. Exercise both age
    // orders: equal-latency pairing must split before EX, never retain a result.
    send(32'h026086b3,fp('h61,14,2,0),1); // mul x13,x1,x6=144; fcvt.w.d x14,f2=32
    send(fp('h61,15,2,0),32'h02608833,1); // fcvt.w.d x15,f2=32; mul x16,x1,x6=144
    send(32'h00168693); // x13=x13+1 waits for the direct multiply write
    settle(); expect_gpr(13,145); expect_gpr(14,32); expect_gpr(15,32); expect_gpr(16,144);

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
    // Zfa uses the same EX launch, WB authorization and variable-latency return paths.
    // FLI's rs1 field is an immediate index, not an integer or FP source register.
    send(fp('h78,25,16,1),32'h01900c93,1); // fli.s f25,1.0 + x25=25
    send(32'h01a00d13,fp('h79,26,20,1),1); // x26=26 + fli.d f26,2.0
    send(fp('h71,27,25,0));
    send(fp('h71,28,26,0));
    settle(); expect_gpr(25,25); expect_gpr(26,26);
    expect_gpr(27,64'hffffffff3f800000); expect_gpr(28,64'h4000000000000000);

    // Exercise both precision catalogs, all static rounding modes, and dynamic frm.
    for(int precision=0;precision<2;precision++) begin
      send(fp('h78+precision,1,18,1)); // 1.5
      send(fp('h78+precision,2,20,1)); // 2.0
      send(fp('h14+precision,3,1,2,2)); // fminm -> 1.5
      send(fp('h71,3,3,0));
      send(fp('h14+precision,4,1,2,3)); // fmaxm -> 2.0
      send(fp('h71,4,4,0));
      send(fp('h50+precision,5,1,2,4)); // fleq -> true
      send(fp('h50+precision,6,2,1,5)); // fltq -> false
      settle();
      expect_gpr(3,precision==1 ? 64'h3ff8000000000000 : 64'hffffffff3fc00000);
      expect_gpr(4,precision==1 ? 64'h4000000000000000 : 64'hffffffff40000000);
      expect_gpr(5,1); expect_gpr(6,0);
      for(int rm=0;rm<5;rm++) begin
        send(fp('h20+precision,7,1,4,rm)); // fround without NX
        send(fp('h60+precision,7,7,0)); // dependent GPR conversion
        send(32'h00102473); // csrr x8,fflags
        settle(); expect_gpr(7,(rm==1 || rm==2) ? 1 : 2); expect_gpr(8,0);
      end
      send(32'h00215073); settle(); // csrwi frm,2 (RDN); wait for serialized redirect
      send(fp('h20+precision,7,1,5,7)); // froundnx dynamic -> 1, NX
      send(fp('h60+precision,7,7,0));
      send(32'h00102473);
      settle(); expect_gpr(7,1); expect_gpr(8,1);
      send(32'h00301073); settle(); // clear fcsr

      // Quiet NaNs propagate through MINM/MAXM; quiet compares must not raise NV.
      send(fp('h78+precision,2,31,1)); // canonical NaN
      send(fp('h14+precision,3,1,2,2));
      send(fp('h71,3,3,0));
      send(fp('h14+precision,4,2,1,3));
      send(fp('h71,4,4,0));
      send(fp('h50+precision,5,1,2,4));
      send(fp('h50+precision,6,2,1,5));
      send(32'h00102473);
      settle();
      expect_gpr(3,precision==1 ? 64'h7ff8000000000000 : 64'hffffffff7fc00000);
      expect_gpr(4,precision==1 ? 64'h7ff8000000000000 : 64'hffffffff7fc00000);
      expect_gpr(5,0); expect_gpr(6,0); expect_gpr(8,0);

      load_data=precision==1 ? 64'h7ff0000000000001 : 64'hffffffff7f800001;
      send(32'h10003107); // fld f2: signaling NaN, correctly boxed for S
      send(fp('h50+precision,5,1,2,4));
      send(fp('h50+precision,6,2,1,5));
      send(32'h00102473);
      settle(); expect_gpr(5,0); expect_gpr(6,0); expect_gpr(8,16);
      send(32'h00101073); settle();
    end

    // An unboxed single operand is canonical NaN, not the low-word number.
    load_data=64'h000000003f800000;
    send(32'h10003107);
    send(fp('h78,1,20,1)); // boxed 2.0
    send(fp('h14,3,1,2,2));
    send(fp('h71,3,3,0));
    send(fp('h50,5,2,1,5));
    send(32'h00102473);
    settle(); expect_gpr(3,64'hffffffff7fc00000); expect_gpr(5,0); expect_gpr(8,0);

    // Zfa 1.0 specifies FCVT.W.D flags even though FCVTMOD wraps the result.
    // https://docs.riscv.org/reference/isa/v20260120/unpriv/zfa.html#_modular_convert_to_integer_instruction
    load_data=64'h41f8000000180000; // 6442450945.5 -> 0x80000001, NV (signed-word overflow)
    send(32'h10003507); // fld f10,256(x0)
    send(fp('h61,11,10,8,1),32'h00c00613,1);
    send(32'h00158693); // dependent addi x13,x11,1
    send(32'h00102773);
    settle(); expect_gpr(11,64'hffffffff80000001); expect_gpr(12,12);
    expect_gpr(13,64'hffffffff80000002); expect_gpr(14,16);
    send(32'h00101073); settle();

    // A killed FROUNDNX must change neither destination nor flags/FS.
    send(fp('h79,20,16,1)); // f20=1.0
    send(fp('h79,1,18,1)); // f1=1.5
    send(32'h000040b7); send(32'h30009073); settle(); // FS=Clean
    inject_fault=1; inject_pc=next_pc; expected_traps++;
    send(32'h00100893,fp('h21,20,1,5,0),1); settle();
    inject_fault=0;
    inject_fault=1; inject_pc=next_pc; expected_traps++;
    send(32'h00100893,fp('h61,11,1,8,1),1); settle(); // killed GPR/NX return
    inject_fault=0;
    expect_gpr(11,64'hffffffff80000001);
    send(32'h300027f3); settle(); // csrr x15,mstatus before any new FP write
    send(32'h00102873); settle();
    send(fp('h71,21,20,0));
    settle(); expect_gpr(21,64'h3ff0000000000000); expect_gpr(16,0);
    assert((gprs[15]&64'h6000)==64'h4000) else $fatal(1,"killed Zfa dirtied FS");
    expected_traps++;
    send(fp('h21,20,1,4,5)); settle(); // reserved static rm
    assert(traps==expected_traps) else $fatal(1,"Zfa reserved rm did not trap");
    send(32'h00235073); settle(); // frm=6
    expected_traps++;
    send(fp('h21,20,1,4,7)); settle(); // reserved dynamic rm
    assert(traps==expected_traps) else $fatal(1,"Zfa reserved frm did not trap");
    send(32'h30001073); settle(); // FS=Off
    expected_traps++;
    send(fp('h79,20,16,1)); settle();
    assert(traps==expected_traps) else $fatal(1,"Zfa FS-off did not trap");
    // Both fixtures support Zfhmin; only the ordinary-latency fixture enables full Zfh.
    send(32'h000020b7); send(32'h30009073); settle(); // enable FS
    send(32'h00301073); settle(); // reset rounding/flags
    send(32'h0000c537); // x10=0xc000 (-2.0 half image)
    send(fp('h7a,0,10,0)); // fmv.h.x f0,x10: box into 64-bit FPR
    send(fp('h72,11,0,0));
    settle(); expect_gpr(11,64'hffffffffffffc000);
    expect_fpr(0,64'hffffffffffffc000);
    send(fp('h20,1,0,2)); // fcvt.s.h
    send(fp('h21,2,0,2)); // fcvt.d.h
    expect_fpr(1,64'hffffffffc0000000);
    expect_fpr(2,64'hc000000000000000);
    send(fp('h22,3,1,0)); // fcvt.h.s
    send(fp('h22,4,2,1)); // fcvt.h.d
    expect_fpr(3,64'hffffffffffffc000);
    expect_fpr(4,64'hffffffffffffc000);

    // Raw halfwords use every aligned byte lane; masked stores preserve their lane.
    for(int lane=0;lane<8;lane+=2) begin
      load_data=64'h3c00<<(8*lane);
      send((32'(256+lane)<<20)|(32'd1<<12)|(32'd5<<7)|32'h07); // flh f5
      expect_fpr(5,64'hffffffffffff3c00);
      send((32'd8<<25)|(32'd5<<20)|(32'd1<<12)|(32'(8+lane)<<7)|32'h27); // fsh f5,264+lane
      settle();
      assert(stored_width==1 && stored_mask==(8'h03<<lane) &&
             (stored_data & (64'hffff<<(8*lane)))==(64'h3c00<<(8*lane)))
        else $fatal(1,"FSH lost width/mask/data at lane %0d",lane);
    end
    slow_load=1; load_data=64'hbc00000000000000;
    send(32'h10601307); // flh f6,262(x0), delayed return
    send(fp('h20,7,6,2)); // dependent conversion waits for the load
    expect_fpr(7,64'hffffffffbf800000);
    slow_load=0;
    load_data=64'h3e00;
    send(32'h10701407); // misaligned flh f8,263(x0)
    settle(); expect_fpr(8,64'hffffffffffff3e00);
    send((32'd8<<25)|(32'd8<<20)|(32'd1<<12)|(32'd15<<7)|32'h27); // fsh f8,271(x0)
    settle();
    assert(split_count==4 && stored_data[15:0]==16'h3e00) else $fatal(1,"split half access lost data");

    // Narrowing observes frm, reports NX, and boxes the result; widening rejects bad boxes.
    send(fp('h79,1,17,1)); // fli.d 1.25
    send(fp('h22,2,1,1));
    expect_fpr(2,64'hffffffffffff3d00);
    load_data=64'h3ff0020000000000; // halfway between half 1.0 and its successor
    send(32'h10003087);
    send(32'h0021d073); settle(); // frm=RUP
    send(fp('h22,2,1,1,7));
    expect_fpr(2,64'hffffffffffff3c01);
    send(32'h00102673); settle(); expect_gpr(12,1);
    send(32'h00301073); settle();
    load_data=64'h0000000000003c00;
    send(32'h10003087);
    send(fp('h21,2,1,2));
    expect_fpr(2,64'h7ff8000000000000);

`ifdef RV2WIDE_ZFHMIN
    expected_traps++;
    send(fp('h02,2,0,0)); settle(); // arithmetic requires full Zfh
    assert(traps==expected_traps) else $fatal(1,"Zfhmin admitted half arithmetic");
    expected_traps++;
    send(fp('h7a,2,16,1)); settle(); // Zfa+Zfhmin does not enable FLI.H
    assert(traps==expected_traps) else $fatal(1,"Zfhmin admitted FLI.H");
`else
    send(fp('h7a,1,20,1)); // fli.h 2
    send(fp('h7a,2,22,1)); // fli.h 3
    send(fp('h7a,3,16,1)); // fli.h 1
    send(fp('h02,4,1,2),32'h00900593,1); // fadd.h 5 with integer peer
    expect_fpr(4,64'hffffffffffff4500); expect_gpr(11,9);
    send(32'h00a00593,fp('h06,4,1,2),1); // fsub.h -1 in younger slot
    expect_fpr(4,64'hffffffffffffbc00); expect_gpr(11,10);
    send(fp('h0a,4,1,2)); // fmul.h 6
    expect_fpr(4,64'hffffffffffff4600);
    send(fp('h0e,5,4,1)); // fdiv.h 3 (WB launch)
    expect_fpr(5,64'hffffffffffff4200);
    send(fp('h7a,6,23,1)); // 4
    send(fp('h2e,6,6,0)); // fsqrt.h 2 (WB launch)
    expect_fpr(6,64'hffffffffffff4000);
    for(int op=0;op<4;op++) begin
      send((32'd3<<27)|(32'd2<<25)|(32'd2<<20)|(32'd1<<15)|(32'd7<<7)|32'('h43+op*4));
      expect_fpr(7,op==0 ? 64'hffffffffffff4700 : op==1 ? 64'hffffffffffff4500 : op==2 ? 64'hffffffffffffc500 : 64'hffffffffffffc700);
    end
    send(fp('h12,4,1,2,0)); expect_fpr(4,64'hffffffffffff4000);
    send(fp('h12,4,1,2,1)); expect_fpr(4,64'hffffffffffffc000);
    send(fp('h12,4,1,4,2)); expect_fpr(4,64'hffffffffffffc000);
    send(fp('h16,4,1,2,0)); expect_fpr(4,64'hffffffffffff4000);
    send(fp('h16,4,1,2,1)); expect_fpr(4,64'hffffffffffff4200);
    for(int relation=0;relation<3;relation++) begin
      send(fp('h52,12,1,relation==2 ? 1 : 2,relation));
      settle(); expect_gpr(12,1);
    end
    send(fp('h72,12,1,0,1)); settle(); expect_gpr(12,64);
    send(32'h00900513); // integer 9
    for(int kind=0;kind<4;kind++) begin
      send(fp('h6a,8,10,kind)); // W/WU/L/LU -> half
      send(fp('h62,12,8,kind)); // half -> W/WU/L/LU
      settle(); expect_gpr(12,9);
      expect_fpr(8,64'hffffffffffff4880);
    end

    // Half Zfa shares the same decode/schedule, including NX and quiet comparisons.
    send(fp('h7a,1,18,1)); // 1.5
    send(fp('h7a,2,20,1)); // 2
    send(fp('h16,3,1,2,2)); expect_fpr(3,64'hffffffffffff3e00);
    send(fp('h16,3,1,2,3)); expect_fpr(3,64'hffffffffffff4000);
    send(fp('h22,3,1,4,1)); expect_fpr(3,64'hffffffffffff3c00); // fround.h RTZ
    send(32'h00102673); settle(); expect_gpr(12,0);
    send(fp('h22,3,1,5,0)); expect_fpr(3,64'hffffffffffff4000); // froundnx.h RNE
    send(32'h00102673); settle(); expect_gpr(12,1);
    send(32'h00101073); settle();
    send(fp('h7a,2,31,1)); // qNaN
    send(fp('h52,12,1,2,4)); settle(); expect_gpr(12,0);
    send(fp('h52,12,1,2,5)); settle(); expect_gpr(12,0);
    send(32'h00102673); settle(); expect_gpr(12,0);
    send(fp('h16,3,1,2,2)); expect_fpr(3,64'hffffffffffff7e00);

    // A faulting older peer must kill an EX half result and prevent a WB divide launch.
    send(fp('h7a,20,16,1)); // preserve 1
    send(32'h000040b7); send(32'h30009073); settle(); // FS=Clean
    inject_fault=1; inject_pc=next_pc; expected_traps++;
    send(32'h00100893,fp('h22,20,1,5),1); settle(); // killed NX result
    inject_pc=next_pc; expected_traps++;
    send(32'h00100893,fp('h0e,20,1,1),1); settle(); // killed divide
    inject_fault=0;
    send(32'h300026f3); settle();
    assert((gprs[13]&64'h6000)==64'h4000) else $fatal(1,"killed half operation dirtied FS");
    send(32'h00102673); settle(); expect_gpr(12,0);
    expect_fpr(20,64'hffffffffffff3c00);
`endif
    expected_traps++;
    send(fp('h22,4,1,1,5)); settle(); // reserved conversion rm
    assert(traps==expected_traps) else $fatal(1,"half reserved rounding mode did not trap");
    send(32'h30001073); settle(); // FS off rejects even half loads
    expected_traps++;
    send(32'h10001087); settle();
    assert(traps==expected_traps) else $fatal(1,"half FS-off did not trap");
    $display("RV2Wide F/D/half/Zfa mixed-latency, pairing, load/store, CSR and legality checks passed");
    $finish;
  end
endmodule
