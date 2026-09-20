// Exercises page-walk intervals through PTE waits, completion stalls, faults, cancellation, and reset.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_walk_trace_tb;
  logic clock=0, reset=1, command_valid=0, command_ready, cancel=0;
  logic [63:0] address=64'h4000, pte=0, memory_address;
  logic memory_ready=0, memory_fault=0, response_valid=0, memory_valid;
  logic completion_ready=0, completed, fault, access_fault;
  RV5StageWalkTrace dut(.*);
  always #5 clock=~clock;
  import "DPI-C" function void walk_bind();
  import "DPI-C" function void walk_sample(int unsigned reset, int unsigned cancel, int unsigned start,
      longint unsigned address, int unsigned request, longint unsigned memory_address,
      int unsigned finish, int unsigned fault, int unsigned access_fault);
  import "DPI-C" function void walk_check();
  import "DPI-C" function void walk_finish();
  always @(posedge clock) begin
    walk_sample(int'(reset),int'(cancel),int'(command_valid && command_ready),address,
        int'(memory_valid && memory_ready),memory_address,int'(completed && completion_ready),int'(fault),int'(access_fault));
    #1; walk_check();
  end
  task automatic tick; @(posedge clock); #2; endtask
  task automatic start(input logic [63:0] location=64'h4000);
    address=location; command_valid=1;
    #1; assert(command_ready) else $fatal(1,"walker not idle");
    tick(); command_valid=0;
  endtask
  task automatic read_pte(input logic [63:0] expected_address, value);
    #1; assert(memory_valid && memory_address==expected_address) else $fatal(1,"PTE address mismatch");
    repeat(3) tick();
    memory_ready=1; tick(); memory_ready=0;
    repeat(3) tick();
    pte=value; response_valid=1; tick(); response_valid=0;
  endtask
  task automatic finish(input bit page_fault=0, bus_fault=0);
    #1; assert(completed && fault==page_fault && access_fault==bus_fault) else $fatal(1,"translation result mismatch");
    repeat(4) tick();
    completion_ready=1; tick(); completion_ready=0;
    assert(command_ready && !completed) else $fatal(1,"walker did not release");
  endtask
  initial begin
    walk_bind(); tick(); reset=0; tick();
    // Repeated virtual addresses must create distinct owners; all three PTEs share one owner.
    repeat(3) begin
      start(); read_pte('h1000,('h2<<10)|1); read_pte('h2000,('h3<<10)|1);
      read_pte('h3020,('h5<<10)|'hc3); finish();
    end
    start(); read_pte('h1000,0); finish(1,0);
    start(64'h8000000000); finish(1,0); // Noncanonical: no memory traffic.
    start(); memory_ready=1; memory_fault=1; tick(); memory_ready=0; memory_fault=0; finish(0,1);
    // Cancellation while the first PTE is blocked, while its response is outstanding,
    // and while completion is held all terminate the same resident occurrence.
    start(); repeat(3) tick(); cancel=1; tick(); cancel=0;
    start(); memory_ready=1; tick(); memory_ready=0; repeat(3) tick(); cancel=1; tick(); cancel=0;
    start(64'h8000000000); cancel=1; completion_ready=1; tick(); cancel=0; completion_ready=0;
    // An offered request on a cancel edge is discarded, not a zero-length residency.
    command_valid=1; cancel=1; tick(); command_valid=0; cancel=0;
    start(); repeat(2) tick(); reset=1; tick(); reset=0; tick();
    start(); read_pte('h1000,('h2<<10)|1); read_pte('h2000,('h3<<10)|1);
    read_pte('h3020,('h5<<10)|'hc3); finish();
    tick(); walk_finish(); $finish;
  end
  initial begin #10000; $fatal(1,"walker trace timeout"); end
endmodule
