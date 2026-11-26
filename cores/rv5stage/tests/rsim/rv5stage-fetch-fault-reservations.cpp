// Checks Decode admission and FP/vector reservations with an older unresolved FP load.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"

constexpr unsigned Candidate = 0x30, Handler = 0x100;
constexpr unsigned Scalar = 0x00108153; // fadd.s f2,f1,f1
constexpr unsigned Vector = 0x428010d7; // vfmv.f.s f1,v8
unsigned opcode, stalls, admissions, fp_bookings, vector_bookings, stores;
bool fault, returned, pending_load, word_valid;
decltype(instruction_access_in.presponse.pbits) word;
decltype(data_access_in.presponse.pbits) load;

extern "C" void test_fault_decode(uint64_t rst, uint64_t valid, uint64_t ready,
                                  uint64_t pc, uint64_t instruction, uint64_t failed) {
  if (rst || !valid || pc != Candidate) return;
  CHECK(instruction == opcode && failed == fault);
  if (!ready && pending_load && !returned) ++stalls;
  if (ready) {
    CHECK(fault ? (pending_load && !returned) : returned);
    ++admissions;
  }
}

extern "C" void test_fp_booking(uint64_t rst, uint64_t booked) {
  if (!rst && booked) ++fp_bookings;
}

extern "C" void test_vector_booking(uint64_t rst, uint64_t valid, uint64_t ready) {
  if (!rst && valid && ready) ++vector_bookings;
}

uint32_t instruction_at(uint64_t pc) {
  switch (pc) {
  case 0x00: return 0x10000093; // li x1,0x100
  case 0x04: return 0x30509073; // csrw mtvec,x1
  case 0x08: return 0x000020b7; // FS and VS Initial
  case 0x0c: return 0x20008093;
  case 0x10: return 0x30009073; // csrw mstatus,x1
  case 0x14: return 0xc100f057; // vsetivli x0,1,e32,m1
  case 0x18: return 0x00003087; // fld f1,0(x0)
  case Candidate: return opcode;
  case Candidate + 4: return 0x00003c23; // sd x0,24(x0)
  case Handler:      return 0x342021f3; // csrr x3,mcause
  case Handler + 4:  return 0x00303023; // sd x3,0(x0)
  case Handler + 8:  return 0x341021f3; // csrr x3,mepc
  case Handler + 12: return 0x00303423; // sd x3,8(x0)
  case Handler + 16: return 0x343021f3; // csrr x3,mtval
  case Handler + 20: return 0x00303823; // sd x3,16(x0)
  case Candidate + 8:
  case Handler + 24: return 0x0000006f; // j .
  default: return 0x13;
  }
}

void drive() {
  instruction_access_in.prequest.pready = instruction_access_out.pflush || !word_valid;
  instruction_access_in.presponse = {.pvalid = word_valid, .pbits = word};
  data_access_in.prequest.pready = 1;
  data_access_in.pdrained = !pending_load;
  data_access_in.presponse = {.pvalid = pending_load && returned, .pbits = load};
}

void observe() {
  if (reset) return;
  if (instruction_access_out.pflush || (word_valid && instruction_access_out.presponse.pready))
    defer(word_valid, false);
  if (instruction_access_out.prequest.pvalid && instruction_access_in.prequest.pready) {
    const auto pc = instruction_access_out.prequest.pbits.paddress;
    if (pc == Handler) CHECK(fault && returned && !pending_load);
    defer(word_valid, true);
    defer(word.pword, instruction_at(pc));
    defer(word.paccess_ufault, pc == Candidate && fault);
  }
  if (data_access_in.presponse.pvalid && data_access_out.presponse.pready)
    defer(pending_load, false);
  if (!data_access_out.prequest.pvalid || !data_access_in.prequest.pready) return;
  const auto &req = data_access_out.prequest.pbits;
  if (req.paccess == 1) {
    CHECK(!pending_load && !returned && req.paddress == 0);
    defer(load.pcontext, req.pcontext);
    defer(pending_load, true);
  } else {
    CHECK(req.paccess == 2 && returned);
    CHECK(req.paddress == (fault ? stores * 8 : 24));
    CHECK(req.pdata == (fault ? (stores == 0 ? 1 : Candidate) : 0));
    defer(stores, stores + 1);
  }
}

void falling_update() {}

void scenario(unsigned candidate, bool failed) {
  reset = 1;
  opcode = candidate;
  fault = failed;
  pending_load = returned = word_valid = false;
  word = {};
  load = {};
  stalls = admissions = fp_bookings = vector_bookings = stores = 0;
  rising();
  rising();
  falling();
  reset = 0;
  for (int i = 0; i < 100; ++i) rising();
  CHECK(pending_load && !stores && !fp_bookings && !vector_bookings);
  CHECK(fault ? admissions == 1 : (admissions == 0 && stalls >= 3));
  returned = true;
  for (int i = 0; i < 100; ++i) rising();
  CHECK(admissions == 1 && stores == (fault ? 3 : 1) && !pending_load);
  CHECK(fp_bookings == (!fault && opcode == Scalar));
  CHECK(vector_bookings == (!fault && opcode == Vector));
}

int main() {
  return run_test([] {
    for (auto candidate : {Scalar, Vector})
      for (bool failed : {false, true}) scenario(candidate, failed);
  });
}
