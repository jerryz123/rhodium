// Preserves the rv5stage-vector-fp cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using data_resp_bits_t =
    std::remove_cvref_t<decltype(data_access_in.presponse.pbits)>;
// Executes vector FP and scalar FP together through WB, checking ordered memory-visible results and flags.

bool response_valid = 0;

bool reject_request = 1;

std::uint32_t response_word;

std::uint32_t program_words[1024];

std::uint64_t memory_words[512];

std::uint64_t expected_data[512], expected_address[512];

int expected_width[512];

int pc = 0, expected_count = 0, stores = 0, cycles = 0, load_delay = 0;

data_resp_bits_t pending_load;

void emit(std::uint32_t word) { program_words[pc++] = word; }
void li(int rd, int value) {
  emit((((((value + 2048) >> 12) << 12) | (rd << 7) | UINT64_C(55)) &
        low_mask(32)));
  emit(((((value & 4095) << 20) | (rd << 15) | (rd << 7) | UINT64_C(19)) &
        low_mask(32)));
}
void vset(int sew, int vl, int lmul = 0) {
  emit(((UINT64_C(0xc0007057) | (sew << 23) | (lmul << 20) | (vl << 15)) &
        low_mask(32)));
}
void vec(int funct6, int rd, int vs2, int vs1, std::uint8_t masked = 0,
         int funct3 = 1) {
  emit((((funct6 << 26) | (int(!masked) << 25) | (vs2 << 20) | (vs1 << 15) |
         (funct3 << 12) | (rd << 7) | UINT64_C(87)) &
        low_mask(32)));
}
void vload(int rd, int address, int width) {
  li(10, address);
  emit(((UINT64_C(0x2050007) |
         ((width == 0   ? 0
           : width == 1 ? 5
           : width == 2 ? 6
                        : 7)
          << 12) |
         (rd << 7)) &
        low_mask(32)));
}
void expect_store(int address, std::uint64_t value, int width) {
  expected_address[expected_count] = ((address)&low_mask(64));
  expected_data[expected_count] = value;
  expected_width[expected_count++] = width;
}
void vstore(int rd, int address, int width) {
  li(10, address);
  emit(((UINT64_C(0x2050027) |
         ((width == 0   ? 0
           : width == 1 ? 5
           : width == 2 ? 6
                        : 7)
          << 12) |
         (rd << 7)) &
        low_mask(32)));
}
void signature(int csr, int address, std::uint64_t value) {
  emit((((csr << 20) | UINT64_C(8691)) &
        low_mask(32))); // csrr x3,csr; all state observers must drain FP
  li(10, address);
  emit(UINT64_C(0x353023)); // sd x3,0(x10)
  expect_store(address, value, 3);
}
void widening_conversion(int selector, int rd, int vs2, int address,
                         std::uint64_t first, std::uint64_t second) {
  vset(2, 2);
  vec(UINT64_C(18), rd, vs2, selector);
  vset(3, 2, 1);
  vstore(rd, address, 3);
  expect_store(address, first, 3);
  expect_store(address + 8, second, 3);
}
void narrowing_conversion(int selector, int rd, int vs2, int address,
                          std::uint32_t first, std::uint32_t second) {
  vset(2, 2);
  vec(UINT64_C(18), rd, vs2, selector);
  vstore(rd, address, 2);
  expect_store(address, ((first)&low_mask(64)), 2);
  expect_store(address + 4, ((second)&low_mask(64)), 2);
}

void drive() {
  {
    instruction_access_in = {};
    instruction_access_in.prequest.pready =
        instruction_access_out.pflush || !response_valid;
    instruction_access_in.presponse.pvalid = response_valid;
    instruction_access_in.presponse.pbits.pword = response_word;
    data_access_in = {};
    // Reject once, then hold readiness through replay instead of phase-locking
    // a periodic ready waveform against the core's fixed replay latency.
    data_access_in.prequest.pready = load_delay == 0 && !reject_request;
    data_access_in.presponse.pvalid = load_delay == 1;
    data_access_in.presponse.pbits = pending_load;
    data_access_in.pdrained = load_delay == 0;
  }
}

