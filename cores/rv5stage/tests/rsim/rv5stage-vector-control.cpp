// Preserves the rv5stage-vector-control cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using word_t = std::remove_cvref_t<decltype(scalar1)>;
// Runs the RV64 128=128 vector configuration and CSR contract.

// Exercises vector configuration, extension geometry, vector CSR state, privilege, and decode legality.
typedef std::uint64_t word_t;

int checks = 0, retired = 0;

std::uint32_t csr_word(int address, int op, int source = 1) {
  return (((address)&low_mask(32)) << 20) | (((source)&low_mask(32)) << 15) |
         (((op)&low_mask(32)) << 12) | UINT64_C(243);
}
std::uint32_t vset(int kind, int raw_type, int rs1 = 1, int rd = 1) {
  std::uint32_t high;
  high = kind == 0 ? (((raw_type)&low_mask(32)) << 20)
         : kind == 1
             ? (UINT64_C(0xc0000000) | (((raw_type)&low_mask(32)) << 20))
             : UINT64_C(0x80200000);
  return high | (((rs1)&low_mask(32)) << 15) | (((rd)&low_mask(32)) << 7) |
         UINT64_C(28759);
}
int maximum(word_t raw_type) {
  int sew, lm;
  sew = int(bit_slice(raw_type, 3, 3));
  lm = int(bit_slice(raw_type, 0, 3));
  if (lm >= 4)
    lm -= 8;
  if ((raw_type >> 8) != 0 || sew > 3 || lm == -4 || sew > lm + 3)
    return 0;
  return lm >= 0 ? ((128 / (8 << sew)) << lm) : ((128 / (8 << sew)) >> -lm);
}
std::uint32_t vector_extension(int selector, int source, int destination,
                               std::uint8_t masked = 0) {
  return (UINT64_C(18) << 26) | (((!masked) & low_mask(32)) << 25) |
         (((source)&low_mask(32)) << 20) | (((selector)&low_mask(32)) << 15) |
         (UINT64_C(2) << 12) | (((destination)&low_mask(32)) << 7) |
         UINT64_C(87);
}
std::uint32_t vector_fp_conversion(int selector, int source, int destination,
                                   std::uint8_t masked = 0) {
  return (UINT64_C(18) << 26) | (((!masked) & low_mask(32)) << 25) |
         (((source)&low_mask(32)) << 20) | (((selector)&low_mask(32)) << 15) |
         (UINT64_C(1) << 12) | (((destination)&low_mask(32)) << 7) |
         UINT64_C(87);
}
int zvfh_e8_selector(int index) {
  return index < 2 ? 10 + index : index < 4 ? 14 + index : 18 + index;
}
std::uint32_t whole_register_vmem(std::uint8_t store, int width, int registers,
                                  int regno, int base) {
  return (field(((registers - 1) & low_mask(3)), 3, 29) |
          field(UINT64_C(0), 1, 28) | field(UINT64_C(0), 2, 26) |
          field(UINT64_C(1), 1, 25) | field(UINT64_C(8), 5, 20) |
          field(((base)&low_mask(5)), 5, 15) |
          field(((store || width == 0 ? 0 : width + 4) & low_mask(3)), 3, 12) |
          field(((regno)&low_mask(5)), 5, 7) |
          field(store ? UINT64_C(39) : UINT64_C(7), 7, 0));
}
std::uint32_t whole_register_move(int registers, int destination, int source) {
  return (field(UINT64_C(39), 6, 26) | field(UINT64_C(1), 1, 25) |
          field(((source)&low_mask(5)), 5, 20) |
          field(((registers - 1) & low_mask(5)), 5, 15) |
          field(UINT64_C(3), 3, 12) | field(((destination)&low_mask(5)), 5, 7) |
          field(UINT64_C(87), 7, 0));
}

void send(std::uint32_t word, word_t a, word_t b, std::uint8_t trap_expected,
          std::uint8_t wb_expected, word_t expected_value,
          std::uint8_t explicit_fault = 0, std::uint8_t saturate_event = 0) {
  falling();
  instruction = word;
  scalar1 = a;
  scalar2 = b;
  exception_valid = explicit_fault;
  saturate = saturate_event;
  commit_valid = 1;
  settle();
  CHECK(decoded_valid && redirect_valid == trap_expected &&
        writeback_valid == wb_expected);
  if (wb_expected)
    CHECK(writeback_value == expected_value);
  rising();
  settle();
  if (!trap_expected)
    retired++;
  checks++;
  falling();
  commit_valid = 0;
  exception_valid = 0;
  saturate = 0;
}
void read_csr(int address, word_t value) {
  send(csr_word(address, 2, 0), 0, 0, 0, 1, value);
}
void write_csr(int address, word_t value, word_t old) {
  send(csr_word(address, 1), value, 0, 0, 1, old);
}

