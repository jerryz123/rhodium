// Checks F/D/half/Zfa execution, paired ownership, FP hazards, LSU bridges, and traps.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "driver.hpp"
auto &resolution(unsigned i) { return i ? resolution_1_in : resolution_0_in; }
const auto &memory_stage(unsigned i) {
  return i ? memory_stage_1_out : memory_stage_0_out;
}
const auto &retired(unsigned i) { return i ? retired_1_out : retired_0_out; }
int cycles = 0, commits = 0, expected_traps = 0, traps = 0, fp_pairs = 0;
std::array<std::uint64_t, 32> gprs{};
std::uint64_t next_pc = 0x8000, load_data = 0x4008000000000000, stored_data = 0,
              inject_pc = 0;
unsigned stored_mask = 0, stored_width = 0, expected_locality = 0;
int stores = 0, response_due = 0, split_due = 0, split_count = 0;
bool slow_load = 0, pending_load = 0, inject_fault = 0, pending_split = 0;
void drive() {
  resolution(0) = {};
  resolution(1) = {};
  memory_in.prequest.pready = !pending_load;
  memory_in.pdrained = !pending_load;
  memory_in.presponse.pvalid = pending_load && cycles >= response_due;
  memory_in.presponse.pbits = load_data;
  for (int lane = 0; lane < 2; lane++)
    if (inject_fault && memory_stage(lane).pvalid &&
        memory_stage(lane).pbits.ppc == inject_pc) {
      resolution(lane).pvalid = 1;
      resolution(lane).pbits = {1, 2, 0, {}};
    }
  pipeline_in.pcommit_uready = 1;
  split_in.prequest.pready = !pending_split;
  split_in.presponse.pvalid = pending_split && cycles >= split_due;
  split_in.presponse.pbits = {};
  split_in.presponse.pbits.presponse.pdata = load_data;
}
void falling_update() {
  if (!reset)
    cycles++;
}
void observe() {
  if (reset)
    return;
  if (cycles > 20000)
    fail(1, "FP core timeout");
  defer(pipeline_in.presponse.pvalid, pipeline_out.prequest.pvalid);
  defer(pipeline_in.presponse.pbits.poutcome,
        slow_load                                  ? UINT64_C(0)
        : pipeline_out.prequest.pbits.paccess == 1 ? UINT64_C(1)
                                                   : UINT64_C(2));
  defer(pipeline_in.presponse.pbits.pdata, load_data);
  if (pipeline_out.prequest.pvalid &&
      pipeline_out.prequest.pbits.paccess == 2) {
    stored_data = pipeline_out.prequest.pbits.pdata;
    stored_mask = pipeline_out.prequest.pbits.pmask;
    stored_width = pipeline_out.prequest.pbits.pwidth;
  }
  if (pipeline_out.pcommit)
    stores++;
  if (split_out.prequest.pvalid && split_in.prequest.pready) {
    defer(pending_split, 1);
    split_due = cycles + 4;
    split_count++;
    if (split_out.prequest.pbits.paccess == 2)
      stored_data = split_out.prequest.pbits.pdata;
  }
  if (split_in.presponse.pvalid)
    defer(pending_split, 0);
  if (memory_out.prequest.pvalid && memory_in.prequest.pready) {
    CHECK(slow_load && memory_out.prequest.pbits.paccess == 1);
    CHECK(memory_out.prequest.pbits.plocality == expected_locality);
    defer(pending_load, 1);
    response_due = cycles + 12;
  }
  if (memory_in.presponse.pvalid && memory_out.presponse.pready)
    defer(pending_load, 0);
  if (retired_count == 2 &&
      ((retired(0).pbits.pfetched.pinstruction & 127) == UINT64_C(0x53) ||
       (retired(1).pbits.pfetched.pinstruction & 127) == UINT64_C(0x53)))
    fp_pairs++;
  for (int i = 0; i < 2; i++)
    if (retired(i).pvalid) {
      commits++;
      if (retired(i).pbits.pwrite && !retired(i).pbits.pdeferred)
        gprs[retired(i).pbits.prd] = retired(i).pbits.pdata;
    }
  if (completed_out.pvalid && completed_out.pbits.pwrite)
    gprs[completed_out.pbits.prd] = completed_out.pbits.pdata;
  if (redirect_out.pvalid && redirect_out.pbits.presolution.pdisposition == 1) {
    traps++;
    CHECK(traps <= expected_traps &&
          redirect_out.pbits.presolution.pcause == 2);
  }
}
std::uint32_t fp(int funct7, int rd, int rs1, int rs2, int rm = 0) {
  return (((funct7)&low_mask(32)) << 25) | (((rs2)&low_mask(32)) << 20) |
         (((rs1)&low_mask(32)) << 15) | (((rm)&low_mask(32)) << 12) |
         (((rd)&low_mask(32)) << 7) | UINT64_C(0x53);
}
void send(std::uint32_t a, std::uint32_t b = UINT64_C(19),
          std::uint8_t paired = 0) {
  falling();
  instructions_in.pvalid = 1;
  instructions_in.pbits = {};
  instructions_in.pbits.pcount = paired ? 2 : 1;
  instructions_in.pbits.pentries[0] = {.ppc = next_pc,
                                       .pinstruction = a,
                                       .praw_uinstruction = a,
                                       .psequential_upc = next_pc + 4};
  instructions_in.pbits.pentries[1] = {.ppc = next_pc + 4,
                                       .pinstruction = b,
                                       .praw_uinstruction = b,
                                       .psequential_upc = next_pc + 8};
  accept([&] { return instructions_out.pready; });
  next_pc += paired ? 8 : 4;
  falling();
  instructions_in.pvalid = 0;
}
void drain() {
  for (int repeat_index = 0; repeat_index < (100); ++repeat_index)
    falling();
}
void expect_gpr(int rd, std::uint64_t value) { CHECK(gprs[rd] == value); }
void expect_fpr(int rd, std::uint64_t value) {
  send(fp(UINT64_C(0x71), 30, rd, 0));
  drain();
  expect_gpr(30, value);
}
int main() {
  return run_test([] {
    reset = 1;
    for (int i = 0; i < 32; i++)
      gprs[i] = 0;
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
      falling();
    reset = 0;
    // FS disabled must trap before any numerical request or FPR update.
    expected_traps = 1;
    send(fp(UINT64_C(0x69), 1, 0, 0));
    drain();
    CHECK(traps == 1);
    send(UINT64_C(0x20b7));     // lui x1,2: FS=Initial
    send(UINT64_C(0x30009073)); // csrw mstatus,x1
    drain();
    send(UINT64_C(0x1000093), UINT64_C(0x900313), 1); // x1=16, x6=9
    send(fp(UINT64_C(0x69), 1, 1, 0), UINT64_C(0x700393),
         1); // fcvt.d.w f1,x1 + integer
    drain();
    send(UINT64_C(0xb00413), fp(UINT64_C(1), 2, 1, 1),
         1);                           // integer + fadd.d f2,f1,f1
    send(fp(UINT64_C(0x61), 3, 2, 0)); // fcvt.w.d x3,f2
    send(fp(UINT64_C(13), 3, 2, 1));   // fdiv.d f3,f2,f1 (WB)
    send(fp(UINT64_C(0x61), 4, 3, 0));
    drain();
    expect_gpr(3, 32);
    expect_gpr(4, 2);
    expect_gpr(7, 7);
    expect_gpr(8, 11);
    CHECK(fp_pairs >= 2);

    // Same-destination pairing preserves age when FP returns at WB or later.
    send(fp(UINT64_C(0x61), 21, 2, 0), UINT64_C(0x6300a93),
         1);                 // older FP x21=32, younger integer x21=99
    send(UINT64_C(0xa8b13)); // x22=x21 must forward the younger value
    drain();
    expect_gpr(21, 99);
    expect_gpr(22, 99);
    send(UINT64_C(0x700b93), fp(UINT64_C(0x61), 23, 2, 0),
         1);                 // older integer x23=7, younger FP x23=32
    send(UINT64_C(0xb8c13)); // x24=x23 waits for the younger FP value
    drain();
    expect_gpr(23, 32);
    expect_gpr(24, 32);

    // Fixed multiply and FP-to-GPR returns share a calendar. Exercise both age
    // orders: equal-latency pairing must split before EX, never retain a result.
    send(UINT64_C(0x26086b3), fp(UINT64_C(0x61), 14, 2, 0),
         1); // mul x13,x1,x6=144; fcvt.w.d x14,f2=32
    send(fp(UINT64_C(0x61), 15, 2, 0), UINT64_C(0x2608833),
         1);                  // fcvt.w.d x15,f2=32; mul x16,x1,x6=144
    send(UINT64_C(0x168693)); // x13=x13+1 waits for the direct multiply write
    drain();
    expect_gpr(13, 145);
    expect_gpr(14, 32);
    expect_gpr(15, 32);
    expect_gpr(16, 144);

    // f0 is writable, and FPR forwarding must cover the load port too.
    send(fp(UINT64_C(0x69), 0, 1, 0));
    send(fp(UINT64_C(0x61), 5, 0, 0));
    drain();
    expect_gpr(5, 16);
    send(UINT64_C(0x10003407)); // fld f8,256(x0): 3.0
    send(fp(UINT64_C(0x61), 9, 8, 0));
    drain();
    expect_gpr(9, 3);
    send(UINT64_C(0x10803427)); // fsd f8,264(x0)
    drain();
    CHECK(stores == 1 && stored_data == load_data);

    slow_load = 1;
    expected_locality = 4;
    send(UINT64_C(0x500033), UINT64_C(0x10003607),
         1); // NTL.ALL; fld f12,256(x0)
    send(fp(UINT64_C(1), 14, 1,
            1)); // independent arithmetic while the load is pending
    send(fp(UINT64_C(0x61), 15, 12, 0));
    drain();
    expect_gpr(15, 3);
    slow_load = 0;
    expected_locality = 0;

    send(UINT64_C(0x10303c07)); // misaligned fld f24,259(x0)
    drain();
    send(fp(UINT64_C(0x61), 24, 24, 0));
    drain();
    expect_gpr(24, 3);
    send((UINT64_C(8) << 25) | (UINT64_C(24) << 20) | (UINT64_C(3) << 12) |
         (UINT64_C(11) << 7) | UINT64_C(0x27)); // fsd f24,267(x0)
    drain();
    CHECK(split_count == 2 && stored_data == load_data);

    // Kill already-launched younger arithmetic with an older same-group fault.
    send(fp(UINT64_C(0x69), 20, 1, 0));
    drain();
    inject_fault = 1;
    inject_pc = next_pc;
    expected_traps = 2;
    send(UINT64_C(0x100893), fp(UINT64_C(1), 20, 1, 1), 1);
    drain();
    inject_fault = 0;
    CHECK(traps == 2);
    send(fp(UINT64_C(0x61), 21, 20, 0));
    drain();
    expect_gpr(21, 16);

    // Single precision boxing and mixed fixed-latency return ordering.
    send(fp(UINT64_C(0x68), 10, 1, 0)); // fcvt.s.w f10,x1
    send(fp(UINT64_C(0), 11, 10, 10));
    send(fp(UINT64_C(0x60), 12, 11, 0));
    drain();
    expect_gpr(12, 32);
    send(fp(UINT64_C(0x71), 13, 10, 0));
    drain();
    expect_gpr(13, UINT64_C(0xffffffff41800000));
    send(UINT64_C(0x302573)); // csrr x10,fcsr
    drain();
    expect_gpr(10, 0);
    send((UINT64_C(1) << 27) | (UINT64_C(1) << 25) | (UINT64_C(3) << 20) |
         (UINT64_C(1) << 15) | (UINT64_C(22) << 7) |
         UINT64_C(0x43)); // fmadd.d f22,f1,f3,f1
    send(fp(UINT64_C(0x61), 22, 22, 0));
    drain();
    expect_gpr(22, 48);
    send(fp(UINT64_C(0x79), 0, 0, 0)); // fmv.d.x f0,x0
    send(fp(UINT64_C(13), 16, 1, 0));  // 16/0: DZ, +infinity
    send(fp(UINT64_C(0x71), 17, 16, 0));
    send(UINT64_C(0x302973)); // csrr x18,fcsr drains prior arithmetic
    drain();
    expect_gpr(17, UINT64_C(0x7ff0000000000000));
    expect_gpr(18, 8);
    send(UINT64_C(0x101073)); // csrw fflags,x0
    drain();
    expected_traps = 3;
    send(fp(UINT64_C(1), 20, 1, 1, 5));
    drain();
    CHECK(traps == 3);
    // Zfa uses the same EX launch, WB authorization and variable-latency return paths.
    // FLI's rs1 field is an immediate index, not an integer or FP source register.
    send(fp(UINT64_C(0x78), 25, 16, 1), UINT64_C(0x1900c93),
         1); // fli.s f25,1.0 + x25=25
    send(UINT64_C(0x1a00d13), fp(UINT64_C(0x79), 26, 20, 1),
         1); // x26=26 + fli.d f26,2.0
    send(fp(UINT64_C(0x71), 27, 25, 0));
    send(fp(UINT64_C(0x71), 28, 26, 0));
    drain();
    expect_gpr(25, 25);
    expect_gpr(26, 26);
    expect_gpr(27, UINT64_C(0xffffffff3f800000));
    expect_gpr(28, UINT64_C(0x4000000000000000));

    // Exercise both precision catalogs, all static rounding modes, and dynamic frm.
    for (int precision = 0; precision < 2; precision++) {
      send(fp(UINT64_C(0x78) + precision, 1, 18, 1)); // 1.5
      send(fp(UINT64_C(0x78) + precision, 2, 20, 1)); // 2.0
      send(fp(UINT64_C(20) + precision, 3, 1, 2, 2)); // fminm -> 1.5
      send(fp(UINT64_C(0x71), 3, 3, 0));
      send(fp(UINT64_C(20) + precision, 4, 1, 2, 3)); // fmaxm -> 2.0
      send(fp(UINT64_C(0x71), 4, 4, 0));
      send(fp(UINT64_C(0x50) + precision, 5, 1, 2, 4)); // fleq -> true
      send(fp(UINT64_C(0x50) + precision, 6, 2, 1, 5)); // fltq -> false
      drain();
      expect_gpr(3, precision == 1 ? UINT64_C(0x3ff8000000000000)
                                   : UINT64_C(0xffffffff3fc00000));
      expect_gpr(4, precision == 1 ? UINT64_C(0x4000000000000000)
                                   : UINT64_C(0xffffffff40000000));
      expect_gpr(5, 1);
      expect_gpr(6, 0);
      for (int rm = 0; rm < 5; rm++) {
        send(fp(UINT64_C(0x20) + precision, 7, 1, 4, rm)); // fround without NX
        send(fp(UINT64_C(0x60) + precision, 7, 7,
                0));              // dependent GPR conversion
        send(UINT64_C(0x102473)); // csrr x8,fflags
        drain();
        expect_gpr(7, (rm == 1 || rm == 2) ? 1 : 2);
        expect_gpr(8, 0);
      }
      send(UINT64_C(0x215073));
      drain(); // csrwi frm,2 (RDN); wait for serialized redirect
      send(fp(UINT64_C(0x20) + precision, 7, 1, 5,
              7)); // froundnx dynamic -> 1, NX
      send(fp(UINT64_C(0x60) + precision, 7, 7, 0));
      send(UINT64_C(0x102473));
      drain();
      expect_gpr(7, 1);
      expect_gpr(8, 1);
      send(UINT64_C(0x301073));
      drain(); // clear fcsr

      // Quiet NaNs propagate through MINM/MAXM; quiet compares must not raise NV.
      send(fp(UINT64_C(0x78) + precision, 2, 31, 1)); // canonical NaN
      send(fp(UINT64_C(20) + precision, 3, 1, 2, 2));
      send(fp(UINT64_C(0x71), 3, 3, 0));
      send(fp(UINT64_C(20) + precision, 4, 2, 1, 3));
      send(fp(UINT64_C(0x71), 4, 4, 0));
      send(fp(UINT64_C(0x50) + precision, 5, 1, 2, 4));
      send(fp(UINT64_C(0x50) + precision, 6, 2, 1, 5));
      send(UINT64_C(0x102473));
      drain();
      expect_gpr(3, precision == 1 ? UINT64_C(0x7ff8000000000000)
                                   : UINT64_C(0xffffffff7fc00000));
      expect_gpr(4, precision == 1 ? UINT64_C(0x7ff8000000000000)
                                   : UINT64_C(0xffffffff7fc00000));
      expect_gpr(5, 0);
      expect_gpr(6, 0);
      expect_gpr(8, 0);

      load_data = precision == 1 ? UINT64_C(0x7ff0000000000001)
                                 : UINT64_C(0xffffffff7f800001);
      send(
          UINT64_C(0x10003107)); // fld f2: signaling NaN, correctly boxed for S
      send(fp(UINT64_C(0x50) + precision, 5, 1, 2, 4));
      send(fp(UINT64_C(0x50) + precision, 6, 2, 1, 5));
      send(UINT64_C(0x102473));
      drain();
      expect_gpr(5, 0);
      expect_gpr(6, 0);
      expect_gpr(8, 16);
      send(UINT64_C(0x101073));
      drain();
    }

    // An unboxed single operand is canonical NaN, not the low-word number.
    load_data = UINT64_C(0x3f800000);
    send(UINT64_C(0x10003107));
    send(fp(UINT64_C(0x78), 1, 20, 1)); // boxed 2.0
    send(fp(UINT64_C(20), 3, 1, 2, 2));
    send(fp(UINT64_C(0x71), 3, 3, 0));
    send(fp(UINT64_C(0x50), 5, 2, 1, 5));
    send(UINT64_C(0x102473));
    drain();
    expect_gpr(3, UINT64_C(0xffffffff7fc00000));
    expect_gpr(5, 0);
    expect_gpr(8, 0);

    // Zfa 1.0 specifies FCVT.W.D flags even though FCVTMOD wraps the result.
    // https://docs.riscv.org/reference/isa/v20260120/unpriv/zfa.html#_modular_convert_to_integer_instruction
    load_data = UINT64_C(
        0x41f8000000180000); // 6442450945.5 -> 0x80000001, NV (signed-word overflow)
    send(UINT64_C(0x10003507)); // fld f10,256(x0)
    send(fp(UINT64_C(0x61), 11, 10, 8, 1), UINT64_C(0xc00613), 1);
    send(UINT64_C(0x158693)); // dependent addi x13,x11,1
    send(UINT64_C(0x102773));
    drain();
    expect_gpr(11, UINT64_C(0xffffffff80000001));
    expect_gpr(12, 12);
    expect_gpr(13, UINT64_C(0xffffffff80000002));
    expect_gpr(14, 16);
    send(UINT64_C(0x101073));
    drain();

    // A killed FROUNDNX must change neither destination nor flags/FS.
    send(fp(UINT64_C(0x79), 20, 16, 1)); // f20=1.0
    send(fp(UINT64_C(0x79), 1, 18, 1));  // f1=1.5
    send(UINT64_C(0x40b7));
    send(UINT64_C(0x30009073));
    drain(); // FS=Clean
    inject_fault = 1;
    inject_pc = next_pc;
    expected_traps++;
    send(UINT64_C(0x100893), fp(UINT64_C(0x21), 20, 1, 5, 0), 1);
    drain();
    inject_fault = 0;
    inject_fault = 1;
    inject_pc = next_pc;
    expected_traps++;
    send(UINT64_C(0x100893), fp(UINT64_C(0x61), 11, 1, 8, 1), 1);
    drain(); // killed GPR/NX return
    inject_fault = 0;
    expect_gpr(11, UINT64_C(0xffffffff80000001));
    send(UINT64_C(0x300027f3));
    drain(); // csrr x15,mstatus before any new FP write
    send(UINT64_C(0x102873));
    drain();
    send(fp(UINT64_C(0x71), 21, 20, 0));
    drain();
    expect_gpr(21, UINT64_C(0x3ff0000000000000));
    expect_gpr(16, 0);
    CHECK((gprs[15] & UINT64_C(0x6000)) == UINT64_C(0x4000));
    expected_traps++;
    send(fp(UINT64_C(0x21), 20, 1, 4, 5));
    drain(); // reserved static rm
    CHECK(traps == expected_traps);
    send(UINT64_C(0x235073));
    drain(); // frm=6
    expected_traps++;
    send(fp(UINT64_C(0x21), 20, 1, 4, 7));
    drain(); // reserved dynamic rm
    CHECK(traps == expected_traps);
    send(UINT64_C(0x30001073));
    drain(); // FS=Off
    expected_traps++;
    send(fp(UINT64_C(0x79), 20, 16, 1));
    drain();
    CHECK(traps == expected_traps);
    // Both fixtures support Zfhmin; only the ordinary-latency fixture enables full Zfh.
    send(UINT64_C(0x20b7));
    send(UINT64_C(0x30009073));
    drain(); // enable FS
    send(UINT64_C(0x301073));
    drain();                            // reset rounding/flags
    send(UINT64_C(0xc537));             // x10=0xc000 (-2.0 half image)
    send(fp(UINT64_C(0x7a), 0, 10, 0)); // fmv.h.x f0,x10: box into 64-bit FPR
    send(fp(UINT64_C(0x72), 11, 0, 0));
    drain();
    expect_gpr(11, UINT64_C(0xffffffffffffc000));
    expect_fpr(0, UINT64_C(0xffffffffffffc000));
    send(fp(UINT64_C(0x20), 1, 0, 2)); // fcvt.s.h
    send(fp(UINT64_C(0x21), 2, 0, 2)); // fcvt.d.h
    expect_fpr(1, UINT64_C(0xffffffffc0000000));
    expect_fpr(2, UINT64_C(0xc000000000000000));
    send(fp(UINT64_C(0x22), 3, 1, 0)); // fcvt.h.s
    send(fp(UINT64_C(0x22), 4, 2, 1)); // fcvt.h.d
    expect_fpr(3, UINT64_C(0xffffffffffffc000));
    expect_fpr(4, UINT64_C(0xffffffffffffc000));

    // Raw halfwords use every aligned byte lane; masked stores preserve their lane.
    for (int lane = 0; lane < 8; lane += 2) {
      load_data = UINT64_C(0x3c00) << (8 * lane);
      send((((256 + lane) & low_mask(32)) << 20) | (UINT64_C(1) << 12) |
           (UINT64_C(5) << 7) | UINT64_C(7)); // flh f5
      expect_fpr(5, UINT64_C(0xffffffffffff3c00));
      send((UINT64_C(8) << 25) | (UINT64_C(5) << 20) | (UINT64_C(1) << 12) |
           (((8 + lane) & low_mask(32)) << 7) |
           UINT64_C(0x27)); // fsh f5,264+lane
      drain();
      CHECK(stored_width == 1 && stored_mask == (UINT64_C(3) << lane) &&
            (stored_data & (UINT64_C(0xffff) << (8 * lane))) ==
                (UINT64_C(0x3c00) << (8 * lane)));
    }
    slow_load = 1;
    load_data = UINT64_C(0xbc00000000000000);
    send(UINT64_C(0x10601307)); // flh f6,262(x0), delayed return
    send(
        fp(UINT64_C(0x20), 7, 6, 2)); // dependent conversion waits for the load
    expect_fpr(7, UINT64_C(0xffffffffbf800000));
    slow_load = 0;
    load_data = UINT64_C(0x3e00);
    send(UINT64_C(0x10701407)); // misaligned flh f8,263(x0)
    drain();
    expect_fpr(8, UINT64_C(0xffffffffffff3e00));
    send((UINT64_C(8) << 25) | (UINT64_C(8) << 20) | (UINT64_C(1) << 12) |
         (UINT64_C(15) << 7) | UINT64_C(0x27)); // fsh f8,271(x0)
    drain();
    CHECK(split_count == 4 && (stored_data & 65535) == UINT64_C(0x3e00));

    // Narrowing observes frm, reports NX, and boxes the result; widening rejects bad boxes.
    send(fp(UINT64_C(0x79), 1, 17, 1)); // fli.d 1.25
    send(fp(UINT64_C(0x22), 2, 1, 1));
    expect_fpr(2, UINT64_C(0xffffffffffff3d00));
    load_data = UINT64_C(
        0x3ff0020000000000); // halfway between half 1.0 and its successor
    send(UINT64_C(0x10003087));
    send(UINT64_C(0x21d073));
    drain(); // frm=RUP
    send(fp(UINT64_C(0x22), 2, 1, 1, 7));
    expect_fpr(2, UINT64_C(0xffffffffffff3c01));
    send(UINT64_C(0x102673));
    drain();
    expect_gpr(12, 1);
    send(UINT64_C(0x301073));
    drain();
    load_data = UINT64_C(0x3c00);
    send(UINT64_C(0x10003087));
    send(fp(UINT64_C(0x21), 2, 1, 2));
    expect_fpr(2, UINT64_C(0x7ff8000000000000));

#if ZFHMIN
    expected_traps++;
    send(fp(UINT64_C(2), 2, 0, 0));
    drain(); // arithmetic requires full Zfh
    CHECK(traps == expected_traps);
    expected_traps++;
    send(fp(UINT64_C(0x7a), 2, 16, 1));
    drain(); // Zfa+Zfhmin does not enable FLI.H
    CHECK(traps == expected_traps);
#else
    send(fp(UINT64_C(0x7a), 1, 20, 1)); // fli.h 2
    send(fp(UINT64_C(0x7a), 2, 22, 1)); // fli.h 3
    send(fp(UINT64_C(0x7a), 3, 16, 1)); // fli.h 1
    send(fp(UINT64_C(2), 4, 1, 2), UINT64_C(0x900593),
         1); // fadd.h 5 with integer peer
    expect_fpr(4, UINT64_C(0xffffffffffff4500));
    expect_gpr(11, 9);
    send(UINT64_C(0xa00593), fp(UINT64_C(6), 4, 1, 2),
         1); // fsub.h -1 in younger slot
    expect_fpr(4, UINT64_C(0xffffffffffffbc00));
    expect_gpr(11, 10);
    send(fp(UINT64_C(10), 4, 1, 2)); // fmul.h 6
    expect_fpr(4, UINT64_C(0xffffffffffff4600));
    send(fp(UINT64_C(14), 5, 4, 1)); // fdiv.h 3 (WB launch)
    expect_fpr(5, UINT64_C(0xffffffffffff4200));
    send(fp(UINT64_C(0x7a), 6, 23, 1)); // 4
    send(fp(UINT64_C(0x2e), 6, 6, 0));  // fsqrt.h 2 (WB launch)
    expect_fpr(6, UINT64_C(0xffffffffffff4000));
    for (int op = 0; op < 4; op++) {
      send((UINT64_C(3) << 27) | (UINT64_C(2) << 25) | (UINT64_C(2) << 20) |
           (UINT64_C(1) << 15) | (UINT64_C(7) << 7) |
           ((UINT64_C(0x43) + op * 4) & low_mask(32)));
      expect_fpr(7, op == 0   ? UINT64_C(0xffffffffffff4700)
                    : op == 1 ? UINT64_C(0xffffffffffff4500)
                    : op == 2 ? UINT64_C(0xffffffffffffc500)
                              : UINT64_C(0xffffffffffffc700));
    }
    send(fp(UINT64_C(18), 4, 1, 2, 0));
    expect_fpr(4, UINT64_C(0xffffffffffff4000));
    send(fp(UINT64_C(18), 4, 1, 2, 1));
    expect_fpr(4, UINT64_C(0xffffffffffffc000));
    send(fp(UINT64_C(18), 4, 1, 4, 2));
    expect_fpr(4, UINT64_C(0xffffffffffffc000));
    send(fp(UINT64_C(22), 4, 1, 2, 0));
    expect_fpr(4, UINT64_C(0xffffffffffff4000));
    send(fp(UINT64_C(22), 4, 1, 2, 1));
    expect_fpr(4, UINT64_C(0xffffffffffff4200));
    for (int relation = 0; relation < 3; relation++) {
      send(fp(UINT64_C(0x52), 12, 1, relation == 2 ? 1 : 2, relation));
      drain();
      expect_gpr(12, 1);
    }
    send(fp(UINT64_C(0x72), 12, 1, 0, 1));
    drain();
    expect_gpr(12, 64);
    send(UINT64_C(0x900513)); // integer 9
    for (int kind = 0; kind < 4; kind++) {
      send(fp(UINT64_C(0x6a), 8, 10, kind)); // W/WU/L/LU -> half
      send(fp(UINT64_C(0x62), 12, 8, kind)); // half -> W/WU/L/LU
      drain();
      expect_gpr(12, 9);
      expect_fpr(8, UINT64_C(0xffffffffffff4880));
    }

    // Half Zfa shares the same decode/schedule, including NX and quiet comparisons.
    send(fp(UINT64_C(0x7a), 1, 18, 1)); // 1.5
    send(fp(UINT64_C(0x7a), 2, 20, 1)); // 2
    send(fp(UINT64_C(22), 3, 1, 2, 2));
    expect_fpr(3, UINT64_C(0xffffffffffff3e00));
    send(fp(UINT64_C(22), 3, 1, 2, 3));
    expect_fpr(3, UINT64_C(0xffffffffffff4000));
    send(fp(UINT64_C(0x22), 3, 1, 4, 1));
    expect_fpr(3, UINT64_C(0xffffffffffff3c00)); // fround.h RTZ
    send(UINT64_C(0x102673));
    drain();
    expect_gpr(12, 0);
    send(fp(UINT64_C(0x22), 3, 1, 5, 0));
    expect_fpr(3, UINT64_C(0xffffffffffff4000)); // froundnx.h RNE
    send(UINT64_C(0x102673));
    drain();
    expect_gpr(12, 1);
    send(UINT64_C(0x101073));
    drain();
    send(fp(UINT64_C(0x7a), 2, 31, 1)); // qNaN
    send(fp(UINT64_C(0x52), 12, 1, 2, 4));
    drain();
    expect_gpr(12, 0);
    send(fp(UINT64_C(0x52), 12, 1, 2, 5));
    drain();
    expect_gpr(12, 0);
    send(UINT64_C(0x102673));
    drain();
    expect_gpr(12, 0);
    send(fp(UINT64_C(22), 3, 1, 2, 2));
    expect_fpr(3, UINT64_C(0xffffffffffff7e00));

    // A faulting older peer must kill an EX half result and prevent a WB divide launch.
    send(fp(UINT64_C(0x7a), 20, 16, 1)); // preserve 1
    send(UINT64_C(0x40b7));
    send(UINT64_C(0x30009073));
    drain(); // FS=Clean
    inject_fault = 1;
    inject_pc = next_pc;
    expected_traps++;
    send(UINT64_C(0x100893), fp(UINT64_C(0x22), 20, 1, 5), 1);
    drain(); // killed NX result
    inject_pc = next_pc;
    expected_traps++;
    send(UINT64_C(0x100893), fp(UINT64_C(14), 20, 1, 1), 1);
    drain(); // killed divide
    inject_fault = 0;
    send(UINT64_C(0x300026f3));
    drain();
    CHECK((gprs[13] & UINT64_C(0x6000)) == UINT64_C(0x4000));
    send(UINT64_C(0x102673));
    drain();
    expect_gpr(12, 0);
    expect_fpr(20, UINT64_C(0xffffffffffff3c00));
#endif
    expected_traps++;
    send(fp(UINT64_C(0x22), 4, 1, 1, 5));
    drain(); // reserved conversion rm
    CHECK(traps == expected_traps);
    send(UINT64_C(0x30001073));
    drain(); // FS off rejects even half loads
    expected_traps++;
    send(UINT64_C(0x10001087));
    drain();
    CHECK(traps == expected_traps);
  });
}