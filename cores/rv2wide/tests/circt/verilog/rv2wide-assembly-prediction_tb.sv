// Checks accepted immediate-target repairs, packet cuts, straddles, RAS actions, and compressed FP expansion.
// SPDX-License-Identifier: Apache-2.0
module rv2wide_assembly_prediction_tb;
  typedef struct packed { logic valid; logic [63:0] cause, value; } fault_t;
  typedef struct packed { logic valid; logic [63:0] pc, target; logic compressed; logic [1:0] ras_action; } prediction_t;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction, raw_instruction; logic [63:0] sequential_pc; logic compressed_illegal; fault_t fault; prediction_t prediction; logic [1:0] speculated_ras_action; } instruction_t;
  typedef struct packed { logic [1:0] count; instruction_t [1:0] entries; } packet_t;
  typedef struct packed { logic valid; packet_t bits; } packet_flow_t;
  typedef struct packed { logic [63:0] pc, data; fault_t fault; prediction_t prediction; } block_t;
  typedef struct packed { logic valid; block_t bits; } block_flow_t;
  typedef struct packed { logic [63:0] target; logic invalidate; logic [63:0] entry; } repair_t;
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
      speculations++;
      assert(speculate_out.bits.action==1 && speculate_out.bits.return_address==64'('h600+speculations*4)) else $fatal(1,"RAS action owner/count");
    end
  end
  task automatic offer(logic [63:0] pc,data,pred_pc=0,pred_target=0,bit compressed=0,fault=0);
    @(negedge clock);
    blocks_in='{1'b1,'{pc,data,'{fault,64'd1,pc},'{pred_pc!=0,pred_pc,pred_target,compressed,2'd0}}};
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
    $display("RV2Wide assembly prediction repair/cut/straddle/RAS tests passed (%0d instructions)",retired); $finish;
  end
  initial begin #10000; $fatal(1,"assembly timeout"); end
endmodule
