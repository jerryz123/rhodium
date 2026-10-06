// Exercises real-core scalar observation with delayed work, replay, traps, interrupts and reset.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_cosim_tb;
  import "DPI-C" function void core_cosim_reset(longint epoch, int xlen);
  import "DPI-C" function void core_cosim_expect(longint pc, int instruction, int rd, longint value, int cause, longint tval, longint next_pc, int is_interrupt);
  import "DPI-C" function void core_cosim_memory(int kind, longint address, int bytes, int read, int write, longint read_data, longint write_data, int result);
  import "DPI-C" function void core_cosim_begin(longint sample, int is_interrupt);
  import "DPI-C" function void core_cosim_end();
  import "DPI-C" function void core_cosim_pending(int count);
  import "DPI-C" function void core_cosim_finish();
  import "DPI-C" function void core_cosim_memory_offset(int offset);
  logic clock=0, reset=1, packet_valid=0, packet_ready;
  logic [63:0] epoch=0, ticks=0, pc=0, data_address, data_value, response_data=0, fault_address=0;
  logic [31:0] instruction=0;
  logic data_ready=1, data_fault=0, page_fault=0, response_valid=0, response_fault=0, reservation=0, mswi=0, drained=1, data_valid, response_ready;
  logic [8:0] response_tag=0, data_tag;
  logic [7:0] architectural_width;
  logic [1:0] pipeline_mode=0;
  logic [63:0] pipeline_data=0;
  logic [8:0] requests[$];
  RV5StageCosimFixture dut(.*);
  task automatic tick;
    #5;
    if (!reset && data_valid && data_ready && !data_fault && !page_fault) requests.push_back(data_tag);
    core_cosim_begin(ticks, int'(mswi));
    clock=1; #1; core_cosim_end(); #4; clock=0; ticks++; #1;
  endtask
  task automatic idle(input int cycles=10);
    repeat(cycles) tick();
  endtask
  task automatic packet(input longint address, input logic [31:0] word);
    int waited;
    pc=address; instruction=word; packet_valid=1; waited=0; #1;
    while (!packet_ready) begin tick(); waited++; if (waited>200) $fatal(1,"packet timeout"); end
    tick(); packet_valid=0;
  endtask
  task automatic expect_instruction(input longint address, input logic [31:0] word, input int rd=0, input longint value=0);
    core_cosim_expect(address, int'(word), rd, value, -1, 0, address+((word[1:0]==3)?4:2), 0);
  endtask
  task automatic retire(input longint address, input logic [31:0] word, input int rd=0, input longint value=0);
    expect_instruction(address,word,rd,value); packet(address,word); idle(); core_cosim_pending(0);
  endtask
  task automatic respond(input longint value=0, input bit fault=0);
    int waited;
    if (requests.size()==0) $fatal(1,"missing request");
    response_tag=requests.pop_front(); response_data=value; response_fault=fault; response_valid=1; waited=0; #1;
    while (!response_ready) begin tick(); waited++; if(waited>200) $fatal(1,"response timeout"); end
    tick(); response_valid=0; response_fault=0; idle();
  endtask
  initial begin
    #1; core_cosim_reset(epoch,int'(architectural_width)); idle(3); reset=0; idle(3);
    retire('h00,32'h00700093,1,7); // x1=7
    retire('h04,32'h00900113,2,9); // x2=9
    expect_instruction('h08,32'h022081b3,3,63); // MUL, deferred result
    expect_instruction('h0c,32'h00100213,4,1);
    packet('h08,32'h022081b3); packet('h0c,32'h00100213); idle(100); core_cosim_pending(0);
    expect_instruction('h10,32'h0211c2b3,5,9); // DIV x5,x3,x1
    expect_instruction('h14,32'h00200313,6,2);
    packet('h10,32'h0211c2b3); packet('h14,32'h00200313); idle(100); core_cosim_pending(0);
    // Older memory delays publication of a younger, already-complete ADDI.
    drained=0;
    expect_instruction('h18,32'h00002383,7,'h12345678);
    core_cosim_memory(0,0,15,1,0,'h12345678,0,0);
    packet('h18,32'h00002383);
    expect_instruction('h1c,32'h00300413,8,3); packet('h1c,32'h00300413);
    idle(); core_cosim_pending(2); respond('h12345678); drained=1; core_cosim_pending(0);
    // Two x0 loads must retain separate owners even though their destination tags match.
    expect_instruction('h20,32'h00402003); core_cosim_memory(0,4,15,1,0,11,0,0); packet('h20,32'h00402003); idle();
    expect_instruction('h24,32'h00802003); core_cosim_memory(0,8,15,1,0,22,0,0); packet('h24,32'h00802003); idle();
    core_cosim_pending(2); respond(11); core_cosim_pending(1); respond(22); core_cosim_pending(0);
    // Rejected WB dispatch is not a new architectural instruction.
    data_ready=0; packet('h28,32'h00c02483); packet('h2c,32'h022089b3);
    idle(100); core_cosim_pending(0); data_ready=1; // younger EX multiply is canceled
    expect_instruction('h28,32'h00c02483,9,33); core_cosim_memory(0,12,15,1,0,33,0,0);
    packet('h28,32'h00c02483); idle(); respond(33); core_cosim_pending(0);
    expect_instruction('h2c,32'h00102823); core_cosim_memory(1,16,15,0,1,0,7,0);
    packet('h2c,32'h00102823); idle(); respond(); core_cosim_pending(0);
    // Hit paths complete at WB, without a slow-service owner or response.
    pipeline_mode=1; pipeline_data=77;
    expect_instruction('h180,32'h00002a03,20,77); core_cosim_memory(0,0,15,1,0,77,0,0);
    packet('h180,32'h00002a03); idle(); core_cosim_pending(0);
    pipeline_mode=2;
    expect_instruction('h184,32'h00102023); core_cosim_memory(1,0,15,0,1,0,7,0);
    packet('h184,32'h00102023); idle(); core_cosim_pending(0); pipeline_mode=0;
    if(requests.size()!=0) $fatal(1,"hit used slow service");
    core_cosim_expect('h188,32'h0080006f,0,0,-1,0,'h190,0);
    packet('h188,32'h0080006f); idle(); core_cosim_pending(0);
    // LR, successful/failed SC, and an AMO's old and updated memory values.
    expect_instruction('h190,32'h10002a2f,20,42); core_cosim_memory(3,0,15,1,0,42,0,0);
    packet('h190,32'h10002a2f); idle(); respond(42); core_cosim_pending(0);
    expect_instruction('h194,32'h18102aaf,21,0); core_cosim_memory(4,0,15,0,1,0,7,0);
    packet('h194,32'h18102aaf); idle(); respond(0); core_cosim_pending(0);
    expect_instruction('h198,32'h18102aaf,21,1); core_cosim_memory(4,0,15,0,0,0,0,2);
    packet('h198,32'h18102aaf); idle(); respond(1); core_cosim_pending(0);
    expect_instruction('h19c,32'h00202b2f,22,42); core_cosim_memory(2,0,15,1,1,42,51,0);
    packet('h19c,32'h00202b2f); idle(); respond(42); core_cosim_pending(0);
    // Compressed encoding and x0's lack of a GPR effect.
    retire('h30,32'h00000001);
    retire('h34,32'h12300513,10,'h123);
    retire('h38,32'h340515f3,11,0); // CSRRW mscratch
    retire('h3c,32'h34002673,12,'h123); // CSRRS read
    retire('h40,32'h10300693,13,'h103);
    retire('h44,32'h30569073); // WARL mtvec => 0x100
    core_cosim_expect('h48,32'h00000073,0,0,11,0,'h100,0);
    packet('h48,32'h00000073); idle(); core_cosim_pending(0);
    // Preserve the MMU's second-page fault address.
    retire('hf0,32'h000026b7,13,'h2000);
    retire('hf4,32'hfff68693,13,'h1fff);
    page_fault=1; fault_address='h2000;
    core_cosim_expect('h100,32'h0006a703,0,0,13,'h2000,'h100,0);
    core_cosim_memory(0,'h2000,7,0,0,0,0,1); core_cosim_memory_offset(1);
    packet('h100,32'h0006a703); idle(); page_fault=0; core_cosim_pending(0);
    // A synchronous trap waits for older load completion without allocating again.
    drained=0;
    expect_instruction('h104,32'h00002783,15,55); core_cosim_memory(0,0,15,1,0,55,0,0);
    packet('h104,32'h00002783); idle();
    core_cosim_expect('h108,32'hffffffff,0,0,2,64'hffffffff,'h100,0); packet('h108,32'hffffffff);
    idle(); core_cosim_pending(2); respond(55); drained=1; idle(); core_cosim_pending(0);
    // Pending WRS and maintenance retain the initial WB owner.
    reservation=1; expect_instruction('h110,32'h00d00073); packet('h110,32'h00d00073);
    idle(); core_cosim_pending(1); reservation=0; idle(); core_cosim_pending(0);
    expect_instruction('h114,32'h0010200f); core_cosim_memory(5,0,255,0,0,0,0,0);
    packet('h114,32'h0010200f); idle(); core_cosim_pending(1); respond(); core_cosim_pending(0);
    core_cosim_expect('h1a0,32'h0010200f,0,0,7,0,'h100,0); core_cosim_memory(5,0,255,0,0,0,0,1);
    packet('h1a0,32'h0010200f); idle(); respond(0,1); core_cosim_pending(0);
    core_cosim_expect('h1a4,32'h30200073,0,0,-1,0,'h1a0,0);
    packet('h1a4,32'h30200073); idle(); core_cosim_pending(0);
    // Enable machine software interrupts, then take one at an empty boundary.
    retire('h118,32'h00800813,16,8);
    retire('h11c,32'h30481073); // mie = MSIE
    retire('h120,32'h30082073); // csrrs x0,mstatus,x16
    core_cosim_expect('h124,0,0,0,3,0,'h100,1); mswi=1; idle(); mswi=0; core_cosim_pending(0);
    // Reset abandons an accepted unfinished load, then order zero can be reused in a new epoch.
    expect_instruction('h128,32'h00002883,17,0); core_cosim_memory(0,0,15,1,0,0,0,0);
    packet('h128,32'h00002883); idle(); core_cosim_pending(1);
    reset=1; epoch=1; requests.delete(); core_cosim_reset(epoch,int'(architectural_width)); idle(3);
    reset=0; idle(3); retire(0,32'hfff00093,1,-1);
    core_cosim_finish(); $finish;
  end
endmodule
