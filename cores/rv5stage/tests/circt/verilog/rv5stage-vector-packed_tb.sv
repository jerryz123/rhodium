// Checks unified packed sequencing, geometry, replay, reordered completions, and write-port contention.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_packed_tb;
  logic clock=0, reset=1;
  logic [63:0] base, vl, vstart, vtype;
  logic [2:0] nf;
  logic [1:0] eew, mode;
  logic store, masked, request_valid=0, issue_ready=1, retry=0, slow=0, alignment_available=1;
  logic write_available=1;
  logic [63:0] hit_data=0;
  struct packed {logic valid; RV5StageVectorCompletion bits;} response_in;
  struct packed {logic valid; VectorRegisterWrite bits;} initialize_in, written_out;
  struct packed {logic valid; RV5StageVectorToken bits;} attempt_out;
  RV5StageVectorToken token;
  logic request_ready, active, issued, retired, sequenced;
  logic [$bits(response_in.bits.data)-1:0] sequence_address;
  bit prior_sequence=0;
  logic [$bits(response_in.bits.data)-1:0] prior_sequence_address;
  RV5StageVectorPackedFixture dut(.*);
  localparam int SLOTS = 1 << $bits(token.completion_tag);
  localparam int BYTES = $bits(response_in.bits.data)/8;
  byte unsigned memory [8192], expected_memory [8192];
  localparam int ROW_BITS = $bits(written_out.bits.data), ROW_BYTES = ROW_BITS/8, DEPTH = 32*128/ROW_BITS;
  logic [ROW_BITS-1:0] bank [DEPTH], expected_bank [DEPTH];
  bit outstanding [SLOTS];
  logic [63:0] returns [SLOTS];
  int due [SLOTS];
  int cycle=0, accepted=0, requests=0, writes=0, retired_count=0;
  bit exercise_retry=0, did_retry=0, exercise_delay=0, exercise_stalls=0, exercise_alignment_stalls=0;
  int tests=0, max_consecutive=0, consecutive=0;
  int simultaneous_completions=0;
  bit all_masked=0;

  function automatic logic [63:0] memory_word(input int address);
    logic [63:0] value='0;
    for (int b=0;b<BYTES;b++) value[b*8+:8]=memory[address+b];
    return value;
  endfunction
  task automatic tick;
    issue_ready = !exercise_stalls || cycle%7!=2;
    alignment_available = !exercise_alignment_stalls || cycle%7>=3;
    // A fast-store VRF read reserves its next-cycle issue and align path.
    if (store && (mode==2 || mode==3 || (!masked && nf==0))) begin issue_ready=1; alignment_available=1; end
    write_available = !exercise_stalls || cycle%5>=2;
    retry=0; slow=0; hit_data=0; response_in='0;
    #1;
    if (!reset && attempt_out.valid) begin
      retry=exercise_retry && !did_retry && accepted==1;
      slow=exercise_delay && (accepted%2==0);
      hit_data=memory_word(int'(attempt_out.bits.address));
    end
    for (int t=0;t<SLOTS;t++)
      if (outstanding[t] && due[t]<=cycle) begin
        response_in.valid=1;
        response_in.bits.tag=$bits(response_in.bits.tag)'(t);
        response_in.bits.data=$bits(response_in.bits.data)'(returns[t]);
      end
    #1;
    if (!reset) begin
      assert(issued == (prior_sequence && !retry))
        else $fatal(1,"packed issue must follow the common sequencer by exactly one cycle");
      if (issued) assert(token.address == prior_sequence_address)
        else $fatal(1,"packed issue lost its scheduled address");
      if (issued) begin
        requests++;
        consecutive++;
        if (consecutive>max_consecutive) max_consecutive=consecutive;
        assert((token.address & (BYTES*8)'(BYTES-1))==0 && token.memory_width==2'($clog2(BYTES)))
          else $fatal(1,"unaligned/non-word transport");
      end else consecutive=0;
      if (response_in.valid) outstanding[int'(response_in.bits.tag)]=0;
      if (attempt_out.valid) begin
        if (retry) did_retry=1;
        else begin
          int tag;
          tag=int'(attempt_out.bits.completion_tag);
          accepted++;
          if (!slow && response_in.valid) simultaneous_completions++;
          if (store)
            for (int b=0;b<BYTES;b++)
              if (attempt_out.bits.byte_mask[b])
                memory[int'(attempt_out.bits.address)+b]=attempt_out.bits.store_data[b*8+:8];
          if (slow) begin
            assert(!outstanding[tag]) else $fatal(1,"slot reused before return");
            outstanding[tag]=1; returns[tag]=hit_data;
            due[tag]=cycle+3+(SLOTS-tag)*2;
          end
        end
      end
      if (written_out.valid) begin
        int row;
        assert(write_available) else $fatal(1,"packed result stole a reserved write cycle");
        row=int'(written_out.bits.address);
        bank[row]=(bank[row]&~written_out.bits.mask)|(written_out.bits.data&written_out.bits.mask);
        writes++;
      end
      if (retired) retired_count++;
    end
    prior_sequence = !reset && sequenced;
    prior_sequence_address = sequence_address;
    #3 clock=1; #1 clock=0; #4;
    cycle++;
  endtask

  task automatic run_case(input bit writing, input int offset, size, fields, start,
                          input bit masking, replaying, delayed, input int transfer_mode=0, lm=1, length=-1);
    int count, element_bytes, register_stride, first, last, expected_beats;
    tests++;
    store=writing; base=64'(256+offset); eew=2'(size); nf=3'(fields-1);
    if (fields>4 && transfer_mode==0) lm=0;
    mode=2'(transfer_mode); masked=masking; vtype=(64'(size)<<3)|64'(lm);
    vl=64'(length>=0 ? length : ((16<<lm)-1)>>size); vstart=64'(start);
    exercise_retry=replaying; did_retry=0; exercise_delay=delayed;
    exercise_stalls=replaying || delayed;
    exercise_alignment_stalls=writing && (replaying || delayed);
    accepted=0; requests=0; writes=0; retired_count=0; consecutive=0; max_consecutive=0;
    for (int t=0;t<SLOTS;t++) outstanding[t]=0;
    for (int b=0;b<8192;b++) begin
      memory[b]=8'(b*37+tests*11); expected_memory[b]=memory[b];
    end
    for (int row=0;row<DEPTH;row++) begin
      bank[row]=ROW_BITS'(64'hb7832065ea4c9d01 ^ (64'(row)*64'h01030507090b0d0f));
      if (row==0) bank[row]=ROW_BITS'(64'h965a3cc369a5965a);
      if (all_masked && row<128/ROW_BITS) bank[row]=0;
      expected_bank[row]=bank[row];
      initialize_in.valid=1; initialize_in.bits.address=$bits(initialize_in.bits.address)'(row);
      initialize_in.bits.data=bank[row]; initialize_in.bits.mask='1;
      tick();
    end
    initialize_in='0;
    element_bytes=1<<size; count=int'(vl); register_stride=16<<lm;
    if (transfer_mode==2) begin count=fields*16/element_bytes; fields=1; end
    if (transfer_mode==3) begin count=(int'(vl)+7)/8; fields=1; element_bytes=1; end
    for (int element=start;element<count;element++)
      for (int field=0;field<fields;field++)
        if (!masking || transfer_mode!=0 || bank[element/ROW_BITS][element%ROW_BITS])
          for (int b=0;b<element_bytes;b++) begin
            int address, position, row, lane;
            address=int'(base)+(element*fields+field)*element_bytes+b;
            position=8*16+field*register_stride+element*element_bytes+b;
            row=position/ROW_BYTES; lane=position%ROW_BYTES;
            if (writing) expected_memory[address]=bank[row][lane*8+:8];
            else expected_bank[row][lane*8+:8]=memory[address];
          end
    first=int'(base)+start*fields*element_bytes;
    last=int'(base)+count*fields*element_bytes;
    expected_beats=start>=count ? 1 : (last+BYTES-1)/BYTES-first/BYTES;
    request_valid=1;
    #1; assert(request_ready) else $fatal(1,"macro not ready");
    tick(); request_valid=0;
    for (int timeout=0;timeout<5000 && retired_count==0;timeout++) tick();
    assert(retired_count==1 && !active) else $fatal(1,"packed timeout test=%0d accepted=%0d requests=%0d",tests,accepted,requests);
    assert(accepted==expected_beats) else $fatal(1,"beat count test=%0d got=%0d expected=%0d",tests,accepted,expected_beats);
    if (replaying) assert(did_retry) else $fatal(1,"retry not exercised");
    for (int row=0;row<DEPTH;row++)
      assert(bank[row]===expected_bank[row])
        else $fatal(1,"VRF test=%0d store=%0b offset=%0d size=%0d fields=%0d row=%0d got=%h expected=%h",tests,writing,offset,size,fields,row,bank[row],expected_bank[row]);
    for (int b=0;b<8192;b++)
      assert(memory[b]===expected_memory[b])
        else $fatal(1,"store test=%0d offset=%0d size=%0d fields=%0d byte=%0d got=%h expected=%h",tests,offset,size,fields,b,memory[b],expected_memory[b]);
    if (!masking && fields==1 && !delayed && !replaying && expected_beats>=3 && SLOTS>2)
      assert(max_consecutive>=3) else $fatal(1,"word throughput regressed");
    tick();
  endtask
  initial begin
    initialize_in='0; response_in='0; base=0; vl=0; vstart=0; vtype=0;
    nf=0; eew=0; mode=0; store=0; masked=0;
    tick(); tick(); reset=0; tick();
    for (int size=0;size<=$clog2(BYTES);size++)
      for (int offset=0;offset<8;offset+=(1<<size))
        for (int writing=0;writing<2;writing++) begin
          run_case(1'(writing),offset,size,1,0,0,0,0);
          run_case(1'(writing),offset,size,1,1,1,1,1);
          for (int fields=2;fields<=8;fields++)
            run_case(1'(writing),offset,size,fields,size==3 && fields>4 ? 0 : 1,1'(fields%2),1,1);
        end
    for (int offset=0;offset<8;offset++)
      for (int writing=0;writing<2;writing++) begin
        run_case(1'(writing),offset,0,8,1,0,1,1,2);
        run_case(1'(writing),offset,0,1,0,0,0,1,3);
      end
    run_case(0,3,0,1,61,1,1,1,0,3,121);
    run_case(1,5,0,1,61,1,1,1,0,3,121);
    for (int writing=0;writing<2;writing++) begin
      run_case(1'(writing),3,0,1,0,0,0,0,0,1,0);
      run_case(1'(writing),3,0,1,7,0,0,0,0,1,7);
      run_case(1'(writing),3,0,1,256,0,0,0);
      run_case(1'(writing),3,0,8,256,0,0,0,2);
      run_case(1'(writing),3,0,1,256,0,0,0,3);
      all_masked=1;
      run_case(1'(writing),3,0,1,0,1,1,1);
      all_masked=0;
    end
    if (SLOTS>2) assert(simultaneous_completions>0) else $fatal(1,"simultaneous hit/return not exercised");
    $display("PASS packed vector memory XLEN%0d: %0d cases",BYTES*8,tests);
    $finish;
  end
endmodule
