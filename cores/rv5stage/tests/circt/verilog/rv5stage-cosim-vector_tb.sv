// SPDX-License-Identifier: Apache-2.0
module rv5stage_cosim_vector_tb;
  import "DPI-C" function void vector_cosim_reset(longint epoch);
  import "DPI-C" function int vector_cosim_count();
  import "DPI-C" function int vector_cosim_instruction(int index);
  import "DPI-C" function void vector_cosim_begin(longint sample);
  import "DPI-C" function void vector_cosim_end();
  import "DPI-C" function void vector_cosim_finish();
  logic clock=0, reset=1, packet_valid=0, packet_ready;
  logic [63:0] ticks=0, pc=0, data_address, data_value, response_data=0, fault_address=0;
  logic [31:0] instruction=0;
  logic data_ready=1, data_fault=0, page_fault=0, response_valid=0, response_fault=0, reservation=0, mswi=0, drained=1, data_valid, response_ready;
  logic [8:0] response_tag=0, data_tag;
  logic [7:0] architectural_width;
  logic [1:0] pipeline_mode=0;
  logic [63:0] pipeline_data=0;
  logic restart_valid;
  logic [63:0] restart_pc;
  logic certify=0, data_store, lookup_request, lookup_store;
  logic [7:0] data_mask;
  logic [63:0] lookup_address;
  localparam logic [63:0] BASE=64'h80000000;
  byte unsigned ram[4096];
  typedef struct packed {logic [8:0] tag; logic [63:0] data; logic [63:0] due;} Pending;
  Pending pending[$];
  int requests=0, blocked=0, hits=0;
  bit stall_next=1;
  int cursor;
  RV5StageCosimFixture dut(.*);
  task automatic tick;
    logic request_fire, response_fire;
    Pending accepted;
    #1;
    data_ready=(pending.size()<4) && !stall_next;
    drained=pending.size()==0;
    response_valid=pending.size()!=0 && pending[0].due<=ticks;
    response_tag=response_valid ? pending[0].tag : 0;
    response_data=response_valid ? pending[0].data : 0;
    pipeline_mode=(lookup_request && !lookup_store && ticks%3==0) ? 1 : 0;
    pipeline_data=0;
    if(lookup_request) for(int b=0;b<8;b++) begin
      if(lookup_address>=BASE && lookup_address+64'(b)<BASE+4096)
        pipeline_data[8*b+:8]=ram[int'(lookup_address-BASE)+b];
    end
    #5;
    if(!reset && lookup_request && pipeline_mode==1) hits++;
    if(!reset && data_valid && !data_ready) begin
      blocked++;
      // Reject an offer once, then allow its retry. A periodic clock-based
      // rejection can alias the retry interval and unfairly reject it forever.
      if(pending.size()<4) stall_next=0;
    end
    request_fire=!reset && data_valid && data_ready;
    response_fire=!reset && response_valid && response_ready;
    accepted='0;
    if(request_fire) begin
      if(data_address<BASE || data_address+8>BASE+4096) $fatal(1,"memory address outside fixture RAM");
      requests++;
      stall_next=(requests%3==0);
      accepted.tag=data_tag; accepted.due=ticks+9;
      for(int b=0;b<8;b++) begin
        accepted.data[8*b+:8]=ram[int'(data_address-BASE)+b];
        if(data_store && b>=int'(data_address[2:0]) && data_mask[b])
          ram[int'((data_address&~64'd7)-BASE)+b]=data_value[8*(b-int'(data_address[2:0]))+:8];
      end
    end
    if(packet_valid && packet_ready) cursor++;
    if(restart_valid) cursor=int'((restart_pc-BASE)/4);
    vector_cosim_begin(ticks); clock=1;
    #1; vector_cosim_end(); #4; clock=0; ticks++; #1;
    if(response_fire) void'(pending.pop_front());
    if(request_fire) pending.push_back(accepted);
  endtask
  initial begin
    // Rebind a new epoch after a complete run: all passive owner state resets.
    for(int epoch=0;epoch<2;epoch++) begin
      reset=1; packet_valid=0; vector_cosim_reset(longint'(epoch));
      certify=(epoch==0); pending.delete(); requests=0; blocked=0; hits=0; stall_next=1;
      for(int i=0;i<4096;i++) ram[i]=i<2048 ? 0 : 8'(i*37+11);
      repeat(3) tick(); reset=0; repeat(3) tick();
      cursor=0;
      // Model frontend recovery as well as ready/valid admission. A full WB
      // descriptor FIFO may replay the last offered instruction and its suffix.
      for(int cycle=0;cycle<10000;cycle++) begin
        packet_valid=cursor<vector_cosim_count();
        pc=BASE+64'(4*cursor);
        instruction=packet_valid ? vector_cosim_instruction(cursor) : 0;
        tick();
      end
      packet_valid=0;
      $display("vector epoch=%0d cursor=%0d requests=%0d blocked=%0d hits=%0d pending=%0d data_valid=%0d response_ready=%0d",epoch,cursor,requests,blocked,hits,pending.size(),data_valid,response_ready);
      vector_cosim_finish();
      if(requests==0 || blocked==0 || hits==0) $fatal(1,"memory path coverage missing requests=%0d blocked=%0d hits=%0d",requests,blocked,hits);
    end
    $finish;
  end
endmodule
