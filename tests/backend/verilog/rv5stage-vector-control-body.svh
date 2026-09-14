// Exercises committed vector configuration, WARL CSR state, privilege gating, and decode legality.
// SPDX-License-Identifier: Apache-2.0
  typedef logic [XLEN-1:0] word_t;
  typedef struct packed { word_t vl, vtype, vstart; logic [1:0] vxrm; logic vxsat; } vector_state_t;
  logic clock = 0, reset = 1;
  logic [31:0] instruction = 0;
  word_t scalar1 = 0, scalar2 = 0, test_vtype = 0;
  logic commit_valid = 0, exception_valid = 0;
  logic decoded_valid, legal, writeback_valid, redirect_valid;
  word_t writeback_value, mstatus;
  vector_state_t state;
  logic [1:0] configuration, operand;
  logic mask_destination, invert_comparison, swap_operands, widening, wide_vs2, left_signed, right_signed, subtract;
  integer checks = 0, retired = 0;
  RV5StageVectorControlFixture dut (.*);
  always #5 clock = ~clock;

  function automatic logic [31:0] csr_word(input int address, input int op, input int source = 1);
    return (32'(address) << 20) | (32'(source) << 15) | (32'(op) << 12) | 32'hf3;
  endfunction
  function automatic logic [31:0] vset(input int kind, input int raw_type, input int rs1 = 1, input int rd = 1);
    logic [31:0] high;
    high = kind == 0 ? (32'(raw_type) << 20) :
           kind == 1 ? (32'hc0000000 | (32'(raw_type) << 20)) : 32'h80200000;
    return high | (32'(rs1) << 15) | (32'(rd) << 7) | 32'h7057;
  endfunction
  function automatic integer maximum(input word_t raw_type);
    integer sew, lm;
    sew = int'(raw_type[5:3]);
    lm = int'(raw_type[2:0]);
    if (lm >= 4) lm -= 8;
    if ((raw_type >> 8) != 0 || sew > 3 || lm == -4 || sew > lm + 3) return 0;
    return lm >= 0 ? ((VLEN / (8 << sew)) << lm) : ((VLEN / (8 << sew)) >> -lm);
  endfunction

  task automatic send(input logic [31:0] word, input word_t a, input word_t b,
                      input bit trap_expected, input bit wb_expected, input word_t expected_value,
                      input bit explicit_fault = 0);
    @(negedge clock);
    instruction = word; scalar1 = a; scalar2 = b;
    exception_valid = explicit_fault; commit_valid = 1;
    #1;
    assert (decoded_valid && redirect_valid == trap_expected && writeback_valid == wb_expected)
      else $fatal(1, "RV%0d instruction %h trap=%b wb=%b", XLEN, word, redirect_valid, writeback_valid);
    if (wb_expected) assert (writeback_value == expected_value)
      else $fatal(1, "%h returned %h expected %h", word, writeback_value, expected_value);
    @(posedge clock); #1;
    if (!trap_expected) retired++;
    checks++;
    @(negedge clock); commit_valid = 0; exception_valid = 0;
  endtask
  task automatic read_csr(input int address, input word_t value);
    send(csr_word(address, 2, 0), 0, 0, 0, 1, value);
  endtask
  task automatic write_csr(input int address, input word_t value, input word_t old);
    send(csr_word(address, 1), value, 0, 0, 1, old);
  endtask

  initial begin
    integer max_vl, avl, selected;
    word_t expected_type, saved_type, saved_vl;
    repeat (3) @(posedge clock);
    @(negedge clock); reset = 0;
    assert (state.vl == 0 && state.vtype == (word_t'(1) << (XLEN-1)) && mstatus[10:9] == 0)
      else $fatal(1, "reset vector state");
    send(vset(0, 0), 4, 0, 1, 0, 0); // VS Off
    send(csr_word('hc20, 2, 0), 0, 0, 1, 0, 0);
    read_csr('h342, 2);
    // CSRRS avoids depending on RV32/RV64 fixed status fields.
    @(negedge clock); instruction = csr_word('h300, 2, 0); #1; saved_type = mstatus;
    send(csr_word('h300, 2), word_t'('h200), 0, 0, 1, saved_type);
    assert (mstatus[10:9] == 1) else $fatal(1, "VS Initial write");
    read_csr('hc22, word_t'(VLEN) / 8);

    // Sweep the full vsetvli vtype field, including every reserved upper bit.
    for (int raw_type = 0; raw_type < 2048; raw_type++) begin
      max_vl = maximum(word_t'(raw_type));
      for (int choice = 0; choice < 4; choice++) begin
        avl = choice == 0 ? 0 : choice == 1 ? 1 : choice == 2 ? max_vl : max_vl + 9;
        selected = avl < max_vl ? avl : max_vl;
        expected_type = max_vl == 0 ? (word_t'(1) << (XLEN-1)) : word_t'(raw_type);
        send(vset(0, raw_type), word_t'(avl), 0, 0, 1, word_t'(selected));
        assert (state.vl == word_t'(selected) && state.vtype == expected_type && state.vstart == 0 && mstatus[10:9] == 3 && mstatus[XLEN-1])
          else $fatal(1, "vtype sweep type=%h vl=%h", state.vtype, state.vl);
      end
    end
    // Immediate AVL zero is really zero; rs1=x0 register forms select VLMAX.
    send(vset(1, 0, 0), '1, 0, 0, 1, 0);
    send(vset(0, 0, 0), 0, 0, 0, 1, word_t'(VLEN)/8);
    send(vset(2, 0, 0), 0, word_t'('h10), 0, 1, word_t'(VLEN)/32);
    for (int immediate_avl = 0; immediate_avl < 32; immediate_avl++) begin
      selected = immediate_avl < VLEN/32 ? immediate_avl : VLEN/32;
      send(vset(1, 'hd0, immediate_avl), '1, 0, 0, 1, word_t'(selected));
    end
    // vsetvl checks every XLEN vtype bit, not just the immediate field.
    for (int bit_index = 8; bit_index < XLEN; bit_index++) begin
      send(vset(2, 0), 4, word_t'(1) << bit_index, 0, 1, 0);
      assert (state.vtype == (word_t'(1) << (XLEN-1))) else $fatal(1, "high vtype bit ignored");
    end
    send(vset(0, 'h10), 3, 0, 0, 1, 3); // e32,m1
    send(vset(0, 'h19, 0, 0), 0, 0, 0, 0, 0); // e64,m2, same VLMAX
    assert (state.vl == 3 && state.vtype == 'h19) else $fatal(1, "keep VL");
    send(vset(0, 0, 0, 0), 0, 0, 1, 0, 0); // reserved VLMAX change
    assert (state.vl == 3 && state.vtype == 'h19) else $fatal(1, "reserved keep modified state");

    write_csr('h008, '1, 0);
    read_csr('h008, word_t'(VLEN)-1);
    write_csr('h00f, '1, 0);
    read_csr('h00a, 3); read_csr('h009, 1); read_csr('h00f, 7);
    write_csr('h009, 0, 1);
    send(csr_word('h00a, 3), 1, 0, 0, 1, 3); // clear vxrm bit0
    read_csr('h00f, 4);
    send(vset(0, 0), 0, 0, 0, 1, 0);
    assert (state.vstart == 0) else $fatal(1, "zero VL config must reset vstart");

    // No state update or architectural retirement on an explicit older fault.
    saved_type = state.vtype; saved_vl = state.vl;
    send(vset(0, 'h18), 4, 0, 1, 0, 0, 1);
    assert (state.vtype == saved_type && state.vl == saved_vl) else $fatal(1, "faulted configuration changed state");
    send(csr_word('h00f, 1), 7, 0, 1, 0, 0, 1);
    read_csr('h00f, 4);
    for (int address = 'hc20; address <= 'hc22; address++)
      send(csr_word(address, 1), 0, 0, 1, 0, 0);
    read_csr('h343, word_t'(csr_word('hc22, 1)));
    read_csr('h341, 'h100);
    read_csr('hc02, word_t'(retired));

    // Move source metadata and mask-register geometry are not ordinary data groups.
    @(negedge clock);
    for (int lm = 0; lm < 4; lm++) begin
      test_vtype = word_t'(lm);
      for (int op = 24; op < 32; op++) begin
        instruction = (32'(op) << 26) | 32'h0272a1d7; #1; // vd=3,vs1=5,vs2=7
        assert (decoded_valid && legal && mask_destination && operand == 0) else $fatal(1, "mask registers inherited LMUL");
        instruction[11:7] = 0; #1;
        assert (legal) else $fatal(1, "mask result v0 rejected");
        instruction[25] = 0; #1;
        assert (!decoded_valid) else $fatal(1, "reserved masked mask-logic encoding");
      end
      instruction = 32'h5e0fc057; #1; // vmv.v.x v0,x31
      assert (decoded_valid && legal && operand == 1) else $fatal(1, "broadcast inferred a vector source");
      instruction = 32'h5e080457; #1; // vmv.v.v v8,v16
      assert (decoded_valid && legal && operand == 0) else $fatal(1, "move vector source");
      instruction[24:20] = 1; #1;
      assert (!decoded_valid) else $fatal(1, "move accepted nonzero reserved vs2");
      instruction = 32'h5c880c57; #1; // vmerge.vvm v24,v8,v16,v0
      assert (decoded_valid && legal) else $fatal(1, "merge rejected");
      instruction[24:20] = 0; #1;
      assert (!legal) else $fatal(1, "merge read v0 at both EEW=1 and SEW");
      instruction[24:20] = 8; instruction[19:15] = 0; #1;
      assert (!legal) else $fatal(1, "merge second vector source overlaps v0");
      instruction[14:12] = 4; #1;
      assert (legal && operand == 1) else $fatal(1, "merge scalar x0 is not vector v0");
      instruction[14:12] = 3; #1;
      assert (legal && operand == 2) else $fatal(1, "merge immediate zero is not vector v0");
      instruction[11:7] = 0; #1;
      assert (!legal) else $fatal(1, "merge overwrote its selection mask");
    end
    // Same-width data groups and mask destinations have different overlap rules.
    @(negedge clock);
    test_vtype = 1; // e8,m2
    instruction = 32'h02220257; #1; // vadd.vv v4,v2,v4, unmasked
    assert (decoded_valid && legal && operand == 0 && !mask_destination) else $fatal(1, "legal aligned group");
    instruction[11:7] = 3; #1;
    assert (!legal) else $fatal(1, "unaligned destination group");
    instruction[11:7] = 0; instruction[25] = 0; #1;
    assert (!legal) else $fatal(1, "masked data destination overlaps v0");
    instruction = 32'h622200d7; #1; // vmseq.vv v1,v2,v4, nonoverlap
    assert (decoded_valid && mask_destination && legal) else $fatal(1, "mask destination group");
    instruction[11:7] = 3; #1;
    assert (!legal) else $fatal(1, "mask overlaps upper source register");
    instruction[11:7] = 2; #1;
    assert (legal) else $fatal(1, "mask may overlap low source register");
    // Widening doubles destination EMUL. A narrow source may overlap only the
    // highest-numbered portion of that group, and SEW64/LMUL8 are reserved.
    test_vtype = 0; // e8,m1
    instruction = 32'hc3012457; #1; // vwaddu.vv v8,v16,v2, unmasked
    assert (decoded_valid && legal && widening && !left_signed && !right_signed && !subtract) else $fatal(1, "unsigned widening add decode");
    instruction = 32'hce916457; #1; // vwsub.vx v8,v9,x2, unmasked; high-source overlap
    assert (decoded_valid && legal && widening && left_signed && right_signed && subtract && operand == 1) else $fatal(1, "signed widening subtract decode");
    instruction[24:20] = 8; #1;
    assert (!legal) else $fatal(1, "widening accepted low-source overlap");
    instruction[24:20] = 16; instruction[11:7] = 9; #1;
    assert (!legal) else $fatal(1, "widening accepted unaligned destination");
    instruction[11:7] = 8; instruction[25] = 0; #1;
    assert (legal) else $fatal(1, "masked widening rejected non-v0 destination");
    instruction[11:7] = 0; #1;
    assert (!legal) else $fatal(1, "masked widening destination overlaps v0");
    test_vtype = 'h18; instruction = 32'hc3012457; #1; // e64,m1
    assert (!legal) else $fatal(1, "SEW64 widening accepted");
    test_vtype = 3; #1; // e8,m8 -> destination EMUL16
    assert (!legal) else $fatal(1, "widening destination exceeds EMUL8");
    // Wide-source forms align vs2 to the doubled EMUL, permit vd=vs2, and
    // retain the narrow source's high-part-only overlap rule.
    test_vtype = 0;
    instruction = 32'hd3012457; #1; // vwaddu.wv v8,v16,v2, unmasked
    assert (decoded_valid && legal && widening && wide_vs2 && !left_signed && !right_signed && !subtract) else $fatal(1, "unsigned wide-source add decode");
    instruction = 32'hde816457; #1; // vwsub.wx v8,v8,x2, unmasked; in-place wide source
    assert (decoded_valid && legal && widening && wide_vs2 && left_signed && right_signed && subtract && operand == 1) else $fatal(1, "signed wide-source subtract decode");
    instruction[24:20] = 9; #1;
    assert (!legal) else $fatal(1, "wide source accepted unaligned doubled group");
    instruction = 32'hde916457; #1; // vd=v8, vs2=v9 is not a doubled-group base
    assert (!legal) else $fatal(1, "wide source accepted partial destination overlap");
    instruction = 32'hd688a457; #1; // vwadd.wv v8,v8,v17; narrow source disjoint
    assert (decoded_valid && legal && wide_vs2) else $fatal(1, "wide source in-place operation rejected");
    instruction[19:15] = 8; #1;
    assert (!legal) else $fatal(1, "wide-source form accepted low narrow-source overlap");
    instruction[19:15] = 9; #1;
    assert (legal) else $fatal(1, "wide-source form rejected high narrow-source overlap");
    test_vtype = 'h80; instruction = 32'h662180d7; #1;
    assert (decoded_valid && invert_comparison) else $fatal(1, "vmsne inversion");
    instruction = 32'h7a21c0d7; #1;
    assert (decoded_valid && swap_operands) else $fatal(1, "vmsgt operand swap");
    test_vtype = word_t'(1) << (XLEN-1); #1;
    assert (!legal) else $fatal(1, "vill accepted arithmetic");

    // Memory EEW changes EMUL, independently of configured SEW. Check every
    // legal/illegal exponent and register alignment, with masked v0 rules.
    for (int sew = 0; sew < 4; sew++) begin
      for (int lm = -3; lm <= 3; lm++) begin
        for (int eew = 0; eew < 4; eew++) begin
          for (int store = 0; store < 2; store++) begin
            for (int regno = 0; regno < 32; regno++) begin
              automatic int emul = lm + eew - sew;
              automatic bit aligned = emul <= 0 ? 1 : (regno % (1 << emul)) == 0;
              automatic bit expected_legal = XLEN == 64 && sew <= lm + 3 && emul >= -3 && emul <= 3 && aligned && (store != 0 || regno != 0);
              test_vtype = (word_t'(sew) << 3) | (word_t'(lm) & 7);
              instruction = {6'b0,1'b0,5'b0,5'd1,3'(eew == 0 ? 0 : eew+4),5'(regno),store != 0 ? 7'h27 : 7'h07};
              #1;
              assert(decoded_valid && legal == expected_legal)
                else $fatal(1,"memory legality sew=%0d lm=%0d eew=%0d reg=%0d store=%0d",sew,lm,eew,regno,store);
              checks++;
            end
          end
        end
      end
    end

    // The initial FP subset admits aligned same-width FP32/64 groups on RV64.
    for (int op = 0; op < 3; op++) begin
      for (int sew = 0; sew < 4; sew++) begin
        for (int lm = -3; lm <= 3; lm++) begin
          for (int rd = 0; rd < 16; rd++) begin
            automatic bit expected_legal = XLEN == 64 && sew >= 2 && sew <= lm + 3 && rd != 0 && (lm <= 0 || rd % (1 << lm) == 0);
            test_vtype = (word_t'(sew) << 3) | (word_t'(lm) & 7);
            instruction = {6'(op == 0 ? 0 : op == 1 ? 2 : 36), 1'b0, 5'd16, 5'd24, 3'd1, 5'(rd), 7'h57};
            #1;
            assert (decoded_valid && legal == expected_legal) else $fatal(1, "FP vector geometry sew=%0d lm=%0d rd=%0d", sew, lm, rd);
            checks++;
          end
        end
      end
    end

    // Mul/div uses ordinary same-width groups; VX's rs1 is not a vector group.
    for (int op = 32; op < 40; op++) begin
      for (int sew = 0; sew < 4; sew++) begin
        for (int lm = -3; lm <= 3; lm++) begin
          for (int vx = 0; vx < 2; vx++) begin
            for (int rd = 0; rd < 16; rd++) begin
              automatic bit aligned = lm <= 0 || rd % (1 << lm) == 0;
              automatic bit source_aligned = vx != 0 || lm <= 0;
              automatic bit expected_legal = XLEN == 64 && sew <= lm + 3 && rd != 0 && aligned && source_aligned;
              test_vtype = (word_t'(sew) << 3) | (word_t'(lm) & 7);
              instruction = {6'(op), 1'b0, 5'd16, 5'd3, vx != 0 ? 3'd6 : 3'd2, 5'(rd), 7'h57};
              #1;
              assert(decoded_valid && legal == expected_legal) else $fatal(1, "muldiv geometry op=%0d sew=%0d lm=%0d vx=%0d rd=%0d",op,sew,lm,vx,rd);
              checks++;
            end
          end
        end
      end
    end

    // Element moves ignore LMUL alignment. Reduction seed/destination are
    // single registers, while vs2 still obeys the source group's alignment.
    for (int sew=0;sew<4;sew++) begin
      for (int lm=-3;lm<=3;lm++) begin
        test_vtype=(word_t'(sew)<<3)|(word_t'(lm)&7);
        instruction={6'h10,1'b1,5'd3,5'd0,3'd2,5'd5,7'h57}; #1;
        assert(decoded_valid && legal==(sew<=lm+3)) else $fatal(1,"scalar extraction geometry");
        instruction={6'h10,1'b1,5'd0,5'd5,3'd6,5'd3,7'h57}; #1;
        assert(decoded_valid && legal==(sew<=lm+3)) else $fatal(1,"scalar insertion geometry");
        for (int op=0;op<8;op++) begin
          for (int src=8;src<10;src++) begin
            instruction={6'(op),1'b0,5'(src),5'd3,3'd2,5'd0,7'h57}; #1;
            assert(decoded_valid && legal==(sew<=lm+3 && (lm<=0 || src%(1<<lm)==0)))
              else $fatal(1,"reduction source alignment");
            checks++;
          end
        end
      end
    end
    instruction=32'h402020d7; #1; assert(!decoded_valid) else $fatal(1,"masked extraction reserved");
    instruction=32'h400160d7; #1; assert(!decoded_valid) else $fatal(1,"masked insertion reserved");
    test_vtype=0;
    instruction={6'd0,1'b0,5'd8,5'd0,3'd2,5'd7,7'h57}; #1;
    assert(decoded_valid && !legal) else $fatal(1,"masked reduction reads v0 at two EEWs");

    // Mask sources name single registers; only element destinations use LMUL.
    for(int lm=-3;lm<=3;lm++) begin
      test_vtype=word_t'(lm)&7;
      for(int op=0;op<7;op++) begin
        for(int dest=0;dest<32;dest++) begin
          for(int source=0;source<32;source++) begin
            for(int masked=0;masked<2;masked++) begin
              int selector, group;
              bit expected;
              group=lm>0 ? 1<<lm : 1;
              selector=op==0 ? 16 : op==1 ? 17 : op==2 ? 1 : op==3 ? 3 : op==4 ? 2 : op==5 ? 16 : 17;
              instruction={6'(op<2 ? 16 : 20),1'(masked==0),5'(op==6 ? 0 : source),5'(selector),3'd2,5'(dest),7'h57}; #1;
              expected=op<2 || ((masked==0 || dest!=0) && (op<5 ? dest!=source : dest%group==0 && (op==6 || source/group!=dest/group)));
              assert(decoded_valid && legal==expected) else $fatal(1,"scan legality op%0d dest%0d src%0d lm%0d masked%0d",op,dest,source,lm,masked);
              checks++;
            end
          end
        end
      end
    end
    // Slide-up groups may not overlap; down may be in-place. Scalar/unsigned
    // immediate fields are never subject to vector source-group alignment.
    for(int sew=0;sew<4;sew++) begin
      for(int lm=-3;lm<=3;lm++) begin
        for(int form=0;form<6;form++) begin
          for(int dest=0;dest<32;dest++) begin
            for(int source=0;source<32;source++) begin
              for(int masked=0;masked<2;masked++) begin
                int group, mode;
                bit up, expected;
                group=lm>0 ? 1<<lm : 1; up=form inside {0,1,4}; mode=form>=4 ? 6 : form inside {1,3} ? 3 : 4;
                test_vtype=(word_t'(sew)<<3)|(word_t'(lm)&7);
                instruction={6'(up ? 14 : 15),1'(masked==0),5'(source),5'd3,3'(mode),5'(dest),7'h57}; #1;
                expected=sew<=lm+3 && dest%group==0 && source%group==0 && (!up || dest!=source) && (masked==0 || (dest!=0 && source!=0));
                assert(decoded_valid && legal==expected) else $fatal(1,"slide legality form%0d dest%0d src%0d sew%0d lm%0d masked%0d",form,dest,source,sew,lm,masked);
                checks++;
              end
            end
          end
        end
      end
    end
    // Gather checks independent data/index groups, including fractional EMUL,
    // mixed-EEW source aliases, masked v0, and full-register interval overlap.
    for(int sew=0;sew<4;sew++) for(int lm=-3;lm<=3;lm++) begin
      for(int form=0;form<4;form++) for(int d=0;d<32;d++) begin
        for(int s1=0;s1<32;s1++) for(int variant=0;variant<4;variant++) begin
          for(int masked=0;masked<2;masked++) begin
            int s2, g, ig, ie;
            bit expected, disjoint_indices, disjoint_sources;
            s2=variant==0 ? 8 : variant==1 ? s1 : variant==2 ? d : 0;
            g=lm>0 ? 1<<lm : 1; ie=lm+(form==1 ? 1-sew : 0); ig=ie>0 ? 1<<ie : 1;
            disjoint_indices=d>=s1+ig || s1>=d+g;
            disjoint_sources=s2>=s1+ig || s1>=s2+g;
            expected=sew<=lm+3 && d%g==0 && s2%g==0 && d!=s2 && (masked==0 || (d!=0 && s2!=0));
            if(form<2) expected &= s1%ig==0 && disjoint_indices && (masked==0 || s1!=0);
            if(form==1) expected &= ie>=-3 && ie<=3 && (sew==1 || disjoint_sources);
            test_vtype=(word_t'(sew)<<3)|(word_t'(lm)&7);
            instruction={6'(form==1 ? 14 : 12),1'(masked==0),5'(s2),5'(s1),3'(form<2 ? 0 : form==2 ? 4 : 3),5'(d),7'h57}; #1;
            assert(decoded_valid && legal==expected) else $fatal(1,"gather legality form%0d d%0d s1%0d s2%0d sew%0d lm%0d masked%0d",form,d,s1,s2,sew,lm,masked);
            checks++;
          end
        end
      end
    end
    // Compress reads one ordinary data group and one single mask register.
    // Its destination must be disjoint from both, and the differently sized
    // source operands cannot alias each other.
    for(int sew=0;sew<4;sew++) for(int lm=-3;lm<=3;lm++) begin
      for(int d=0;d<32;d++) for(int s1=0;s1<32;s1++) begin
        for(int variant=0;variant<4;variant++) begin
          int s2, g;
          bit expected, mask_disjoint;
          s2=variant==0 ? 8 : variant==1 ? s1 : variant==2 ? d : 0;
          g=lm>0 ? 1<<lm : 1;
          mask_disjoint=(s1<d || s1>=d+g) && (s1<s2 || s1>=s2+g);
          expected=sew<=lm+3 && d%g==0 && s2%g==0 && d!=s2 && mask_disjoint;
          test_vtype=(word_t'(sew)<<3)|(word_t'(lm)&7);
          instruction={6'h17,1'b1,5'(s2),5'(s1),3'd2,5'(d),7'h57}; #1;
          assert(decoded_valid && legal==expected) else $fatal(1,"compress legality d%0d s1%0d s2%0d sew%0d lm%0d",d,s1,s2,sew,lm);
          checks++;
        end
      end
    end
    instruction=32'h5c21a0d7; #1;
    assert(!decoded_valid) else $fatal(1,"masked compress encoding accepted");
    // Sstatus aliases VS; reads do not dirty it, writes to vector state do.
    @(negedge clock); instruction = csr_word('h300, 2, 0); #1; saved_type = mstatus;
    write_csr('h300, word_t'('h400), saved_type);
    read_csr('h00a, 2);
    assert (mstatus[10:9] == 2 && !mstatus[XLEN-1]) else $fatal(1, "CSR read dirtied VS");
    send(csr_word('h100, 3), word_t'('h600), 0, 0, 1, (XLEN == 64 ? (word_t'(2) << 32) : 0) | 'h400);
    send(vset(0, 0), 1, 0, 1, 0, 0);
    send(csr_word('h008, 2, 0), 0, 0, 1, 0, 0);
    // Vector state is accessible in U and S when VS is enabled; privileged
    // CSR rejection still traps normally and returns the test to M mode.
    for (int lower_mode = 0; lower_mode < 2; lower_mode++) begin
      @(negedge clock); #1; saved_type = mstatus;
      write_csr('h300, (word_t'(lower_mode) << 11) | 'h200, saved_type);
      send(32'h30200073, 0, 0, 1, 0, 0); // MRET redirects, not an exception
      send(vset(0, 0), 1, 0, 0, 1, 1);
      read_csr('hc20, 1);
      send(csr_word('h300, 2, 0), 0, 0, 1, 0, 0);
    end
    $display("RV%0d VLEN=%0d vector configuration/CSR/decode passed %0d commits", XLEN, VLEN, checks);
    $finish;
  end
  initial begin #2000000; $fatal(1, "vector control timeout"); end
