// Runs paged guests with precise privileged faults, invalidation, and WB-owned effects.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_hypervisor_core_tb;
  logic clock = 0, reset = 1;
  logic [31:0] instruction_word;
  logic instruction_valid;
  logic instruction_cacheable, instruction_device;
  logic [63:0] instruction_address;
  logic memory_ready, memory_fault, response_valid;
  logic reservation_valid = 0;
  logic [63:0] response_data;
  logic memory_valid, memory_write, memory_walker, response_ready;
  logic [3:0] memory_operation;
  logic [1:0] memory_pbmt;
  logic [63:0] memory_address, memory_data;
  logic [7:0] memory_mask;
  logic translation_flush, virtualized;
  logic [5:0] interrupts = 0;
  logic [63:0] time_counter = 0;
  bit sstc_case = 0;
  RV5StageHypervisorCoreFixture dut (.*);
  always #5 clock = ~clock;

  byte unsigned ram [0:196607];
  int cycle, delay_left, stores, walks, flushes, traps, scenario;
  int cmo_count, last_cmo;
  bit cmo_completed;
  bit pbmt_check = 0;
  int expected_type, expected_pte_type, pbmt_data_reads;
  logic [63:0] pbmt_code, pbmt_data;
  bit pending;
  logic [63:0] reply;
  int cursor, literal_cursor;
  logic [63:0] expected_pc;
  localparam logic [63:0] SIGNATURE = 'h20000;
  localparam logic [63:0] GVA = 64'h40000000;

  function automatic logic [63:0] read64(input logic [63:0] address);
    logic [63:0] value = 0;
    assert (address + 7 < 196608) else $fatal(1, "physical read outside test RAM: %h", address);
    for (int lane = 0; lane < 8; lane++) value[lane*8 +: 8] = ram[int'(address)+lane];
    return value;
  endfunction
  task automatic write64(input int address, input logic [63:0] value);
    for (int lane = 0; lane < 8; lane++) ram[address+lane] = value[lane*8 +: 8];
  endtask
  task automatic emit(input logic [31:0] instruction);
    for (int lane = 0; lane < 4; lane++) ram[cursor+lane] = instruction[lane*8 +: 8];
    cursor += 4;
  endtask
  function automatic logic [31:0] addi(input int rd, input int rs, input int immediate);
    return {12'(immediate), 5'(rs), 3'b000, 5'(rd), 7'h13};
  endfunction
  function automatic logic [31:0] ld(input int rd, input int rs, input int immediate);
    return {12'(immediate), 5'(rs), 3'b011, 5'(rd), 7'h03};
  endfunction
  function automatic logic [31:0] sd(input int rs2, input int rs1, input int immediate);
    return {7'(immediate >> 5), 5'(rs2), 5'(rs1), 3'b011, 5'(immediate), 7'h23};
  endfunction
  function automatic logic [31:0] fp(input int funct7, input int rd, input int rs1, input int rs2, input int rm = 0);
    return {7'(funct7), 5'(rs2), 5'(rs1), 3'(rm), 5'(rd), 7'h53};
  endfunction
  function automatic logic [31:0] fld(input int rd, input int rs, input int immediate, input bit single_precision = 0);
    return {12'(immediate), 5'(rs), single_precision ? 3'b010 : 3'b011, 5'(rd), 7'h07};
  endfunction
  function automatic logic [31:0] fsd(input int rs2, input int rs1, input int immediate, input bit single_precision = 0);
    return {7'(immediate >> 5), 5'(rs2), 5'(rs1), single_precision ? 3'b010 : 3'b011, 5'(immediate), 7'h27};
  endfunction
  function automatic logic [31:0] vset(input int width, input int rs = 11);
    return {1'b0, 11'(width << 3), 5'(rs), 3'b111, 5'd12, 7'h57};
  endfunction
  function automatic logic [31:0] vmem(input bit store, input int width, input int regno, input int base, input bit indexed = 0, input bit fault_first = 0);
    return {4'b0, indexed ? 2'b11 : 2'b00, 1'b1, 5'(indexed ? 4 : fault_first ? 16 : 0), 5'(base), 3'(width == 0 ? 0 : width+4), 5'(regno), store ? 7'h27 : 7'h07};
  endfunction
  function automatic logic [31:0] vadd(input int vd, input int vs2, input int vs1, input int mode);
    return {6'b0, 1'b1, 5'(vs2), 5'(vs1), 3'(mode), 5'(vd), 7'h57};
  endfunction
  function automatic logic [31:0] guest_load(input int size, input int extension, input int rd = 12);
    return {7'('h30+2*size), 5'(extension), 5'd10, 3'b100, 5'(rd), 7'h73};
  endfunction
  function automatic logic [31:0] guest_store(input int size);
    return {7'('h31+2*size), 5'd11, 5'd10, 3'b100, 5'd0, 7'h73};
  endfunction
  task automatic constant(input int rd, input logic [63:0] value);
    write64(literal_cursor, value);
    emit(ld(rd, 31, literal_cursor - 'h4000));
    literal_cursor += 8;
  endtask
  task automatic csrw(input int address, input logic [63:0] value);
    constant(1, value);
    emit({12'(address), 5'd1, 3'b001, 5'd0, 7'h73});
  endtask
  task automatic machine_boot;
    cursor = 'h1000; literal_cursor = 'h3800;
    emit(32'h00004fb7); // lui x31, 4: constant pool base
    // Firmware explicitly initializes state access for host and guest software.
    for (int i = 0; i < 4; i++) csrw('h30c+i,'1);
    for (int i = 0; i < 4; i++) csrw('h60c+i,'1);
  endtask
  task automatic save_csr(input int address, input int offset);
    emit({12'(address), 5'd0, 3'b010, 5'd21, 7'h73});
    emit(sd(21, 20, offset));
  endtask

  always_comb begin
    instruction_word = 32'h00000013;
    if (instruction_valid && instruction_address + 3 < 196608)
      for (int lane = 0; lane < 4; lane++) instruction_word[lane*8 +: 8] = ram[int'(instruction_address)+lane];
    memory_ready = !pending && (cycle % 7 != 2);
    response_valid = pending && delay_left == 0;
    response_data = reply;
    memory_fault = (scenario == 12 && memory_walker && memory_address == 'hd050) ||
                   (scenario == 13 && !memory_walker && memory_address == 'h19000);
  end
  always @(posedge clock) begin
    if (reset) begin
      cycle <= 0; delay_left <= 0; pending <= 0; reply <= 0;
      interrupts <= 0;
      time_counter <= 100;
      stores = 0; walks = 0; flushes = 0; traps = 0;
      cmo_count = 0; last_cmo = 0; cmo_completed = 0;
    end else begin
      cycle <= cycle + 1;
      if (pbmt_check && instruction_valid && instruction_address >= pbmt_code && instruction_address < pbmt_code+128)
        assert (instruction_cacheable == (expected_type == 0) && instruction_device == (expected_type == 2))
          else $fatal(1,"fetch PBMT routing expected=%0d got=%b/%b",expected_type,instruction_cacheable,instruction_device);
      if ($test$plusargs("debug") && cycle < 250 && (memory_valid || response_valid || translation_flush))
        $display("cycle=%0d req=%b ready=%b addr=%h wr=%b pte=%b response=%b/%b data=%h flush=%b", cycle, memory_valid, memory_ready, memory_address, memory_write, memory_walker, response_valid, response_ready, response_data, translation_flush);
      if (translation_flush) flushes++;
      if (pending && delay_left != 0) delay_left <= delay_left - 1;
      else if (response_valid && response_ready) begin
        pending <= 0;
        if (cmo_count != 0) cmo_completed = 1;
      end
      if (memory_valid && memory_ready && !memory_fault) begin
        if (pbmt_check) begin
          if (!memory_walker && memory_address >= pbmt_data && memory_address < pbmt_data+128) begin
            assert (memory_pbmt == 2'(expected_type)) else $fatal(1,"data PBMT lost across arbitration");
            if (!memory_write) pbmt_data_reads++;
          end
          if (memory_walker && memory_address >= 'h8000 && memory_address < 'hb000)
            assert (memory_pbmt == 2'(expected_pte_type)) else $fatal(1,"implicit VS PTE attributes lost");
        end
        assert (!pending) else $fatal(1, "overlapping physical transaction");
        assert (memory_address < 196608) else $fatal(1, "untranslated request %h", memory_address);
        if (memory_walker) walks++;
        if (memory_operation >= 6) begin
          cmo_count++; last_cmo = int'(memory_operation);
          assert ((memory_address & ~64'd63) == 'h19000 && read64('h19080) == 'h66)
            else $fatal(1,"CBO used wrong translation or passed older store");
          // Model only the external cache-service contract here; production
          // line mutation and CHI effects have their own cache regressions.
          if (memory_operation == 6)
            for (int lane = 0; lane < 64; lane++) ram['h19000+lane] = 0;
        end
        if (memory_write) begin
          for (int lane = 0; lane < 8; lane++)
            if (memory_mask[lane]) ram[(int'(memory_address) & ~7)+lane] = memory_data[lane*8 +: 8];
          stores++;
          if (scenario >= 100 && memory_address == 'h19088)
            assert (cmo_completed) else $fatal(1,"I/O-only fence failed to drain the preceding CBO");
          if (memory_address == SIGNATURE+48) traps++;
          // The marker is younger than the divide, without a result dependency.
          // Raise HS timer delivery while the older FP operation may still run.
          if (scenario == 48 && memory_address == 'h19078) interrupts <= 6'b001000;
          assert (memory_address != 'h21000) else $fatal(1, "unexpected M trap, mcause %h", memory_data);
        end
        reply <= read64(memory_address & ~64'd7);
        delay_left <= (((scenario == 59 || (sstc_case && scenario == 37)) && memory_address == 'h19000) || memory_operation >= 6) ? 40 : 2 + cycle % 3;
        if (memory_address == 'h19000 && !memory_write && !memory_walker) begin
          if (sstc_case) time_counter <= 101;
          else if (scenario == 59) interrupts <= 6'b001000;
        end
        pending <= 1;
      end
    end
  end

  task automatic prepare(input int kind, input bit timer = 0);
    reset = 1;
    sstc_case = timer;
    pbmt_check = 0;
    scenario = kind;
    for (int i = 0; i < 196608; i++) ram[i] = 0;
    machine_boot();
    csrw('h305, 'h2000);
    csrw('h105, 'h3000);
    csrw('h302, 'hffffff);
    csrw('h280, 64'h8000000000000008);
    csrw('h680, 64'h8000000000000004);
    csrw('h341, (kind >= 14 && kind < 36 && !(kind inside {26,27})) ? 'h5000 : GVA);
    if (kind >= 14) csrw('h600, (kind inside {22,24}) ? 'h200 : 'h100);
    if (kind == 35) csrw('h200, 1 << 18); // VS SUM applies to HLVX's load semantics
    if (kind >= 36) begin
      csrw('h205, GVA+'h300);
      csrw('h603, 'h444);
      csrw('h604, 64'd1 << (2+(kind-36)*4));
      if (timer) begin
        csrw('h14d,64'hffffffffffffffff); csrw('h24d,151); csrw('h605,50);
        csrw('h30a,64'h8000000000000000); csrw('h60a,64'h8000000000000000);
        csrw('h306,2); csrw('h606,2);
      end else csrw('h645, 64'd1 << (2+(kind-36)*4));
    end
    if (kind == 11) begin csrw('h602, 1 << 13); csrw('h205, GVA+'h300); end
    if (kind == 10) begin
      constant(29, 1 << 17);
      csrw('h300, 64'h8000020800); // MPRV + MPV, effective VS data access
      emit(32'h40001537); emit(ld(12, 10, 0)); emit(32'h300eb073); // clear MPRV
    end else if (kind == 34) csrw('h300, 'h1800); // M-mode explicit guest access
    else if (kind >= 14 && kind < 36 && !(kind inside {26,27}))
      csrw('h300, (kind inside {22,23}) ? 0 : 'h800); // HS, or HU-controlled U
    else csrw('h300, 64'h8000000800); // MPV=1, MPP=S
    emit(32'h30200073);

    // Three-level VS and G tables. VS PTE reads themselves require G translation.
    write64('h8008, ('h9 << 10) | 1);
    write64('h9000, ('ha << 10) | 1);
    write64('ha000, ('h10 << 10) | 'hcb); // guest code, supervisor RXAD
    write64('ha008, ('h11 << 10) | ((kind == 7) ? 'hc3 : 'hc7));
    write64('ha010, ('h12 << 10) | 'hcf);
    write64('h4000, ('hc << 10) | 1);
    write64('hc000, ('hd << 10) | 1);
    write64('hd040, ('h8 << 10) | 'hdf);
    write64('hd048, ('h9 << 10) | 'hdf);
    write64('hd050, ('ha << 10) | 'hdf);
    write64('hd080, ('h18 << 10) | 'hdf);
    write64('hd088, ('h19 << 10) | ((kind == 6) ? 'hd3 : 'hdf));
    write64('h8010, ('hb << 10) | 1); // missing G mapping for a VS intermediate table
    write64('h19000, 'h1234);
    write64('h19020, 'hfeed);
    write64('h1a000, 'h5678);

    cursor = 'h18000;
    // Guest constants use immediate arithmetic, never the host pool.
    emit(32'h40001537); // lui x10, 0x40001
    case (kind)
      1, 11: begin emit(32'h40003537); emit(ld(11, 10, 0)); end
      2: begin emit(32'h40002537); emit(ld(11, 10, 0)); end
      3: begin emit(32'h40002537); emit(sd(0, 10, 0)); end
      4: begin emit(32'h40002537); emit(32'h00050067); end // jalr x0,x10
      5: begin
        emit(addi(10, 0, 1)); emit(32'h01f51513); // slli x10,x10,31
        emit(ld(11, 10, 0));
      end
      6, 7: begin emit(ld(11, 10, 0)); emit(sd(11, 10, 8)); end
      9: emit(32'h22000073); // HFENCE.VVMA in VS raises virtual-instruction
      26: emit(guest_load(3, 0));
      27: emit(guest_store(3));
      default: begin emit(ld(11, 10, 0)); emit(addi(11, 11, 1)); emit(sd(11, 10, 8)); end
    endcase
    if (kind inside {1, 2, 3, 5, 6, 7, 9, 12, 13}) begin
      emit(32'h400017b7); emit(sd(0, 15, 32)); // younger mutation must be squashed
    end
    emit(32'h00000073);
    if (kind == 8) begin
      emit(ld(11, 10, 0)); emit(sd(11, 10, 16)); emit(32'h00000073);
    end
    emit(32'h0000006f);

    cursor = 'h18300; // VS trap handler, translated through both stages
    emit(32'h40001537);
    emit(32'h142026f3); emit(sd(13, 10, 128));
    emit(32'h143026f3); emit(sd(13, 10, 136));
    emit(32'h00000073); emit(32'h0000006f);

    cursor = 'h2000;
    emit(32'h00021a37); save_csr('h342, 0); emit(32'h0000006f);
    cursor = 'h3000;
    emit(32'h00020a37);
    save_csr('h142, 0); save_csr('h143, 8);
    save_csr('h643, 16); save_csr('h64a, 24);
    save_csr('h600, 32); save_csr('h141, 40);
    emit(sd(12, 20, 56));
    emit(addi(21, 0, 85)); emit(sd(21, 20, 48));
    if (kind == 8) begin
      constant(22, 'hd088); constant(23, ('h1a << 10) | 'hdf);
      emit(sd(23, 22, 0)); emit(32'h62000073); // HFENCE.GVMA after the PTE store
      emit(32'h14102af3); emit(addi(21, 21, 4)); emit(32'h141a9073); emit(32'h10200073);
    end else emit(32'h0000006f);
    case (kind)
      1, 2, 3, 6, 7: expected_pc = GVA+8;
      4: expected_pc = GVA+'h2000;
      5: expected_pc = GVA+12;
      8: expected_pc = GVA+28;
      9, 13: expected_pc = GVA+4;
      11: expected_pc = GVA+'h314;
      12: expected_pc = GVA;
      default: expected_pc = GVA+16;
    endcase
    if (kind >= 14) begin
      // Explicit accesses run with V=0 but use VS/G translation. A physical
      // signature store after every load also detects leaked guest context.
      write64('h19000, 64'h8000000080008080);
      if (kind inside {15,17,18,19,21,31,32,33}) write64('ha008, ('h11 << 10) | 'hc9);
      if (kind inside {15,18,19,21,31}) write64('hd088, ('h19 << 10) | 'hd9);
      if (kind == 17) write64('hd088, ('h19 << 10) | 'hd3);
      if (kind == 32) write64('hd088, ('h1a << 10) | 'hd9);
      if (kind == 33) write64('hd088, ('h1b << 10) | 'hd9);
      if (kind inside {22,24,25}) write64('ha008, ('h11 << 10) | 'hd7);
      if (kind == 35) begin
        write64('ha008, ('h11 << 10) | 'hd9);
        write64('hd088, ('h19 << 10) | 'hd9);
      end
      if (kind == 18) begin
        write64('hd040, ('h8 << 10) | 'hd3);
        write64('hd048, ('h9 << 10) | 'hd3);
        write64('hd050, ('ha << 10) | 'hd3);
      end
      if (kind == 19) write64('hd050, ('ha << 10) | 'hd9);
      if (kind == 30) write64('hd088, ('h19 << 10) | 'hd3);
      cursor = 'h5000;
      emit(32'h40001537); // guest VA in x10
      emit(32'h00020a37); // physical signature in x20
      if (kind == 14) begin
        for (int size = 0; size < 4; size++) begin
          emit(guest_load(size, 0)); emit(sd(12, 20, 128+size*16));
          if (size < 3) begin emit(guest_load(size, 1)); emit(sd(12, 20, 136+size*16)); end
        end
        constant(11, 64'h123456789abcdef0);
        for (int size = 0; size < 4; size++) begin
          emit(addi(10, 10, (size == 0) ? 8 : ((size < 3) ? 2 : 4)));
          emit(guest_store(size));
        end
      end else if (kind == 34) begin
        constant(29, 1 << 17);
        csrw('h300, 64'h8000020000); // MPRV=1, MPV=1, MPP=U; explicit SPVP remains S
        emit(guest_load(3,0));
        emit(32'h300eb073); // clear MPRV before host constant loads
        csrw('h341, 'h5100); csrw('h300, 'h800); emit(32'h30200073);
        cursor = 'h5100; expected_pc = 64'(cursor);
      end else begin
        if (kind == 20) emit(guest_load(1, 0)); // warm R-only VS leaf
        if (kind == 21) emit(guest_load(1, 3)); // warm X-only leaves
        if (kind inside {28,29}) emit(addi(10, 10, 1));
        expected_pc = 64'(cursor);
        if (kind inside {29,30}) emit(guest_store(3));
        else if (kind == 31) emit(guest_load(2,3));
        else emit(guest_load((kind inside {15,16,17,18,19,20,32,33,35}) ? 1 : 3,
                            (kind inside {15,16,17,18,19,20,32,33,35}) ? 3 : 0));
      end
      if (kind inside {14,15,18,22,24,31,35}) expected_pc = 64'(cursor);
      emit(32'h00000073);
      emit(sd(0, 10, 32)); emit(32'h0000006f);
      if (kind inside {26,27}) expected_pc = GVA+4;
    end
    if (kind >= 36) begin
      cursor = 'h18000;
      emit(32'h40001537); emit(ld(12, 10, 0));
      // A pending, locally enabled interrupt wakes WFI even with VS SIE=0.
      // The prior load must complete; the CSR's serializing boundary then
      // enables delivery before the younger signature mutation can retire.
      emit(32'h10500073); emit(32'h10016073); // wfi; csrsi sstatus, 2
      emit(addi(13, 0, 1)); emit(sd(13, 10, 40)); emit(32'h00000073);
      emit(32'h0000006f);
      expected_pc = GVA+24;
      cursor = 'h18300;
      emit(32'h14202773); emit(sd(14, 10, 64)); // guest scause
      emit(32'h14102773); emit(sd(14, 10, 72)); // guest sepc
      emit(32'h10002773); emit(sd(14, 10, 80)); // guest sstatus
      emit(32'h14405073); // csrwi sip, 0: acknowledges only software pending
      if (timer) begin
        emit(addi(14,0,300)); emit(32'h14d71073); // guest stimecmp rearm
      end else emit(32'h10405073); // csrwi sie, 0: masks retained injected pending
      emit(32'h10200073); // sret resumes the interrupted guest instruction
    end
    repeat (5) @(negedge clock);
    reset = 0;
  endtask
  task automatic run_virtual_interrupt(input int kind, input bit timer = 0);
    @(negedge clock); prepare(kind,timer);
    for (int limit = 0; limit < 30000; limit++) begin
      @(negedge clock);
      if (traps == 1) break;
    end
    assert (traps == 1 && !virtualized) else $fatal(1, "virtual interrupt case %0d timeout", kind);
    assert (read64('h19040) == (64'h8000000000000001 + (64'(kind)-36)*4))
      else $fatal(1, "incorrect VS interrupt cause %h", read64('h19040));
    assert (read64('h19048) == GVA+16 && read64('h19050) == 64'h200000120)
      else $fatal(1, "guest interrupt lost precise PC/status: %h %h", read64('h19048), read64('h19050));
    assert (read64(SIGNATURE) == 10 && read64(SIGNATURE+40) == expected_pc &&
            read64(SIGNATURE+56) == 64'h8000000080008080 && read64('h19028) == 1)
      else $fatal(1, "guest interrupt return lost load result or skipped the resumed instruction");
    $display("paged guest interrupt %0d Sstc=%b passed", kind-36,timer);
  endtask
  task automatic run_explicit(input int kind, input logic [63:0] cause,
                               input logic [63:0] value = 0, input logic [63:0] gpa = 0,
                               input logic [63:0] transformed = 0);
    @(negedge clock); prepare(kind);
    for (int limit = 0; limit < 30000; limit++) begin
      @(negedge clock);
      if (traps == 1) break;
    end
    assert (traps == 1) else $fatal(1, "explicit case %0d timeout PC=%h", kind, instruction_address);
    assert (read64(SIGNATURE) == cause && read64(SIGNATURE+8) == value &&
            read64(SIGNATURE+16) == gpa >> 2 && read64(SIGNATURE+24) == transformed &&
            read64(SIGNATURE+40) == expected_pc)
      else $fatal(1, "explicit case %0d cause/tval/htval/htinst/epc: %h %h %h %h %h expected PC %h", kind,
                  read64(SIGNATURE), read64(SIGNATURE+8), read64(SIGNATURE+16), read64(SIGNATURE+24), read64(SIGNATURE+40), expected_pc);
    assert (!virtualized && read64('h19020) == 'hfeed) else $fatal(1, "explicit case %0d lost precise ownership", kind);
    if (cause inside {4,6,13,15,21,23})
      assert ((read64(SIGNATURE+32) & 'h40) != 0) else $fatal(1, "explicit fault lost GVA");
    if (kind == 14) begin
      assert (read64(SIGNATURE+128) == 64'hffffffffffffff80 && read64(SIGNATURE+136) == 'h80);
      assert (read64(SIGNATURE+144) == 64'hffffffffffff8080 && read64(SIGNATURE+152) == 'h8080);
      assert (read64(SIGNATURE+160) == 64'hffffffff80008080 && read64(SIGNATURE+168) == 64'h80008080);
      assert (read64(SIGNATURE+176) == 64'h8000000080008080);
      assert (read64('h19008) == 64'h9abcdef0def000f0 && read64('h19010) == 64'h123456789abcdef0)
        else $fatal(1, "HSV width or lane placement");
    end
    if (kind inside {15,18,35}) assert (read64(SIGNATURE+56) == 'h8080) else $fatal(1, "HLVX data");
    if (kind == 31) assert (read64(SIGNATURE+56) == 64'h80008080) else $fatal(1, "HLVX.WU data");
    if (kind inside {22,24,34}) assert (read64(SIGNATURE+56) == 64'h8000000080008080) else $fatal(1, "SPVP/HU/MPRV data");
    if (kind == 30) assert (read64('h19000) == 64'h8000000080008080) else $fatal(1, "denied HSV mutated memory");
    $display("explicit guest core case %0d passed", kind);
  endtask

  task automatic run_case(input int kind, input logic [63:0] cause,
                           input logic [63:0] value, input logic [63:0] gpa,
                           input logic [63:0] transformed = 0);
    @(negedge clock); prepare(kind);
    for (int limit = 0; limit < 30000; limit++) begin
      @(negedge clock);
      if (traps >= ((kind == 8) ? 2 : 1)) break;
    end
    assert (traps == ((kind == 8) ? 2 : 1)) else $fatal(1, "case %0d timed out, walks=%0d stores=%0d PC=%h V=%b", kind, walks, stores, instruction_address, virtualized);
    assert (read64(SIGNATURE) == cause && read64(SIGNATURE+8) == value &&
            read64(SIGNATURE+16) == (gpa >> 2) && read64(SIGNATURE+24) == transformed)
      else $fatal(1, "case %0d trap cause/tval/htval/htinst: %h %h %h %h", kind, read64(SIGNATURE), read64(SIGNATURE+8), read64(SIGNATURE+16), read64(SIGNATURE+24));
    assert (walks > 0 && !virtualized) else $fatal(1, "guest did not translate or leave VS");
    assert (read64(SIGNATURE+40) == expected_pc) else $fatal(1, "case %0d lost EPC: %h expected %h", kind, read64(SIGNATURE+40), expected_pc);
    assert (read64('h19020) == 'hfeed) else $fatal(1, "younger store escaped a precise fault");
    if (kind != 0 && kind != 8 && kind != 9 && kind != 10 && kind != 11)
      assert ((read64(SIGNATURE+32) & 'h40) != 0) else $fatal(1, "fault lost GVA");
    if (kind == 0 || kind == 8)
      assert (read64('h19008) == 'h1235) else $fatal(1, "guest load/store result wrong");
    if (kind == 8)
      assert (read64('h1a010) == 'h5678 && flushes >= 3) else $fatal(1, "HFENCE/SRET did not observe remapped data");
    if (kind == 10)
      assert (read64(SIGNATURE+56) == 'h1234) else $fatal(1, "MPRV/MPV data translation failed");
    if (kind == 11)
      assert (read64('h19080) == 13 && read64('h19088) == GVA+'h3000) else $fatal(1, "VS delegation lost cause or address");
    $display("guest core case %0d passed: %0d PTE reads, %0d flushes", kind, walks, flushes);
  endtask
  task automatic run_fp(input int kind);
    // Reuse the delayed physical memory and three-level VS/G page tables.
    @(negedge clock); prepare(0); reset = 1; scenario = kind;
    machine_boot();
    csrw('h305, 'h2000); csrw('h105, 'h3000); csrw('h302, 'hffffff);
    if (kind == 48) begin csrw('h303,'h20); csrw('h304,'h20); end
    csrw('h280, 64'h8000000000000008); csrw('h680, 64'h8000000000000004);
    csrw('h200, (kind inside {40,42}) ? 0 : 'h2000);
    csrw('h300, 64'h8000000800 | ((kind inside {41,43}) ? 0 : 'h2000));
    csrw('h341, GVA); emit(32'h30200073);
    write64('h19000, 'h3fc00000); // single 1.5
    write64('h19008, 64'h3ff0000000000000); // double 1
    write64('h19010, 64'h4008000000000000); // double 3
    cursor = 'h18000;
    emit(32'h40001537);
    expected_pc = GVA+4;
    case (kind)
      40,41: emit(fp('h78, 1, 0, 0)); // fmv.w.x: disabled even without flags
      42,43: emit(32'h00302673); // frcsr: illegal, not virtual instruction
      44: begin emit(32'h40002537); expected_pc = GVA+8; emit(fld(1,10,0)); end
      45: begin emit(32'h40002537); expected_pc = GVA+8; emit(fsd(1,10,0)); end
      default: begin
        emit(fld(1,10,0,1)); emit(fp(0,2,1,1)); emit(fsd(2,10,40,1)); // F add
        emit(fld(4,10,8)); emit(fld(5,10,16));
        emit(32'h0021d073); // csrwi frm,3: dynamic RUP
        emit(fp('h0d,6,4,5,7)); // fdiv.d f6,f4,f5,dyn, nontrivial latency
        if (kind == 46) begin
          // No dependency on f6: synchronous fault must drain the older divide.
          expected_pc = GVA+32; emit(32'hffffffff);
        end else if (kind == 47) begin
          // A faulting FP load must drain the older divide and keep its flags.
          emit(32'h400025b7); expected_pc = GVA+36; emit(fld(7,11,0));
        end else if (kind == 48) begin
          emit(sd(0,10,120));
          repeat (64) emit(32'h00000013);
          emit(sd(0,10,32)); emit(32'h0000006f);
        end else begin
          emit(fsd(6,10,48));
          emit(32'h00302673); emit(sd(12,10,64)); // shared fcsr
          emit(32'h10002673); emit(sd(12,10,72)); // guest FS/SD
          expected_pc = GVA+52;
          emit(32'h00000073);
          // HS cleans only VS.FS, then returns without changing the FPRs.
          emit(fsd(6,10,56)); emit(32'h10002673); emit(sd(12,10,80));
          emit(32'h00000073); emit(32'h0000006f);
        end
      end
    endcase
    if (kind != 39) begin emit(sd(0,10,32)); emit(32'h0000006f); end
    cursor = 'h3000;
    emit(32'h00020a37);
    save_csr('h142,0); save_csr('h143,8); save_csr('h643,16); save_csr('h64a,24);
    save_csr('h141,40); save_csr('h100,96); save_csr('h200,104);
    if (kind inside {39,46,47,48}) begin
      save_csr('h003,80); emit(fsd(6,20,88));
    end
    emit(addi(21,0,85)); emit(sd(21,20,48));
    if (kind == 39) begin
      csrw('h200,'h2000); // cleaning guest FS must not clean HS FS
      emit(32'h14102af3); emit(addi(21,21,4)); emit(32'h141a9073); emit(32'h10200073);
    end else emit(32'h0000006f);
    repeat (5) @(negedge clock); reset = 0;
    for (int limit = 0; limit < 30000; limit++) begin
      @(negedge clock);
      if (traps == (kind == 39 ? 2 : 1)) break;
    end
    assert (traps == (kind == 39 ? 2 : 1)) else $fatal(1,"FP guest %0d timeout PC=%h",kind,instruction_address);
    assert (!virtualized && walks > 0) else $fatal(1,"FP guest did not translate and trap");
    if (kind inside {39,46,47,48}) begin
      assert (read64('h19028) == 'h40400000 && read64(SIGNATURE+88) == 64'h3fd5555555555556 &&
              read64(SIGNATURE+80) == 'h61)
        else $fatal(1,"FP guest %0d result/flags not drained: %h %h %h",kind,read64('h19028),read64(SIGNATURE+88),read64(SIGNATURE+80));
      assert ((read64(SIGNATURE+96) & 64'h8000000000006000) == 64'h8000000000006000)
        else $fatal(1,"guest modification did not dirty HS");
    end
    case (kind)
      39: begin
        assert (read64(SIGNATURE) == 10 && read64('h19030) == 64'h3fd5555555555556 &&
                read64('h19038) == read64('h19030) && read64('h19040) == 'h61 &&
                read64('h19048) == 64'h8000000200006000 && read64('h19050) == 'h200002000)
          else $fatal(1,"guest FP arithmetic, shared state, or SRET status mismatch");
      end
      40,41,42,43,46: begin
        assert (read64(SIGNATURE) == 2 && read64(SIGNATURE+40) == expected_pc)
          else $fatal(1,"FP guest %0d did not take precise illegal trap: %h %h",kind,read64(SIGNATURE),read64(SIGNATURE+40));
      end
      44,45,47: begin
        assert (read64(SIGNATURE) == (kind == 45 ? 23 : 21) && read64(SIGNATURE+8) == GVA+'h2000 &&
                read64(SIGNATURE+16) == 'h4800 && read64(SIGNATURE+40) == expected_pc)
          else $fatal(1,"FP guest %0d lost two-stage fault provenance",kind);
      end
      48: begin
        assert (read64(SIGNATURE) == 64'h8000000000000005 && read64(SIGNATURE+40) >= GVA+36 &&
                read64(SIGNATURE+40) < GVA+292)
          else $fatal(1,"interrupt lost precise FP completion boundary");
      end
    endcase
    if (kind != 39) assert (read64('h19020) == 'hfeed) else $fatal(1,"younger FP fault mutation escaped");
    $display("paged FP guest %0d passed",kind);
  endtask

  task automatic run_vector(input int kind, input bit timer = 0);
    logic [63:0] fault_pc;
    bit restart;
    restart = kind == 56;
    @(negedge clock); prepare(0); reset = 1; scenario = kind;
    sstc_case = timer;
    machine_boot();
    csrw('h305,'h2000); csrw('h105,'h3000); csrw('h302,'hffffff);
    if (kind == 59) begin csrw('h303,'h20); csrw('h304,'h20); end
    if (timer) begin csrw('h14d,101); csrw('h30a,64'h8000000000000000); end
    csrw('h280,64'h8000000000000008); csrw('h680,64'h8000000000000004);
    csrw('h200,(kind inside {51,53}) ? 'h2000 : (kind == 54 ? 'h200 : 'h2200));
    csrw('h300,64'h8000000800 | (kind == 52 ? 'h2000 : 'h2200));
    csrw('h341,GVA); emit(32'h30200073);
    write64('h19000,11); write64('h19008,22);
    write64('h19040,0); write64('h19048,'h1000);
    write64('h19ff8,11); write64('h1c000,22);
    if (kind == 55) write64('hd090,('h1c << 10) | 'hdf);
    if (kind inside {50,54}) begin
      write64('h19000,64'h400000003f800000); // 1, 2
      write64('h19008,64'h4080000040400000); // 3, 4
    end
    cursor = 'h18000;
    emit(32'h40001537); emit(addi(11,0,(kind inside {50,54}) ? 4 : 2));
    fault_pc = GVA+64'(cursor-'h18000);
    if (kind == 53) emit(32'hc2202673); // vlenb, disabled in VS
    else emit(vset((kind inside {50,54}) ? 2 : 3));
    if (kind == 60) begin
      emit(addi(11,0,1)); emit(32'h01f59593); // guest VA 0x80000000, implicit PTE at GPA 0xb000
      fault_pc = GVA+64'(cursor-'h18000); emit(vmem(0,3,2,11));
    end else if (kind == 61) begin
      emit(32'h400025b7);
      fault_pc = GVA+64'(cursor-'h18000); emit(vmem(0,3,2,11,0,1));
    end else if (kind == 56) begin
      emit(addi(13,10,64)); emit(vmem(0,3,4,13));
      fault_pc = GVA+64'(cursor-'h18000); emit(vmem(0,3,2,10,1));
    end else if (kind == 57) begin
      emit(vmem(0,3,2,10)); emit(32'h400025b7); emit(addi(11,11,-8));
      fault_pc = GVA+64'(cursor-'h18000); emit(vmem(1,3,2,11));
    end else if (kind inside {55,58}) begin
      emit(32'h400025b7); emit(addi(11,11,-8));
      emit(vmem(0,3,2,11,0,kind == 58));
    end else begin
      emit(vmem(0,(kind inside {50,54}) ? 2 : 3,2,10));
      if (kind inside {50,54}) begin
        fault_pc = GVA+64'(cursor-'h18000); emit(vadd(2,2,2,1));
      end else if (kind == 49) emit(vadd(2,2,1,3)); // vadd.vi, 1
    end
    if (kind == 59) begin
      repeat (64) emit(32'h00000013);
      emit(32'h0000006f);
    end else begin
      emit(addi(13,10,128)); emit(vmem(1,(kind inside {50,54}) ? 2 : 3,2,13));
      emit(32'hc2002673); emit(sd(12,10,144)); // vl after fault-first
      emit(32'h00802673); emit(sd(12,10,152)); // vstart after completion
      emit(32'h00000073); emit(32'h0000006f);
    end
    cursor = 'h3000;
    emit(32'h00020a37);
    save_csr('h142,0); save_csr('h143,8); save_csr('h643,16); save_csr('h64a,24);
    save_csr('h600,32); save_csr('h141,40); save_csr('h100,96); save_csr('h200,104);
    if (kind != 52) save_csr('h008,112);
    if (kind == 59) begin
      emit(addi(13,20,256)); emit(vmem(1,3,2,13)); // observes shared VRF after IRQ drain
      emit(32'hc2002673); emit(sd(12,20,272));
    end
    emit(addi(21,0,85)); emit(sd(21,20,48));
    if (restart) begin
      constant(22,'hd090); constant(23,('h1c << 10) | 'hdf);
      emit(sd(23,22,0)); emit(32'h62000073); // repair G mapping and invalidate
      constant(22,'h19000); emit(sd(0,22,0)); // element 0 must not execute again
      emit(32'h10200073); // retry exactly sepc, retaining vstart=1
    end else emit(32'h0000006f);
    repeat (5) @(negedge clock); reset = 0;
    for (int limit = 0; limit < 50000; limit++) begin
      @(negedge clock);
      if (traps == 1) break;
    end
    assert (traps == 1) else $fatal(1,"vector guest %0d timeout PC=%h",kind,instruction_address);
    if (kind inside {51,52,53,54}) begin
      assert (read64(SIGNATURE) == 2 && read64(SIGNATURE+40) == fault_pc)
        else $fatal(1,"vector status gate %0d cause/PC %h %h expected %h",kind,read64(SIGNATURE),read64(SIGNATURE+40),fault_pc);
    end else if (kind inside {56,57,60,61}) begin
      assert (read64(SIGNATURE) == (kind == 57 ? 23 : 21) && read64(SIGNATURE+8) == (kind == 60 ? 64'h80000000 : GVA+'h2000) &&
              read64(SIGNATURE+16) == (kind == 60 ? 'h2c00 : 'h4800) && read64(SIGNATURE+24) == (kind == 60 ? 'h3000 : 0) &&
              read64(SIGNATURE+40) == fault_pc && read64(SIGNATURE+112) == (kind inside {60,61} ? 0 : 1) &&
              (read64(SIGNATURE+32) & 'h40) != 0)
        else $fatal(1,"vector fault %0d lost cause/GPA/PC/vstart: %h %h %h %h %h",kind,read64(SIGNATURE),read64(SIGNATURE+8),read64(SIGNATURE+16),read64(SIGNATURE+40),read64(SIGNATURE+112));
      if (kind == 57) assert (read64('h19ff8) == 11 && read64('h19080) == 0)
        else $fatal(1,"faulting vector store lost precise element ordering");
    end else if (kind == 59) begin
      assert (read64(SIGNATURE) == 64'h8000000000000005 && read64(SIGNATURE+256) == 11 &&
              read64(SIGNATURE+264) == 22 && read64(SIGNATURE+272) == 2)
        else $fatal(1,"guest vector IRQ did not drain shared state");
    end else begin
      assert (read64(SIGNATURE) == 10) else $fatal(1,"vector guest %0d cause %h",kind,read64(SIGNATURE));
      if (kind == 50) begin
        assert (read64('h19080) == 64'h4080000040000000 && read64('h19088) == 64'h4100000040c00000)
          else $fatal(1,"guest vector FP results");
      end else begin
        assert (read64('h19080) == (kind == 49 ? 12 : 11) &&
                read64('h19088) == (kind == 58 ? 0 : kind == 49 ? 23 : 22))
          else $fatal(1,"guest vector %0d results %h %h",kind,read64('h19080),read64('h19088));
      end
      assert (read64('h19090) == (kind == 58 ? 1 : kind == 50 ? 4 : 2) && read64('h19098) == 0)
        else $fatal(1,"guest vector VL/vstart");
      assert ((read64(SIGNATURE+96) & 'h600) == 'h600 && (read64(SIGNATURE+104) & 'h600) == 'h600)
        else $fatal(1,"guest vector state did not dirty both banks");
    end
    if (restart) begin
      for (int limit = 0; limit < 50000; limit++) begin
        @(negedge clock);
        if (traps == 2) break;
      end
      assert (traps == 2 && read64(SIGNATURE) == 10 && read64('h19080) == 11 &&
              read64('h19088) == 22 && read64('h19098) == 0 && flushes >= 3)
        else $fatal(1,"guest vector restart/HFENCE lost partial result: %h %h %h",read64(SIGNATURE),read64('h19080),read64('h19088));
    end
    $display("paged vector guest %0d passed",kind);
  endtask

  task automatic run_cmo(input int op, input int mode, input int policy);
    bit guest, user_mode, illegal, virt, force_flush;
    logic [31:0] instruction;
    logic [63:0] m, h, s, operation_pc;
    guest = mode inside {1,2}; user_mode = mode inside {2,3};
    m = policy inside {1,7} ? 1 : policy == 6 ? 'hd1 : 'hf1;
    h = policy inside {2,7,8} ? 1 : policy == 4 ? 'hd1 : 'hf1;
    s = policy == 3 ? 1 : policy == 5 ? 'hd1 : 'hf1;
    illegal = policy inside {1,7} || (mode == 3 && policy == 3);
    virt = guest && (policy inside {2,8} || (mode == 2 && policy == 3));
    force_flush = op == 0 && (policy == 6 || (guest && policy == 4) || (user_mode && policy == 5));
    instruction = {12'(op == 3 ? 4 : op),5'd10,3'b010,5'd0,7'h0f};
    @(negedge clock); prepare(0); reset = 1; scenario = 100+op*40+mode*10+policy;
    machine_boot();
    csrw('h305,'h2000); csrw('h105,'h3000); csrw('h302,'hffffff);
    csrw('h30a,m); csrw('h60a,h); csrw('h10a,s);
    csrw('h280,64'h8000000000000008); csrw('h680,64'h8000000000000004);
    csrw('h300,(guest ? 64'h8000000000 : 0) | (user_mode ? 0 : 'h800));
    csrw('h341,guest ? GVA : 'h5000); emit(32'h30200073);
    if (guest && user_mode) begin
      write64('ha000,('h10 << 10) | 'hdb); write64('ha008,('h11 << 10) | 'hd7);
    end
    write64('h19000,'h12345678); write64('h19038,'habcdef);
    cursor = guest ? 'h18000 : 'h5000;
    emit(guest ? 32'h40001537 : 32'h00019537); // data base
    emit(addi(11,0,'h66)); emit(sd(11,10,128));
    emit(addi(10,10,7));
    if (policy == 8) emit(32'h40002537); // denial precedes missing guest translation
    operation_pc = (guest ? GVA : 64'h5000) + 64'(cursor-(guest ? 'h18000 : 'h5000));
    emit(instruction);
    emit(32'h0840000f); // fence i,o: FIOM requires memory as well
    emit(guest ? 32'h40001537 : 32'h00019537);
    emit(sd(11,10,136)); // younger observable effect
    emit(32'h00000073); emit(32'h0000006f);
    cursor = 'h3000;
    emit(32'h00020a37); save_csr('h142,0); save_csr('h143,8); save_csr('h141,40);
    emit(addi(21,0,85)); emit(sd(21,20,48)); emit(32'h0000006f);
    repeat (5) @(negedge clock); reset = 0;
    for (int limit = 0; limit < 30000; limit++) begin
      @(negedge clock); if (traps == 1) break;
    end
    assert (traps == 1) else $fatal(1,"CBO op=%0d mode=%0d policy=%0d timeout",op,mode,policy);
    if (illegal || virt) begin
      assert (read64(SIGNATURE) == (illegal ? 2 : 22) && read64(SIGNATURE+8) == 64'(instruction) &&
              read64(SIGNATURE+40) == operation_pc && cmo_count == 0 && read64('h19088) == 0 &&
              read64('h19000) == 'h12345678 && read64('h19038) == 'habcdef)
        else $fatal(1,"CBO denial op=%0d mode=%0d policy=%0d cause=%h tval=%h effects=%0d",op,mode,policy,read64(SIGNATURE),read64(SIGNATURE+8),cmo_count);
    end else begin
      assert (read64(SIGNATURE) == (user_mode ? 8 : guest ? 10 : 9) && cmo_count == 1 &&
              last_cmo == (op == 3 ? 6 : force_flush ? 9 : 7+op) && read64('h19088) == 'h66)
        else $fatal(1,"CBO execution op=%0d mode=%0d policy=%0d cause=%h effects=%0d/%0d",op,mode,policy,read64(SIGNATURE),cmo_count,last_cmo);
      assert (read64('h19000) == (op == 3 ? 0 : 'h12345678) && read64('h19038) == (op == 3 ? 0 : 'habcdef))
        else $fatal(1,"CBO data mutation");
    end
  endtask

  task automatic run_pbmt(input int vs_type, input int g_type, input int pte_type,
                          input bit vs_paged = 1, input bit g_paged = 1,
                          input bit vectors = 0, input bit enable_m = 1, input bit enable_h = 1);
    logic [63:0] cause, entry, data_va;
    bit bad_vs, bad_g, bad_pte;
    @(negedge clock); prepare(0); reset = 1; scenario = 80;
    expected_type = vs_paged && vs_type != 0 ? vs_type : g_paged ? g_type : 0;
    expected_pte_type = g_paged ? pte_type : 0;
    pbmt_code = g_paged ? 'h18000 : 'h10000;
    pbmt_data = g_paged ? 'h19000 : 'h11000;
    entry = vs_paged ? GVA : 'h10000;
    data_va = vs_paged ? GVA+'h1000 : 'h11000;
    bad_vs = vs_paged && (vs_type == 3 || (vs_type != 0 && !(enable_m && enable_h)));
    bad_g = g_paged && (g_type == 3 || (g_type != 0 && !enable_m));
    bad_pte = vs_paged && g_paged && (pte_type == 3 || (pte_type != 0 && !enable_m));
    cause = bad_pte ? 20 : bad_vs ? 12 : bad_g ? 20 : 10;
    machine_boot();
    csrw('h305,'h2000); csrw('h105,'h3000); csrw('h302,'hffffff);
    csrw('h30a,enable_m ? 64'h4000000000000000 : 0);
    csrw('h60a,enable_h ? 64'h4000000000000000 : 0);
    csrw('h280,vs_paged ? 64'h8000000000000008 : 0);
    csrw('h680,g_paged ? 64'h8000000000000004 : 0);
    csrw('h200,'h600); csrw('h300,64'h8000000e00);
    csrw('h341,entry); emit(32'h30200073);
    write64('ha000,read64('ha000) | (64'(vs_type) << 61));
    write64('ha008,read64('ha008) | (64'(vs_type) << 61));
    write64('hd080,read64('hd080) | (64'(g_type) << 61));
    write64('hd088,read64('hd088) | (64'(g_type) << 61));
    for (int p = 'hd040; p <= 'hd050; p += 8) write64(p,read64(64'(p)) | (64'(pte_type) << 61));
    write64(int'(pbmt_data),'h1234); write64(int'(pbmt_data+8),'h5678);
    cursor = int'(pbmt_code);
    emit(vs_paged ? 32'h40001537 : 32'h00011537);
    if (vectors) begin
      emit(addi(11,0,2)); emit(vset(3)); emit(vmem(0,3,2,10));
      emit(addi(10,10,32)); emit(vmem(1,3,2,10));
    end else begin
      emit(ld(11,10,0)); emit(ld(12,10,8)); emit(sd(11,10,32)); emit(sd(12,10,40));
    end
    emit(32'h00000073); emit(32'h0000006f);
    pbmt_check = 1; pbmt_data_reads = 0;
    repeat (5) @(negedge clock); reset = 0;
    for (int limit = 0; limit < 30000; limit++) begin
      @(negedge clock); if (traps == 1) break;
    end
    assert (traps == 1 && read64(SIGNATURE) == cause)
      else $fatal(1,"PBMT VS=%0d G=%0d PTE=%0d modes=%b%b enable=%b%b cause=%h expected=%h PC=%h",vs_type,g_type,pte_type,vs_paged,g_paged,enable_m,enable_h,read64(SIGNATURE),cause,instruction_address);
    if (cause == 10)
      assert (read64(pbmt_data+32) == 'h1234 && read64(pbmt_data+40) == 'h5678 && pbmt_data_reads >= 2)
        else $fatal(1,"PBMT scalar/vector data lost");
    else assert (read64(SIGNATURE+40) == entry && pbmt_data_reads == 0)
      else $fatal(1,"faulting PBMT instruction had effects");
    pbmt_check = 0;
  endtask

  // Execute real tagged accesses through nested translation. Different source
  // controls deliberately disagree, so selecting the host's mask cannot pass.
  task automatic run_pointer_mask(input int kind, input int mode, input bit vs_paged = 1,
                                   input bit g_paged = 1, input int fault = 0,
                                   input bit vectors = 0);
    bit normal_guest, target_user, caller_user, mprv, machine_caller;
    logic [63:0] data_va, data_pa, code_pa, code_va, tagged_address, cause, fault_pc, mask_bits;
    normal_guest = kind < 2; target_user = kind inside {1,3,5,7};
    caller_user = kind == 3; mprv = kind inside {4,5}; machine_caller = mprv || kind == 8;
    data_va = vs_paged ? GVA+'h1000 : 'h11000;
    data_pa = g_paged ? 'h19000 : 'h11000;
    code_pa = normal_guest ? (g_paged ? 'h18000 : 'h10000) : 'h5000;
    code_va = normal_guest ? (vs_paged ? GVA : 'h10000) : 'h5000;
    mask_bits = mode == 2 ? 64'hfe00000000000000 : 64'habcd000000000000;
    tagged_address = data_va | mask_bits | (fault == 1 ? 1 : 0);
    cause = fault == 1 ? 4 : fault != 0 || kind == 6 ? (vs_paged ? 13 : g_paged ? 21 : 5) : normal_guest ? (target_user ? 8 : 10) : caller_user ? 8 : 9;
    @(negedge clock); prepare(0); reset = 1; scenario = 90;
    machine_boot();
    csrw('h305,'h2000); csrw('h105,'h3000); csrw('h302,'hffffff);
    csrw('h280,vs_paged ? 64'h8000000000000008 : 0);
    csrw('h680,g_paged ? 64'h8000000000000004 : 0);
    csrw('h60a,target_user ? 0 : 64'(mode)<<32);
    csrw('h10a,target_user && !caller_user ? 64'(mode)<<32 : 0);
    csrw('h600,(caller_user ? 64'(mode)<<48 : 0) | (target_user ? 0 : 'h100) | 'h200);
    csrw('h200,'h600 | (fault == 3 ? 'h80000 : 0));
    constant(10,tagged_address); constant(11,2); constant(29,'h20000);
    csrw('h300,(normal_guest || mprv ? 64'h8000000000 : 0) | (machine_caller ? 'h1800 : caller_user || (normal_guest && target_user) ? 0 : 'h800) | 'h600 | (fault == 2 ? 'h80000 : 0));
    csrw('h341,code_va); emit(32'h30200073);
    if (normal_guest && target_user) write64('ha000,('h10<<10)|'hdb);
    write64('ha008,('h11<<10)|(target_user ? 'hdf : 'hcf)); // HLVX uses X, but never masks
    write64(int'(data_pa),'h1234); write64(int'(data_pa+8),'h5678);
    write64(int'(data_pa+32),0); write64(int'(data_pa+40),0);
    cursor = int'(code_pa);
    if (mprv) begin
      // Set effective VS/VU only after entering M, with no host data access
      // until the explicitly cleared MPRV after the tested operations.
      constant(1,64'h8000020000 | (target_user ? 0 : 'h800) | 'h600 | (fault == 2 ? 'h80000 : 0));
      emit(32'h30009073);
    end
    fault_pc = code_va + 64'(cursor-int'(code_pa));
    if (vectors) begin
      emit(vset(3)); fault_pc += 4;
      emit(vmem(0,3,2,10)); emit(addi(10,10,32)); emit(vmem(1,3,2,10));
    end else if (normal_guest || mprv) begin
      emit(ld(12,10,0)); emit(ld(13,10,8)); emit(sd(12,10,32)); emit(sd(13,10,40));
    end else begin
      emit(guest_load(kind == 6 ? 2 : 3, kind == 6 ? 3 : 0));
      emit(addi(11,12,0));
      if (kind == 2 && fault == 0) begin
        // Change HS-owned policy between guest transactions; the younger HSV
        // must capture the new mode after the serializing CSR restart.
        csrw('h60a,64'(mode == 2 ? 3 : 2)<<32);
        constant(10,(data_va+32) | (mode == 2 ? 64'habcd000000000000 : 64'hfe00000000000000));
      end else emit(addi(10,10,32));
      emit(guest_store(3));
    end
    if (machine_caller) begin
      if (mprv) emit(32'h300eb073); // clear MPRV without a host memory operand
      csrw('h300,'h800); csrw('h341,'h6000); emit(32'h30200073);
      cursor = 'h6000;
    end
    emit(32'h00000073); emit(32'h0000006f);
    repeat (5) @(negedge clock); reset = 0;
    for (int limit = 0; limit < 30000; limit++) begin
      @(negedge clock); if (traps == 1) break;
    end
    assert (traps == 1 && read64(SIGNATURE) == cause)
      else $fatal(1,"guest PMM kind=%0d mode=%0d paged=%b%b fault=%0d vector=%b cause=%h expected=%h pc=%h",kind,mode,vs_paged,g_paged,fault,vectors,read64(SIGNATURE),cause,instruction_address);
    if (fault != 0 || kind == 6) begin
      assert (read64(SIGNATURE+8) == (fault == 1 ? data_va+1 : tagged_address) && read64(SIGNATURE+40) == fault_pc && read64(data_pa+32) == 0)
        else $fatal(1,"guest PMM fault provenance/effects kind=%0d tval=%h pc=%h expected=%h data=%h",kind,read64(SIGNATURE+8),read64(SIGNATURE+40),fault_pc,read64(data_pa+32));
    end else begin
      assert (read64(data_pa+32) == 'h1234 && (!(normal_guest || mprv) || read64(data_pa+40) == 'h5678))
        else $fatal(1,"guest PMM data lost kind=%0d vector=%b",kind,vectors);
    end
  endtask

  task automatic run_state_enable(input int target, input bit machine_allow, input bit guest_allow,
                                  input bit user_mode, input bit write_access);
    int address, index;
    logic [31:0] instruction;
    logic [63:0] gate, expected_cause;
    bit allowed;
    address = target == 0 ? 'h10a : target == 1 ? 'h10e : 'h60a;
    index = target == 1 ? 2 : 0;
    gate = target == 1 ? 64'h8000000000000000 : 64'h4000000000000000;
    allowed = machine_allow && guest_allow && !user_mode && target != 2;
    expected_cause = allowed ? 10 : machine_allow ? 22 : 2;
    @(negedge clock); prepare(0); reset = 1;
    machine_boot();
    csrw('h305,'h2000); csrw('h105,'h3000); csrw('h302,'hffffff);
    csrw('h280,64'h8000000000000008); csrw('h680,64'h8000000000000004);
    csrw('h10a,0);
    csrw('h60c+index,guest_allow ? gate : 0);
    csrw('h30c+index,machine_allow ? gate : 0);
    csrw('h300,user_mode ? 64'h8000000000 : 64'h8000000800);
    csrw('h341,GVA); emit(32'h30200073);
    if (user_mode) begin
      write64('ha000,('h10 << 10) | 'hdb);
      write64('ha008,('h11 << 10) | 'hd7);
    end
    instruction = {12'(address), (write_access ? 5'd14 : 5'd0), (write_access ? 3'b001 : 3'b010), 5'd12, 7'h73};
    cursor = 'h18000;
    emit(32'h40001537); emit(addi(14,0,1)); emit(instruction);
    emit(addi(13,0,1)); emit(sd(13,10,32)); emit(32'h00000073); emit(32'h0000006f);
    repeat (5) @(negedge clock); reset = 0;
    for (int limit = 0; limit < 30000; limit++) begin
      @(negedge clock); if (traps == 1) break;
    end
    assert (traps == 1 && !virtualized && read64(SIGNATURE) == expected_cause &&
            read64(SIGNATURE+40) == GVA + (allowed ? 20 : 8) &&
            read64(SIGNATURE+8) == (allowed ? 0 : 64'(instruction)) &&
            read64('h19020) == (allowed ? 1 : 'hfeed))
      else $fatal(1,"state-enable guest failed target=%0d M=%b H=%b U=%b W=%b cause=%h pc=%h",target,machine_allow,guest_allow,user_mode,write_access,read64(SIGNATURE),read64(SIGNATURE+40));
  endtask

  task automatic run_invalidation(input int kind, input bit batch);
    logic [31:0] invalidate;
    @(negedge clock); prepare(0); reset = 1;
    machine_boot();
    csrw('h305,'h2000); csrw('h105,'h3000); csrw('h302,'hffffff);
    csrw('h280,64'h8000000000000008); csrw('h680,64'h8000000000000004);
    csrw('h300,kind == 0 ? 64'h8000000800 : 'h800);
    csrw('h600,'h100); // HS explicit loads use VS permissions (SPVP=S)
    csrw('h341,kind == 0 ? GVA : 'h5000); emit(32'h30200073);
    // New guest data page and a guest-writable alias of its own leaf PTE page.
    write64('hd090,('h1c << 10) | 'hdf);
    write64('hd098,('ha << 10) | 'hdf);
    write64('ha010,('h13 << 10) | 'hc7);
    write64('h1c000,'h5678);
    invalidate = kind == 0 ? 'h16000073 : kind == 1 ? 'h26000073 : 'h66000073;
    cursor = kind == 0 ? 'h18000 : 'h5000;
    emit(32'h40001537); // data VA in x10
    if (kind == 0) begin
      emit(ld(12,10,0)); emit(sd(12,10,8)); // warm old composed translation
      emit(32'h40002737); // guest alias of leaf PTE page in x14
      emit(32'h000057b7); emit(addi(15,15,(('h12 << 10) | 'hc7)-'h5000)); // PTE -> GPA 0x12000
      emit(sd(15,14,8));
    end else begin
      emit(32'h00020a37); // signature in x20
      emit(guest_load(3,0)); emit(sd(12,20,128));
      constant(22,kind == 1 ? 'ha008 : 'hd088);
      constant(23,kind == 1 ? ('h12 << 10) | 'hc7 : ('h1c << 10) | 'hdf);
      emit(sd(23,22,0));
    end
    emit(32'h18000073); // SFENCE.W.INVAL after delayed PTE store
    emit(invalidate);
    if (batch) emit(invalidate | (5 << 15) | (3 << 20)); // conservative all-entry scope
    emit(32'h18100073); // SFENCE.INVAL.IR before new implicit reads
    if (kind == 0) begin emit(ld(12,10,0)); emit(sd(12,10,16)); end
    else begin emit(guest_load(3,0)); emit(sd(12,20,136)); end
    emit(32'h00000073); emit(32'h0000006f);
    repeat (5) @(negedge clock); reset = 0;
    for (int limit = 0; limit < 30000; limit++) begin
      @(negedge clock); if (traps == 1) break;
    end
    assert (traps == 1 && read64(SIGNATURE) == (kind == 0 ? 10 : 9) && flushes >= (batch ? 4 : 3))
      else $fatal(1,"invalidation timeout/trap kind=%0d batch=%b cause=%h tval=%h htval=%h pc=%h",kind,batch,read64(SIGNATURE),read64(SIGNATURE+8),read64(SIGNATURE+16),read64(SIGNATURE+40));
    assert (kind == 0 ? (read64('h19008) == 'h1234 && read64('h1c010) == 'h5678) :
                       (read64(SIGNATURE+128) == 'h1234 && read64(SIGNATURE+136) == 'h5678))
      else $fatal(1,"stale translation after invalidation kind=%0d batch=%b",kind,batch);
  endtask

  task automatic run_invalidation_denial(input int op, input bit user_mode);
    logic [31:0] instruction;
    bit allowed;
    case (op)
      0: instruction = 'h16000073;
      1: instruction = 'h18000073;
      2: instruction = 'h18100073;
      3: instruction = 'h26000073;
      4: instruction = 'h66000073;
    endcase
    allowed = !user_mode && op inside {1,2};
    @(negedge clock); prepare(0); reset = 1;
    machine_boot();
    csrw('h305,'h2000); csrw('h105,'h3000); csrw('h302,'hffffff);
    csrw('h280,64'h8000000000000008); csrw('h680,64'h8000000000000004);
    csrw('h600,'h100000); // VTVM must not trap the two ordering instructions
    csrw('h300,user_mode ? 64'h8000100000 : 64'h8000100800); // TVM also set
    csrw('h341,GVA); emit(32'h30200073);
    if (user_mode) begin write64('ha000,('h10 << 10) | 'hdb); write64('ha008,('h11 << 10) | 'hd7); end
    cursor = 'h18000;
    emit(32'h40001537); emit(instruction); emit(sd(0,10,32)); emit(32'h00000073);
    repeat (5) @(negedge clock); reset = 0;
    for (int limit = 0; limit < 30000; limit++) begin
      @(negedge clock); if (traps == 1) break;
    end
    assert (traps == 1 && read64(SIGNATURE) == (allowed ? 10 : 22) &&
            read64(SIGNATURE+40) == GVA + (allowed ? 12 : 4) &&
            read64(SIGNATURE+8) == (allowed ? 0 : 64'(instruction)) &&
            read64('h19020) == (allowed ? 0 : 'hfeed))
      else $fatal(1,"invalidation precise trap op=%0d U=%b cause=%h pc=%h",op,user_mode,read64(SIGNATURE),read64(SIGNATURE+40));
  endtask

  task automatic run_sha_fetch(input int kind, input bit delegate_vs);
    logic [63:0] start_pc, cause, trap_value, fault_gpa, fault_inst;
    bit delegated;
    @(negedge clock); prepare(0); reset = 1;
    start_pc = GVA + (kind == 2 ? 'h1ffffe : 'hffe);
    cause = kind == 0 ? 12 : kind inside {1,2} ? 20 : kind == 3 ? 1 : kind inside {4,5} ? 2 : 10;
    trap_value = kind < 4 ? start_pc+2 : kind == 4 ? 'h8000 : kind == 5 ? 'hb : 0;
    fault_gpa = kind == 1 ? 'h11000 : kind == 2 ? 'hb000 : 0;
    fault_inst = kind == 2 ? 'h3000 : 0;
    delegated = delegate_vs && cause inside {1,2,12};
    machine_boot();
    csrw('h305,'h2000); csrw('h105,'h3000); csrw('h205,GVA+'h300);
    csrw('h302,'hffffff); csrw('h602,delegate_vs ? 'hffffff : 0);
    csrw('h280,64'h8000000000000008); csrw('h680,64'h8000000000000004);
    csrw('h300,64'h8000000800); csrw('h341,start_pc);
    constant(10,GVA+'h2000); emit(32'h30200073);
    // The second executable page and the handler's independent data page.
    write64('ha008,('h11 << 10) | 'hcb);
    write64('ha010,('h12 << 10) | 'hc7);
    write64('hd090,('h1c << 10) | 'hdf);
    write64('h1c020,'hfeed);
    if (kind == 0) write64('ha008,0);
    if (kind == 1) write64('hd088,0);
    if (kind == 2) begin
      // Crossing a VS leaf-table boundary makes the second half's G-stage
      // fault belong to an implicit VS PTE read, not the instruction GPA.
      write64('haff8,('h10 << 10) | 'hcb);
      write64('h9008,('hb << 10) | 1);
    end
    if (kind == 3) write64('hd088,('h1a << 10) | 'hdf); // non-executable PMA
    cursor='h18ffe;
    if (kind == 4) begin ram[cursor]=0; ram[cursor+1]='h80; cursor+=2; end
    else emit(kind == 5 ? 32'h0000000b : addi(12,0,85));
    if (kind == 6) emit(32'h00000073); // successfully assembled cross-page ADDI
    emit(sd(0,10,32)); emit(32'h0000006f);
    cursor='h18300;
    emit(32'h40002537);
    emit(32'h142026f3); emit(sd(13,10,128));
    emit(32'h143026f3); emit(sd(13,10,136));
    emit(32'h141026f3); emit(sd(13,10,144));
    emit(32'h00000073); emit(32'h0000006f);
    repeat (5) @(negedge clock); reset=0;
    for (int limit=0; limit<30000; limit++) begin
      @(negedge clock); if (traps == 1) break;
    end
    assert (traps == 1 && read64(SIGNATURE) == (delegated ? 10 : cause))
      else $fatal(1,"Sha fetch kind=%0d VS=%b cause=%h pc=%h",kind,delegate_vs,read64(SIGNATURE),read64(SIGNATURE+40));
    if (delegated) begin
      assert (read64('h1c080) == cause && read64('h1c088) == trap_value && read64('h1c090) == start_pc)
        else $fatal(1,"VS fetch provenance kind=%0d cause=%h tval=%h pc=%h",kind,read64('h1c080),read64('h1c088),read64('h1c090));
    end else begin
      assert (read64(SIGNATURE+8) == trap_value && read64(SIGNATURE+16) == (fault_gpa >> 2) &&
              read64(SIGNATURE+24) == fault_inst && read64(SIGNATURE+40) == start_pc+(kind == 6 ? 4 : 0))
        else $fatal(1,"HS fetch provenance kind=%0d tval=%h htval=%h htinst=%h pc=%h",kind,read64(SIGNATURE+8),read64(SIGNATURE+16),read64(SIGNATURE+24),read64(SIGNATURE+40));
    end
    assert (read64('h1c020) == 'hfeed && (kind != 6 || read64(SIGNATURE+56) == 85))
      else $fatal(1,"cross-page execution/precise squash kind=%0d",kind);
  endtask

  task automatic run_sha_data(input int kind, input bit delegate_vs);
    logic [63:0] cause, address;
    @(negedge clock); prepare(0); reset=1;
    cause = kind == 0 ? 13 : kind == 1 ? 15 : kind == 2 ? 5 : kind == 3 ? 7 : kind == 4 ? 4 : 6;
    address = GVA+'h1000+(kind >= 4 ? 1 : 0);
    machine_boot();
    csrw('h305,'h2000); csrw('h105,'h3000); csrw('h205,GVA+'h300);
    csrw('h302,'hffffff); csrw('h602,delegate_vs ? 'hffffff : 0);
    csrw('h280,64'h8000000000000008); csrw('h680,64'h8000000000000004);
    csrw('h300,64'h8000000800); csrw('h341,GVA); emit(32'h30200073);
    write64('ha010,('h12 << 10) | 'hc7); write64('hd090,('h1c << 10) | 'hdf);
    if (kind == 0) write64('ha008,0);
    if (kind == 1) write64('ha008,('h11 << 10) | 'hc3);
    if (kind inside {2,3}) scenario=13; // explicit physical service denial
    cursor='h18000; emit(32'h40001537);
    emit(kind[0] ? sd(0,10,kind >= 4 ? 1 : 0) : ld(12,10,kind >= 4 ? 1 : 0));
    emit(sd(0,10,32)); emit(32'h0000006f);
    cursor='h18300;
    emit(32'h40002537);
    emit(32'h142026f3); emit(sd(13,10,128));
    emit(32'h143026f3); emit(sd(13,10,136));
    emit(32'h141026f3); emit(sd(13,10,144));
    emit(32'h00000073); emit(32'h0000006f);
    repeat (5) @(negedge clock); reset=0;
    for (int limit=0; limit<30000; limit++) begin
      @(negedge clock); if (traps == 1) break;
    end
    assert (traps == 1 && read64(SIGNATURE) == (delegate_vs ? 10 : cause))
      else $fatal(1,"Sha data trap kind=%0d VS=%b cause=%h",kind,delegate_vs,read64(SIGNATURE));
    if (delegate_vs) begin
      assert (read64('h1c080) == cause && read64('h1c088) == address && read64('h1c090) == GVA+4)
        else $fatal(1,"VS data provenance kind=%0d cause=%h tval=%h pc=%h",kind,read64('h1c080),read64('h1c088),read64('h1c090));
    end else begin
      assert (read64(SIGNATURE+8) == address && read64(SIGNATURE+16) == 0 && read64(SIGNATURE+40) == GVA+4)
        else $fatal(1,"HS data provenance kind=%0d tval=%h htval=%h pc=%h",kind,read64(SIGNATURE+8),read64(SIGNATURE+16),read64(SIGNATURE+40));
    end
    assert (read64('h19000) == 'h1234 && read64('h19020) == 'hfeed)
      else $fatal(1,"faulting/younger store escaped kind=%0d",kind);
  endtask

  // Exercise the retained WB owner with a cache-supplied live reservation.
  // wake: 0 = timeout, 1 = early reservation loss, 2 = late loss, 3 = MTIP.
  task automatic run_wrs(input bit guest, input bit user_mode, input bit tw,
                         input bit vtw, input bit short_wait, input int wake);
    logic [31:0] instruction;
    logic [63:0] cause;
    int started, elapsed;
    bit fault_expected;
    reset = 1; scenario = 62; sstc_case = 0; pbmt_check = 0;
    reservation_valid = 1;
    instruction = short_wait ? 32'h01d00073 : 32'h00d00073;
    fault_expected = !short_wait && wake == 0 && (tw || (guest && vtw));
    cause = wake == 3 ? 64'h8000000000000007 : tw ? 64'd2 : 64'd22;
    for (int i = 0; i < 196608; i++) ram[i] = 0;
    machine_boot();
    csrw('h305, 'h2000); csrw('h105, 'h3000);
    csrw('h302, (1 << 2) | (1 << 22));
    csrw('h306, 4); csrw('h606, 4); csrw('h106, 4);
    csrw('h304, 128); // MTIP is locally enabled, including while WRS waits.
    csrw('h600, vtw ? 64'd1 << 21 : 0);
    csrw('h300, (guest ? 64'd1 << 39 : 0) | (user_mode ? 0 : 'h800) | (tw ? 64'd1 << 21 : 0));
    csrw('h341, 'h5000);
    constant(20, SIGNATURE);
    emit(32'h30200073);
    cursor = 'h5000;
    emit(32'hc0202b73); // x22 = instret before WRS
    emit(instruction);
    emit(32'hc0202bf3); // x23 = instret after WRS
    emit(32'h416b8bb3); // sub x23, x23, x22
    emit(sd(23,20,32));
    emit(addi(21,0,1)); emit(sd(21,20,40)); // younger side effect
    emit(sd(21,20,48)); emit(32'h0000006f);
    for (int machine = 0; machine < 2; machine++) begin
      cursor = machine != 0 ? 'h2000 : 'h3000;
      emit(machine != 0 ? 32'hb0202bf3 : 32'hc0202bf3);
      emit(32'h416b8bb3); emit(sd(23,20,32));
      save_csr(machine != 0 ? 'h342 : 'h142,0);
      save_csr(machine != 0 ? 'h341 : 'h141,8);
      save_csr(machine != 0 ? 'h343 : 'h143,16);
      emit(addi(21,0,1)); emit(sd(21,20,48)); emit(32'h0000006f);
    end
    repeat (5) @(negedge clock);
    reset = 0; started = -1;
    for (int step = 0; step < 6500 && read64(SIGNATURE+48) == 0; step++) begin
      @(negedge clock);
      if (started < 0 && instruction_valid && instruction_address == 'h5004) started = cycle;
      if (started >= 0) begin
        elapsed = cycle - started;
        if (wake == 1 && elapsed == 80) reservation_valid = 0;
        if (wake == 2 && elapsed == 4300) begin
          assert (read64(SIGNATURE+48) == 0 && read64(SIGNATURE+40) == 0)
            else $fatal(1,"WRS.NTO completed without applicable timeout or wake");
          reservation_valid = 0;
        end
        if (wake == 3 && elapsed == 80) interrupts = 6'b000100;
      end
    end
    assert (started >= 0 && read64(SIGNATURE+48) == 1)
      else $fatal(1,"WRS hung guest=%b user=%b TW=%b VTW=%b short=%b wake=%0d",guest,user_mode,tw,vtw,short_wait,wake);
    if (fault_expected || wake == 3) begin
      assert (read64(SIGNATURE) == cause && read64(SIGNATURE+8) == (wake == 3 ? 'h5008 : 'h5004) &&
              read64(SIGNATURE+16) == (wake == 3 ? 0 : 64'(instruction)))
        else $fatal(1,"WRS trap cause/PC/value: %h/%h/%h",read64(SIGNATURE),read64(SIGNATURE+8),read64(SIGNATURE+16));
      assert (read64(SIGNATURE+32) == (wake == 3 ? 2 : 1) && read64(SIGNATURE+40) == 0)
        else $fatal(1,"WRS trap retirement or younger side effect");
    end else begin
      assert (read64(SIGNATURE+32) == 2 && read64(SIGNATURE+40) == 1)
        else $fatal(1,"WRS normal retirement");
    end
    if (wake == 0) assert (cycle-started >= 4096 && cycle-started < 4400)
      else $fatal(1,"WRS timeout duration %0d",cycle-started);
    reservation_valid = 0;
  endtask

  initial begin
    int selected;
    for (int user_mode = 0; user_mode < 2; user_mode++) begin
      for (int controls = 0; controls < 4; controls++) begin
        run_wrs(1,1'(user_mode),controls[0],controls[1],0,controls == 0 ? 2 : 0);
        run_wrs(1,1'(user_mode),controls[0],controls[1],1,0);
      end
      for (int tw = 0; tw < 2; tw++) begin
        run_wrs(1,1'(user_mode),1'(tw),1,0,1);
        run_wrs(1,1'(user_mode),1'(tw),1,0,3);
      end
    end
    run_wrs(0,0,0,1,0,2); // VTW must not constrain HS execution.
    $display("25 hypervisor WRS timeout, retirement and wake cases passed");
    if ($test$plusargs("wrs-only")) $finish;
    for (int kind=0; kind<7; kind++) begin run_sha_fetch(kind,0); run_sha_fetch(kind,1); end
    $display("14 compressed/straddled guest fetch qualification cases passed");
    for (int kind=0; kind<6; kind++) begin run_sha_data(kind,0); run_sha_data(kind,1); end
    $display("12 guest data fault/delegation qualification cases passed");
    for (int kind = 0; kind < 3; kind++) begin run_invalidation(kind,0); run_invalidation(kind,1); end
    for (int op = 0; op < 5; op++) begin run_invalidation_denial(op,0); run_invalidation_denial(op,1); end
    $display("16 Svinval paged remapping and precise permission cases passed");
    for (int target = 0; target < 3; target++)
      for (int controls = 0; controls < 16; controls++)
        run_state_enable(target,controls[0],controls[1],controls[2],controls[3]);
    $display("48 paged guest state-enable cases passed");
    if ($value$plusargs("vector-case=%d", selected)) begin
      run_vector(selected);
      $finish;
    end
    run_case(0, 10, 0, 0);
    run_case(1, 13, GVA+'h3000, 0);
    run_case(2, 21, GVA+'h2000, 'h12000);
    run_case(3, 23, GVA+'h2000, 'h12000);
    run_case(4, 20, GVA+'h2000, 'h12000);
    run_case(5, 21, 64'h80000000, 'hb000, 'h3000);
    run_case(6, 23, GVA+'h1008, 'h11008);
    run_case(7, 15, GVA+'h1008, 0);
    run_case(8, 10, 0, 0);
    run_case(9, 22, 'h22000073, 0);
    run_case(10, 10, 0, 0);
    run_case(11, 10, 0, 0);
    run_case(12, 1, GVA, 0);
    run_case(13, 5, GVA+'h1000, 0);
    run_explicit(14, 9);
    run_explicit(15, 9);
    run_explicit(16, 13, GVA+'h1000);
    run_explicit(17, 21, GVA+'h1000, 'h11000);
    run_explicit(18, 9);
    run_explicit(19, 21, GVA+'h1000, 'ha008, 'h3000);
    run_explicit(20, 13, GVA+'h1000);
    run_explicit(21, 13, GVA+'h1000);
    run_explicit(22, 8);
    run_explicit(23, 2, 64'(guest_load(3,0)));
    run_explicit(24, 9);
    run_explicit(25, 13, GVA+'h1000);
    run_explicit(26, 22, 64'(guest_load(3,0)));
    run_explicit(27, 22, 64'(guest_store(3)));
    run_explicit(28, 4, GVA+'h1001);
    run_explicit(29, 6, GVA+'h1001);
    run_explicit(30, 23, GVA+'h1000, 'h11000);
    run_explicit(31, 9);
    run_explicit(32, 5, GVA+'h1000);
    run_explicit(33, 5, GVA+'h1000);
    run_explicit(34, 9);
    run_explicit(35, 9);
    run_virtual_interrupt(36);
    run_virtual_interrupt(37);
    run_virtual_interrupt(38);
    run_virtual_interrupt(37,1);
    run_vector(59,1);
    for (int kind = 39; kind <= 48; kind++) run_fp(kind);
    for (int kind = 49; kind <= 61; kind++) run_vector(kind);
    for (int op = 0; op < 4; op++) begin
      for (int mode = 0; mode < 4; mode++)
        for (int policy = 0; policy < 8; policy++) run_cmo(op,mode,policy);
      run_cmo(op,1,8);
    end
    $display("132 host/guest CMO permission and FIOM ordering cases passed");
    for (int modes = 0; modes < 4; modes++)
      for (int v = 0; v < 3; v++)
        for (int g = 0; g < 3; g++) run_pbmt(v,g,1,1'(modes&1),1'(modes>>1));
    for (int v = 0; v < 3; v++)
      for (int g = 0; g < 3; g++) run_pbmt(v,g,2,1,1,1);
    run_pbmt(1,0,0,1,1,0,1,0);
    run_pbmt(1,0,0,1,1,0,0,1);
    run_pbmt(0,1,0,1,1,0,0,1);
    run_pbmt(0,0,1,1,1,0,0,1);
    run_pbmt(3,0,0); run_pbmt(0,3,0); run_pbmt(0,0,3);
    $display("52 PBMT guest routing, Bare-stage, vector and fault cases passed");
    for (int mode = 2; mode <= 3; mode++) begin
      for (int kind = 0; kind < 9; kind++) run_pointer_mask(kind,mode);
      for (int paging = 0; paging < 3; paging++) begin
        run_pointer_mask(0,mode,1'(paging&1),1'(paging>>1));
        run_pointer_mask(1,mode,1'(paging&1),1'(paging>>1),0,1);
      end
      run_pointer_mask(0,mode,1,1,0,1);
      run_pointer_mask(0,mode,1,1,1); // transformed misalignment value
      run_pointer_mask(0,mode,1,1,2); // HS MXR disables guest masking
      run_pointer_mask(1,mode,1,1,3); // VS MXR disables VU masking
      run_pointer_mask(2,mode,1,1,3); // VS MXR applies to explicit guest access
    end
    $display("40 guest pointer-masking execution cases passed");
    $display("rv5stage-hypervisor-core passed");
    $finish;
  end
endmodule
