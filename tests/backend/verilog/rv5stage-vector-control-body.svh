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
  logic mask_destination, invert_comparison, swap_operands;
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
    test_vtype = 'h80; instruction = 32'h662180d7; #1;
    assert (decoded_valid && invert_comparison) else $fatal(1, "vmsne inversion");
    instruction = 32'h7a21c0d7; #1;
    assert (decoded_valid && swap_operands) else $fatal(1, "vmsgt operand swap");
    test_vtype = word_t'(1) << (XLEN-1); #1;
    assert (!legal) else $fatal(1, "vill accepted arithmetic");

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
