// Checks cross-block assembly, credits, prediction/fault ownership, repairs, RAS actions, and compressed FP expansion.
// SPDX-License-Identifier: Apache-2.0
module rv2wide_assembly_prediction_tb;
  typedef struct packed { logic valid; logic [63:0] cause, value; logic [65:0] guest; } fault_t;
  typedef struct packed { logic valid; logic [63:0] pc, target; logic compressed; logic [1:0] ras_action; } prediction_t;
  typedef struct packed { logic valid; logic [11:0] index; logic [9:0] history; logic taken; } direction_t;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction, raw_instruction; logic [63:0] sequential_pc; logic compressed_illegal; fault_t fault; prediction_t prediction; logic [1:0] speculated_ras_action; direction_t direction; } instruction_t;
  typedef struct packed { logic [1:0] count; instruction_t [1:0] entries; } packet_t;
  typedef struct packed { logic valid; packet_t bits; } packet_flow_t;
  typedef struct packed { logic [63:0] pc, data; fault_t fault; prediction_t prediction; direction_t [3:0] directions; direction_t prefix_direction; } block_t;
  typedef struct packed { logic valid; block_t bits; } block_flow_t;
  typedef struct packed { logic [63:0] target; logic invalidate; logic [63:0] entry; logic [9:0] history; } repair_t;
  typedef struct packed { logic valid; repair_t bits; } repair_flow_t;
  typedef struct packed { logic [1:0] action; logic [63:0] return_address; } ras_update_t;
  typedef struct packed { logic valid; ras_update_t bits; } ras_flow_t;
  logic clock=0,reset=1,blocks_out,instructions_in=1,clear_in=0,pause=0;
  logic ras_head_valid=0;
  logic [63:0] ras_head=0;
  logic [1:0] available;
  block_flow_t blocks_in='0;
  packet_flow_t instructions_out;
  repair_flow_t repair_out;
  ras_flow_t speculate_out;
  logic [63:0] expected_pc[$];
  int repairs=0,speculations=0,retired=0;
  int return_speculations=0;
  RV2WideInstructionAssembler dut(.*,.discover_out());
  always #5 clock=~clock;
  always @(posedge clock) if(!reset) begin
    if(instructions_out.valid && instructions_in) for(int lane=0;lane<int'(instructions_out.bits.count);lane++) begin
      logic [63:0] want;
      assert(expected_pc.size()>0) else $fatal(1,"unexpected assembled PC %h",instructions_out.bits.entries[lane].pc);
      want=expected_pc.pop_front();
      assert(instructions_out.bits.entries[lane].pc==want) else $fatal(1,"assembled PC %h expected %h",instructions_out.bits.entries[lane].pc,want);
      if(want=='h706) assert(instructions_out.bits.entries[lane].fault.valid && instructions_out.bits.entries[lane].fault.value=='h708) else $fatal(1,"continuation fault owner");
      if(want=='h406) assert(instructions_out.bits.entries[lane].prediction.valid && instructions_out.bits.entries[lane].prediction.target=='h500 && instructions_out.bits.count==1) else $fatal(1,"straddle prediction/cut");
      if(want=='h806) assert(instructions_out.bits.entries[lane].instruction==32'h0fa0006f && instructions_out.bits.entries[lane].prediction.valid && instructions_out.bits.entries[lane].prediction.target=='h900 && instructions_out.bits.count==1) else $fatal(1,"upper-only predicted prefix was discarded");
      if(want=='hb00 || want=='hb80 || want=='hc00 || want=='hc40 || want=='hc80) begin
        logic [63:0] target;
        case(want)
          'hb00: target='hb40;
          'hb80: target='hba0;
          'hc00: target='hc20;
          default: target=want;
        endcase
        assert(instructions_out.bits.entries[lane].prediction.valid && instructions_out.bits.entries[lane].prediction.target==target && instructions_out.bits.count==1)
          else $fatal(1,"immediate target was not corrected pc=%h",want);
        assert(repair_out.valid && repair_out.bits.target==target && repair_out.bits.invalidate==(want!='hb00)) else $fatal(1,"missing immediate repair");
      end
      if(want=='hd00) assert(!instructions_out.bits.entries[lane].prediction.valid && !repair_out.valid) else $fatal(1,"conditional direction changed");
      if(want=='he00 || want=='he40 || want=='he86 || want=='hec0 || want=='hf00 || want=='hf40) begin
        logic [63:0] target;
        bit correction;
        target=want=='hec0 ? 'hdead : 'h1234;
        correction=want!='hec0 && want!='hf00;
        assert(instructions_out.bits.entries[lane].prediction.valid && instructions_out.bits.entries[lane].prediction.target==target && instructions_out.bits.count==1)
          else $fatal(1,"return did not refresh live RAS at %h",want);
        assert(repair_out.valid==correction) else $fatal(1,"wrong return repair at %h",want);
        if(correction) assert(repair_out.bits.target==target && !repair_out.bits.invalidate) else $fatal(1,"return repair invalidated context-independent BTB entry");
      end
      if(want>='ha00 && want<'ha08) begin
        logic [31:0] canonical;
        logic [15:0] raw;
        case(want)
          'ha00: begin raw=16'h2000; canonical=32'h00043407; end // C.FLD f8,0(x8)
          'ha02: begin raw=16'ha004; canonical=32'h00943027; end // C.FSD f9,0(x8)
          'ha04: begin raw=16'h2002; canonical=32'h00013007; end // C.FLDSP f0,0(sp)
          default: begin raw=16'ha006; canonical=32'h00113027; end // C.FSDSP f1,0(sp)
        endcase
        assert(!instructions_out.bits.entries[lane].compressed_illegal && instructions_out.bits.entries[lane].instruction==canonical &&
               instructions_out.bits.entries[lane].raw_instruction==32'(raw) && instructions_out.bits.entries[lane].sequential_pc==want+2)
          else $fatal(1,"configured D compressed expansion pc=%h",want);
      end
      retired++;
    end
    if(repair_out.valid) begin
      repairs++;
      if(repairs==1) assert(repair_out.bits.invalidate && repair_out.bits.entry=='h104 && repair_out.bits.target=='h106) else $fatal(1,"middle-instruction repair");
      if(repairs==2) assert(repair_out.bits.invalidate && repair_out.bits.entry=='h200 && repair_out.bits.target=='h240) else $fatal(1,"stale length/direct fallback");
    end
    if(speculate_out.valid) begin
      if(speculate_out.bits.action==1) begin
        speculations++;
        assert(speculate_out.bits.return_address==64'('h600+speculations*4)) else $fatal(1,"RAS action owner/count");
      end else begin
        return_speculations++;
        assert(speculate_out.bits.action==(return_speculations==6 ? 3 : 2)) else $fatal(1,"return action changed");
      end
    end
  end
  task automatic offer(logic [63:0] pc,data,pred_pc=0,pred_target=0,bit compressed=0,fault=0,
                       direction_t [3:0] directions='0,direction_t prefix='0);
    @(negedge clock);
    blocks_in='{1'b1,'{pc,data,'{fault,64'd1,pc,'0},'{pred_pc!=0,pred_pc,pred_target,compressed,2'd0},directions,prefix}};
    do @(posedge clock); while(!blocks_out);
    @(negedge clock); blocks_in='0;
  endtask
  task automatic drain;
    wait(expected_pc.size()==0); repeat(3) @(negedge clock);
  endtask
  task automatic clear;
    @(negedge clock); clear_in=1;
    @(negedge clock); clear_in=0;
  endtask
  initial begin
    repeat(3) @(negedge clock); reset=0;
    expected_pc.push_back('h100); expected_pc.push_back('h102);
    offer('h100,{16'h0001,32'h00700513,16'h0001},'h104,'h800,1); drain();
    expected_pc.push_back('h200);
    offer('h200,{32'h00100013,32'h0400006f},'h200,'h900,1); drain();
    expected_pc.push_back('h300); expected_pc.push_back('h302); expected_pc.push_back('h304);
    offer('h300,{16'h0001,16'ha001,16'h0001,16'h0001},'h304,'h304,1); drain();
    assert(repairs==2) else $fatal(1,"correct prediction repaired or suffix escaped");
    expected_pc.push_back('h400); expected_pc.push_back('h402); expected_pc.push_back('h404);
    expected_pc.push_back('h406);
    // JAL x0,+250 = 0x0fa0006f, split at the last parcel.
    offer('h400,{16'h006f,16'h0001,16'h0001,16'h0001},'h406,'h500);
    offer('h408,{16'h0001,16'h0001,16'h0001,16'h0fa0},'h406,'h500); drain();
    assert(repairs==2) else $fatal(1,"correct straddle repaired");
    // Restart directly at the incomplete prefix: prediction must not cut it
    // before the continuation makes the instruction complete.
    expected_pc.push_back('h806);
    offer('h806,{16'h006f,16'h0001,16'h0001,16'h0001},'h806,'h900);
    offer('h808,{16'h0001,16'h0001,16'h0001,16'h0fa0},'h806,'h900); drain();
    assert(repairs==2) else $fatal(1,"correct upper-only straddle repaired");
    expected_pc.push_back('h600); expected_pc.push_back('h604);
    instructions_in=0;
    offer('h600,{32'h000180e7,32'h000180e7});
    repeat(4) @(negedge clock);
    assert(speculations==0) else $fatal(1,"stalled call changed RAS");
    instructions_in=1; drain();
    assert(speculations==2) else $fatal(1,"two calls did not serialize stack actions");
    clear();
    expected_pc.push_back('h706);
    offer('h706,64'h0613000000000000);
    offer('h708,0,0,0,0,1); drain();
    clear();
    for(int p='ha00;p<'ha08;p+=2) expected_pc.push_back(64'(p));
    offer('ha00,{16'ha006,16'h2002,16'ha004,16'h2000}); drain();
    expected_pc.push_back('hb00);
    offer('hb00,{32'h00100013,32'h0400006f}); drain();
    expected_pc.push_back('hb80);
    instructions_in=0;
    offer('hb80,{32'h00100013,32'h0200006f},'hb80,'hd00);
    repeat(4) @(negedge clock);
    assert(!repair_out.valid) else $fatal(1,"unaccepted jump repaired");
    instructions_in=1; drain();
    expected_pc.push_back('hc00);
    offer('hc00,{32'h00100013,32'h02000063},'hc00,'hd00); drain();
    expected_pc.push_back('hc40);
    offer('hc40,{48'h000100010001,16'hc001},'hc40,'hd00,1); drain();
    expected_pc.push_back('hc80);
    offer('hc80,{48'h000100010001,16'ha001},'hc80,'hd00,1); drain();
    expected_pc.push_back('hd00); expected_pc.push_back('hd04);
    offer('hd00,{32'h00100013,32'h00000063}); drain();
    assert(repairs==7 && speculations==2) else $fatal(1,"repair/RAS count changed");
    // Warm BTB targets were frozen before an older call updated the RAS.
    ras_head_valid=1; ras_head='h1234;
    expected_pc.push_back('he00);
    offer('he00,{32'h00100013,32'h00008067},'he00,'hdead); drain();
    expected_pc.push_back('he40);
    instructions_in=0; ras_head='h1111;
    offer('he40,{48'h000100010001,16'h8082},'he40,'hdead,1);
    repeat(4) @(negedge clock);
    assert(return_speculations==1 && !repair_out.valid) else $fatal(1,"stalled return changed RAS/cursor");
    ras_head='h1234; instructions_in=1; drain();
    expected_pc.push_back('he86);
    offer('he86,{16'h8067,48'h000100010001},'he86,'hdead);
    offer('he88,{48'h000100010001,16'h0000}); drain();
    ras_head_valid=0;
    expected_pc.push_back('hec0);
    offer('hec0,{32'h00100013,32'h00008067},'hec0,'hdead); drain();
    ras_head_valid=1;
    expected_pc.push_back('hf00);
    offer('hf00,{32'h00100013,32'h00008067},'hf00,'h1234); drain();
    expected_pc.push_back('hf40);
    offer('hf40,{32'h00100013,32'h000082e7},'hf40,'hdead); drain(); // JALR x5,x1: pop-push.
    assert(repairs==11 && return_speculations==6) else $fatal(1,"return repair/action count changed");
    // A buffered next block fills either output slot, including a straddling
    // first or second instruction. Direction ownership follows each start PC.
    for(int offset=0;offset<4;offset++) for(int first_c=0;first_c<2;first_c++) for(int second_c=0;second_c<2;second_c++) begin
      logic [127:0] bytes;
      logic [63:0] base,pc,second_pc;
      logic [31:0] first_raw,second_raw;
      direction_t [3:0] first_directions,second_directions;
      direction_t prefix;
      packet_t held;
      int first_bytes,second_bytes,second_parcel;
      clear(); instructions_in=0;
      base='h10000+64'(int'((offset*4+first_c*2+second_c)*32)); pc=base+64'(offset*2);
      first_bytes=first_c!=0?2:4; second_bytes=second_c!=0?2:4; second_pc=pc+64'(first_bytes);
      first_raw=first_c!=0?32'h00000001:32'h00100013;
      second_raw=second_c!=0?32'h00000001:32'h00200013;
      for(int parcel=0;parcel<8;parcel++) bytes[parcel*16+:16]=16'h0001;
      bytes[offset*16+:32]=first_raw;
      if(first_c!=0) bytes[(offset+1)*16+:16]=16'h0001;
      second_parcel=offset+first_bytes/2;
      bytes[second_parcel*16+:32]=second_raw;
      if(second_c!=0) bytes[(second_parcel+1)*16+:16]=16'h0001;
      for(int parcel=0;parcel<4;parcel++) begin
        first_directions[parcel]='{1'b1,12'(parcel),10'd17,1'b0};
        second_directions[parcel]='{1'b1,12'(4+parcel),10'd29,1'b0};
      end
      prefix='{1'b1,12'd3,10'd17,1'b0};
      expected_pc.push_back(pc); expected_pc.push_back(second_pc);
      offer(pc,bytes[63:0],0,0,0,0,first_directions);
      offer(base+8,bytes[127:64],0,0,0,0,second_directions,prefix);
      assert(instructions_out.valid && instructions_out.bits.count==2) else $fatal(1,"cross-block pair missing offset=%0d sizes=%0d/%0d",offset,first_bytes,second_bytes);
      assert(instructions_out.bits.entries[0].raw_instruction==first_raw && instructions_out.bits.entries[1].raw_instruction==second_raw)
        else $fatal(1,"cross-block bytes changed");
      for(int lane=0;lane<2;lane++) begin
        int parcel,size;
        parcel=lane==0?offset:second_parcel; size=lane==0?first_bytes:second_bytes;
        assert(instructions_out.bits.entries[lane].direction.history==(parcel<4?10'd17:10'd29)) else $fatal(1,"cross-block direction history owner");
        assert(instructions_out.bits.entries[lane].direction.index==12'(parcel)) else $fatal(1,"cross-block direction index owner");
        assert(instructions_out.bits.entries[lane].sequential_pc==instructions_out.bits.entries[lane].pc+64'(size)) else $fatal(1,"cross-block length changed");
      end
      held=instructions_out.bits;
      repeat(4) begin @(negedge clock); assert(instructions_out.valid && instructions_out.bits==held && available==1) else $fatal(1,"stalled window changed"); end
      instructions_in=1; #1;
      assert(available==2'(1+(offset*2+first_bytes+second_bytes)/8)) else $fatal(1,"consumed block credit was not immediately reusable");
      @(posedge clock); @(negedge clock); instructions_in=0;
      assert(expected_pc.size()==0) else $fatal(1,"cross-block packet did not transfer");
    end
    // An unavailable second block never delays a complete first instruction.
    clear(); instructions_in=0;
    expected_pc.push_back('h11006);
    offer('h11006,64'h0001000100010001);
    assert(instructions_out.valid && instructions_out.bits.count==1) else $fatal(1,"complete instruction waited for lookahead");
    instructions_in=1; @(posedge clock); @(negedge clock); instructions_in=0;
    // A fault in a second instruction's continuation preserves the first
    // instruction and reports the straddling PC with the continuation fault VA.
    clear();
    expected_pc.push_back('h11104); expected_pc.push_back('h11106);
    offer('h11104,{16'h0013,16'h0001,32'h00010001});
    offer('h11108,0,0,0,0,1);
    assert(instructions_out.valid && instructions_out.bits.count==2 && !instructions_out.bits.entries[0].fault.valid &&
           instructions_out.bits.entries[1].fault.valid && instructions_out.bits.entries[1].fault.value=='h11108)
      else $fatal(1,"second-slot continuation fault ownership");
    instructions_in=1; @(posedge clock); @(negedge clock); instructions_in=0;
    // Drain all three resident blocks as a continuous parcel stream, rather
    // than emitting a singleton at every eight-byte boundary.
    clear();
    for(int parcel=0;parcel<9;parcel++) expected_pc.push_back('h11206+64'(parcel*2));
    offer('h11206,64'h0001000100010001);
    offer('h11208,64'h0001000100010001);
    offer('h11210,64'h0001000100010001);
    assert(available==0 && !blocks_out) else $fatal(1,"full block window did not retain backpressure");
    instructions_in=1; #1;
    assert(available==1) else $fatal(1,"full window release credit unavailable");
    while(expected_pc.size()>0) begin
      assert(instructions_out.valid && instructions_out.bits.count==(expected_pc.size()>1?2'd2:2'd1))
        else $fatal(1,"block boundary broke sustained assembly");
      @(posedge clock); @(negedge clock);
    end
    instructions_in=0;
    // A taken branch in slot one can release both contributing blocks. Neither
    // fallthrough bytes nor a following noncontiguous target enter the packet.
    clear();
    expected_pc.push_back('h11306); expected_pc.push_back('h11308);
    offer('h11306,64'h0001000100010001);
    offer('h11308,{32'h00100013,32'h0400006f},'h11308,'h11348);
    assert(instructions_out.valid && instructions_out.bits.count==2 && instructions_out.bits.entries[1].prediction.valid && !repair_out.valid)
      else $fatal(1,"cross-block taken suffix lost prediction");
    instructions_in=1; #1;
    assert(available==3) else $fatal(1,"taken suffix did not release both block credits");
    @(posedge clock); @(negedge clock); instructions_in=0;
    expected_pc.push_back('h1134e);
    offer('h1134e,64'h0001000100010001);
    offer('h12000,64'h0001000100010001);
    assert(instructions_out.valid && instructions_out.bits.count==1) else $fatal(1,"assembled across noncontiguous blocks");
    instructions_in=1; @(posedge clock); @(negedge clock); instructions_in=0;
    // Fresh continuation direction can override the prefix's provisional BTB
    // taken prediction without inventing an assembler repair.
    clear();
    expected_pc.push_back('h12106); expected_pc.push_back('h1210a);
    offer('h12106,{16'h0063,48'h000100010001},'h12106,'h12306);
    offer('h12108,{48'h000100010001,16'h2000},0,0,0,0,'0,'{1'b1,12'h123,10'd41,1'b0});
    assert(instructions_out.valid && instructions_out.bits.count==2 && !instructions_out.bits.entries[0].prediction.valid &&
           instructions_out.bits.entries[0].direction.valid && !repair_out.valid)
      else $fatal(1,"not-taken continuation repaired a recognized branch");
    instructions_in=1; @(posedge clock); @(negedge clock); instructions_in=0;
    clear(); instructions_in=1;
    $display("RV2Wide assembly prediction repair/cut/straddle/RAS tests passed (%0d instructions)",retired); $finish;
  end
  initial begin #10000; $fatal(1,"assembly timeout"); end
endmodule