void drive() {}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = 1;
  instruction = 0;
  scalar1 = 0;
  scalar2 = 0;
  test_vtype = 0;
  test_vstart = 0;
  commit_valid = 0;
  exception_valid = 0;
  saturate = 0;
  {
    int max_vl, avl, selected;
    word_t expected_type, saved_type, saved_vl;
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      rising();
    falling();
    reset = 0;
    CHECK(state.pvl == 0 && state.pvtype == (word_t(1) << (64 - 1)) &&
          bit_slice(mstatus, 9, 2) == 0);
    send(vset(0, 0), 4, 0, 1, 0, 0); // VS Off
    send(csr_word(UINT64_C(3104), 2, 0), 0, 0, 1, 0, 0);
    read_csr(UINT64_C(834), 2);
    // CSRRS avoids depending on RV32/RV64 fixed status fields.
    falling();
    instruction = csr_word(UINT64_C(768), 2, 0);
    settle();
    saved_type = mstatus;
    send(csr_word(UINT64_C(768), 2), word_t(UINT64_C(512)), 0, 0, 1,
         saved_type);
    CHECK(bit_slice(mstatus, 9, 2) == 1);
    read_csr(UINT64_C(3106), word_t(128) / 8);

    // Sweep the full vsetvli vtype field, including every reserved upper bit.
    for (int raw_type = 0; raw_type < 2048; raw_type++) {
      max_vl = maximum(word_t(raw_type));
      for (int choice = 0; choice < 4; choice++) {
        avl = choice == 0   ? 0
              : choice == 1 ? 1
              : choice == 2 ? max_vl
                            : max_vl + 9;
        selected = avl < max_vl ? avl : max_vl;
        expected_type =
            max_vl == 0 ? (word_t(1) << (64 - 1)) : word_t(raw_type);
        send(vset(0, raw_type), word_t(avl), 0, 0, 1, word_t(selected));
        CHECK(state.pvl == word_t(selected) && state.pvtype == expected_type &&
              state.pvstart == 0 && bit_slice(mstatus, 9, 2) == 3 &&
              bit_slice(mstatus, 64 - 1, 1));
      }
    }
    // Immediate AVL zero is really zero; rs1=x0 register forms select VLMAX.
    send(vset(1, 0, 0), UINT64_MAX, 0, 0, 1, 0);
    send(vset(0, 0, 0), 0, 0, 0, 1, word_t(128) / 8);
    send(vset(2, 0, 0), 0, word_t(UINT64_C(16)), 0, 1, word_t(128) / 32);
    for (int immediate_avl = 0; immediate_avl < 32; immediate_avl++) {
      selected = immediate_avl < 128 / 32 ? immediate_avl : 128 / 32;
      send(vset(1, UINT64_C(208), immediate_avl), UINT64_MAX, 0, 0, 1,
           word_t(selected));
    }
    // vsetvl checks every 64 vtype bit, not just the immediate field.
    for (int bit_index = 8; bit_index < 64; bit_index++) {
      send(vset(2, 0), 4, word_t(1) << bit_index, 0, 1, 0);
      CHECK(state.pvtype == (word_t(1) << (64 - 1)));
    }
    send(vset(0, UINT64_C(16)), 3, 0, 0, 1, 3);       // e32,m1
    send(vset(0, UINT64_C(25), 0, 0), 0, 0, 0, 0, 0); // e64,m2, same VLMAX
    CHECK(state.pvl == 3 && state.pvtype == UINT64_C(25));
    send(vset(0, 0, 0, 0), 0, 0, 1, 0, 0); // reserved VLMAX change
    CHECK(state.pvl == 3 && state.pvtype == UINT64_C(25));

    write_csr(UINT64_C(8), UINT64_MAX, 0);
    read_csr(UINT64_C(8), word_t(128) - 1);
    write_csr(UINT64_C(15), UINT64_MAX, 0);
    read_csr(UINT64_C(10), 3);
    read_csr(UINT64_C(9), 1);
    read_csr(UINT64_C(15), 7);
    write_csr(UINT64_C(9), 0, 1);
    falling();
    saturate = 1;
    rising();
    settle();
    saturate = 0;
    read_csr(UINT64_C(9), 1);
    send(csr_word(UINT64_C(9), 1), 0, 0, 0, 1, 1, 0,
         1); // explicit write wins over sticky set
    read_csr(UINT64_C(9), 0);
    send(csr_word(UINT64_C(10), 3), 1, 0, 0, 1, 3); // clear vxrm bit0
    read_csr(UINT64_C(15), 4);
    send(vset(0, 0), 0, 0, 0, 1, 0);
    CHECK(state.pvstart == 0);

    // No state update or architectural retirement on an explicit older fault.
    saved_type = state.pvtype;
    saved_vl = state.pvl;
    send(vset(0, UINT64_C(24)), 4, 0, 1, 0, 0, 1);
    CHECK(state.pvtype == saved_type && state.pvl == saved_vl);
    send(csr_word(UINT64_C(15), 1), 7, 0, 1, 0, 0, 1);
    read_csr(UINT64_C(15), 4);
    for (int address = UINT64_C(3104); address <= UINT64_C(3106); address++)
      send(csr_word(address, 1), 0, 0, 1, 0, 0);
    read_csr(UINT64_C(835), word_t(csr_word(UINT64_C(3106), 1)));
    read_csr(UINT64_C(833), UINT64_C(256));
    read_csr(UINT64_C(3074), word_t(retired));

    // Move source metadata and mask-register geometry are not ordinary data groups.
    falling();
    for (int lm = 0; lm < 4; lm++) {
      test_vtype = word_t(lm);
      for (int op = 24; op < 32; op++) {
        instruction = (((op)&low_mask(32)) << 26) | UINT64_C(0x272a1d7);
        settle(); // vd=3,vs1=5,vs2=7
        CHECK(decoded_valid && legal && mask_destination && operand == 0);
        bit_slice(instruction, 7, 5) = 0;
        settle();
        CHECK(legal);
        bit_slice(instruction, 25, 1) = 0;
        settle();
        CHECK(!decoded_valid);
      }
      instruction = UINT64_C(0x5e0fc057);
      settle(); // vmv.v.x v0,x31
      CHECK(decoded_valid && legal && operand == 1);
      instruction = UINT64_C(0x5e080457);
      settle(); // vmv.v.v v8,v16
      CHECK(decoded_valid && legal && operand == 0);
      bit_slice(instruction, 20, 5) = 1;
      settle();
      CHECK(!decoded_valid);
      instruction = UINT64_C(0x5c880c57);
      settle(); // vmerge.vvm v24,v8,v16,v0
      CHECK(decoded_valid && legal);
      bit_slice(instruction, 20, 5) = 0;
      settle();
      CHECK(!legal);
      bit_slice(instruction, 20, 5) = 8;
      bit_slice(instruction, 15, 5) = 0;
      settle();
      CHECK(!legal);
      bit_slice(instruction, 12, 3) = 4;
      settle();
      CHECK(legal && operand == 1);
      bit_slice(instruction, 12, 3) = 3;
      settle();
      CHECK(legal && operand == 2);
      bit_slice(instruction, 7, 5) = 0;
      settle();
      CHECK(!legal);
    }
    // Zvbb remains extension-gated while reusing ordinary vector legality.
    test_vtype = 0;
    instruction = UINT64_C(0x62180d7);
    settle(); // vandn.vv v1,v2,v3
    CHECK(decoded_valid && !base_decoded_valid && legal && operand == 0);
    instruction = UINT64_C(0x4a2520d7);
    settle(); // vbrev.v v1,v2
    CHECK(decoded_valid && !base_decoded_valid && legal);
    instruction = UINT64_C(0x562fb0d7);
    settle(); // vror.vi v1,v2,63
    CHECK(decoded_valid && !base_decoded_valid && legal && operand == 2);
    instruction = (field(UINT64_C(53), 6, 26) | field(UINT64_C(1), 1, 25) |
                   field(UINT64_C(16), 5, 20) | field(UINT64_C(2), 5, 15) |
                   field(UINT64_C(0), 3, 12) | field(UINT64_C(8), 5, 7) |
                   field(UINT64_C(87), 7, 0));
    settle(); // vwsll.vv v8,v16,v2
    CHECK(decoded_valid && !base_decoded_valid && legal && widening &&
          operand == 0);
    test_vtype = UINT64_C(24);
    settle(); // e64,m1 cannot produce EEW=128
    CHECK(decoded_valid && !legal);

    // Same-width data groups and mask destinations have different overlap rules.
    falling();
    test_vtype = 1; // e8,m2
    instruction = UINT64_C(0x2220257);
    settle(); // vadd.vv v4,v2,v4, unmasked
    CHECK(decoded_valid && legal && operand == 0 && !mask_destination);
    bit_slice(instruction, 7, 5) = 3;
    settle();
    CHECK(!legal);
    bit_slice(instruction, 7, 5) = 0;
    bit_slice(instruction, 25, 1) = 0;
    settle();
    CHECK(!legal);
    instruction = UINT64_C(0x622200d7);
    settle(); // vmseq.vv v1,v2,v4, nonoverlap
    CHECK(decoded_valid && mask_destination && legal);
    bit_slice(instruction, 7, 5) = 3;
    settle();
    CHECK(!legal);
    bit_slice(instruction, 7, 5) = 2;
    settle();
    CHECK(legal);
    // Carry/borrow forms read v0 as ordinary EEW=1 input data, never as a
    // predication mask. Data results cannot overwrite that input, while mask
    // results can target v0 and the vm=1 forms omit carry-in.
    test_vtype = 0;
    instruction = UINT64_C(0x40880c57);
    settle(); // vadc.vvm v24,v8,v16,v0
    CHECK(decoded_valid && legal && carry_input && mask_result_select &&
          !mask_destination && !subtract);
    bit_slice(instruction, 7, 5) = 0;
    settle();
    CHECK(!legal);
    instruction = UINT64_C(0x40800c57);
    settle(); // vadc.vvm with vs2=v0
    CHECK(!legal);
    instruction = UINT64_C(0x40800c57);
    bit_slice(instruction, 20, 5) = 8;
    bit_slice(instruction, 15, 5) = 0;
    settle();
    CHECK(!legal);
    instruction = UINT64_C(0x42880c57);
    settle(); // reserved vm=1 vadc.vv spelling
    CHECK(!decoded_valid);
    instruction = UINT64_C(0x44880057);
    settle(); // vmadc.vvm v0,v8,v16,v0
    CHECK(decoded_valid && legal && carry_input && mask_result_select &&
          mask_destination && !subtract);
    instruction = UINT64_C(0x46880057);
    settle(); // vmadc.vv v0,v8,v16
    CHECK(decoded_valid && legal && !carry_input && mask_result_select &&
          mask_destination && !subtract);
    instruction = UINT64_C(0x48880c57);
    settle(); // vsbc.vvm v24,v8,v16,v0
    CHECK(decoded_valid && legal && carry_input && mask_result_select &&
          !mask_destination && subtract);
    instruction = UINT64_C(0x4c880057);
    settle(); // vmsbc.vvm v0,v8,v16,v0
    CHECK(decoded_valid && legal && carry_input && mask_result_select &&
          mask_destination && subtract);
    instruction = UINT64_C(0x4e880057);
    settle(); // vmsbc.vv v0,v8,v16
    CHECK(decoded_valid && legal && !carry_input && mask_result_select &&
          mask_destination && subtract);
    // Unary extension reads source elements at SEW/2, /4, or /8 and writes an
    // ordinary LMUL destination group. Unsupported EEW/EMUL pairs are illegal.
    for (int ratio_index = 0; ratio_index < 3; ratio_index++) {
      for (int sew = 0; sew < 4; sew++) {
        for (int lm = -3; lm <= 3; lm++) {
          int power = ratio_index + 1;
          bool expected_legal =
              sew <= lm + 3 && sew >= power && lm - power >= -3;
          test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
          instruction = vector_extension(6 - 2 * ratio_index, 8, 16);
          settle();
          CHECK(decoded_valid && extension && !extension_signed &&
                int(extension_ratio) == ratio_index && operand == 2 &&
                legal == expected_legal);
          bit_slice(instruction, 15, 5) = ((7 - 2 * ratio_index) & low_mask(5));
          settle();
          CHECK(decoded_valid && extension_signed && legal == expected_legal);
          checks += 2;
        }
      }
    }
    test_vtype = UINT64_C(
        19); // e32,m8: vf4 source EMUL2 occupies the high destination quarter.
    instruction = vector_extension(4, 6, 0);
    settle();
    CHECK(legal);
    for (int source = 0; source <= 4; source += 2) {
      bit_slice(instruction, 20, 5) = ((source)&low_mask(5));
      settle();
      CHECK(!legal);
    }
    instruction = vector_extension(4, 8, 1);
    settle();
    CHECK(!legal);
    test_vtype = UINT64_C(24);
    instruction = vector_extension(6, 8, 16, 1);
    settle();
    CHECK(legal);
    bit_slice(instruction, 7, 5) = 0;
    settle();
    CHECK(!legal);
    instruction = vector_extension(6, 0, 16, 1);
    settle();
    CHECK(!legal);
    // Widening doubles destination EMUL. A narrow source may overlap only the
    // highest-numbered portion of that group, and SEW64/LMUL8 are reserved.
    test_vtype = 0; // e8,m1
    instruction = UINT64_C(0xc3012457);
    settle(); // vwaddu.vv v8,v16,v2, unmasked
    CHECK(decoded_valid && legal && widening && !left_signed && !right_signed &&
          !subtract);
    instruction = UINT64_C(0xce916457);
    settle(); // vwsub.vx v8,v9,x2, unmasked; high-source overlap
    CHECK(decoded_valid && legal && widening && left_signed && right_signed &&
          subtract && operand == 1);
    bit_slice(instruction, 20, 5) = 8;
    settle();
    CHECK(!legal);
    bit_slice(instruction, 20, 5) = 16;
    bit_slice(instruction, 7, 5) = 9;
    settle();
    CHECK(!legal);
    bit_slice(instruction, 7, 5) = 8;
    bit_slice(instruction, 25, 1) = 0;
    settle();
    CHECK(legal);
    bit_slice(instruction, 7, 5) = 0;
    settle();
    CHECK(!legal);
    test_vtype = UINT64_C(24);
    instruction = UINT64_C(0xc3012457);
    settle(); // e64,m1
    CHECK(!legal);
    test_vtype = 3;
    settle(); // e8,m8 -> destination EMUL16
    CHECK(!legal);
    // Wide-source forms align vs2 to the doubled EMUL, permit vd=vs2, and
    // retain the narrow source's high-part-only overlap rule.
    test_vtype = 0;
    instruction = UINT64_C(0xd3012457);
    settle(); // vwaddu.wv v8,v16,v2, unmasked
    CHECK(decoded_valid && legal && widening && wide_vs2 && !left_signed &&
          !right_signed && !subtract);
    instruction = UINT64_C(0xde816457);
    settle(); // vwsub.wx v8,v8,x2, unmasked; in-place wide source
    CHECK(decoded_valid && legal && widening && wide_vs2 && left_signed &&
          right_signed && subtract && operand == 1);
    bit_slice(instruction, 20, 5) = 9;
    settle();
    CHECK(!legal);
    instruction = UINT64_C(0xde916457);
    settle(); // vd=v8, vs2=v9 is not a doubled-group base
    CHECK(!legal);
    instruction = UINT64_C(0xd688a457);
    settle(); // vwadd.wv v8,v8,v17; narrow source disjoint
    CHECK(decoded_valid && legal && wide_vs2);
    bit_slice(instruction, 15, 5) = 8;
    settle();
    CHECK(!legal);
    bit_slice(instruction, 15, 5) = 9;
    settle();
    CHECK(legal);
    // Narrowing shifts read vs2 at twice SEW, reuse the existing wide shifter,
    // and permit destination overlap only in the wide source's low part.
    instruction = UINT64_C(0xb3010457);
    settle(); // vnsrl.wv v8,v16,v2, unmasked
    CHECK(decoded_valid && legal && narrowing && wide_vs2 && !widening &&
          operand == 0);
    instruction = UINT64_C(0xb6814457);
    settle(); // vnsra.wx v8,v8,x2, unmasked
    CHECK(decoded_valid && legal && narrowing && wide_vs2 && operand == 1);
    bit_slice(instruction, 7, 5) = 9;
    settle();
    CHECK(!legal);
    bit_slice(instruction, 7, 5) = 16;
    settle();
    CHECK(legal);
    bit_slice(instruction, 20, 5) = 9;
    settle();
    CHECK(!legal);
    instruction = UINT64_C(0xb2848857);
    settle(); // vnsrl.wv v16,v8,v9, unmasked
    CHECK(!legal);
    bit_slice(instruction, 15, 5) = 10;
    settle();
    CHECK(legal);
    test_vtype = 1;
    instruction = UINT64_C(0xb081cc57);
    settle(); // masked vnsrl.wx v24,v8,x3, LMUL=2
    CHECK(legal);
    test_vtype = 0;
    instruction = UINT64_C(0xb0080457);
    settle(); // masked vnsrl.wv v8,v0,v16
    CHECK(!legal);
    test_vtype = UINT64_C(24);
    instruction = UINT64_C(0xb3010457);
    settle();
    CHECK(!legal);
    test_vtype = 3;
    settle();
    CHECK(!legal);
    // Fixed-point add/sub variants select physical arithmetic modes without
    // changing ordinary same-width register-group legality.
    test_vtype = 0;
    instruction = UINT64_C(0x822180d7);
    settle(); // vsaddu.vv v1,v2,v3
    CHECK(decoded_valid && legal && arithmetic_mode == 1 && !rounding &&
          !subtract);
    instruction = UINT64_C(0x8e2180d7);
    settle(); // vssub.vv v1,v2,v3
    CHECK(decoded_valid && legal && arithmetic_mode == 2 && !rounding &&
          subtract);
    instruction = UINT64_C(0x2221a0d7);
    settle(); // vaaddu.vv v1,v2,v3
    CHECK(decoded_valid && legal && arithmetic_mode == 3 && rounding &&
          !subtract);
    instruction = UINT64_C(0x2e21a0d7);
    settle(); // vasub.vv v1,v2,v3
    CHECK(decoded_valid && legal && arithmetic_mode == 4 && rounding &&
          subtract);
    instruction = UINT64_C(0x9e2180d7);
    settle(); // vsmul.vv v1,v2,v3
    CHECK(decoded_valid && legal == (64 == 64) && !divide && left_signed &&
          right_signed && multiply_result == 2);
    // Widening multiply shares doubled-EMUL alignment and overlap policy with
    // widening add/sub while retaining singleton shared-multiplier execution.
    test_vtype = 0;
    instruction = UINT64_C(0xe3012457);
    settle(); // vwmulu.vv v8,v16,v2, unmasked
    CHECK(decoded_valid && legal == (64 == 64) && widening && !divide &&
          !left_signed && !right_signed && multiply_result == 3);
    instruction = UINT64_C(0xee916457);
    settle(); // vwmul.vx v8,v9,x2, unmasked; high-source overlap
    CHECK(decoded_valid && legal == (64 == 64) && widening && left_signed &&
          right_signed && operand == 1);
    instruction = UINT64_C(0xffd0a157);
    settle(); // vwmaccsu.vv v2,v1,v29
    CHECK(decoded_valid && widening && !left_signed && right_signed);
    instruction = UINT64_C(0xff836057);
    settle(); // vwmaccsu.vx v0,x6,v24
    CHECK(decoded_valid && widening && !left_signed && right_signed);
    instruction = UINT64_C(0xfb866057);
    settle(); // vwmaccus.vx v0,x12,v24
    CHECK(decoded_valid && widening && left_signed && !right_signed);
    instruction = UINT64_C(0xee916457);
    settle();
    bit_slice(instruction, 20, 5) = 8;
    settle();
    CHECK(!legal);
    bit_slice(instruction, 20, 5) = 16;
    bit_slice(instruction, 7, 5) = 9;
    settle();
    CHECK(!legal);
    bit_slice(instruction, 7, 5) = 8;
    bit_slice(instruction, 25, 1) = 0;
    settle();
    CHECK(legal == (64 == 64));
    bit_slice(instruction, 7, 5) = 0;
    settle();
    CHECK(!legal);
    test_vtype = 7;
    instruction = UINT64_C(0xe3012457);
    settle(); // e8,mf2 -> widened EMUL1
    CHECK(legal == (64 == 64));
    bit_slice(instruction, 20, 5) = 8;
    settle();
    CHECK(!legal);
    test_vtype = UINT64_C(24);
    instruction = UINT64_C(0xe3012457);
    settle();
    CHECK(!legal);
    test_vtype = 3;
    settle();
    CHECK(!legal);
    test_vtype = UINT64_C(128);
    instruction = UINT64_C(0x662180d7);
    settle();
    CHECK(decoded_valid && invert_comparison);
    instruction = UINT64_C(0x7a21c0d7);
    settle();
    CHECK(decoded_valid && swap_operands);
    test_vtype = word_t(1) << (64 - 1);
    settle();
    CHECK(!legal);

    // Memory EEW changes EMUL, independently of configured SEW. Check every
    // legal/illegal exponent and register alignment, with masked v0 rules.
    for (int sew = 0; sew < 4; sew++) {
      for (int lm = -3; lm <= 3; lm++) {
        for (int eew = 0; eew < 4; eew++) {
          for (int store = 0; store < 2; store++) {
            for (int regno = 0; regno < 32; regno++) {
              int emul = lm + eew - sew;
              bool aligned = emul <= 0 ? 1 : (regno % (1 << emul)) == 0;
              bool expected_legal = 64 == 64 && sew <= lm + 3 && emul >= -3 &&
                                    emul <= 3 && aligned && regno != 0;
              test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
              instruction =
                  (field(UINT64_C(0), 6, 26) | field(UINT64_C(0), 1, 25) |
                   field(UINT64_C(0), 5, 20) | field(UINT64_C(1), 5, 15) |
                   field(((eew == 0 ? 0 : eew + 4) & low_mask(3)), 3, 12) |
                   field(((regno)&low_mask(5)), 5, 7) |
                   field(store != 0 ? UINT64_C(39) : UINT64_C(7), 7, 0));
              settle();
              CHECK(decoded_valid && legal == expected_legal);
              checks++;
            }
          }
        }
      }
    }
    // Indexed memory takes data geometry from vtype and offset geometry from
    // the encoded EEW. Both addressing modes use the same conservative
    // element-order implementation.
    for (int sew = 0; sew < 4; sew++) {
      for (int lm = -3; lm <= 3; lm++) {
        for (int index_eew = 0; index_eew < 4; index_eew++) {
          for (int store = 0; store < 2; store++) {
            for (int ordered = 0; ordered < 2; ordered++) {
              int index_emul = lm + index_eew - sew;
              bool expected_legal = 64 == 64 && sew <= lm + 3 &&
                                    index_emul >= -3 && index_emul <= 3;
              test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
              instruction =
                  (field(UINT64_C(0), 4, 28) |
                   field(ordered != 0 ? UINT64_C(3) : UINT64_C(1), 2, 26) |
                   field(UINT64_C(1), 1, 25) | field(UINT64_C(24), 5, 20) |
                   field(UINT64_C(1), 5, 15) |
                   field(((index_eew == 0 ? 0 : index_eew + 4) & low_mask(3)),
                         3, 12) |
                   field(UINT64_C(8), 5, 7) |
                   field(store != 0 ? UINT64_C(39) : UINT64_C(7), 7, 0));
              settle();
              CHECK(decoded_valid && legal == expected_legal);
              checks++;
            }
          }
        }
      }
    }
    // Non-segment indexed loads follow the architectural destination/source
    // overlap rules. Equal EEWs may overlap anywhere in aligned groups.
    test_vtype = UINT64_C(16); // e32,m1
    instruction = (field(UINT64_C(0), 4, 28) | field(UINT64_C(1), 2, 26) |
                   field(UINT64_C(1), 1, 25) | field(UINT64_C(8), 5, 20) |
                   field(UINT64_C(1), 5, 15) | field(UINT64_C(6), 3, 12) |
                   field(UINT64_C(8), 5, 7) | field(UINT64_C(7), 7, 0));
    settle();
    CHECK(legal == (64 == 64));
    bit_slice(instruction, 0, 7) = UINT64_C(39);
    settle();
    CHECK(legal == (64 == 64));
    bit_slice(instruction, 12, 3) = UINT64_C(5);
    settle();
    CHECK(!legal);
    // A narrower destination may overlap only the low part of the index group.
    test_vtype = UINT64_C(8); // e16,m1
    instruction = (field(UINT64_C(0), 4, 28) | field(UINT64_C(1), 2, 26) |
                   field(UINT64_C(1), 1, 25) | field(UINT64_C(8), 5, 20) |
                   field(UINT64_C(1), 5, 15) | field(UINT64_C(6), 3, 12) |
                   field(UINT64_C(8), 5, 7) | field(UINT64_C(7), 7, 0));
    settle();
    CHECK(legal == (64 == 64));
    bit_slice(instruction, 7, 5) = UINT64_C(9);
    settle();
    CHECK(!legal);
    // A wider destination may overlap an integer-EMUL index group only at the
    // high end of the destination group.
    test_vtype = UINT64_C(17); // e32,m2
    instruction = (field(UINT64_C(0), 4, 28) | field(UINT64_C(1), 2, 26) |
                   field(UINT64_C(1), 1, 25) | field(UINT64_C(9), 5, 20) |
                   field(UINT64_C(1), 5, 15) | field(UINT64_C(5), 3, 12) |
                   field(UINT64_C(8), 5, 7) | field(UINT64_C(7), 7, 0));
    settle();
    CHECK(legal == (64 == 64));
    bit_slice(instruction, 20, 5) = UINT64_C(8);
    settle();
    CHECK(!legal);
    test_vtype = UINT64_C(16); // e32,m1; e16 index has fractional EMUL
    instruction = (field(UINT64_C(0), 4, 28) | field(UINT64_C(1), 2, 26) |
                   field(UINT64_C(1), 1, 25) | field(UINT64_C(8), 5, 20) |
                   field(UINT64_C(1), 5, 15) | field(UINT64_C(5), 3, 12) |
                   field(UINT64_C(8), 5, 7) | field(UINT64_C(7), 7, 0));
    settle();
    CHECK(!legal);
    // Indexed segment loads always require disjoint destination and index groups.
    test_vtype = UINT64_C(16); // e32,m1
    instruction = (field(UINT64_C(1), 3, 29) | field(UINT64_C(0), 1, 28) |
                   field(UINT64_C(1), 2, 26) | field(UINT64_C(1), 1, 25) |
                   field(UINT64_C(8), 5, 20) | field(UINT64_C(1), 5, 15) |
                   field(UINT64_C(6), 3, 12) | field(UINT64_C(8), 5, 7) |
                   field(UINT64_C(7), 7, 0));
    settle();
    CHECK(!legal);
    test_vtype = 0;
    instruction = (field(UINT64_C(0), 4, 28) | field(UINT64_C(3), 2, 26) |
                   field(UINT64_C(0), 1, 25) | field(UINT64_C(0), 5, 20) |
                   field(UINT64_C(1), 5, 15) | field(UINT64_C(0), 3, 12) |
                   field(UINT64_C(8), 5, 7) | field(UINT64_C(7), 7, 0));
    settle();
    CHECK(!legal);
    instruction = (field(UINT64_C(0), 4, 28) | field(UINT64_C(3), 2, 26) |
                   field(UINT64_C(0), 1, 25) | field(UINT64_C(8), 5, 20) |
                   field(UINT64_C(1), 5, 15) | field(UINT64_C(0), 3, 12) |
                   field(UINT64_C(0), 5, 7) | field(UINT64_C(39), 7, 0));
    settle();
    CHECK(!legal);

    // Unit- and constant-stride segment groups use ceil(EMUL) registers per field. Every
    // field group is aligned, the total footprint is at most eight registers,
    // and the final field cannot wrap past v31.
    for (int strided = 0; strided < 2; strided++) {
      for (int sew = 0; sew < 4; sew++) {
        for (int lm = -3; lm <= 3; lm++) {
          for (int eew = 0; eew < 4; eew++) {
            for (int nf = 1; nf < 8; nf++) {
              for (int regno = 0; regno < 32; regno++) {
                int emul = lm + eew - sew;
                int registers = emul <= 0 ? 1 : 1 << emul;
                int footprint = registers * (nf + 1);
                bool aligned = regno % registers == 0;
                bool expected_legal = 64 == 64 && sew <= lm + 3 && emul >= -3 &&
                                      emul <= 3 && aligned && footprint <= 8 &&
                                      regno + footprint <= 32;
                test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
                instruction =
                    (field(((nf)&low_mask(3)), 3, 29) |
                     field(UINT64_C(0), 1, 28) |
                     field(strided != 0 ? UINT64_C(2) : UINT64_C(0), 2, 26) |
                     field(UINT64_C(1), 1, 25) |
                     field(strided != 0 ? UINT64_C(2) : UINT64_C(0), 5, 20) |
                     field(UINT64_C(1), 5, 15) |
                     field(((eew == 0 ? 0 : eew + 4) & low_mask(3)), 3, 12) |
                     field(((regno)&low_mask(5)), 5, 7) |
                     field(UINT64_C(7), 7, 0));
                settle();
                CHECK(decoded_valid && legal == expected_legal);
                checks++;
              }
            }
          }
        }
      }
    }

    // Fault-only-first forms share ordinary unit-stride geometry. NFIELDS
    // changes only the complete destination footprint.
    test_vtype = UINT64_C(25); // e64,m2
    instruction = UINT64_C(0x3047407);
    settle();
    CHECK(decoded_valid && legal == (64 == 64));
    instruction = UINT64_C(0x23047407);
    settle();
    CHECK(decoded_valid && legal == (64 == 64));
    checks += 2;

    // Indexed segments use SEW/LMUL for every data field and the encoded EEW
    // for one shared index group. Loads require the complete data footprint to
    // be disjoint from that index group; same-EEW stores may alias it.
    for (int ordered = 0; ordered < 2; ordered++) {
      for (int store = 0; store < 2; store++) {
        for (int sew = 0; sew < 4; sew++) {
          for (int lm = -3; lm <= 3; lm++) {
            for (int index_eew = 0; index_eew < 4; index_eew++) {
              for (int nf = 1; nf < 8; nf++) {
                for (int regno = 0; regno < 32; regno++) {
                  for (int index_choice = 0; index_choice < 5; index_choice++) {
                    int index_emul = lm + index_eew - sew;
                    int data_registers = lm <= 0 ? 1 : 1 << lm;
                    int index_registers = index_emul <= 0 ? 1 : 1 << index_emul;
                    int footprint = data_registers * (nf + 1);
                    int index_reg = index_choice == 0   ? 0
                                    : index_choice == 1 ? 1
                                    : index_choice == 2 ? regno
                                    : index_choice == 3
                                        ? (regno + data_registers < 32
                                               ? regno + data_registers
                                               : 31)
                                        : 31;
                    bool data_aligned = regno % data_registers == 0;
                    bool index_aligned = index_reg % index_registers == 0;
                    bool disjoint = regno >= index_reg + index_registers ||
                                    index_reg >= regno + footprint;
                    bool overlap_legal =
                        store != 0 ? index_eew == sew || disjoint : disjoint;
                    bool expected_legal =
                        64 == 64 && sew <= lm + 3 && index_emul >= -3 &&
                        index_emul <= 3 && data_aligned && index_aligned &&
                        footprint <= 8 && regno + footprint <= 32 &&
                        overlap_legal;
                    test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
                    instruction =
                        (field(((nf)&low_mask(3)), 3, 29) |
                         field(UINT64_C(0), 1, 28) |
                         field(ordered != 0 ? UINT64_C(3) : UINT64_C(1), 2,
                               26) |
                         field(UINT64_C(1), 1, 25) |
                         field(((index_reg)&low_mask(5)), 5, 20) |
                         field(UINT64_C(1), 5, 15) |
                         field(((index_eew == 0 ? 0 : index_eew + 4) &
                                low_mask(3)),
                               3, 12) |
                         field(((regno)&low_mask(5)), 5, 7) |
                         field(store != 0 ? UINT64_C(39) : UINT64_C(7), 7, 0));
                    settle();
                    CHECK(decoded_valid && legal == expected_legal);
                    checks++;
                  }
                }
              }
            }
          }
        }
      }
    }

    // Same-width FP admits aligned FP32/64 groups on RV64.
    for (int op = 0; op < 3; op++) {
      for (int sew = 0; sew < 4; sew++) {
        for (int lm = -3; lm <= 3; lm++) {
          for (int rd = 0; rd < 16; rd++) {
            bool expected_legal = 64 == 64 && sew >= 2 && sew <= lm + 3 &&
                                  rd != 0 && (lm <= 0 || rd % (1 << lm) == 0);
            test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
            instruction =
                (field(((op == 0   ? 0
                         : op == 1 ? 2
                                   : 36) &
                        low_mask(6)),
                       6, 26) |
                 field(UINT64_C(0), 1, 25) | field(UINT64_C(16), 5, 20) |
                 field(UINT64_C(24), 5, 15) | field(UINT64_C(1), 3, 12) |
                 field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(87), 7, 0));
            settle();
            CHECK(decoded_valid && legal == expected_legal);
            CHECK(full_half_legal ==
                  (64 == 64 && sew >= 1 && sew <= lm + 3 && rd != 0 &&
                   (lm <= 0 || rd % (1 << lm) == 0)));
            checks++;
          }
        }
      }
    }

    // Width-changing FP conversions normally use RV64D E32<->E64. Zvfhmin
    // additionally admits only the FP-to-FP E16<->E32 pair.
    for (int narrow = 0; narrow < 2; narrow++) {
      for (int sew = 0; sew < 4; sew++) {
        for (int lm = -3; lm <= 3; lm++) {
          bool expected_legal = 64 == 64 && sew == 2 && lm >= -1 && lm <= 2;
          test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
          instruction = vector_fp_conversion(narrow != 0 ? 16 : 8, 16, 8);
          settle();
          CHECK(decoded_valid && widening == (narrow == 0) &&
                narrowing == (narrow != 0) && legal == expected_legal);
          checks++;
        }
      }
    }
    test_vtype = UINT64_C(8); // e16,m1
    instruction = vector_fp_conversion(12, 16, 8);
    settle(); // vfwcvt.f.f.v
    CHECK(decoded_valid && legal && widening && fp_zvfhmin_e16);
    instruction = vector_fp_conversion(20, 16, 8);
    settle(); // vfncvt.f.f.w
    CHECK(decoded_valid && legal && narrowing && fp_zvfhmin_e16);
    instruction = vector_fp_conversion(8, 16, 8);
    settle(); // vfwcvt.xu.f.v
    CHECK(decoded_valid && !legal && full_half_legal && !fp_zvfhmin_e16);
    instruction = (field(UINT64_C(0), 6, 26) | field(UINT64_C(1), 1, 25) |
                   field(UINT64_C(16), 5, 20) | field(UINT64_C(24), 5, 15) |
                   field(UINT64_C(1), 3, 12) | field(UINT64_C(8), 5, 7) |
                   field(UINT64_C(87), 7, 0));
    settle(); // vfadd.vv
    CHECK(decoded_valid && !legal && full_half_legal);
    checks += 4;
    test_vtype = UINT64_C(0); // e8,m1
    for (int index = 0; index < 6; index++) {
      instruction = vector_fp_conversion(zvfh_e8_selector(index), 16, 8);
      settle();
      CHECK(decoded_valid && !legal && full_half_legal && fp_zvfh_e8);
      checks++;
    }
    instruction = vector_fp_conversion(8, 16, 8);
    settle();
    CHECK(decoded_valid && !legal && !full_half_legal && !fp_zvfh_e8);
    checks++;
    test_vtype = UINT64_C(16); // e32,m1
    instruction = vector_fp_conversion(8, 9, 8);
    settle();
    CHECK(legal == (64 == 64));
    bit_slice(instruction, 20, 5) = 8;
    settle();
    CHECK(!legal);
    instruction = vector_fp_conversion(8, 16, 9);
    settle();
    CHECK(!legal);
    instruction = vector_fp_conversion(16, 8, 8);
    settle();
    CHECK(legal == (64 == 64));
    bit_slice(instruction, 7, 5) = 9;
    settle();
    CHECK(!legal);
    instruction = vector_fp_conversion(16, 9, 16);
    settle();
    CHECK(!legal);
    instruction = vector_fp_conversion(16, 8, 0, 1);
    settle();
    CHECK(!legal);

    // Widening FP arithmetic shares conversion geometry, but .w forms read
    // vs2 at destination width and may update that wide group in place.
    instruction = (field(UINT64_C(48), 6, 26) | field(UINT64_C(1), 1, 25) |
                   field(UINT64_C(9), 5, 20) | field(UINT64_C(16), 5, 15) |
                   field(UINT64_C(1), 3, 12) | field(UINT64_C(8), 5, 7) |
                   field(UINT64_C(87), 7, 0));
    settle(); // vfwadd.vv v8,v9,v16
    CHECK(decoded_valid && legal == (64 == 64) && widening && !wide_vs2);
    bit_slice(instruction, 20, 5) = 8;
    settle();
    CHECK(!legal);
    instruction = (field(UINT64_C(52), 6, 26) | field(UINT64_C(1), 1, 25) |
                   field(UINT64_C(8), 5, 20) | field(UINT64_C(9), 5, 15) |
                   field(UINT64_C(1), 3, 12) | field(UINT64_C(8), 5, 7) |
                   field(UINT64_C(87), 7, 0));
    settle(); // vfwadd.wv v8,v8,v9
    CHECK(decoded_valid && legal == (64 == 64) && widening && wide_vs2);
    bit_slice(instruction, 20, 5) = 9;
    settle();
    CHECK(!legal);
    instruction = (field(UINT64_C(52), 6, 26) | field(UINT64_C(1), 1, 25) |
                   field(UINT64_C(8), 5, 20) | field(UINT64_C(31), 5, 15) |
                   field(UINT64_C(5), 3, 12) | field(UINT64_C(8), 5, 7) |
                   field(UINT64_C(87), 7, 0));
    settle(); // vfwadd.wf v8,v8,f31
    CHECK(decoded_valid && legal == (64 == 64) && widening && wide_vs2 &&
          operand == 3);
    instruction = (field(UINT64_C(60), 6, 26) | field(UINT64_C(1), 1, 25) |
                   field(UINT64_C(9), 5, 20) | field(UINT64_C(16), 5, 15) |
                   field(UINT64_C(1), 3, 12) | field(UINT64_C(8), 5, 7) |
                   field(UINT64_C(87), 7, 0));
    settle(); // vfwmacc.vv v8,v9,v16
    CHECK(decoded_valid && legal == (64 == 64) && widening);

    // Mul/div uses ordinary same-width groups; VX's rs1 is not a vector group.
    for (int op = 32; op < 40; op++) {
      for (int sew = 0; sew < 4; sew++) {
        for (int lm = -3; lm <= 3; lm++) {
          for (int vx = 0; vx < 2; vx++) {
            for (int rd = 0; rd < 16; rd++) {
              bool aligned = lm <= 0 || rd % (1 << lm) == 0;
              bool source_aligned = vx != 0 || lm <= 0;
              bool expected_legal = 64 == 64 && sew <= lm + 3 && rd != 0 &&
                                    aligned && source_aligned;
              test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
              instruction =
                  (field(((op)&low_mask(6)), 6, 26) |
                   field(UINT64_C(0), 1, 25) | field(UINT64_C(16), 5, 20) |
                   field(UINT64_C(3), 5, 15) |
                   field(vx != 0 ? UINT64_C(6) : UINT64_C(2), 3, 12) |
                   field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(87), 7, 0));
              settle();
              CHECK(decoded_valid && legal == expected_legal);
              checks++;
            }
          }
        }
      }
    }

    // Element moves ignore LMUL alignment. Reduction seed/destination are
    // single registers, while vs2 still obeys the source group's alignment.
    for (int sew = 0; sew < 4; sew++) {
      for (int lm = -3; lm <= 3; lm++) {
        test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
        instruction = (field(UINT64_C(16), 6, 26) | field(UINT64_C(1), 1, 25) |
                       field(UINT64_C(3), 5, 20) | field(UINT64_C(0), 5, 15) |
                       field(UINT64_C(2), 3, 12) | field(UINT64_C(5), 5, 7) |
                       field(UINT64_C(87), 7, 0));
        settle();
        CHECK(decoded_valid && legal == (sew <= lm + 3));
        instruction = (field(UINT64_C(16), 6, 26) | field(UINT64_C(1), 1, 25) |
                       field(UINT64_C(0), 5, 20) | field(UINT64_C(5), 5, 15) |
                       field(UINT64_C(6), 3, 12) | field(UINT64_C(3), 5, 7) |
                       field(UINT64_C(87), 7, 0));
        settle();
        CHECK(decoded_valid && legal == (sew <= lm + 3));
        // FP scalar-element moves also ignore LMUL, including odd registers.
        if (sew >= 1) {
          instruction = (field(UINT64_C(16), 6, 26) |
                         field(UINT64_C(1), 1, 25) | field(UINT64_C(1), 5, 20) |
                         field(UINT64_C(0), 5, 15) | field(UINT64_C(1), 3, 12) |
                         field(UINT64_C(12), 5, 7) | field(UINT64_C(87), 7, 0));
          settle(); // vfmv.f.s f12,v1
          CHECK(decoded_valid && full_half_legal == (sew <= lm + 3) &&
                legal == (sew >= 2 && sew <= lm + 3));
          instruction =
              (field(UINT64_C(16), 6, 26) | field(UINT64_C(1), 1, 25) |
               field(UINT64_C(0), 5, 20) | field(UINT64_C(12), 5, 15) |
               field(UINT64_C(5), 3, 12) | field(UINT64_C(1), 5, 7) |
               field(UINT64_C(87), 7, 0));
          settle(); // vfmv.s.f v1,f12
          CHECK(decoded_valid && full_half_legal == (sew <= lm + 3) &&
                legal == (sew >= 2 && sew <= lm + 3));
          instruction = (field(UINT64_C(0), 6, 26) | field(UINT64_C(1), 1, 25) |
                         field(UINT64_C(1), 5, 20) | field(UINT64_C(8), 5, 15) |
                         field(UINT64_C(1), 3, 12) | field(UINT64_C(16), 5, 7) |
                         field(UINT64_C(87), 7, 0));
          settle(); // vfadd.vv v16,v1,v8
          CHECK(decoded_valid &&
                full_half_legal == (sew <= lm + 3 && lm <= 0) &&
                legal == (sew >= 2 && sew <= lm + 3 && lm <= 0));
        }
        for (int op = 0; op < 8; op++) {
          for (int src = 8; src < 10; src++) {
            instruction =
                (field(((op)&low_mask(6)), 6, 26) | field(UINT64_C(0), 1, 25) |
                 field(((src)&low_mask(5)), 5, 20) | field(UINT64_C(3), 5, 15) |
                 field(UINT64_C(2), 3, 12) | field(UINT64_C(0), 5, 7) |
                 field(UINT64_C(87), 7, 0));
            settle();
            CHECK(decoded_valid &&
                  legal ==
                      (sew <= lm + 3 && (lm <= 0 || src % (1 << lm) == 0)));
            checks++;
          }
        }
      }
    }
    // Widening reductions retain a single-register wide seed/result and a
    // narrow LMUL-sized source. Different-EEW source aliases are reserved.
    for (int sew = 0; sew < 4; sew++) {
      for (int lm = -3; lm <= 3; lm++) {
        bool expected;
        test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
        expected = sew < 3 && sew <= lm + 3;
        instruction = (field(UINT64_C(48), 6, 26) | field(UINT64_C(1), 1, 25) |
                       field(UINT64_C(8), 5, 20) | field(UINT64_C(3), 5, 15) |
                       field(UINT64_C(0), 3, 12) | field(UINT64_C(7), 5, 7) |
                       field(UINT64_C(87), 7, 0));
        settle();
        CHECK(decoded_valid && legal == expected && widening && !right_signed &&
              !subtract);
        bit_slice(instruction, 26, 6) = UINT64_C(49);
        settle();
        CHECK(decoded_valid && legal == expected && widening && right_signed &&
              !subtract);
        bit_slice(instruction, 15, 5) = 8;
        settle();
        CHECK(!legal);
        checks++;
      }
    }
    test_vtype = 0;
    instruction = (field(UINT64_C(48), 6, 26) | field(UINT64_C(0), 1, 25) |
                   field(UINT64_C(8), 5, 20) | field(UINT64_C(3), 5, 15) |
                   field(UINT64_C(0), 3, 12) | field(UINT64_C(0), 5, 7) |
                   field(UINT64_C(87), 7, 0));
    settle();
    CHECK(decoded_valid && legal);
    instruction = UINT64_C(0x402020d7);
    settle();
    CHECK(!decoded_valid);
    instruction = UINT64_C(0x400160d7);
    settle();
    CHECK(!decoded_valid);
    test_vtype = 0;
    instruction = (field(UINT64_C(0), 6, 26) | field(UINT64_C(0), 1, 25) |
                   field(UINT64_C(8), 5, 20) | field(UINT64_C(0), 5, 15) |
                   field(UINT64_C(2), 3, 12) | field(UINT64_C(7), 5, 7) |
                   field(UINT64_C(87), 7, 0));
    settle();
    CHECK(decoded_valid && !legal);

    // Mask sources name single registers; only element destinations use LMUL.
    for (int lm = -3; lm <= 3; lm++) {
      test_vtype = word_t(lm) & 7;
      for (int op = 0; op < 7; op++) {
        for (int dest = 0; dest < 32; dest++) {
          for (int source = 0; source < 32; source++) {
            for (int masked = 0; masked < 2; masked++) {
              int selector, group;
              bool expected;
              group = lm > 0 ? 1 << lm : 1;
              selector = op == 0   ? 16
                         : op == 1 ? 17
                         : op == 2 ? 1
                         : op == 3 ? 3
                         : op == 4 ? 2
                         : op == 5 ? 16
                                   : 17;
              instruction =
                  (field(((op < 2 ? 16 : 20) & low_mask(6)), 6, 26) |
                   field(((masked == 0) & low_mask(1)), 1, 25) |
                   field(((op == 6 ? 0 : source) & low_mask(5)), 5, 20) |
                   field(((selector)&low_mask(5)), 5, 15) |
                   field(UINT64_C(2), 3, 12) |
                   field(((dest)&low_mask(5)), 5, 7) |
                   field(UINT64_C(87), 7, 0));
              settle();
              expected =
                  op < 2 ||
                  ((masked == 0 || dest != 0) &&
                   (op < 5 ? dest != source
                           : dest % group == 0 &&
                                 (op == 6 || source / group != dest / group)));
              CHECK(decoded_valid && legal == expected);
              checks++;
            }
          }
        }
      }
    }
    // Slide-up groups may not overlap; down may be in-place. Scalar/unsigned
    // immediate fields are never subject to vector source-group alignment.
    for (int sew = 0; sew < 4; sew++) {
      for (int lm = -3; lm <= 3; lm++) {
        for (int form = 0; form < 6; form++) {
          for (int dest = 0; dest < 32; dest++) {
            for (int source = 0; source < 32; source++) {
              for (int masked = 0; masked < 2; masked++) {
                int group, mode;
                bool up, expected;
                group = lm > 0 ? 1 << lm : 1;
                up = (form == 0 || form == 1 || form == 4);
                mode = form >= 4 ? 6 : (form == 1 || form == 3) ? 3 : 4;
                test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
                instruction = (field(((up ? 14 : 15) & low_mask(6)), 6, 26) |
                               field(((masked == 0) & low_mask(1)), 1, 25) |
                               field(((source)&low_mask(5)), 5, 20) |
                               field(UINT64_C(3), 5, 15) |
                               field(((mode)&low_mask(3)), 3, 12) |
                               field(((dest)&low_mask(5)), 5, 7) |
                               field(UINT64_C(87), 7, 0));
                settle();
                expected = sew <= lm + 3 && dest % group == 0 &&
                           source % group == 0 && (!up || dest != source) &&
                           (masked == 0 || (dest != 0 && source != 0));
                CHECK(decoded_valid && legal == expected);
                checks++;
              }
            }
          }
        }
      }
    }
    // Gather checks independent data/index groups, including fractional EMUL,
    // mixed-EEW source aliases, masked v0, and full-register interval overlap.
    for (int sew = 0; sew < 4; sew++)
      for (int lm = -3; lm <= 3; lm++) {
        for (int form = 0; form < 4; form++)
          for (int d = 0; d < 32; d++) {
            for (int s1 = 0; s1 < 32; s1++)
              for (int variant = 0; variant < 4; variant++) {
                for (int masked = 0; masked < 2; masked++) {
                  int s2, g, ig, ie;
                  bool expected, disjoint_indices, disjoint_sources;
                  s2 = variant == 0   ? 8
                       : variant == 1 ? s1
                       : variant == 2 ? d
                                      : 0;
                  g = lm > 0 ? 1 << lm : 1;
                  ie = lm + (form == 1 ? 1 - sew : 0);
                  ig = ie > 0 ? 1 << ie : 1;
                  disjoint_indices = d >= s1 + ig || s1 >= d + g;
                  disjoint_sources = s2 >= s1 + ig || s1 >= s2 + g;
                  expected = sew <= lm + 3 && d % g == 0 && s2 % g == 0 &&
                             d != s2 && (masked == 0 || (d != 0 && s2 != 0));
                  if (form < 2)
                    expected &= s1 % ig == 0 && disjoint_indices &&
                                (masked == 0 || s1 != 0);
                  if (form == 1)
                    expected &=
                        ie >= -3 && ie <= 3 && (sew == 1 || disjoint_sources);
                  test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
                  instruction =
                      (field(((form == 1 ? 14 : 12) & low_mask(6)), 6, 26) |
                       field(((masked == 0) & low_mask(1)), 1, 25) |
                       field(((s2)&low_mask(5)), 5, 20) |
                       field(((s1)&low_mask(5)), 5, 15) |
                       field(((form < 2    ? 0
                               : form == 2 ? 4
                                           : 3) &
                              low_mask(3)),
                             3, 12) |
                       field(((d)&low_mask(5)), 5, 7) |
                       field(UINT64_C(87), 7, 0));
                  settle();
                  CHECK(decoded_valid && legal == expected);
                  checks++;
                }
              }
          }
      }
    // Compress reads one ordinary data group and one single mask register.
    // Its destination must be disjoint from both, and the differently sized
    // source operands cannot alias each other.
    for (int sew = 0; sew < 4; sew++)
      for (int lm = -3; lm <= 3; lm++) {
        for (int d = 0; d < 32; d++)
          for (int s1 = 0; s1 < 32; s1++) {
            for (int variant = 0; variant < 4; variant++) {
              int s2, g;
              bool expected, mask_disjoint;
              s2 = variant == 0 ? 8 : variant == 1 ? s1 : variant == 2 ? d : 0;
              g = lm > 0 ? 1 << lm : 1;
              mask_disjoint =
                  (s1 < d || s1 >= d + g) && (s1 < s2 || s1 >= s2 + g);
              expected = sew <= lm + 3 && d % g == 0 && s2 % g == 0 &&
                         d != s2 && mask_disjoint;
              test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
              instruction =
                  (field(UINT64_C(23), 6, 26) | field(UINT64_C(1), 1, 25) |
                   field(((s2)&low_mask(5)), 5, 20) |
                   field(((s1)&low_mask(5)), 5, 15) |
                   field(UINT64_C(2), 3, 12) | field(((d)&low_mask(5)), 5, 7) |
                   field(UINT64_C(87), 7, 0));
              settle();
              CHECK(decoded_valid && legal == expected);
              checks++;
            }
          }
      }
    instruction = UINT64_C(0x5c21a0d7);
    settle();
    CHECK(!decoded_valid);
    // State-dependent operation legality belongs to the operation checker.
    // Reductions, compress, and non-index scans require vstart=0; VID.V does not.
    test_vtype = 0;
    test_vstart = 0;
    instruction = (field(UINT64_C(0), 6, 26) | field(UINT64_C(1), 1, 25) |
                   field(UINT64_C(8), 5, 20) | field(UINT64_C(3), 5, 15) |
                   field(UINT64_C(2), 3, 12) | field(UINT64_C(7), 5, 7) |
                   field(UINT64_C(87), 7, 0));
    settle();
    CHECK(decoded_valid && legal);
    test_vstart = 1;
    settle();
    CHECK(!legal);
    test_vstart = 0;
    instruction = (field(UINT64_C(23), 6, 26) | field(UINT64_C(1), 1, 25) |
                   field(UINT64_C(8), 5, 20) | field(UINT64_C(1), 5, 15) |
                   field(UINT64_C(2), 3, 12) | field(UINT64_C(16), 5, 7) |
                   field(UINT64_C(87), 7, 0));
    settle();
    CHECK(decoded_valid && legal);
    test_vstart = 1;
    settle();
    CHECK(!legal);
    test_vstart = 0;
    instruction = (field(UINT64_C(20), 6, 26) | field(UINT64_C(1), 1, 25) |
                   field(UINT64_C(8), 5, 20) | field(UINT64_C(16), 5, 15) |
                   field(UINT64_C(2), 3, 12) | field(UINT64_C(16), 5, 7) |
                   field(UINT64_C(87), 7, 0));
    settle();
    CHECK(decoded_valid && legal);
    test_vstart = 1;
    settle();
    CHECK(!legal);
    instruction = (field(UINT64_C(20), 6, 26) | field(UINT64_C(1), 1, 25) |
                   field(UINT64_C(0), 5, 20) | field(UINT64_C(17), 5, 15) |
                   field(UINT64_C(2), 3, 12) | field(UINT64_C(16), 5, 7) |
                   field(UINT64_C(87), 7, 0));
    settle();
    CHECK(decoded_valid && legal);
    // Whole-register moves use their encoded NREG rather than LMUL, but use
    // SEW to interpret vstart and therefore still require a legal vtype.
    test_vtype = 0;
    test_vstart = 0;
    for (int registers = 1; registers <= 8; registers *= 2) {
      for (int destination = 0; destination < 32; destination++) {
        for (int source = 0; source < 32; source++) {
          bool expected;
          expected = destination % registers == 0 &&
                     destination + registers <= 32 && source % registers == 0 &&
                     source + registers <= 32;
          instruction = whole_register_move(registers, destination, source);
          settle();
          CHECK(decoded_valid && legal == expected);
          checks++;
        }
      }
      test_vtype = UINT64_C(24);
      test_vstart = word_t(registers * 128 / 64 - 1);
      instruction = whole_register_move(registers, 0, 8);
      settle();
      CHECK(decoded_valid && legal);
      test_vstart = word_t(registers * 128 / 64);
      settle();
      CHECK(!legal);
      test_vtype = 0;
      test_vstart = 0;
    }
    instruction = whole_register_move(1, 1, 2);
    bit_slice(instruction, 25, 1) = 0;
    settle();
    CHECK(!decoded_valid);
    test_vtype = UINT64_MAX;
    instruction = whole_register_move(1, 1, 2);
    settle();
    CHECK(decoded_valid && !legal);
    // Whole-register legality is independent of vl/vtype, but NREG still
    // constrains base-register alignment and forbids wrapping past v31.
    test_vtype = UINT64_MAX;
    for (int registers = 1; registers <= 8; registers *= 2) {
      for (int destination = 0; destination < 32; destination++) {
        bool expected;
        expected = 64 == 64 && destination % registers == 0 &&
                   destination + registers <= 32;
        for (int width = 0; width < 4; width++) {
          instruction =
              whole_register_vmem(0, width, registers, destination, 8);
          settle();
          CHECK(decoded_valid && legal == expected);
          checks++;
        }
        instruction = whole_register_vmem(1, 0, registers, destination, 8);
        settle();
        CHECK(decoded_valid && legal == expected);
        checks++;
      }
    }
    // Mask transfers use one register and byte-granular EVL regardless of
    // SEW/LMUL, but still depend on a non-vill vtype and RV64 memory support.
    for (int sew = 0; sew < 4; sew++) {
      for (int lm = -3; lm <= 3; lm++) {
        test_vtype = (word_t(sew) << 3) | (word_t(lm) & 7);
        for (int destination = 0; destination < 32; destination++) {
          instruction = (field(UINT64_C(0), 3, 29) | field(UINT64_C(0), 1, 28) |
                         field(UINT64_C(0), 2, 26) | field(UINT64_C(1), 1, 25) |
                         field(UINT64_C(11), 5, 20) |
                         field(UINT64_C(8), 5, 15) | field(UINT64_C(0), 3, 12) |
                         field(((destination)&low_mask(5)), 5, 7) |
                         field(UINT64_C(7), 7, 0));
          settle();
          CHECK(decoded_valid && legal == (64 == 64 && sew <= lm + 3));
          bit_slice(instruction, 0, 7) = UINT64_C(39);
          settle();
          CHECK(decoded_valid && legal == (64 == 64 && sew <= lm + 3));
          checks += 2;
        }
      }
    }
    test_vtype = UINT64_MAX;
    instruction = UINT64_C(0x2b10087);
    settle();
    CHECK(decoded_valid && !legal);
    // Sstatus aliases VS; reads do not dirty it, writes to vector state do.
    falling();
    instruction = csr_word(UINT64_C(768), 2, 0);
    settle();
    saved_type = mstatus;
    write_csr(UINT64_C(768), word_t(UINT64_C(1024)), saved_type);
    read_csr(UINT64_C(10), 2);
    CHECK(bit_slice(mstatus, 9, 2) == 2 && !bit_slice(mstatus, 64 - 1, 1));
    send(csr_word(UINT64_C(256), 3), word_t(UINT64_C(1536)), 0, 0, 1,
         (64 == 64 ? (word_t(2) << 32) : 0) | UINT64_C(1024));
    send(vset(0, 0), 1, 0, 1, 0, 0);
    send(csr_word(UINT64_C(8), 2, 0), 0, 0, 1, 0, 0);
    // Vector state is accessible in U and S when VS is enabled; privileged
    // CSR rejection still traps normally and returns the test to M mode.
    for (int lower_mode = 0; lower_mode < 2; lower_mode++) {
      falling();
      settle();
      saved_type = mstatus;
      write_csr(UINT64_C(768), (word_t(lower_mode) << 11) | UINT64_C(512),
                saved_type);
      send(UINT64_C(0x30200073), 0, 0, 1, 0,
           0); // MRET redirects, not an exception
      send(vset(0, 0), 1, 0, 0, 1, 1);
      read_csr(UINT64_C(3104), 1);
      send(csr_word(UINT64_C(768), 2, 0), 0, 0, 1, 0, 0);
    }

    throw Finished{};
  }
}

int main() {
  return run_test([] {
    cycle_limit = 400002;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
