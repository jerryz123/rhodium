// Drives independent next-PC/RAS predictions, squash/replay/traps, and retained retirement through public core ports.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_retirement_trace_tb;
  import "DPI-C" function void retirement_trace_init();
  import "DPI-C" function void retirement_trace_expect(longint unsigned pc, int unsigned instruction, int unsigned prediction, int unsigned ras_mismatch);
  import "DPI-C" function void retirement_trace_check(int unsigned reset);
  import "DPI-C" function void retirement_trace_pending(int unsigned count);
  import "DPI-C" function void retirement_trace_finish();
  logic clock=0, reset=1, packet_valid=0, packet_ready, predicted=0;
  logic [63:0] pc=0, target=0, data_address, data_value;
  logic [31:0] instruction=0;
  logic [1:0] ras=0;
  logic data_ready=1, data_fault=0, response_valid=0, response_fault=0, reservation=0, data_valid;
  logic [8:0] response_tag=0, data_tag, saved_tag=0;
  RV5StageRetirementTrace dut(.*);
  task automatic tick;
    #5; if (data_valid && data_ready) saved_tag=data_tag;
    clock=1; #1; retirement_trace_check(int'(reset)); #4; clock=0; #1;
  endtask
  task automatic idle(input int cycles=8);
    repeat(cycles) tick();
  endtask
  task automatic packet(input longint unsigned address, input logic [31:0] word,
                        input bit prediction=0, input longint unsigned destination=0, input logic [1:0] ras_action=0);
    int waited;
    pc=address; instruction=word; predicted=prediction; target=destination; ras=ras_action; packet_valid=1;
    waited=0; #1;
    while (!packet_ready) begin tick(); waited++; if (waited>100) $fatal(1,"packet admission timeout"); end
    tick(); packet_valid=0;
  endtask
  task automatic retire(input longint unsigned address, input logic [31:0] word,
                        input int status=0, input bit prediction=0, input longint unsigned destination=0,
                        input bit ras_mismatch=0, input logic [1:0] ras_action=0);
    retirement_trace_expect(address,word,status,int'(ras_mismatch));
    packet(address,word,prediction,destination,ras_action); idle(); retirement_trace_pending(0);
  endtask
  task automatic respond(input bit fault=0);
    response_tag=saved_tag; response_fault=fault; response_valid=1; tick(); response_valid=0; idle();
  endtask
  initial begin
    retirement_trace_init(); idle(3); reset=0; idle(3);
    retire('h0,32'h08000093); // x1=0x80, indirect target
    retire('h10,32'h00000463,2); // cold taken BEQ, predicted fallthrough
    retire('h10,32'h00000463,1,1,'h18); // same PC, trained taken target
    retire('h20,32'h00001463,1); // correctly not-taken BNE
    retire('h20,32'h00001463,2,1,'h28); // loop-exit style direction miss
    retire('h30,32'h00000463,2,1,'h3c); // correct direction, wrong target
    retire('h40,32'h0080006f,1,1,'h48); // direct JAL
    retire('h50,32'h00008067,1,1,'h80,1); // return target right, RAS action None is wrong
    retire('h50,32'h00008067,2,1,'h84,1); // target and RAS action both wrong
    retire('h50,32'h00008067,1,1,'h80,0,2); // target and Pop action both right
    retire('h50,32'h00008067,2,1,'h84,0,2); // target wrong, Pop action right
    retire('h54,32'h008002ef,1,1,'h5c,1,2); // JAL x5 needs Push, not Pop
    retire('h54,32'h008002ef,1,1,'h5c,0,1); // correctly predicted Push
    retire('h58,32'h000082e7,1,1,'h80,1,1); // JALR x5,x1 needs PopPush, not Push
    retire('h58,32'h000082e7,1,1,'h80,0,3); // correctly predicted PopPush
    retire('h5c,32'h00008082,1,1,'h80,1); // C.JR ra needs Pop
    retire('h5c,32'h00008082,1,1,'h80,0,2); // compressed return with matching action
    retire('h64,32'h00000013,0,1,'h68,1,1); // stale Push on a nonbranch
    retire('h60,32'h0000a001,1,1,'h60); // C.J self, raw compressed capture
    retire('h60,32'h0000a001,2); // compressed fallthrough is PC+2
    retirement_trace_expect('h70,32'h00000463,2,0);
    packet('h70,32'h00000463); packet('h74,32'h00100113); // younger wrong-path ADDI
    idle(); retirement_trace_pending(0);
    // Rejected dispatch must not retire. Reissue the identical PC and bits once accepted.
    data_ready=0; packet('h80,32'h00003203,1,'h84,1); idle(); retirement_trace_pending(0);
    data_ready=1; retire('h80,32'h00003203); respond();
    // CSR-side traps (not just explicit EX faults) must not create a WB event.
    packet('h90,32'h00000073); idle(); retirement_trace_pending(0); // ECALL
    packet('h94,32'hfff022f3,1,'h98,1); idle(); retirement_trace_pending(0); // nonexistent CSR, stale RAS action
    data_fault=1; packet('h98,32'h00003203); idle(); retirement_trace_pending(0);
    data_fault=0;
    retire('h9c,32'h30200073); // MRET redirects but successfully retires
    // Pending WRS retains its original MEM occurrence, and emits only on wake.
    reservation=1; retirement_trace_expect('ha0,32'h00d00073,0,1);
    packet('ha0,32'h00d00073,1,'ha4,1); idle(12); retirement_trace_pending(1);
    reservation=0; idle(); retirement_trace_pending(0);
    // Accepted CMO retires on completion, not on request admission.
    retirement_trace_expect('hb0,32'h0010200f,0,1); // cbo.clean (x0), stale RAS action retained
    packet('hb0,32'h0010200f,1,'hb4,1); idle(12); retirement_trace_pending(1);
    respond(); retirement_trace_pending(0);
    packet('hb4,32'h0010200f); idle(12); retirement_trace_pending(0);
    respond(1); retirement_trace_pending(0); // completion fault is a trap
    retire('hc0,32'h00100113);
    retire('hc4,32'h00200193);
    retire('hc8,32'h00300213);
    retire('hcc,32'h00400293);
    retirement_trace_finish(); $finish;
  end
endmodule
