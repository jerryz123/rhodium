// SPDX-License-Identifier: Apache-2.0
module rv2wide_mmu_tb;
  typedef struct packed { logic [1:0] disposition; logic [63:0] cause, value; } resolution_t;
  logic clock=0, reset=1;
  always #5 clock=~clock;
  logic [1:0] privilege=1;
  logic [63:0] satp=64'h8000000000000010, mstatus=0;
  logic invalidate=0, fetch_valid=0, ex_valid=0, wb_valid=0, write=0, commit=0;
  logic [63:0] fetch_address='h400000, address='h500008;
  logic physical_ready=0, physical_fault=0, response_valid=0;
  logic [63:0] response_data=0;
  wire [63:0] fetch_physical, index_address, resolve_address, physical_address, completed_data;
  wire index_valid, resolve_valid, result_valid, wb_fault, wb_ready, physical_valid, response_ready, completed;
  wire [2:0] outcome;
  resolution_t fetch_resolution, wb_fault_bits;
  RV2WideMmuFixture dut(.*);
  task automatic tick; @(posedge clock); #1; endtask
  task automatic falling; @(negedge clock); #1; endtask
  task automatic offer_wb(input logic [63:0] va);
    falling(); address=va; wb_valid=1; tick(); falling(); wb_valid=0;
  endtask
  task automatic wait_request(input logic [63:0] pa);
    for(int i=0;!physical_valid && i<80;i++) tick();
    assert(physical_valid && physical_address==pa) else $fatal(1,"PTE expected=%h actual=%h valid=%b",pa,physical_address,physical_valid);
    falling();
  endtask
  task automatic return_data(input logic [63:0] data, input bit core_reply=0);
    response_valid=1; response_data=data; #1;
    assert(response_ready && completed==core_reply) else $fatal(1,"response owner lost");
    if(core_reply) assert(completed_data==data) else $fatal(1,"core data corrupted");
    tick(); falling(); response_valid=0;
  endtask
  task automatic reply(input logic [63:0] pa, data);
    wait_request(pa); physical_ready=1; tick(); falling(); physical_ready=0;
    return_data(data);
  endtask
  task automatic fence;
    falling(); invalidate=1; tick(); falling(); invalidate=0; tick(); falling();
  endtask
  task automatic walk_data(input logic [63:0] leaf);
    reply('h10000,('h11<<10)|1);
    reply('h11010,('h12<<10)|1);
    reply('h12800,leaf);
    repeat(5) tick(); falling();
  endtask
  initial begin
    repeat(3) tick(); falling(); reset=0;
    // Cold EX indexing is unconditional, but a speculative miss never walks.
    ex_valid=1; #1;
    assert(index_valid && index_address=='h500008 && !resolve_valid && !result_valid) else $fatal(1,"EX timing");
    tick(); falling(); ex_valid=0; #1;
    assert(result_valid && outcome==0 && !resolve_valid) else $fatal(1,"MEM cold outcome");
    repeat(8) begin tick(); assert(!physical_valid) else $fatal(1,"speculative walk"); end
    offer_wb('h500008); walk_data(('h15<<10)|'hc7);
    ex_valid=1; #1; assert(index_valid && !resolve_valid) else $fatal(1,"warm EX timing");
    tick(); falling(); ex_valid=0; address=64'hdeadbeef; #1;
    assert(result_valid && resolve_valid && resolve_address=='h15008 && outcome==1) else $fatal(1,"MEM did not use registered VA");
    tick(); falling();
    // Current permissions are checked even on a resident supervisor mapping.
    privilege=0; ex_valid=1; address='h500008; tick(); falling(); ex_valid=0; #1;
    assert(result_valid && outcome==4 && !resolve_valid) else $fatal(1,"user permission recheck");
    tick(); falling(); privilege=3; mstatus=(64'd1<<17)|(64'd1<<11); ex_valid=1;
    tick(); falling(); ex_valid=0; #1;
    assert(resolve_valid && resolve_address=='h15008) else $fatal(1,"MPRV effective data privilege");
    tick(); falling(); privilege=1; mstatus=0;
    // Architectural invalidation forces a fresh walk and permits remapping.
    fence(); offer_wb('h500008); walk_data(('h16<<10)|'hc7);
    ex_valid=1; tick(); falling(); ex_valid=0; #1;
    assert(resolve_valid && resolve_address=='h16008) else $fatal(1,"stale translation after fence");
    tick(); falling();
    // A WB request owns the DTLB instead of the younger MEM lookup.
    ex_valid=1; tick(); falling(); ex_valid=0; wb_valid=1; address='h500010; #1;
    assert(result_valid && outcome==3 && !resolve_valid && physical_address=='h16010) else $fatal(1,"WB translation priority");
    tick(); falling(); wb_valid=0;
    // A pulsed older WB miss is remembered while an instruction walk runs.
    // Dropping the fetch consumer (ordinary replay) must not restart that walk.
    fence(); fetch_valid=1; wait_request('h10000);
    address='h500008; wb_valid=1; tick(); falling(); wb_valid=0; fetch_valid=0;
    reply('h10000,('h11<<10)|1);
    reply('h11010,('h12<<10)|1);
    reply('h12000,('h14<<10)|'hcb);
    // Even a new fetch miss cannot take the next walker slot from retained WB.
    fetch_valid=1; fetch_address='h402000;
    walk_data(('h15<<10)|'hc7);
    fetch_valid=0; fetch_address='h400000;
    // An accepted fetch PTE survives cancellation. A normal WB transaction can
    // issue while that old response is outstanding; FIFO ownership distinguishes both.
    fence(); fetch_valid=1;
    wait_request('h10000); physical_ready=1; tick(); falling(); physical_ready=0;
    fetch_valid=0; invalidate=1; tick(); falling(); invalidate=0;
    privilege=3; address='h80; wb_valid=1; physical_ready=1; #1;
    assert(wb_ready && physical_valid && physical_address=='h80) else $fatal(1,"younger walk blocked WB");
    tick(); falling(); wb_valid=0; physical_ready=0;
    privilege=1; fetch_valid=1; fetch_address='h400000;
    repeat(4) begin tick(); assert(!physical_valid) else $fatal(1,"walker reused canceled response slot"); end
    falling(); return_data(('h11<<10)|1);
    return_data(64'hdeadbeef,1);
    // A fresh translation must start at the root, never use the canceled PTE.
    reply('h10000,('h11<<10)|1);
    reply('h11010,('h12<<10)|1);
    reply('h12000,('h14<<10)|'hcb);
    repeat(5) tick(); falling();
    assert(fetch_resolution.disposition==0 && fetch_physical=='h14000) else $fatal(1,"fetch after canceled walk");
    fetch_valid=0;
    // PTE physical admission failure becomes a precise original data VA fault.
    offer_wb('h501038); wait_request('h10000); physical_fault=1;
    tick(); falling(); physical_fault=0; repeat(5) tick(); falling(); wb_valid=1; #1;
    assert(wb_fault && !wb_ready && wb_fault_bits.cause==5 && wb_fault_bits.value=='h501038) else $fatal(1,"PTE access-fault provenance");
    // An architecturally invalid page table in a device region faults locally;
    // admitting ordinary MMIO must not accidentally admit speculative PTE reads.
    falling(); wb_valid=0; fence(); satp=64'h8000000000000002;
    offer_wb('h500008);
    repeat(12) begin tick(); assert(!physical_valid) else $fatal(1,"PTE reached a device"); end
    falling(); wb_valid=1; #1;
    assert(wb_fault && !wb_ready && wb_fault_bits.cause==5 && wb_fault_bits.value=='h500008) else $fatal(1,"device PTE fault provenance");
    $display("RV2Wide MMU timing, permissions, invalidation, arbitration, and cancel/drain passed");
    $finish;
  end
  initial begin #30000; $fatal(1,"MMU timeout"); end
endmodule