void observe() {
  {
    if (!reset) {
      defer(cycles, cycles + 1);
      if (data_access_out.prequest.pvalid)
        defer(reject_request, data_access_in.prequest.pready);
      if (load_delay > 1 ||
          (load_delay == 1 && data_access_out.presponse.pready))
        defer(load_delay, load_delay - 1);
      if (instruction_access_out.pflush ||
          (response_valid && instruction_access_out.presponse.pready))
        defer(response_valid, 0);
      if (instruction_access_out.prequest.pvalid &&
          instruction_access_in.prequest.pready) {
        defer(response_valid, 1);
        CHECK(instruction_access_out.prequest.pbits.paddress < UINT64_C(4096));
        defer(response_word,
              program_words[bit_slice(
                  instruction_access_out.prequest.pbits.paddress, 2, 10)]);
      }
      if (data_access_out.prequest.pvalid && data_access_in.prequest.pready) {
        // The shared LSU returns a tagged completion for stores as well as loads.
        defer(pending_load.paccess_ufault, 0);
        defer(pending_load.pcontext.pwriteback,
              data_access_out.prequest.pbits.pcontext.pwriteback);
        defer(pending_load.pdata, 0);
        defer(load_delay, 3 + cycles % 4);
        if (data_access_out.prequest.pbits.paccess == 1) {
          CHECK(data_access_out.prequest.pbits.paddress >= UINT64_C(4096) &&
                data_access_out.prequest.pbits.paddress < UINT64_C(8192));
          defer(pending_load.pdata,
                memory_words[(((data_access_out.prequest.pbits.paddress -
                                UINT64_C(4096)) >>
                               3) &
                              low_mask(9))] >>
                    (8 * (data_access_out.prequest.pbits.paddress & 7)));
        } else {
          CHECK(stores < expected_count &&
                data_access_out.prequest.pbits.paccess == 2);
          CHECK(data_access_out.prequest.pbits.paddress ==
                    expected_address[stores] &&
                int(data_access_out.prequest.pbits.pwidth) ==
                    expected_width[stores] &&
                (data_access_out.prequest.pbits.pdata &
                 (expected_width[stores] == 0   ? UINT64_C(255)
                  : expected_width[stores] == 1 ? UINT64_C(65535)
                  : expected_width[stores] == 2
                      ? UINT64_C(0xffffffff)
                      : UINT64_C(0xffffffffffffffff))) ==
                    expected_data[stores]);
          defer(stores, stores + 1);
          if (stores + 1 == expected_count) {

            throw Finished{};
          }
        }
      }
      if (cycles > 15000)
        fail(1, "vector FP timeout stores=%0d fetch=%h", stores,
             instruction_access_out.prequest.pbits.paddress);
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  interrupts = {};
  hart_id = 0;
  time_counter = 0;
  {
    for (int i = 0; i < 1024; i++)
      program_words[i] = UINT64_C(111);
    for (int i = 0; i < 512; i++)
      memory_words[i] = 0;
    li(1, UINT64_C(3840));
    emit(UINT64_C(
        810586227)); // trap vector: timeout reports a precise unexpected fault
    li(1, UINT64_C(8704));
    emit(UINT64_C(0x30009073)); // FS and VS Initial
    vset(2, 4);
    memory_words[0] = UINT64_C(0x400000003f800000); // 1,2
    memory_words[1] = UINT64_C(0x4080000040400000); // 3,4
    memory_words[2] = UINT64_C(0xbf8000003f000000); // .5,-1
    memory_words[3] = UINT64_C(0xc000000040000000); // 2,-2
    vload(8, UINT64_C(4096), 2);
    vload(9, UINT64_C(4112), 2);
    vec(0, 10, 8, 9);
    // Independent scalar FP can compete while the vector completion tail is live.
    li(5, UINT64_C(0x3f800000));
    emit(UINT64_C(0xf00280d3)); // fmv.w.x f1,x5
    emit(UINT64_C(0x10f153));   // fadd.s f2,f1,f1,dyn
    emit(UINT64_C(0xe00101d3)); // fmv.x.w x3,f2
    li(10, UINT64_C(8192));
    emit(UINT64_C(0x353023));
    expect_store(UINT64_C(8192), UINT64_C(0x40000000), 3);
    vec(2, 11, 8, 9);
    vec(UINT64_C(36), 12, 8, 9);
    vstore(10, UINT64_C(8208), 2);
    expect_store(UINT64_C(8208), UINT64_C(0x3fc00000), 2);
    expect_store(UINT64_C(8212), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(8216), UINT64_C(0x40a00000), 2);
    expect_store(UINT64_C(8220), UINT64_C(0x40000000), 2);
    vstore(11, UINT64_C(8224), 2);
    expect_store(UINT64_C(8224), UINT64_C(0x3f000000), 2);
    expect_store(UINT64_C(8228), UINT64_C(0x40400000), 2);
    expect_store(UINT64_C(8232), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(8236), UINT64_C(0x40c00000), 2);
    vstore(12, UINT64_C(8240), 2);
    expect_store(UINT64_C(8240), UINT64_C(0x3f000000), 2);
    expect_store(UINT64_C(8244), UINT64_C(0xc0000000), 2);
    expect_store(UINT64_C(8248), UINT64_C(0x40c00000), 2);
    expect_store(UINT64_C(8252), UINT64_C(0xc1000000), 2);
    // The same shared service handles variable-latency, sign/minmax, fused,
    // and mask-producing vector-vector operations in ordered completion slots.
    vec(UINT64_C(32), 13, 8, 9);
    vstore(13, UINT64_C(8960), 2);
    expect_store(UINT64_C(8960), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(8964), UINT64_C(0xc0000000), 2);
    expect_store(UINT64_C(8968), UINT64_C(0x3fc00000), 2);
    expect_store(UINT64_C(8972), UINT64_C(0xc0000000), 2);
    memory_words[18] = UINT64_C(0x408000003f800000);
    memory_words[19] = UINT64_C(0x4180000041100000);
    vload(14, UINT64_C(4240), 2);
    vec(UINT64_C(19), 15, 14, 0);
    vstore(15, UINT64_C(8976), 2);
    expect_store(UINT64_C(8976), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(8980), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(8984), UINT64_C(0x40400000), 2);
    expect_store(UINT64_C(8988), UINT64_C(0x40800000), 2);
    vec(UINT64_C(9), 16, 8, 9);
    vec(UINT64_C(4), 17, 8, 9);
    vec(UINT64_C(6), 18, 8, 9);
    vstore(16, UINT64_C(8992), 2);
    expect_store(UINT64_C(8992), UINT64_C(0xbf800000), 2);
    expect_store(UINT64_C(8996), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(9000), UINT64_C(0xc0400000), 2);
    expect_store(UINT64_C(9004), UINT64_C(0x40800000), 2);
    vstore(17, UINT64_C(9008), 2);
    expect_store(UINT64_C(9008), UINT64_C(0x3f000000), 2);
    expect_store(UINT64_C(9012), UINT64_C(0xbf800000), 2);
    expect_store(UINT64_C(9016), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(9020), UINT64_C(0xc0000000), 2);
    vstore(18, UINT64_C(9024), 2);
    expect_store(UINT64_C(9024), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(9028), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(9032), UINT64_C(0x40400000), 2);
    expect_store(UINT64_C(9036), UINT64_C(0x40800000), 2);
    vec(UINT64_C(23), 19, 0, 10, 0, 0);
    vec(UINT64_C(44), 19, 8, 9);
    vstore(19, UINT64_C(9040), 2);
    expect_store(UINT64_C(9040), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(9044), UINT64_C(0xbf800000), 2);
    expect_store(UINT64_C(9048), UINT64_C(0x41300000), 2);
    expect_store(UINT64_C(9052), UINT64_C(0xc0c00000), 2);
    vec(UINT64_C(23), 20, 0, 11, 0, 0);
    vec(UINT64_C(40), 20, 8, 9);
    vstore(20, UINT64_C(9056), 2);
    expect_store(UINT64_C(9056), UINT64_C(0x3fa00000), 2);
    expect_store(UINT64_C(9060), UINT64_C(0xbf800000), 2);
    expect_store(UINT64_C(9064), UINT64_C(0x40a00000), 2);
    expect_store(UINT64_C(9068), UINT64_C(0xc1000000), 2);
    memory_words[20] = UINT64_C(0xbf8000003f800000);
    memory_words[21] = UINT64_C(0x4080000000000000);
    vload(21, UINT64_C(4256), 2);
    vec(UINT64_C(24), 0, 8, 21); // vmfeq.vv selects lanes 0 and 3
    vec(UINT64_C(23), 22, 0, 8, 0, 0);
    vec(0, 22, 8, 9, 1);
    vstore(22, UINT64_C(9072), 2);
    expect_store(UINT64_C(9072), UINT64_C(0x3fc00000), 2);
    expect_store(UINT64_C(9076), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(9080), UINT64_C(0x40400000), 2);
    expect_store(UINT64_C(9084), UINT64_C(0x40000000), 2);
    // Vector-scalar FP snapshots the forwarded architectural FPR at WB, then
    // broadcasts it without consuming a general VRF read port.
    li(5, UINT64_C(0x3f800000));
    emit(UINT64_C(0xf0028453)); // fmv.w.x f8,x5
    vec(0, 23, 8, 8, 0, 5);
    li(5, UINT64_C(0x40000000));
    emit(UINT64_C(
        4026696787)); // younger overwrite must not change the admitted macro
    vec(UINT64_C(39), 24, 8, 1, 0, 5);
    emit(UINT64_C(
        1110739)); // fadd.s f5,f1,f1,dyn; the adjacent .vf must wait and forward
    vec(0, 25, 8, 5, 0, 5);
    vec(UINT64_C(23), 26, 0, 8, 0, 0);
    vec(UINT64_C(44), 26, 8, 1, 0, 5);
    memory_words[23] = UINT64_C(0x3f800000);
    li(10, UINT64_C(4280));
    emit(UINT64_C(0x53307)); // fld f6,0(x10): invalid FP32 NaN box
    vec(0, 27, 8, 6, 0, 5);
    vec(UINT64_C(29), 0, 8, 1, 0, 5); // vmfgt.vf: lanes 1..3
    vec(UINT64_C(23), 28, 0, 8, 0, 0);
    emit(UINT64_C(0x815073)); // csrwi vstart,2
    vec(0, 28, 8, 1, 1, 5);
    vstore(23, UINT64_C(9088), 2);
    expect_store(UINT64_C(9088), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(9092), UINT64_C(0x40400000), 2);
    expect_store(UINT64_C(9096), UINT64_C(0x40800000), 2);
    expect_store(UINT64_C(9100), UINT64_C(0x40a00000), 2);
    vstore(24, UINT64_C(9104), 2);
    expect_store(UINT64_C(9104), UINT64_C(0), 2);
    expect_store(UINT64_C(9108), UINT64_C(0xbf800000), 2);
    expect_store(UINT64_C(9112), UINT64_C(0xc0000000), 2);
    expect_store(UINT64_C(9116), UINT64_C(0xc0400000), 2);
    vstore(25, UINT64_C(9120), 2);
    expect_store(UINT64_C(9120), UINT64_C(0x40400000), 2);
    expect_store(UINT64_C(9124), UINT64_C(0x40800000), 2);
    expect_store(UINT64_C(9128), UINT64_C(0x40a00000), 2);
    expect_store(UINT64_C(9132), UINT64_C(0x40c00000), 2);
    vstore(26, UINT64_C(9136), 2);
    expect_store(UINT64_C(9136), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(9140), UINT64_C(0x40800000), 2);
    expect_store(UINT64_C(9144), UINT64_C(0x40c00000), 2);
    expect_store(UINT64_C(9148), UINT64_C(0x41000000), 2);
    vstore(27, UINT64_C(9152), 2);
    for (int i = 0; i < 4; i++)
      expect_store(UINT64_C(9152) + i * 4, UINT64_C(0x7fc00000), 2);
    vstore(28, UINT64_C(9168), 2);
    expect_store(UINT64_C(9168), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(9172), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(9176), UINT64_C(0x40800000), 2);
    expect_store(UINT64_C(9180), UINT64_C(0x40a00000), 2);
    // Same-width conversions choose integer or FP completion data explicitly;
    // fixed-RTZ ignores frm while ordinary conversion uses dynamic RNE.
    emit(UINT64_C(0x105073)); // clear fflags
    vec(UINT64_C(18), 29, 10, 1);
    vec(UINT64_C(18), 30, 10, 7);
    vstore(29, UINT64_C(9216), 2);
    expect_store(UINT64_C(9216), 2, 2);
    expect_store(UINT64_C(9220), 1, 2);
    expect_store(UINT64_C(9224), 5, 2);
    expect_store(UINT64_C(9228), 2, 2);
    vstore(30, UINT64_C(9232), 2);
    expect_store(UINT64_C(9232), 1, 2);
    expect_store(UINT64_C(9236), 1, 2);
    expect_store(UINT64_C(9240), 5, 2);
    expect_store(UINT64_C(9244), 2, 2);
    memory_words[25] = UINT64_C(0xfffffffe00000001);
    memory_words[26] = UINT64_C(0xfffffffc00000003);
    vload(31, UINT64_C(4296), 2);
    vec(UINT64_C(18), 29, 31, 3);
    vstore(29, UINT64_C(9248), 2);
    expect_store(UINT64_C(9248), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(9252), UINT64_C(0xc0000000), 2);
    expect_store(UINT64_C(9256), UINT64_C(0x40400000), 2);
    expect_store(UINT64_C(9260), UINT64_C(0xc0800000), 2);
    vec(UINT64_C(18), 30, 31, 2);
    vstore(30, UINT64_C(9328), 2);
    expect_store(UINT64_C(9328), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(9332), UINT64_C(0x4f800000), 2);
    expect_store(UINT64_C(9336), UINT64_C(0x40400000), 2);
    expect_store(UINT64_C(9340), UINT64_C(0x4f800000), 2);
    vec(UINT64_C(18), 31, 9, 0);
    vstore(31, UINT64_C(9344), 2);
    expect_store(UINT64_C(9344), 0, 2);
    expect_store(UINT64_C(9348), 0, 2);
    expect_store(UINT64_C(9352), 2, 2);
    expect_store(UINT64_C(9356), 0, 2);
    signature(UINT64_C(1), UINT64_C(9360), 17);
    emit(UINT64_C(0x105073));
    // Widening conversions read E32/LMUL1 and write E64/LMUL2. Every result
    // domain and fixed/dynamic rounding form is observed through vector stores.
    memory_words[29] = UINT64_C(0xc03000003fc00000); // 1.5,-2.75
    memory_words[30] = UINT64_C(0xfffffffc00000003); // 3,-4
    memory_words[31] = UINT64_C(0x5ffffffff);        // 2^32-1,5
    vset(2, 2);
    vload(8, UINT64_C(4328), 2);
    vload(9, UINT64_C(4336), 2);
    vload(10, UINT64_C(4344), 2);
    widening_conversion(8, 16, 8, UINT64_C(9472), 2, 0);
    widening_conversion(9, 18, 8, UINT64_C(9488), 2, -3);
    widening_conversion(10, 20, 10, UINT64_C(9504),
                        UINT64_C(0x41efffffffe00000),
                        UINT64_C(0x4014000000000000));
    widening_conversion(11, 22, 9, UINT64_C(9520), UINT64_C(0x4008000000000000),
                        UINT64_C(0xc010000000000000));
    widening_conversion(12, 24, 8, UINT64_C(9536), UINT64_C(0x3ff8000000000000),
                        UINT64_C(0xc006000000000000));
    widening_conversion(14, 26, 8, UINT64_C(9552), 1, 0);
    widening_conversion(15, 28, 8, UINT64_C(9568), 1, -2);
    // Widening arithmetic exactly promotes each E32 operand at the shared FP
    // boundary. The .w forms retain a wide vs2, and fused forms retain wide vd.
    memory_words[45] = UINT64_C(0xc00000003fc00000); // 1.5,-2
    memory_words[46] = UINT64_C(0x4080000040000000); // 2,4
    memory_words[47] = UINT64_C(0x4024000000000000);
    memory_words[48] = UINT64_C(0xc034000000000000); // 10,-20
    memory_words[49] = UINT64_C(0x3ff0000000000000);
    memory_words[50] = UINT64_C(0x4000000000000000); // 1,2
    vset(2, 2);
    vload(8, UINT64_C(4456), 2);
    vload(9, UINT64_C(4464), 2);
    li(5, UINT64_C(0x3f000000));
    emit(UINT64_C(0xf0028453)); // fmv.w.x f8,x5: 0.5
    vec(UINT64_C(48), 18, 8, 9);
    vset(3, 2, 1);
    vstore(18, UINT64_C(9776), 3);
    expect_store(UINT64_C(9776), UINT64_C(0x400c000000000000), 3);
    expect_store(UINT64_C(9784), UINT64_C(0x4000000000000000), 3);
    vset(2, 2);
    vec(UINT64_C(48), 20, 8, 8, 0, 5);
    vset(3, 2, 1);
    vstore(20, UINT64_C(9792), 3);
    expect_store(UINT64_C(9792), UINT64_C(0x4000000000000000), 3);
    expect_store(UINT64_C(9800), UINT64_C(0xbff8000000000000), 3);
    vload(16, UINT64_C(4472), 3);
    vset(2, 2);
    vec(UINT64_C(52), 16, 16, 9);
    vset(3, 2, 1);
    vstore(16, UINT64_C(9808), 3);
    expect_store(UINT64_C(9808), UINT64_C(0x4028000000000000), 3);
    expect_store(UINT64_C(9816), UINT64_C(0xc030000000000000), 3);
    vload(16, UINT64_C(4472), 3);
    vset(2, 2);
    vec(UINT64_C(54), 16, 16, 8, 0, 5);
    vset(3, 2, 1);
    vstore(16, UINT64_C(9824), 3);
    expect_store(UINT64_C(9824), UINT64_C(0x4023000000000000), 3);
    expect_store(UINT64_C(9832), UINT64_C(0xc034800000000000), 3);
    vset(2, 2);
    vec(UINT64_C(56), 22, 8, 9);
    vec(UINT64_C(56), 24, 8, 8, 0, 5);
    vset(3, 2, 1);
    vstore(22, UINT64_C(9840), 3);
    vstore(24, UINT64_C(9856), 3);
    expect_store(UINT64_C(9840), UINT64_C(0x4008000000000000), 3);
    expect_store(UINT64_C(9848), UINT64_C(0xc020000000000000), 3);
    expect_store(UINT64_C(9856), UINT64_C(0x3fe8000000000000), 3);
    expect_store(UINT64_C(9864), UINT64_C(0xbff0000000000000), 3);
    vload(30, UINT64_C(4488), 3);
    vset(2, 2);
    vec(UINT64_C(60), 30, 8, 9);
    vset(3, 2, 1);
    vstore(30, UINT64_C(9872), 3);
    expect_store(UINT64_C(9872), UINT64_C(0x4010000000000000), 3);
    expect_store(UINT64_C(9880), UINT64_C(0xc018000000000000), 3);
    vload(30, UINT64_C(4488), 3);
    vset(2, 2);
    vec(UINT64_C(61), 30, 8, 8, 0, 5);
    vset(3, 2, 1);
    vstore(30, UINT64_C(9888), 3);
    expect_store(UINT64_C(9888), UINT64_C(0xbffc000000000000), 3);
    expect_store(UINT64_C(9896), UINT64_C(0xbff0000000000000), 3);
    vload(30, UINT64_C(4488), 3);
    vset(2, 2);
    vec(UINT64_C(62), 30, 8, 9);
    vset(3, 2, 1);
    vstore(30, UINT64_C(9904), 3);
    expect_store(UINT64_C(9904), UINT64_C(0x4000000000000000), 3);
    expect_store(UINT64_C(9912), UINT64_C(0xc024000000000000), 3);
    vload(30, UINT64_C(4488), 3);
    vset(2, 2);
    vec(UINT64_C(63), 30, 8, 8, 0, 5);
    vset(3, 2, 1);
    vstore(30, UINT64_C(9920), 3);
    expect_store(UINT64_C(9920), UINT64_C(0x3fd0000000000000), 3);
    expect_store(UINT64_C(9928), UINT64_C(0x4008000000000000), 3);
    // A signaling narrow input raises NV during exact widening even though the
    // wide arithmetic lane subsequently receives a quiet NaN.
    emit(UINT64_C(0x105073));
    memory_words[51] = UINT64_C(0x3f8000007f800001);
    vset(2, 2);
    vload(8, UINT64_C(4504), 2);
    vec(UINT64_C(48), 18, 8, 9);
    vset(3, 2, 1);
    vstore(18, UINT64_C(9936), 3);
    expect_store(UINT64_C(9936), UINT64_C(0x7ff8000000000000), 3);
    expect_store(UINT64_C(9944), UINT64_C(0x4014000000000000), 3);
    signature(UINT64_C(1), UINT64_C(9952), 16);
    emit(UINT64_C(0x105073));
    // Floating-point reductions serialize active elements through the shared
    // service, retain only their accumulator, and write vector element zero.
    memory_words[52] = UINT64_C(0x400000003f800000);
    memory_words[53] = UINT64_C(0x4080000040400000); // 1,2,3,4
    memory_words[54] = UINT64_C(0x41200000);         // FP32 seed 10
    memory_words[55] = UINT64_C(0x4024000000000000); // FP64 seed 10
    vset(2, 4);
    vload(8, UINT64_C(4512), 2);
    vload(9, UINT64_C(4528), 2);
    vec(UINT64_C(1), 10, 8, 9);
    vec(UINT64_C(3), 11, 8, 9);
    vec(UINT64_C(5), 12, 8, 9);
    vec(UINT64_C(7), 13, 8, 9);
    vset(2, 1);
    vstore(10, UINT64_C(9968), 2);
    vstore(11, UINT64_C(9976), 2);
    vstore(12, UINT64_C(9984), 2);
    vstore(13, UINT64_C(9992), 2);
    expect_store(UINT64_C(9968), UINT64_C(0x41a00000), 2);
    expect_store(UINT64_C(9976), UINT64_C(0x41a00000), 2);
    expect_store(UINT64_C(9984), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(9992), UINT64_C(0x41200000), 2);
    vset(3, 1);
    vload(14, UINT64_C(4536), 3);
    vset(2, 4);
    vec(UINT64_C(49), 16, 8, 14);
    vec(UINT64_C(51), 18, 8, 14);
    vset(3, 1);
    vstore(16, UINT64_C(10000), 3);
    vstore(18, UINT64_C(10008), 3);
    expect_store(UINT64_C(10000), UINT64_C(0x4034000000000000), 3);
    expect_store(UINT64_C(10008), UINT64_C(0x4034000000000000), 3);
    // With no active elements the seed is copied exactly and no exception is
    // raised, even when the source and seed contain signaling NaNs.
    emit(UINT64_C(0x105073));
    memory_words[56] = UINT64_C(0x7f8000017f800001);
    memory_words[57] = UINT64_C(0x7f800001);
    vset(2, 4);
    vload(20, UINT64_C(4544), 2);
    vload(21, UINT64_C(4552), 2);
    vec(UINT64_C(25), 0, 20, 20, 0, 0); // vmsne.vv v0,v20,v20: all disabled
    vec(UINT64_C(3), 22, 20, 21, 1);
    vset(2, 1);
    vstore(22, UINT64_C(10016), 2);
    expect_store(UINT64_C(10016), UINT64_C(0x7f800001), 2);
    signature(UINT64_C(1), UINT64_C(10024), 0);
    // An active signaling NaN in a non-final fold contributes NV and yields
    // the canonical NaN only when the ordered reduction reaches its final beat.
    emit(UINT64_C(0x105073));
    memory_words[58] = UINT64_C(0x7f8000013f800000);
    memory_words[59] = UINT64_C(0x4040000040000000);
    memory_words[60] = 0;
    vset(2, 4);
    vload(24, UINT64_C(4560), 2);
    vload(25, UINT64_C(4576), 2);
    vec(UINT64_C(3), 26, 24, 25);
    vset(2, 1);
    vstore(26, UINT64_C(10032), 2);
    expect_store(UINT64_C(10032), UINT64_C(0x7fc00000), 2);
    signature(UINT64_C(1), UINT64_C(10040), 16);
    emit(UINT64_C(0x105073));
    // VL=0 performs no fold and leaves the destination untouched.
    memory_words[61] = UINT64_C(0x40e00000);
    vset(2, 1);
    vload(28, UINT64_C(4584), 2);
    vset(2, 0);
    vec(UINT64_C(1), 28, 8, 9);
    vset(2, 1);
    vstore(28, UINT64_C(10048), 2);
    expect_store(UINT64_C(10048), UINT64_C(0x40e00000), 2);
    signature(UINT64_C(1), UINT64_C(10056), 0);
    // Narrowing applies doubled EMUL to its E64 source and writes E32/LMUL1.
    memory_words[32] = UINT64_C(0x3ff8000000000000);
    memory_words[33] = UINT64_C(0xc006000000000000);
    memory_words[34] = UINT64_C(0x1000001);
    memory_words[35] = UINT64_C(0xffffffff);
    memory_words[36] = 3;
    memory_words[37] = -4;
    memory_words[38] = UINT64_C(0x3ff0000010000000);
    memory_words[39] = UINT64_C(0xbff0000010000000);
    vset(3, 2, 1);
    vload(8, UINT64_C(4352), 3);
    vload(10, UINT64_C(4368), 3);
    vload(12, UINT64_C(4384), 3);
    vload(14, UINT64_C(4400), 3);
    narrowing_conversion(16, 16, 8, UINT64_C(9600), 2, 0);
    narrowing_conversion(17, 17, 8, UINT64_C(9616), 2, -3);
    narrowing_conversion(18, 18, 10, UINT64_C(9632), UINT64_C(0x4b800000),
                         UINT64_C(0x4f800000));
    narrowing_conversion(19, 19, 12, UINT64_C(9648), UINT64_C(0x40400000),
                         UINT64_C(0xc0800000));
    narrowing_conversion(20, 20, 8, UINT64_C(9664), UINT64_C(0x3fc00000),
                         UINT64_C(0xc0300000));
    narrowing_conversion(21, 21, 14, UINT64_C(9680), UINT64_C(0x3f800001),
                         UINT64_C(0xbf800001));
    narrowing_conversion(22, 22, 8, UINT64_C(9696), 1, 0);
    narrowing_conversion(23, 23, 8, UINT64_C(9712), 1, -2);
    signature(UINT64_C(1), UINT64_C(9728), 17);
    emit(UINT64_C(0x105073));
    // A restarted, masked widening singleton preserves distinct pre-vstart and
    // masked-off E64 destinations while converting the later enabled element.
    memory_words[40] = 1;
    memory_words[41] = 1; // E32 mask source: 1,0,1
    memory_words[42] = 11;
    memory_words[43] = 22;
    memory_words[44] = 33;
    vset(3, 3, 1);
    vload(30, UINT64_C(4432), 3);
    vset(2, 3);
    vload(8, UINT64_C(4096), 2);
    vload(29, UINT64_C(4416), 2);
    vec(UINT64_C(31), 0, 29, 0, 0, 3);
    emit(UINT64_C(0x80d073));
    vec(UINT64_C(18), 30, 8, 12, 1);
    vset(3, 3, 1);
    vstore(30, UINT64_C(9744), 3);
    expect_store(UINT64_C(9744), 11, 3);
    expect_store(UINT64_C(9752), 22, 3);
    expect_store(UINT64_C(9760), UINT64_C(0x4008000000000000), 3);
    // In-place singleton writes must preserve the other FP32 half and pre-vstart lane.
    vset(2, 4);
    vload(8, UINT64_C(4096), 2);
    vload(9, UINT64_C(4112), 2);
    emit(UINT64_C(0x80d073)); // csrwi vstart,1
    vec(0, 8, 8, 9);
    vstore(8, UINT64_C(8256), 2);
    expect_store(UINT64_C(8256), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(8260), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(8264), UINT64_C(0x40a00000), 2);
    expect_store(UINT64_C(8268), UINT64_C(0x40000000), 2);
    signature(UINT64_C(8), UINT64_C(8272), 0);
    signature(UINT64_C(1), UINT64_C(8280), 0);
    vec(UINT64_C(31), 0, 9, 0, 0, 3); // vmsgt.vi v0,v9,0: lanes 0 and 2 only
    vec(0, 8, 8, 9, 1);
    vstore(8, UINT64_C(8448), 2);
    expect_store(UINT64_C(8448), UINT64_C(0x3fc00000), 2);
    expect_store(UINT64_C(8452), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(8456), UINT64_C(0x40e00000), 2);
    expect_store(UINT64_C(8460), UINT64_C(0x40000000), 2);
    // Double precision uses raw VRF operands, not scalar FPR boxing checks.
    vset(3, 2);
    memory_words[8] = UINT64_C(0x3ff0000000000000);
    memory_words[9] = UINT64_C(0x4000000000000000);
    memory_words[10] = UINT64_C(0x3fe0000000000000);
    memory_words[11] = UINT64_C(0xbff0000000000000);
    vload(8, UINT64_C(4160), 3);
    vload(9, UINT64_C(4176), 3);
    vec(0, 10, 8, 9);
    vec(2, 11, 8, 9);
    vec(UINT64_C(36), 12, 8, 9);
    vstore(10, UINT64_C(8288), 3);
    expect_store(UINT64_C(8288), UINT64_C(0x3ff8000000000000), 3);
    expect_store(UINT64_C(8296), UINT64_C(0x3ff0000000000000), 3);
    vstore(11, UINT64_C(8304), 3);
    expect_store(UINT64_C(8304), UINT64_C(0x3fe0000000000000), 3);
    expect_store(UINT64_C(8312), UINT64_C(0x4008000000000000), 3);
    vstore(12, UINT64_C(8320), 3);
    expect_store(UINT64_C(8320), UINT64_C(0x3fe0000000000000), 3);
    expect_store(UINT64_C(8328), UINT64_C(0xc000000000000000), 3);
    memory_words[24] = UINT64_C(0x3fe0000000000000);
    li(10, UINT64_C(4288));
    emit(UINT64_C(0x53387)); // fld f7,0(x10)
    vec(0, 13, 8, 7, 0, 5);
    vstore(13, UINT64_C(9184), 3);
    expect_store(UINT64_C(9184), UINT64_C(0x3ff8000000000000), 3);
    expect_store(UINT64_C(9192), UINT64_C(0x4004000000000000), 3);
    vec(UINT64_C(18), 14, 9, 1);
    vstore(14, UINT64_C(9280), 3);
    expect_store(UINT64_C(9280), 0, 3);
    expect_store(UINT64_C(9288), UINT64_C(0xffffffffffffffff), 3);
    memory_words[27] = 1;
    memory_words[28] = UINT64_C(0xfffffffffffffffe);
    vload(15, UINT64_C(4312), 3);
    vec(UINT64_C(18), 16, 15, 3);
    vstore(16, UINT64_C(9296), 3);
    expect_store(UINT64_C(9296), UINT64_C(0x3ff0000000000000), 3);
    expect_store(UINT64_C(9304), UINT64_C(0xc000000000000000), 3);
    signature(UINT64_C(1), UINT64_C(9312), 1);
    emit(UINT64_C(0x105073));
    // Masked-off signaling NaNs neither execute nor contribute NV.
    vset(2, 4);
    memory_words[12] = UINT64_C(0x7f8000017f800001);
    memory_words[13] = UINT64_C(0x7f8000017f800001);
    vload(8, UINT64_C(4192), 2);
    vec(UINT64_C(24), 0, 8, 8, 0, 0); // vmseq.vv v0,v8,v8: all enabled
    vec(UINT64_C(25), 0, 8, 8, 0, 0); // vmsne.vv v0,v8,v8: all disabled
    vec(0, 10, 8, 8, 1);
    signature(UINT64_C(1), UINT64_C(8336), 0);
    emit(UINT64_C(0x80006f));
    vec(0, 10, 8, 8); // branch-squashed NaN arithmetic
    signature(UINT64_C(1), UINT64_C(8344), 0);
    vec(0, 10, 8, 8);
    signature(UINT64_C(1), UINT64_C(8352), 16);
    vstore(10, UINT64_C(8368), 2);
    for (int i = 0; i < 4; i++)
      expect_store(UINT64_C(8368) + i * 4, UINT64_C(0x7fc00000), 2);
    emit(UINT64_C(0x105073)); // clear fflags
    // Captured RUP: 1 + 2^-24 rounds upward, setting NX.
    memory_words[14] = UINT64_C(0x3f8000003f800000);
    memory_words[15] = UINT64_C(0x3f8000003f800000);
    memory_words[16] = UINT64_C(0x3380000033800000);
    memory_words[17] = UINT64_C(0x3380000033800000);
    vload(8, UINT64_C(4208), 2);
    vload(9, UINT64_C(4224), 2);
    emit(UINT64_C(0x21d073)); // csrwi frm,3
    vec(0, 10, 8, 9);
    signature(UINT64_C(1), UINT64_C(8384), 1);
    vstore(10, UINT64_C(8400), 2);
    for (int i = 0; i < 4; i++)
      expect_store(UINT64_C(8400) + i * 4, UINT64_C(0x3f800001), 2);
    // More elements than completion slots: reuse tags while a scalar divide is live.
    emit(UINT64_C(
        1069171)); // clear flags so scalar NX and vector NV must both survive
    li(5, UINT64_C(0x40400000));
    emit(UINT64_C(0xf0028253)); // fmv.w.x f4,x5: scalar denominator 3.0
    vset(2, 31, 3);
    vec(UINT64_C(11), 8, 8, 8, 0, 0); // vxor.vv v8,v8,v8
    li(5, UINT64_C(0x7f800001));
    vec(0, 8, 8, 5, 0, 4);      // vadd.vx broadcasts raw signaling NaN
    emit(UINT64_C(0x1840f1d3)); // fdiv.s f3,f1,f4,dyn
    vec(UINT64_C(36), 24, 8, 8);
    emit(UINT64_C(1123));
    vec(0, 24, 8,
        8); // taken BEQ redirects without losing the older authorized FP tail
    vstore(24, UINT64_C(8704), 2);
    for (int i = 0; i < 31; i++)
      expect_store(UINT64_C(8704) + i * 4, UINT64_C(0x7fc00000), 2);
    emit(UINT64_C(0xe00181d3));
    li(10, UINT64_C(8832));
    emit(UINT64_C(0x353023));
    expect_store(UINT64_C(8832), UINT64_C(0x3eaaaaab), 3);
    signature(UINT64_C(1), UINT64_C(8840), 17);
    // FP movement stays on the packed merge/slide datapath. Scalar sources
    // are NaN-box checked, while vector-to-FPR E32 results are NaN-boxed at WB.
    memory_words[62] = UINT64_C(0x400000003f800000);
    memory_words[63] = UINT64_C(0x4080000040400000);
    memory_words[65] = UINT64_C(0x200000001);
    memory_words[66] = UINT64_C(0x400000003);
    vset(2, 4);
    vload(8, UINT64_C(4592), 2);
    vload(19, UINT64_C(4616), 2);
    li(5, UINT64_C(0x3f000000));
    emit(UINT64_C(0xf0028453));        // fmv.w.x f8,x5: 0.5
    vec(UINT64_C(23), 10, 0, 8, 0, 5); // vfmv.v.f
    vec(UINT64_C(31), 0, 19, 2, 0, 3); // vmsgt.vi: lanes 2 and 3
    vec(UINT64_C(23), 11, 8, 8, 1, 5); // vfmerge.vfm
    vec(UINT64_C(14), 12, 8, 8, 0, 5); // vfslide1up.vf
    vec(UINT64_C(15), 13, 8, 8, 0, 5); // vfslide1down.vf
    vload(14, UINT64_C(4592), 2);
    vec(UINT64_C(16), 14, 0, 8, 0, 5); // vfmv.s.f
    vstore(10, UINT64_C(10064), 2);
    vstore(11, UINT64_C(10080), 2);
    vstore(12, UINT64_C(10096), 2);
    vstore(13, UINT64_C(10112), 2);
    vstore(14, UINT64_C(10128), 2);
    for (int i = 0; i < 4; i++)
      expect_store(UINT64_C(10064) + i * 4, UINT64_C(0x3f000000), 2);
    expect_store(UINT64_C(10080), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(10084), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(10088), UINT64_C(0x3f000000), 2);
    expect_store(UINT64_C(10092), UINT64_C(0x3f000000), 2);
    expect_store(UINT64_C(10096), UINT64_C(0x3f000000), 2);
    expect_store(UINT64_C(10100), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(10104), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(10108), UINT64_C(0x40400000), 2);
    expect_store(UINT64_C(10112), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(10116), UINT64_C(0x40400000), 2);
    expect_store(UINT64_C(10120), UINT64_C(0x40800000), 2);
    expect_store(UINT64_C(10124), UINT64_C(0x3f000000), 2);
    expect_store(UINT64_C(10128), UINT64_C(0x3f000000), 2);
    expect_store(UINT64_C(10132), UINT64_C(0x40000000), 2);
    expect_store(UINT64_C(10136), UINT64_C(0x40400000), 2);
    expect_store(UINT64_C(10140), UINT64_C(0x40800000), 2);
    // vfmv.f.s waits for older scalar execution and load reservations even
    // when they write different FPRs, then feeds a younger scalar observer.
    emit(UINT64_C(0x10f2d3)); // fadd.s f5,f1,f1,dyn
    vset(2, 0);
    vec(UINT64_C(16), 9, 8, 0, 0, 1); // vfmv.f.s f9,v8
    emit(((UINT64_C(0xe2000053) | (9 << 15) | (3 << 7)) &
          low_mask(32))); // fmv.x.d x3,f9
    li(10, UINT64_C(10144));
    emit(UINT64_C(0x353023));
    expect_store(UINT64_C(10144), UINT64_C(0xffffffff3f800000), 3);
    li(10, UINT64_C(4280));
    emit(UINT64_C(0x53307)); // fld f6,0(x10)
    vec(UINT64_C(16), 9, 8, 0, 0, 1);
    emit(((UINT64_C(0xe2000053) | (9 << 15) | (3 << 7)) & low_mask(32)));
    li(10, UINT64_C(10152));
    emit(UINT64_C(0x353023));
    expect_store(UINT64_C(10152), UINT64_C(0xffffffff3f800000), 3);
    emit(UINT64_C(0x80006f));
    vec(UINT64_C(16), 9, 9, 0, 0, 1); // squashed vector-to-FPR write
    emit(((UINT64_C(0xe2000053) | (9 << 15) | (3 << 7)) & low_mask(32)));
    li(10, UINT64_C(10216));
    emit(UINT64_C(0x353023));
    expect_store(UINT64_C(10216), UINT64_C(0xffffffff3f800000), 3);
    memory_words[64] = UINT64_C(0x7f800001);
    vset(2, 1);
    vload(18, UINT64_C(4608), 2);
    vset(2, 0);
    vec(UINT64_C(16), 10, 18, 0, 0,
        1); // a moved signaling-NaN payload is not canonicalized
    emit(((UINT64_C(0xe2000053) | (10 << 15) | (3 << 7)) & low_mask(32)));
    li(10, UINT64_C(10208));
    emit(UINT64_C(0x353023));
    expect_store(UINT64_C(10208), UINT64_C(0xffffffff7f800001), 3);
    li(10, UINT64_C(4280));
    emit(UINT64_C(0x53307)); // fld f6,0(x10): invalid FP32 NaN box
    vset(2, 4);
    vec(UINT64_C(23), 18, 0, 6, 0, 5);
    vstore(18, UINT64_C(10224), 2);
    for (int i = 0; i < 4; i++)
      expect_store(UINT64_C(10224) + i * 4, UINT64_C(0x7fc00000), 2);
    // Vector destinations are unchanged at vl=0 for insertion and FP slides.
    vset(2, 4);
    vload(15, UINT64_C(4592), 2);
    vload(16, UINT64_C(4592), 2);
    vload(17, UINT64_C(4592), 2);
    vset(2, 0);
    vec(UINT64_C(16), 15, 0, 8, 0, 5);
    vec(UINT64_C(14), 16, 8, 8, 0, 5);
    vec(UINT64_C(15), 17, 8, 8, 0, 5);
    vset(2, 4);
    vstore(15, UINT64_C(10160), 2);
    vstore(16, UINT64_C(10176), 2);
    vstore(17, UINT64_C(10192), 2);
    for (int destination = 0; destination < 3; destination++) {
      expect_store(UINT64_C(10160) + destination * 16, UINT64_C(0x3f800000), 2);
      expect_store(UINT64_C(10164) + destination * 16, UINT64_C(0x40000000), 2);
      expect_store(UINT64_C(10168) + destination * 16, UINT64_C(0x40400000), 2);
      expect_store(UINT64_C(10172) + destination * 16, UINT64_C(0x40800000), 2);
    }
    // Unary FP classification and estimates share the fixed-latency FP
    // service. Special values contribute flags only for active elements.
    memory_words[67] = UINT64_C(0xbf80000000000000);
    memory_words[68] = UINT64_C(0x7fc000007f800000);
    vset(2, 4);
    vload(8, UINT64_C(4632), 2);
    vec(UINT64_C(19), 10, 8, 16);
    vstore(10, UINT64_C(10240), 2);
    expect_store(UINT64_C(10240), UINT64_C(16), 2);
    expect_store(UINT64_C(10244), UINT64_C(2), 2);
    expect_store(UINT64_C(10248), UINT64_C(128), 2);
    expect_store(UINT64_C(10252), UINT64_C(512), 2);
    memory_words[69] = UINT64_C(0x7f76543200718abc);
    memory_words[70] = UINT64_C(0x7f80000100000000);
    emit(UINT64_C(0x105073));
    vload(8, UINT64_C(4648), 2);
    vec(UINT64_C(19), 11, 8, 5);
    vstore(11, UINT64_C(10256), 2);
    expect_store(UINT64_C(10256), UINT64_C(0x7e900000), 2);
    expect_store(UINT64_C(10260), UINT64_C(0x214000), 2);
    expect_store(UINT64_C(10264), UINT64_C(0x7f800000), 2);
    expect_store(UINT64_C(10268), UINT64_C(0x7fc00000), 2);
    signature(UINT64_C(1), UINT64_C(10272), 24);
    emit(UINT64_C(0x105073));
    memory_words[71] = UINT64_C(0x7f76543200718abc);
    memory_words[72] = UINT64_C(0x7f800000bf800000);
    vload(8, UINT64_C(4664), 2);
    vec(UINT64_C(19), 12, 8, 4);
    vstore(12, UINT64_C(10288), 2);
    expect_store(UINT64_C(10288), UINT64_C(0x5f080000), 2);
    expect_store(UINT64_C(10292), UINT64_C(0x1f820000), 2);
    expect_store(UINT64_C(10296), UINT64_C(0x7fc00000), 2);
    expect_store(UINT64_C(10300), UINT64_C(0), 2);
    signature(UINT64_C(1), UINT64_C(10304), 16);
    emit(UINT64_C(0x105073));
    // Restart and mask suppression preserve old lanes and suppress a masked
    // signaling NaN while the memory interface continues to reject once.
    memory_words[73] = UINT64_C(0x2222222211111111);
    memory_words[74] = UINT64_C(0x4444444433333333);
    memory_words[75] = UINT64_C(1);
    memory_words[76] = UINT64_C(0x100000001);
    memory_words[77] = UINT64_C(0x7f80000100000000);
    memory_words[78] = UINT64_C(0x400000003f800000);
    vload(13, UINT64_C(4680), 2);
    vload(9, UINT64_C(4696), 2);
    vload(8, UINT64_C(4712), 2);
    vec(UINT64_C(31), 0, 9, 0, 0, 3);
    emit(UINT64_C(0x80d073));
    vec(UINT64_C(19), 13, 8, 5, 1);
    vstore(13, UINT64_C(10320), 2);
    expect_store(UINT64_C(10320), UINT64_C(0x11111111), 2);
    expect_store(UINT64_C(10324), UINT64_C(0x22222222), 2);
    expect_store(UINT64_C(10328), UINT64_C(0x3f7f0000), 2);
    expect_store(UINT64_C(10332), UINT64_C(0x3eff0000), 2);
    signature(UINT64_C(8), UINT64_C(10336), 0);
    signature(UINT64_C(1), UINT64_C(10344), 0);
    // A younger, branch-squashed negative reciprocal-square-root neither
    // writes its destination nor raises invalid.
    emit(UINT64_C(0x80006f));
    vec(UINT64_C(19), 14, 8, 4);
    signature(UINT64_C(1), UINT64_C(10352), 0);
    memory_words[79] = UINT64_C(0x3ff0000000000000);
    memory_words[80] = UINT64_C(0x4000000000000000);
    vset(3, 2);
    vload(8, UINT64_C(4728), 3);
    vec(UINT64_C(19), 15, 8, 5);
    vstore(15, UINT64_C(10368), 3);
    expect_store(UINT64_C(10368), UINT64_C(0x3fefe00000000000), 3);
    expect_store(UINT64_C(10376), UINT64_C(0x3fdfe00000000000), 3);
    // Zvfh includes the Zvfhmin F16<->F32 conversions. A signaling half NaN
    // canonicalizes and raises invalid; narrowing reports inexact rounding.
    emit(UINT64_C(0x105073));
    memory_words[81] = UINT64_C(0x7c01c0003c00);
    vset(1, 4);
    vload(8, UINT64_C(4744), 1);
    vec(UINT64_C(18), 16, 8, 12);
    vset(2, 4);
    vstore(16, UINT64_C(10384), 2);
    expect_store(UINT64_C(10384), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(10388), UINT64_C(0xc0000000), 2);
    expect_store(UINT64_C(10392), UINT64_C(0x7fc00000), 2);
    expect_store(UINT64_C(10396), 0, 2);
    signature(UINT64_C(1), UINT64_C(10400), 16);
    emit(UINT64_C(0x105073));
    emit(UINT64_C(0x205073));
    memory_words[82] = UINT64_C(0xc00000003f800000);
    memory_words[83] = UINT64_C(0x3f801000);
    vset(2, 4, 1);
    vload(18, UINT64_C(4752), 2);
    vset(1, 4);
    vec(UINT64_C(18), 20, 18, 20);
    vstore(20, UINT64_C(10416), 1);
    expect_store(UINT64_C(10416), UINT64_C(15360), 1);
    expect_store(UINT64_C(10418), UINT64_C(49152), 1);
    expect_store(UINT64_C(10420), UINT64_C(15360), 1);
    expect_store(UINT64_C(10422), 0, 1);
    signature(UINT64_C(1), UINT64_C(10432), 1);
    emit(UINT64_C(0x105073));
    // Full Zvfh admits same-width FP16 arithmetic and FP scalar moves, widens
    // half operands through the shared FP32 lane, and implements the six
    // additional SEW=8 integer conversion forms with exact 8-bit widths.
    memory_words[84] = UINT64_C(0x4400420040003c00); // 1,2,3,4
    memory_words[85] = UINT64_C(0xc0004000bc003800); // .5,-1,2,-2
    vset(1, 4);
    vload(8, UINT64_C(4768), 1);
    vload(9, UINT64_C(4776), 1);
    vec(0, 10, 8, 9);
    vstore(10, UINT64_C(10448), 1);
    expect_store(UINT64_C(10448), UINT64_C(15872), 1);
    expect_store(UINT64_C(10450), UINT64_C(15360), 1);
    expect_store(UINT64_C(10452), UINT64_C(17664), 1);
    expect_store(UINT64_C(10454), UINT64_C(16384), 1);
    vec(UINT64_C(48), 12, 8, 9);
    vset(2, 4, 1);
    vstore(12, UINT64_C(10464), 2);
    expect_store(UINT64_C(10464), UINT64_C(0x3fc00000), 2);
    expect_store(UINT64_C(10468), UINT64_C(0x3f800000), 2);
    expect_store(UINT64_C(10472), UINT64_C(0x40a00000), 2);
    expect_store(UINT64_C(10476), UINT64_C(0x40000000), 2);
    vset(1, 0);
    vec(UINT64_C(16), 9, 8, 0, 0, 1);
    emit(((UINT64_C(0xe2000053) | (9 << 15) | (3 << 7)) & low_mask(32)));
    li(10, UINT64_C(10480));
    emit(UINT64_C(0x353023));
    expect_store(UINT64_C(10480), UINT64_C(0xffffffffffff3c00), 3);
    memory_words[86] = UINT64_C(0xfc03fe01); // signed bytes 1,-2,3,-4
    vset(0, 4);
    vload(8, UINT64_C(4784), 0);
    vec(UINT64_C(18), 10, 8, 11);
    vset(1, 4, 1);
    vstore(10, UINT64_C(10496), 1);
    expect_store(UINT64_C(10496), UINT64_C(15360), 1);
    expect_store(UINT64_C(10498), UINT64_C(49152), 1);
    expect_store(UINT64_C(10500), UINT64_C(16896), 1);
    expect_store(UINT64_C(10502), UINT64_C(50176), 1);
    memory_words[87] = UINT64_C(0xc4004200c0003c00);
    vset(0, 4);
    vload(12, UINT64_C(4792), 1);
    vec(UINT64_C(18), 14, 12, 17);
    vstore(14, UINT64_C(10512), 0);
    expect_store(UINT64_C(10512), 1, 0);
    expect_store(UINT64_C(10513), UINT64_C(254), 0);
    expect_store(UINT64_C(10514), 3, 0);
    expect_store(UINT64_C(10515), UINT64_C(252), 0);
    // Empty macro clears vstart without executing or modifying flags.
    vset(2, 0);
    emit(UINT64_C(0x83d073));
    vec(0, 10, 8, 8);
    signature(UINT64_C(8), UINT64_C(8416), 0);
    signature(UINT64_C(1), UINT64_C(8424), 0);
    program_words[UINT64_C(3840) / 4] = UINT64_C(
        874521075); // expose unexpected mcause through the public memory port
    program_words[UINT64_C(3844) / 4] = UINT64_C(0x303023);
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
      rising();
    falling();
    reset = 0;
  }
}

int main() {
  return run_test([] {
    cycle_limit = 1000000;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
