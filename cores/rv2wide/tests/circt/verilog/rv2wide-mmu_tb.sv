// SPDX-License-Identifier: Apache-2.0
module rv2wide_mmu_tb;
  typedef struct packed { logic [1:0] disposition; logic [63:0] cause, value; } resolution_t;
  typedef struct packed { logic valid; logic [63:0] address; logic [1:0] operation; } prefetch_t;
  prefetch_t prefetch_in='0, physical_prefetch_out;
  logic clock=0, reset=1;
  always #5 clock=~clock;
  logic [1:0] privilege=1;
  logic [63:0] satp=64'h8000000000000010, mstatus=0;
  logic invalidate=0, fetch_valid=0, ex_valid=0, wb_valid=0, commit=0;
  logic [3:0] access=1;
  logic [2:0] locality=0;
  wire [2:0] physical_locality;
  wire [3:0] physical_access;
  logic [63:0] fetch_address='h400000, address='h500008;
  logic physical_ready=0, physical_fault=0, response_valid=0;
  logic [63:0] response_data=0;
  logic split_valid=0;
  logic [1:0] split_width=3;
  logic [63:0] split_data=64'h8877665544332211;
  wire split_ready, split_completed, split_page_fault, split_access_fault;
  wire [63:0] split_result, split_fault_address, physical_data;
  wire [7:0] physical_mask;
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
    #1; // Settle a just-withdrawn WB offer before observing the next PTE request.
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
    wait_request(pa);
    assert(physical_locality==0) else $fatal(1,"unexpected locality on PTE/default request");
    physical_ready=1; tick(); falling(); physical_ready=0;
    return_data(data);
  endtask
  task automatic fence;
    falling(); invalidate=1; tick(); falling(); invalidate=0; tick(); falling();
  endtask
  task automatic start_split(input logic [63:0] va, input int operation=1, width=3);
    falling(); address=va; access=4'(operation); split_width=2'(width); split_valid=1; #1;
    assert(split_ready) else $fatal(1,"split owner not available");
    tick(); falling(); split_valid=0;
  endtask
  task automatic finish_split(input logic [63:0] value, fault_va=0, input bit page=0, access_error=0);
    for(int i=0;!split_completed && i<80;i++) tick();
    assert(split_completed && split_page_fault==page && split_access_fault==access_error)
      else $fatal(1,"split final fault classification");
    if(page || access_error) assert(split_fault_address==fault_va) else $fatal(1,"split fault VA");
    else assert(split_result==value) else $fatal(1,"split merge got=%h expected=%h",split_result,value);
    assert(!completed) else $fatal(1,"split response routed to scalar deferred owner");
    tick(); falling();
  endtask
  task automatic walk_data(input logic [63:0] leaf);
    reply('h10000,('h11<<10)|1);
    reply('h11010,('h12<<10)|1);
    reply('h12800,leaf);
    repeat(5) tick(); falling();
  endtask
  task automatic walk_at(input logic [63:0] va, leaf, input int level=0);
    reply('h10000+((va>>30)&511)*8,level==2 ? leaf : ('h11<<10)|1);
    if(level<2) reply('h11000+((va>>21)&511)*8,level==1 ? leaf : ('h12<<10)|1);
    if(level<1) reply('h12000+((va>>12)&511)*8,leaf);
    repeat(5) tick(); falling();
  endtask
  task automatic data_hit(input logic [63:0] va, pa, input int operation=1);
    address=va; access=4'(operation); ex_valid=1;
    tick(); falling(); ex_valid=0; #1;
    assert(result_valid && resolve_valid && resolve_address==pa && !physical_valid)
      else $fatal(1,"NAPOT MEM translation va=%h pa=%h got=%h",va,pa,resolve_address);
    tick(); falling(); wb_valid=1; #1;
    assert(physical_valid && physical_address==pa && physical_access==access && !wb_fault)
      else $fatal(1,"NAPOT WB translation va=%h",va);
    // No acceptance: inspect the authorization path without allocating a reply.
    wb_valid=0;
  endtask
  task automatic data_page_fault(input logic [63:0] va, input int operation=1);
    address=va; access=4'(operation); wb_valid=1; #1;
    assert(wb_fault && !physical_valid && !wb_ready && wb_fault_bits.cause==(operation==1 ? 13 : 15) && wb_fault_bits.value==va)
      else $fatal(1,"NAPOT data page fault/provenance va=%h",va);
    tick(); falling(); wb_valid=0;
  endtask
  task automatic hint(input logic [63:0] va, input int operation, input bit accepted, input logic [63:0] pa=0, input int cancel_stage=0);
    // Two registered stages; neither a dropped hint nor a successful probe walks.
    falling(); prefetch_in='{1'b1,va,2'(operation)};
    tick(); falling(); prefetch_in='0;
    if(cancel_stage==1) invalidate=1;
    tick(); falling();
    if(cancel_stage==2) invalidate=1;
    if(cancel_stage==3) mstatus=mstatus ^ (64'd1<<18);
    #1;
    assert(physical_prefetch_out.valid==accepted && !physical_valid && !completed && !wb_fault)
      else $fatal(1,"hint acceptance/side effect va=%h op=%0d accepted=%b got=%b",va,operation,accepted,physical_prefetch_out.valid);
    if(accepted) assert(physical_prefetch_out.address==pa && physical_prefetch_out.operation==2'(operation))
      else $fatal(1,"prefetch translation/alignment");
    tick(); falling(); invalidate=0;
    repeat(5) begin tick(); assert(!physical_valid && !physical_prefetch_out.valid) else $fatal(1,"hint allocated a walk or duplicated"); end
    falling();
  endtask
  initial begin
    repeat(3) tick(); falling(); reset=0;
    repeat(2) tick();
    for(int operation=1;operation<=3;operation++) hint('h50003f,operation,0);
    privilege=3; repeat(2) tick();
    for(int operation=1;operation<=3;operation++) hint('h83,operation,1,'h80);
    hint('h100000000000083,2,0); // Bare PA overflow must not alias a mapped line.
    hint('h3000,2,0);           // Unmapped.
    hint('h2000,2,0);           // Device.
    hint('h2300,2,0);           // Noncacheable.
    hint('h2400,3,0);           // Region does not contain a complete block.
    hint('h2500,1,0);           // Cacheable but not executable.
    hint('h83,2,0,0,1);
    hint('h83,2,0,0,2);
    hint('h83,2,0,0,3);
    privilege=1; mstatus=0; repeat(2) tick(); falling();
    // Cold EX indexing is unconditional, but a speculative miss never walks.
    ex_valid=1; #1;
    assert(index_valid && index_address=='h500008 && !resolve_valid && !result_valid) else $fatal(1,"EX timing");
    tick(); falling(); ex_valid=0; #1;
    assert(result_valid && outcome==0 && !resolve_valid) else $fatal(1,"MEM cold outcome");
    repeat(8) begin tick(); assert(!physical_valid) else $fatal(1,"speculative walk"); end
    offer_wb('h500008); walk_data(('h15<<10)|'hc7);
    hint('h50003f,2,1,'h15000);
    hint('h50003f,3,1,'h15000);
    hint('h50003f,1,0); // A DTLB entry is not an instruction-side entry.
    hint(64'h800050003f,2,0); // A noncanonical alias must not use the resident VPN.
    privilege=0; repeat(2) tick(); hint('h50003f,2,0);
    privilege=1; repeat(2) tick(); falling();
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
    // Atomic MEM translation checks permissions without exposing a speculative
    // physical cache request. WB carries the exact operation to the service.
    for(int operation=3;operation<=9;operation++) begin
      access=4'(operation); address=operation>=6 ? 'h50003f : 'h500008; ex_valid=1;
      tick(); falling(); ex_valid=0; #1;
      assert(result_valid && outcome==0 && !resolve_valid) else $fatal(1,"speculative atomic cache operation");
      tick(); falling(); wb_valid=1; physical_ready=1; #1;
      assert(wb_ready && physical_valid && physical_address==(operation>=6 ? 'h1603f : 'h16008) && physical_access==access)
        else $fatal(1,"atomic operation lost during translation");
      tick(); falling(); wb_valid=0; physical_ready=0;
      return_data(operation==4 ? 64'd1 : 64'hffffffff81234567,1);
    end
    // Read-only mappings permit LR, but fault SC/AMO with the original VA.
    access=1; fence(); offer_wb('h500008); walk_data(('h16<<10)|'h43);
    hint('h50003f,3,1,'h16000); // Hints require some PTE permission, not store permission or D.
    access=3; wb_valid=1; physical_ready=1; #1;
    assert(wb_ready && !wb_fault && physical_access==3) else $fatal(1,"LR denied readable PTE");
    tick(); falling(); wb_valid=0; physical_ready=0; return_data(64'd7,1);
    for(int operation=4;operation<=6;operation++) begin
      access=4'(operation); address=operation==6 ? 'h50003f : 'h500008; wb_valid=1; #1;
      assert(wb_fault && !wb_ready && !physical_valid && wb_fault_bits.cause==15 && wb_fault_bits.value==address)
        else $fatal(1,"atomic write permission/fault provenance");
      tick(); falling(); wb_valid=0;
    end
    // Management uses read-or-write PTE permission, does not require D, and
    // still carries store/AMO fault classification when translation fails.
    for(int operation=7;operation<=9;operation++) begin
      access=4'(operation); address='h50003f; wb_valid=1; physical_ready=1; #1;
      assert(wb_ready && !wb_fault && physical_valid && physical_access==access && physical_address=='h1603f)
        else $fatal(1,"maintenance rejected a readable clean PTE");
      tick(); falling(); wb_valid=0; physical_ready=0; return_data(0,1);
    end
    access=8; fence(); offer_wb('h50003f); walk_data(0);
    wb_valid=1; #1;
    assert(wb_fault && !wb_ready && !physical_valid && wb_fault_bits.cause==15 && wb_fault_bits.value=='h50003f)
      else $fatal(1,"maintenance page-fault address/class");
    tick(); falling(); wb_valid=0;
    // Device PMAs reject all atomics without a physical or uncached transaction.
    privilege=3; address='h2000;
    for(int operation=3;operation<=6;operation++) begin
      access=4'(operation); ex_valid=1; tick(); falling(); ex_valid=0; #1;
      assert(result_valid && outcome==5 && !resolve_valid && !physical_valid)
        else $fatal(1,"atomic device admission");
      tick(); falling();
    end
    // Zero uses its own PMA capability, not atomic permission or natural alignment.
    access=6;
    for(int scenario=0;scenario<4;scenario++) begin
      address=scenario==0 ? 'h2403 : scenario==1 ? 'h2103 : scenario==2 ? 'h253f : 'h233f;
      ex_valid=1; tick(); falling(); ex_valid=0; #1;
      assert(result_valid && !resolve_valid && outcome==(scenario<2 ? 5 : 0))
        else $fatal(1,"CBO whole-block PMA/atomic independence scenario=%0d",scenario);
      tick(); falling();
    end
    // Maintenance has no zero/atomic capability requirement, but checks the
    // whole block and requires at least one of read/write even on device PMAs.
    for(int operation=7;operation<=9;operation++) for(int scenario=0;scenario<4;scenario++) begin
      access=4'(operation); address=scenario==0 ? 'h2103 : scenario==1 ? 'h2403 : scenario==2 ? 'h203f : 'h253f;
      ex_valid=1; tick(); falling(); ex_valid=0; #1;
      assert(result_valid && !resolve_valid && outcome==(scenario<2 ? 5 : 0))
        else $fatal(1,"maintenance full-block PMA scenario=%0d op=%0d",scenario,operation);
      tick(); falling();
    end
    access=1; privilege=1; address='h500008;
    fence(); offer_wb('h500008); walk_data(('h16<<10)|'hc7);
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
    hint('h40003f,1,1,'h14000);
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
    falling(); wb_valid=0; privilege=3; satp=0; fence();
    // Byte masks and shifts preserve all neighboring lanes. Prefix acceptance
    // is irrevocable, but no second fragment appears before its response.
    locality=4; start_split('h307,2,3); locality=0; wait_request('h300);
    assert(physical_locality==4) else $fatal(1,"split prefix lost retained locality");
    assert(physical_mask=='h80 && physical_data==64'h1100000000000000) else $fatal(1,"first store mask/shift");
    physical_ready=1; tick(); falling(); physical_ready=0;
    repeat(4) begin tick(); assert(!physical_valid && !split_completed) else $fatal(1,"advanced before first completion"); end
    falling(); return_data(0); wait_request('h308);
    assert(physical_locality==4) else $fatal(1,"split suffix lost retained locality");
    assert(physical_mask=='h7f && physical_data==64'h0088776655443322) else $fatal(1,"second store mask/shift");
    physical_ready=1; tick(); falling(); physical_ready=0; return_data(0); finish_split(0);
    start_split('h303,1,1); reply('h300,64'h8877665544332211); finish_split('h5544);
    // A cached first translation and missing second PTE must never reissue
    // the accepted first-page store or require rollback.
    privilege=1; satp=64'h8000000000000010; fence();
    locality=2; start_split('h500ffd,2,3); locality=0; walk_data(('h15<<10)|'hc7);
    wait_request('h15ff8); assert(physical_mask=='he0) else $fatal(1,"cross-page prefix mask");
    assert(physical_locality==2) else $fatal(1,"translation lost locality");
    physical_ready=1; tick(); falling(); physical_ready=0; return_data(0);
    reply('h10000,('h11<<10)|1); reply('h11010,('h12<<10)|1); reply('h12808,0);
    finish_split(0,'h501000,1);
    // Device fragments are rejected locally, even when one natural beat would
    // otherwise cover the access. Atomic alignment remains a core decision.
    privilege=3; start_split('h2001,1,1); finish_split(0,'h2001,0,1);
    // One accepted leaf supplies the full 64 KiB mapping through each bank.
    // Cold walks begin at different subpages; neighboring PTEs are never read.
    privilege=1; satp=64'h8000000000000010; access=1;
    for(int first=0;first<16;first+=7) begin
      logic [63:0] va;
      va='h500000+64'(first*4096)+'h128;
      fence(); offer_wb(va); walk_at(va,64'h80000000000060cf);
      for(int page=0;page<16;page++) begin
        data_hit('h500000+64'(page*4096),'h10000+64'(page*4096));
        data_hit('h500128+64'(page*4096),'h10128+64'(page*4096),2);
        data_hit('h500ff8+64'(page*4096),'h10ff8+64'(page*4096));
      end
      hint('h50ffff,2,1,'h1ffc0);
      // Outside the mapping, speculative MEM returns Slow, without walking.
      for(int side=0;side<2;side++) begin
        address=side==0 ? 'h4ffff8 : 'h510000; ex_valid=1;
        tick(); falling(); ex_valid=0; #1;
        assert(result_valid && outcome==0 && !resolve_valid && !physical_valid) else $fatal(1,"NAPOT DTLB overreach");
        tick(); falling();
      end
      fetch_address='h400128+64'(first*4096); fetch_valid=1;
      walk_at(fetch_address,64'h80000000000060cb);
      for(int page=0;page<16;page++) begin
        fetch_address='h400ff8+64'(page*4096); #1;
        assert(fetch_resolution.disposition==0 && fetch_physical=='h10ff8+64'(page*4096) && !physical_valid)
          else $fatal(1,"NAPOT ITLB subpage offset");
        tick(); falling();
      end
      fetch_address='h410000; #1;
      assert(fetch_resolution.disposition==2) else $fatal(1,"NAPOT ITLB overreach");
      fetch_valid=0;
    end
    // Fence removes both banks' whole-region entries, allowing a new base.
    fence(); fetch_address='h400008; #1;
    assert(fetch_resolution.disposition==2) else $fatal(1,"NAPOT ITLB survived fence");
    access=1; offer_wb('h500008); walk_at('h500008,64'h80000000000020cf);
    data_hit('h500008,8);
    fetch_valid=1; walk_at('h400008,64'h80000000000020cb);
    assert(fetch_resolution.disposition==0 && fetch_physical==8) else $fatal(1,"NAPOT ITLB stale base");
    fetch_valid=0;
    // Warm permission checks must apply to every subpage, including current U mode.
    fence(); offer_wb('h503008); walk_at('h503008,64'h8000000000006043);
    data_hit('h50f008,'h1f008);
    data_page_fault('h50e008,2);
    privilege=0; data_page_fault('h50d008); privilege=1;
    // Missing A, missing D, reserved low PPN, and upper-level N fault, not fill.
    for(int scenario=0;scenario<4;scenario++) begin
      logic [63:0] leaf;
      fence(); access=scenario==1 ? 2 : 1;
      leaf=scenario==0 ? 64'h800000000000608f : scenario==1 ? 64'h800000000000604f : scenario==2 ? 64'h80000000000064cf : 64'h80000000000060cf;
      offer_wb('h507128); walk_at('h507128,leaf,scenario==3 ? 1 : 0);
      data_page_fault('h507128,scenario==1 ? 2 : 1);
    end
    fence(); fetch_address='h40f008; fetch_valid=1;
    walk_at(fetch_address,64'h8000000000006043); // Readable but not executable.
    assert(fetch_resolution.disposition==1 && fetch_resolution.cause==12 && fetch_resolution.value=='h40f008 && !physical_valid)
      else $fatal(1,"NAPOT instruction page fault/provenance");
    fetch_valid=0;
    $display("RV2Wide MMU timing, Svnapot mappings, permissions, invalidation, arbitration, and cancel/drain passed");
    $finish;
  end
  initial begin #60000; $fatal(1,"MMU timeout"); end
endmodule
