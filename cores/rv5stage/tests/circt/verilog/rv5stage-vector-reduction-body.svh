// Models reductions, scans, slides, gathers, and compression through public LSU readback and WB recovery.
// SPDX-License-Identifier: Apache-2.0
  localparam int CW = $clog2(VLEN+1), CHUNKS = VLEN/64;
  typedef struct packed { logic [4:0] address; logic [XLEN-1:0] data; } scalar_write_t;
  typedef struct packed { logic valid; scalar_write_t bits; } scalar_port_t;
  logic clock = 0, reset = 1;
  logic [31:0] instruction;
  logic [XLEN-1:0] vtype, vl, vstart, scalar, load_data;
  logic request_valid = 0, issue_ready = 1, cancel = 0, retry_enable = 0;
  logic [CW-1:0] retry_index = 0, wb_index;
  logic active, request_ready, issued, committed, retried, retired, saturate;
  logic [63:0] store_data;
  scalar_port_t scalar_result_out;
  RV5StageVectorReductionFixture dut (.*);
  always #5 clock = ~clock;
  logic [63:0] model [0:31][0:CHUNKS-1];
  logic [63:0] rng = 64'h651b3c5defab7809, scalar_expected;
  int mode = 0, regno = 0, cycles = 0, checks = 0, macros = 0, retry_count = 0;
  int retired_count, commit_count, scalar_count, saturate_count;
  function automatic logic [63:0] random_word();
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng;
  endfunction
  function automatic logic [63:0] element(input int r, i, width);
    return (model[r + i*width/VLEN][(i*width%VLEN)/64] >> (i*width%64)) & ('1 >> (64-width));
  endfunction
  function automatic logic [63:0] sext(input logic [63:0] x, input int width);
    return 64'($signed(x << (64-width)) >>> (64-width));
  endfunction
  function automatic logic [31:0] vec(input int code, d, s2, s1, f3, input bit masked = 0);
    return 32'((code<<26) | (int'(!masked)<<25) | (s2<<20) | (s1<<15) | (f3<<12) | (d<<7) | 'h57);
  endfunction
  function automatic logic [63:0] fold(input int op, width, input logic [63:0] a, b);
    case (op)
      0: return (a+b) & ('1 >> (64-width));
      1: return a & b;
      2: return a | b;
      3: return a ^ b;
      4: return a < b ? a : b;
      5: return $signed(sext(a,width)) < $signed(sext(b,width)) ? a : b;
      6: return a > b ? a : b;
      default: return $signed(sext(a,width)) > $signed(sext(b,width)) ? a : b;
    endcase
  endfunction
  always_comb load_data = XLEN'(element(regno,int'(wb_index),XLEN));
  task automatic tick;
    #1;
    if (!reset) begin
      if (retried) begin retry_count++; end
      if (committed) begin
        commit_count++;
        if (mode == 2) begin
          assert (XLEN'(store_data) == XLEN'(element(regno,int'(wb_index),XLEN)))
            else $fatal(1,"VRF r%0d element%0d got%h expected%h macro%0d",regno,wb_index,store_data,element(regno,int'(wb_index),XLEN),macros);
          checks++;
        end
      end
      if (saturate) begin
        assert (committed) else $fatal(1, "saturation escaped WB authorization");
        saturate_count++;
      end
      if (scalar_result_out.valid) begin
        assert (mode == 3 && scalar_result_out.bits.address == 5 && scalar_result_out.bits.data == XLEN'(scalar_expected))
          else $fatal(1,"scalar result got %h expected %h",scalar_result_out,scalar_expected);
        scalar_count++; checks++;
      end
      if (retired) retired_count++;
    end
    @(posedge clock); #1; @(negedge clock);
    cycles++;
    if (cycles > 1500000) $fatal(1,"pipeline timeout");
  endtask
  task automatic run(input logic [31:0] insn, input int sew, lm, length, start = 0, input int retry_at = -1, kill_after = -1);
    int before_retry, timeout;
    instruction = insn; vtype = (XLEN'(sew)<<3)|XLEN'(lm); vl = XLEN'(length); vstart = XLEN'(start);
    retired_count = 0; commit_count = 0; scalar_count = 0; timeout = 0;
    retry_enable = retry_at >= 0; retry_index = CW'(retry_at); before_retry = retry_count;
    request_valid = 1;
    while (!request_ready) tick();
    tick(); request_valid = 0;
    while (active) begin
      issue_ready = (random_word() & 3) != 0;
      tick();
      if (retry_count != before_retry) retry_enable = 0;
      if (kill_after >= 0 && commit_count >= kill_after && active) begin
        cancel = 1; tick(); cancel = 0; break;
      end
      timeout++; if (timeout > 20000) $fatal(1,"macro stuck insn=%h wb=%0d",insn,wb_index);
    end
    repeat (5) tick();
    assert (retired_count == (kill_after < 0 ? 1 : 0)) else $fatal(1,"macro retirement count %0d",retired_count);
    if (retry_at >= 0) assert (retry_count == before_retry+1) else $fatal(1,"retry not exercised");
    if (mode == 3) assert (scalar_count == (kill_after < 0 ? 1 : 0)) else $fatal(1,"scalar write count");
    retry_enable = 0; issue_ready = 1; macros++;
  endtask
  // Public LSU completions initialize storage; this test adapter is not an RV32 memory-ISA claim.
  task automatic load_reg(input int r);
    mode = 1; regno = r;
    run(32'('h02000007 | ((XLEN==64 ? 7 : 6)<<12) | (r<<7)),XLEN==64 ? 3 : 2,0,VLEN/XLEN);
    mode = 0;
  endtask
  task automatic check_reg(input int r);
    mode = 2; regno = r;
    run(32'('h02000027 | ((XLEN==64 ? 7 : 6)<<12) | (r<<7)),XLEN==64 ? 3 : 2,0,VLEN/XLEN);
    mode = 0;
  endtask
  task automatic widening_reduction_case(input bit signed_operation, input int sew, lm, length, dest, seed = 3, source = 8,
                                          input bit masked = 0, input int retry_at = -1, kill_after = -1);
    logic [63:0] acc, value, wide_mask;
    int width, wide_width;
    width=8<<sew; wide_width=2*width; wide_mask='1>>(64-wide_width);
    acc=element(seed,0,wide_width);
    for(int i=0;i<length;i++) if(!masked || element(0,i,1)!=0) begin
      value=element(source,i,width);
      if(signed_operation) value=sext(value,width);
      acc=(acc+value)&wide_mask;
    end
    run(vec(signed_operation ? 49 : 48,dest,source,seed,0,masked),sew,lm,length,0,retry_at,kill_after);
    if(kill_after<0 && length!=0) model[dest][0]=(model[dest][0]&~wide_mask)|(acc&wide_mask);
    check_reg(dest);
  endtask
  task automatic scan_case(input int op, sew, lm, length, pattern, input bit masked,
                           input int retry_at = -1, kill_after = -1, start = 0);
    logic [63:0] expected [0:8*CHUNKS-1];
    logic [63:0] value, lane_mask;
    int count, first_set, width, lanes, dest, source, selector, row, offset, written, groups;
    bit selected, active_element;
    width=8<<sew; lanes=op<5 ? 64 : 64/width; dest=op<2 ? 5 : op<5 ? 3 : 16;
    source=pattern==6 ? 0 : 7;
    groups=op<5 ? 1 : lm<4 ? 1<<lm : 1;
    for (int c=0;c<CHUNKS;c++) begin
      model[0][c]=pattern==5 ? 0 : masked ? random_word() : '1;
      model[7][c]=pattern==0 ? 0 : pattern==1 ? '1 : pattern==2 || pattern==3 || pattern==4 ? 0 : random_word();
    end
    if (pattern>=2 && pattern<=4) begin
      int position;
      position=pattern==2 ? 63 : pattern==3 ? 64 : VLEN-1;
      model[7][position/64]=64'(1)<<(position%64);
    end
    load_reg(0); load_reg(7);
    for (int c=0;c<groups*CHUNKS;c++) expected[c]=model[dest+c/CHUNKS][c%CHUNKS];
    count=0; first_set=-1;
    // Prefix/index golden model uses architectural elements, never RTL chunks.
    for (int i=0;i<length;i++) begin
      active_element=!masked || element(0,i,1)!=0;
      selected=active_element && element(source,i,1)!=0;
      if (op>=2 && i>=start && active_element) begin
        case(op)
          2: value=64'(first_set<0 && !selected);
          3: value=64'(first_set<0);
          4: value=64'(first_set<0 && selected);
          5: value=64'(count);
          default: value=64'(i);
        endcase
        row=op<5 ? i/64 : i*width/64; offset=op<5 ? i%64 : i*width%64;
        lane_mask=op<5 ? 1 : '1>>(64-width);
        expected[row]=(expected[row]&~(lane_mask<<offset))|((value&lane_mask)<<offset);
      end
      if (selected) begin count++; if(first_set<0) first_set=i; end
    end
    selector=op==0 ? 16 : op==1 ? 17 : op==2 ? 1 : op==3 ? 3 : op==4 ? 2 : op==5 ? 16 : 17;
    if (op<2) begin scalar_expected=op==0 ? 64'(count) : 64'($signed(first_set)); mode=3; end
    run(vec(op<2 ? 16 : 20,dest,op==6 ? 0 : source,selector,2,masked),sew,lm,length,start,retry_at,kill_after);
    mode=0;
    if(op>=2) begin
      written=kill_after<0 ? length : commit_count*lanes;
      for(int i=start;i<length && i<written;i++) begin
        row=op<5 ? i/64 : i*width/64; offset=op<5 ? i%64 : i*width%64;
        lane_mask=(op<5 ? 64'(1) : '1>>(64-width))<<offset;
        model[dest+row/CHUNKS][row%CHUNKS]=(model[dest+row/CHUNKS][row%CHUNKS]&~lane_mask)|(expected[row]&lane_mask);
      end
      for(int r=0;r<groups;r++) check_reg(dest+r);
    end
  endtask
  task automatic slide_case(input int form, sew, length, start, input bit masked,
                            input int retry_at = -1, kill_after = -1, input bit inplace = 0);
    logic [63:0] expected [0:8*CHUNKS-1];
    logic [63:0] value, lane_mask;
    int width, lanes, dest, source, group_elements, count, written, row, offset;
    bit up, one;
    up=form inside {0,1,4}; one=form>=4; dest=inplace ? 8 : 16; source=8;
    width=8<<sew; lanes=64/width; group_elements=VLEN*8/width;
    count=one ? 1 : 3; scalar=one ? XLEN'(-19) : XLEN'(count);
    for(int c=0;c<8*CHUNKS;c++) expected[c]=model[dest+c/CHUNKS][c%CHUNKS];
    for(int i=start;i<length;i++) begin
      if (masked && element(0,i,1)==0) continue;
      if (up && !one && i<count) continue;
      if (one && i==(up ? 0 : length-1)) value=sext(64'(scalar),XLEN);
      else if (!up && i+count>=group_elements) value=0;
      else value=element(source,up ? i-count : i+count,width);
      row=i*width/64; offset=i*width%64; lane_mask=('1>>(64-width))<<offset;
      expected[row]=(expected[row]&~lane_mask)|((value<<offset)&lane_mask);
    end
    run(vec(up ? 14 : 15,dest,source,form inside {1,3} ? count : 3,one ? 6 : form inside {1,3} ? 3 : 4,masked),sew,3,length,start,retry_at,kill_after);
    written=kill_after<0 ? length : (start/lanes+commit_count)*lanes;
    for(int i=start;i<length && i<written;i++) begin
      row=i*width/64; offset=i*width%64; lane_mask=('1>>(64-width))<<offset;
      model[dest+row/CHUNKS][row%CHUNKS]=(model[dest+row/CHUNKS][row%CHUNKS]&~lane_mask)|(expected[row]&lane_mask);
    end
    for(int r=0;r<8;r++) check_reg(dest+r);
  endtask
  task automatic gather_case(input int form, sew, lm, length, start, input bit masked,
                             input int retry_at = -1, kill_after = -1);
    logic [63:0] expected [0:8*CHUNKS-1];
    logic [63:0] value, lane_mask, idx;
    logic [31:0] insn;
    int width, iw, groups, igroups, exponent, ie, maximum, lanes, row, offset, written;
    width=8<<sew; iw=form==1 ? 16 : width; exponent=lm<4 ? lm : lm-8;
    ie=exponent+(form==1 ? 1-sew : 0);
    groups=exponent>0 ? 1<<exponent : 1; igroups=ie>0 ? 1<<ie : 1;
    maximum=exponent>=0 ? (VLEN/width)<<exponent : (VLEN/width)>>(-exponent);
    lanes=form<2 ? 1 : 64/width;
    scalar=XLEN'(maximum-1);
    for(int r=0;r<groups;r++) begin
      for(int c=0;c<CHUNKS;c++) begin
        model[8+r][c]=random_word(); model[24+r][c]=random_word();
      end
      load_reg(8+r); load_reg(24+r);
    end
    for(int c=0;c<CHUNKS;c++) model[0][c]=random_word();
    load_reg(0);
    if(form<2) begin
      for(int i=0;i<maximum;i++) begin
        idx=i%5==0 ? 64'(maximum) : i%5==1 ? 64'(maximum-1) : i%5==2 ? '1 : random_word()%64'(maximum);
        row=i*iw/64; offset=i*iw%64; lane_mask=('1>>(64-iw))<<offset;
        model[16+row/CHUNKS][row%CHUNKS]=(model[16+row/CHUNKS][row%CHUNKS]&~lane_mask)|((idx<<offset)&lane_mask);
      end
      for(int r=0;r<igroups;r++) load_reg(16+r);
    end
    for(int c=0;c<groups*CHUNKS;c++) expected[c]=model[24+c/CHUNKS][c%CHUNKS];
    for(int i=start;i<length;i++) begin
      if(masked && element(0,i,1)==0) continue;
      idx=form<2 ? element(16,i,iw) : form==3 ? 31 : 64'(scalar);
      value=idx>=64'(maximum) ? 0 : element(8,int'(idx),width);
      row=i*width/64; offset=i*width%64; lane_mask=('1>>(64-width))<<offset;
      expected[row]=(expected[row]&~lane_mask)|((value<<offset)&lane_mask);
    end
    insn=vec(form==1 ? 14 : 12,24,8,form<2 ? 16 : form==3 ? 31 : 3,form<2 ? 0 : form==2 ? 4 : 3,masked);
    run(insn,sew,lm,length,start,retry_at,kill_after);
    written=kill_after<0 ? length : (start/lanes+commit_count)*lanes;
    for(int i=start;i<length && i<written;i++) begin
      row=i*width/64; offset=i*width%64; lane_mask=('1>>(64-width))<<offset;
      model[24+row/CHUNKS][row%CHUNKS]=(model[24+row/CHUNKS][row%CHUNKS]&~lane_mask)|(expected[row]&lane_mask);
    end
    for(int r=0;r<groups;r++) check_reg(24+r);
    if(kill_after>=0) begin
      // Restart precisely after the visible prefix, using the same index/data
      // sources; stale second-read context must never write across this edge.
      run(insn,sew,lm,length,written,0);
      for(int c=0;c<groups*CHUNKS;c++) model[24+c/CHUNKS][c%CHUNKS]=expected[c];
      for(int r=0;r<groups;r++) check_reg(24+r);
    end
  endtask
  task automatic compress_case(input int sew, lm, length, pattern, input int retry_at = -1);
    logic [63:0] expected [0:8*CHUNKS-1];
    logic [63:0] value, lane_mask;
    int width, exponent, groups, output_index, row, offset;
    width=8<<sew; exponent=lm<4 ? lm : lm-8; groups=exponent>0 ? 1<<exponent : 1;
    for(int r=0;r<groups;r++) for(int c=0;c<CHUNKS;c++) begin
      model[8+r][c]=random_word(); model[24+r][c]=random_word();
      expected[r*CHUNKS+c]=model[24+r][c];
    end
    for(int c=0;c<CHUNKS;c++) model[5][c]=pattern==0 ? 0 : pattern==1 ? '1 : random_word();
    for(int r=0;r<groups;r++) begin load_reg(8+r); load_reg(24+r); end
    load_reg(5);
    output_index=0;
    for(int i=0;i<length;i++) if(element(5,i,1)!=0) begin
      value=element(8,i,width); row=output_index*width/64; offset=output_index*width%64;
      lane_mask=('1>>(64-width))<<offset;
      expected[row]=(expected[row]&~lane_mask)|((value<<offset)&lane_mask);
      output_index++;
    end
    run(vec(23,24,8,5,2),sew,lm,length,0,retry_at);
    for(int c=0;c<groups*CHUNKS;c++) model[24+c/CHUNKS][c%CHUNKS]=expected[c];
    for(int r=0;r<groups;r++) check_reg(24+r);
  endtask
  initial begin
    logic [63:0] acc, mask, old;
    int width, length, dest;
    instruction=0; vtype=0; vl=0; vstart=0; scalar=0;
    saturate_count=0;
    repeat (3) tick(); reset=0;
    for (int r=0;r<32;r++) begin
      for (int c=0;c<CHUNKS;c++) model[r][c]=random_word();
      load_reg(r);
    end
    // Guaranteed clipping checks that retry/cancel never create an extra
    // sticky-CSR pulse beyond the authorized prefix.
    for (int c=0;c<CHUNKS;c++) begin model[8][c]='1; model[9][c]='1; end
    load_reg(8); load_reg(9);
    begin
      int prior_saturations, beats;
      prior_saturations=saturate_count; run(vec(32,24,8,9,0),0,0,VLEN/8);
      assert(saturate_count-prior_saturations==CHUNKS) else $fatal(1,"saturating add did not pulse once per authorized beat");
      for (int c=0;c<CHUNKS;c++) model[24][c]='1;
      check_reg(24);
      beats=(VLEN/8+3)/4;
      prior_saturations=saturate_count; run(vec(46,24,8,0,3),0,0,VLEN/8);
      assert(saturate_count-prior_saturations==beats) else $fatal(1,"authorized clip saturation count");
      prior_saturations=saturate_count; run(vec(46,24,8,0,3),0,0,VLEN/8,0,0);
      assert(saturate_count-prior_saturations==beats) else $fatal(1,"retry duplicated clip saturation");
      prior_saturations=saturate_count; run(vec(46,24,8,0,3),0,0,VLEN/8,0,-1,1);
      assert(saturate_count-prior_saturations==1) else $fatal(1,"cancel leaked clip saturation");
    end
    for (int sew=0;sew<4;sew++) begin
      width=8<<sew; mask='1>>(64-width);
      for (int lm=0;lm<8;lm++) begin
        if (lm==4 || (lm>=5 && sew > lm-5)) continue;
        length=(VLEN/width)*(lm<4 ? (1<<lm) : 1)/(lm<4 ? 1 : (1<<(8-lm)));
        for (int op=0;op<8;op++) begin
          for (int scenario=0;scenario<4;scenario++) begin
            // Single-register seed/destination may be unaligned, overlap the
            // source group, each other, or the input mask.
            dest=scenario==0 ? 7 : scenario==1 ? 8 : scenario==2 ? 3 : 0;
            if (scenario==3) begin
              for (int c=0;c<CHUNKS;c++) model[0][c]=0;
              load_reg(0);
            end else begin
              for (int c=0;c<CHUNKS;c++) model[0][c]=random_word();
              load_reg(0);
            end
            acc=element(3,0,width);
            for (int i=0;i<length;i++)
              if (scenario==0 || element(0,i,1)!=0) acc=fold(op,width,acc,element(8,i,width));
            mode=0;
            run(vec(op,dest,8,3,2,scenario!=0),sew,lm,length,0,length>2 ? length/2 : 0);
            model[dest][0]=(model[dest][0]&~mask)|(acc&mask);
            check_reg(dest);
          end
        end
      end
      // Empty reductions preserve even element zero; cancellation discards
      // partial authorized accumulation without an architectural VRF write.
      for (int op=0;op<8;op++) begin
        acc=fold(op,width,element(3,0,width),element(8,0,width));
        run(vec(op,7,8,3,2),sew,3,1,0,0);
        model[7][0]=(model[7][0]&~mask)|(acc&mask);
        check_reg(7);
      end
      run(vec(0,7,8,3,2),sew,3,0); check_reg(7);
      run(vec(0,7,8,3,2),sew,3,8,0,-1,2); check_reg(7);
      for (int c=0;c<CHUNKS;c++) model[3][c]=random_word();
      load_reg(3);
      // Both scalar moves ignore LMUL; extraction also ignores VL/vstart.
      for (int empty=0;empty<4;empty++) begin
        scalar=XLEN'(-7); old=model[3][0];
        run(vec(16,3,0,5,6),sew,3,empty==1 ? 0 : 4,empty==2 ? 4 : empty==3 ? 1 : 0,0);
        if (empty==0 || empty==3) model[3][0]=(old&~mask)|(64'(sext(64'(scalar),XLEN))&mask);
        check_reg(3);
        scalar_expected=sext(element(3,0,width),width); mode=3;
        run(vec(16,5,3,0,2),sew,3,0,7,0);
        mode=0;
      end
    end
    // Widening reductions fold narrow LMUL-sized sources into a single wide
    // seed/result element. Every accepted prefix remains retryable at WB.
    for(int sew=0;sew<3;sew++) begin
      for(int lm=0;lm<8;lm++) begin
        int exponent, length;
        exponent=lm<4 ? lm : lm-8;
        if(lm==4 || sew>exponent+3) continue;
        length=exponent>=0 ? (VLEN/(8<<sew))<<exponent : (VLEN/(8<<sew))>>(-exponent);
        for(int c=0;c<CHUNKS;c++) model[0][c]=random_word();
        load_reg(0);
        widening_reduction_case(0,sew,lm,length,24,3,8,0,length>2 ? length/2 : 0);
        widening_reduction_case(1,sew,lm,length,24,3,8,1,length>2 ? length/2 : 0);
      end
      // The scalar destination may overlap either data source or the mask;
      // different-width source/source aliasing is rejected by decode instead.
      widening_reduction_case(0,sew,0,VLEN/(8<<sew),8);
      widening_reduction_case(1,sew,0,VLEN/(8<<sew),3);
      for(int c=0;c<CHUNKS;c++) model[0][c]=0;
      load_reg(0);
      widening_reduction_case(0,sew,0,VLEN/(8<<sew),0,3,8,1);
      widening_reduction_case(1,sew,0,0,7);
      widening_reduction_case(1,sew,0,VLEN/(8<<sew),7,3,8,0,-1,2);
    end
    for(int sew=0;sew<4;sew++) begin
      for(int lm=0;lm<8;lm++) begin
        if(lm==4 || (lm>=5 && sew>lm-5)) continue;
        length=(VLEN/(8<<sew))*(lm<4 ? 1<<lm : 1)/(lm<4 ? 1 : 1<<(8-lm));
        for(int op=0;op<7;op++) begin
          int lanes;
          lanes=op<5 ? 64 : 64/(8<<sew);
          scan_case(op,sew,lm,length,7,0,(length+lanes-1)/lanes/2);
          scan_case(op,sew,lm,length,6,1,0);
          scan_case(op,sew,lm,0,0,1,0);
        end
      end
    end
    for(int op=0;op<7;op++) begin
      for(int pattern=0;pattern<6;pattern++) begin
        scan_case(op,0,3,VLEN,pattern,pattern==5,1);
        scan_case(op,0,3,65,pattern,1,0);
      end
      scan_case(op,0,3,VLEN,7,1,-1,1);
      scan_case(op,0,3,1,1,1,0);
    end
    // vid supports arbitrary vstart; indices do not restart at zero after replay.
    for(int sew=0;sew<4;sew++) begin
      scan_case(6,sew,3,VLEN/(8<<sew)*8,7,1,1,-1,3);
      scan_case(6,sew,3,1,7,0,0,-1,5);
    end
    // Production private-pipeline authorization, including partial-prefix
    // cancellation followed by a nonzero-vstart reissue over preserved state.
    for(int sew=0;sew<4;sew++) begin
      for(int form=0;form<6;form++) begin
        bit down;
        down=form inside {2,3,5}; length=VLEN>>sew;
        slide_case(form,sew,length,0,0,1,-1,down);
        slide_case(form,sew,length-1,1,1,0,-1,down);
        slide_case(form,sew,length,0,1,-1,1,down);
        slide_case(form,sew,length,8>>sew,1,0,-1,down);
        slide_case(form,sew,0,0,1,0);
        slide_case(form,sew,1,0,0,0);
      end
    end
    for(int sew=0;sew<4;sew++) for(int lm=0;lm<8;lm++) begin
      int exponent, maximum;
      exponent=lm<4 ? lm : lm-8;
      if(lm==4 || sew>exponent+3) continue;
      maximum=exponent>=0 ? (VLEN/(8<<sew))<<exponent : (VLEN/(8<<sew))>>(-exponent);
      for(int form=0;form<4;form++) begin
        int ie, lanes;
        ie=exponent+(form==1 ? 1-sew : 0); lanes=form<2 ? 1 : 8>>sew;
        if(form==1 && (ie < -3 || ie > 3)) continue;
        gather_case(form,sew,lm,maximum,0,0,0);
        gather_case(form,sew,lm,maximum-1,1,1,0);
        gather_case(form,sew,lm,0,0,1,0);
        gather_case(form,sew,lm,1,3,0,0);
        if(maximum>lanes) begin
          gather_case(form,sew,lm,maximum,0,1,1);
          gather_case(form,sew,lm,maximum,0,0,-1,1);
        end
      end
    end
    // Compression streams its mask/data sources through the production bank,
    // authorizes every packed destination write at WB, and preserves its tail.
    for(int sew=0;sew<4;sew++) begin
      int maximum;
      maximum=VLEN/(8<<sew);
      compress_case(sew,0,maximum,0);
      compress_case(sew,0,maximum>0 ? maximum-1 : 0,1);
      compress_case(sew,3,8*maximum,2,1);
    end
    compress_case(0,0,0,2);
    $display("vector reductions/moves/scans/slides/gathers/compression XLEN%0d VLEN%0d passed: %0d macros %0d checks %0d retries",XLEN,VLEN,macros,checks,retry_count);
    $finish;
  end
