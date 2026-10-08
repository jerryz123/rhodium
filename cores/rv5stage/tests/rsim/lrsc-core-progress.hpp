// Shares the public-port lrsc-core-progress oracle across hardware specializations.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Tests word/doubleword constrained loops, prediction, read pressure, and competing LR/SC progress.

std::uint8_t image[65536];

int completions = 0, fetches = 0, demands = 0, blocked_snoops = 0;

int failures = 0;

std::uint64_t result;

void tick() {
  if (!reset) {
    if (host_out.presponse.pvalid) {
      CHECK(!host_out.presponse.pbits.paccess_ufault);
      completions++;
      result = host_out.presponse.pbits.pdata;
    }
    if (run) {
      fetches += int(instruction_requests);
      demands += int(data_requests);
      blocked_snoops += int(eviction_waiting);
    }
  }
  rising();
  settle();
}

void access(std::uint8_t write, int address, std::uint64_t data = 0,
            int operation = 0, int data_width = 3) {
  int target_count;
  target_count = completions + 1;
  for (int cycle = 0; !host_out.prequest.pready && cycle < 10000; cycle++)
    tick();
  CHECK(host_out.prequest.pready);
  host_in.prequest.pbits = {};
  host_in.prequest.pbits.paddress = ((address)&low_mask(64));
  host_in.prequest.pbits.paccess = operation != 0 ? ((operation)&low_mask(4))
                                   : write        ? 2
                                                  : 1;
  host_in.prequest.pbits.pwidth = ((data_width)&low_mask(2));
  host_in.prequest.pbits.pbyte_umask =
      ((((1 << (1 << data_width)) - 1) << (address % 8)) & low_mask(8));
  host_in.prequest.pbits.pdata = data;
  host_in.prequest.pbits.pcontext.pwriteback =
      write ? UINT64_C(0) : UINT64_C(128);
  host_in.prequest.pvalid = 1;
  tick();
  host_in.prequest.pvalid = 0;
  for (int cycle = 0; completions < target_count && cycle < 10000; cycle++)
    tick();
  CHECK(completions == target_count);
}

std::uint32_t addi(int rd, int rs, int imm) {
  return (field(((imm)&low_mask(12)), 12, 20) |
          field(((rs)&low_mask(5)), 5, 15) | field(UINT64_C(0), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(19), 7, 0));
}
std::uint32_t branch(int rs, std::uint8_t nonzero, int offset) {
  return (
      field(((offset >> 12) & low_mask(1)), 1, 31) |
      field(((offset >> 5) & low_mask(6)), 6, 25) | field(UINT64_C(0), 5, 20) |
      field(((rs)&low_mask(5)), 5, 15) | field(((nonzero)&low_mask(3)), 3, 12) |
      field(((offset >> 1) & low_mask(4)), 4, 8) |
      field(((offset >> 11) & low_mask(1)), 1, 7) | field(UINT64_C(99), 7, 0));
}
std::uint32_t jal(int offset) {
  return (field(((offset >> 20) & low_mask(1)), 1, 31) |
          field(((offset >> 1) & low_mask(10)), 10, 21) |
          field(((offset >> 11) & low_mask(1)), 1, 20) |
          field(((offset >> 12) & low_mask(8)), 8, 12) |
          field(UINT64_C(0), 5, 7) | field(UINT64_C(111), 7, 0));
}
std::uint32_t csr_write(int csr, int rs) {
  return (field(((csr)&low_mask(12)), 12, 20) |
          field(((rs)&low_mask(5)), 5, 15) | field(UINT64_C(1), 3, 12) |
          field(UINT64_C(0), 5, 7) | field(UINT64_C(115), 7, 0));
}
void word(int address, std::uint32_t data) {
  for (int lane = 0; lane < 4; lane++)
    image[address + lane] = ((data >> (lane * 8)) & low_mask(8));
}
void initialize_program(int loop_pc, std::uint8_t translated,
                        std::uint8_t redirects, std::uint8_t word_access = 0) {
  int pc;
  std::uint64_t data;
  for (int address = UINT64_C(4096); address < UINT64_C(8448); address += 4)
    word(address, UINT64_C(19));
  pc = UINT64_C(4096);
  word(pc, UINT64_C(17079));
  pc += 4; // lui x5,4: reserved word
  word(pc, addi(11, 0, 8));
  pc += 4;
  if (translated) {
    word(pc, addi(10, 0, 1));
    pc += 4;
    word(pc, UINT64_C(0x3f51513));
    pc += 4; // slli x10,x10,63
    word(pc, addi(10, 10, 8));
    pc += 4;
    word(pc, csr_write(UINT64_C(384), 10));
    pc += 4; // satp: Sv39, root at 0x8000
    word(pc, UINT64_C(0x12000073));
    pc += 4; // sfence.vma
    word(pc, UINT64_C(5431));
    pc += 4;
    word(pc, UINT64_C(0x155513));
    pc += 4; // srli x10,x10,1: MPP=S
    word(pc, csr_write(UINT64_C(768), 10));
    pc += 4;
    word(pc, UINT64_C(9527));
    pc += 4;
    word(pc, addi(10, 10, loop_pc - UINT64_C(8192)));
    pc += 4;
    word(pc, csr_write(UINT64_C(833), 10));
    pc += 4;
    word(pc, UINT64_C(0x30200073)); // mret to loop in S mode
  } else
    word(pc, jal(loop_pc - pc));
  // Exactly sixteen sequential instructions, including the retry branch:
  // LR, twelve branch/NOP slots, ADDI, SC, BNE retry. Pressure cases take
  // six forward branches over NOPs, stressing cold and disabled prediction.
  word(loop_pc, word_access ? UINT64_C(0x1002a32f)
                            : UINT64_C(0x1002b32f)); // lr.w/d x6,(x5)
  for (int index = 1; index <= 12; index++)
    word(loop_pc + index * 4, !redirects       ? branch(0, 0, 4)
                              : index % 2 == 1 ? branch(0, 0, 8)
                                               : UINT64_C(19));
  word(loop_pc + 52, addi(6, 6, 1));
  word(loop_pc + 56, word_access ? UINT64_C(0x1862a3af)
                                 : UINT64_C(0x1862b3af)); // sc.w/d x7,x6,(x5)
  word(loop_pc + 60, branch(7, 1, -60));
  word(loop_pc + 64, addi(11, 11, -1));
  word(loop_pc + 68, branch(11, 1, -68));
  word(loop_pc + 72,
       rv32 ? UINT64_C(0x462a023) : UINT64_C(0x462b023)); // coherent signature
  word(loop_pc + 76, UINT64_C(0x10500073));               // wfi
  word(loop_pc + 80, jal(0));
  for (int address = UINT64_C(4096); address < UINT64_C(8448); address += 8) {
    data = 0;
    for (int lane = 0; lane < 8; lane++)
      data |= ((image[address + lane]) & low_mask(64)) << (lane * 8);
    access(1, address, data);
  }
  access(1, UINT64_C(16384), 0);
  access(1, UINT64_C(16448), 0);
  // Three-level identity mapping with 4-KiB leaves and V/R/W/X/A/D.
  // Crossing 0x2000 exercises a distinct ITLB entry and page-table walk.
  access(1, UINT64_C(32768), UINT64_C(9217));
  access(1, UINT64_C(36864), UINT64_C(10241));
  access(1, UINT64_C(40968), UINT64_C(1231));
  access(1, UINT64_C(40976), UINT64_C(2255));
  access(1, UINT64_C(40992), UINT64_C(4303));
}

void drive() {}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = 1;
  run = 0;
  {
    int loop_pc;
    int rival_successes, passed;
    std::uint64_t rival_value;
    bool done;
    passed = 0;
    host_in = {};
    host_in.presponse.pready = 1;
    // Warm a function in L1I, patch it through the core's write-back D-cache,
    // execute FENCE.I, and call it again. Only the new instruction returns 2.
    reset = 1;
    run = 0;
    tick();
    tick();
    reset = 0;
    tick();
    initialize_program(UINT64_C(8128), 0, 0, rv32);
    access(
        1, UINT64_C(4096),
        (field(jal(UINT64_C(8192) - UINT64_C(4100)) | UINT64_C(128), 32, 32) |
         field(UINT64_C(8887), 32, 0)));
    access(1, UINT64_C(4104),
           (field(addi(7, 7, UINT64_C(787)), 32, 32) |
            field(UINT64_C(0x2003b7), 32, 0)));
    access(1, UINT64_C(4112),
           (field(UINT64_C(4111), 32, 32) |
            field(UINT64_C(0x72a023), 32, 0))); // fence.i; sw x7,0(x5)
    access(
        1, UINT64_C(4120),
        (field(UINT64_C(17463), 32, 32) |
         field(jal(UINT64_C(8192) - UINT64_C(4120)) | UINT64_C(128), 32, 0)));
    access(1, UINT64_C(4128),
           (field(UINT64_C(0x10500073), 32, 32) |
            field(rv32 ? UINT64_C(0x4642023) : UINT64_C(0x4643023), 32, 0)));
    access(1, UINT64_C(8192),
           (field(UINT64_C(32871), 32, 32) | field(addi(6, 0, 1), 32, 0)));
    run = 1;
    done = 0;
    for (int poll = 0; poll < 128 && !done; poll++) {
      for (unsigned repeat_index = 0; repeat_index < (128); ++repeat_index)
        tick();
      access(0, UINT64_C(16448));
      done = result == 2;
    }
    CHECK(done);

    for (int word_access = int(rv32); word_access < 2; word_access++) {
      for (int translated = 0; translated < (rv32 ? 1 : 2); translated++) {
        for (int alignment = 0; alignment < 3; alignment++) {
          for (int pressure = 0; pressure < 4; pressure++) {
            reset = 1;
            run = 0;
            tick();
            tick();
            reset = 0;
            tick();
            loop_pc = alignment == 0   ? UINT64_C(8128)
                      : alignment == 1 ? UINT64_C(8160)
                                       : UINT64_C(8190);
            initialize_program(loop_pc, ((translated)&low_mask(1)),
                               pressure != 0, ((word_access)&low_mask(1)));
            fetches = 0;
            demands = 0;
            blocked_snoops = 0;
            rival_successes = 0;
            run = 1;
            done = 0;
            // Rival wins count only after the core participates; cold fetch
            // must not let the host finish the test before any core data traffic.
            if (pressure == 3) {
              for (int startup = 0; startup < 10000 && demands == 0; startup++)
                tick();
              CHECK(demands > 0);
            }
            for (int poll = 0; poll < 512 && !done; poll++) {
              if (pressure == 1) {
                // Repeated read-only replacement in the reserved line's LLC
                // set. Never write the reserved word while the core is running.
                for (int read_index = 0; read_index < 8; read_index++)
                  access(0, UINT64_C(20480) +
                                ((poll * 8 + read_index) % 32) * 128);
              } else if (pressure >= 2) {
                // Other LRs may not starve the core. Competing SCs may win:
                // eventuality guarantees system progress, not per-hart fairness.
                access(0, UINT64_C(16384), 0, 3, word_access != 0 ? 2 : 3);
                rival_value = result;
                if (pressure == 3) {
                  access(0, UINT64_C(16384), rival_value, 4,
                         word_access != 0 ? 2 : 3);
                  if (result == 0)
                    rival_successes++;
                }
              } else
                for (unsigned repeat_index = 0; repeat_index < (128);
                     ++repeat_index)
                  tick();
              access(0, UINT64_C(16448));
              done = result == 8 || (pressure == 3 && rival_successes >= 8);
            }
            if (!done) {
              failures++;

              // Diagnostic recovery is not a pass: record whether stopping the
              // competing traffic alone lets the same unmodified program finish.
              for (int poll = 0; poll < 64 && !done; poll++) {
                for (unsigned repeat_index = 0; repeat_index < (128);
                     ++repeat_index)
                  tick();
                access(0, UINT64_C(16448));
                done = result == 8;
              }

              continue;
            }
            access(0, UINT64_C(16384));
            CHECK((result == 8 ||
                   (pressure == 3 && rival_successes >= 8 && result <= 8)) &&
                  fetches > 0 && demands > 0);
            passed++;
          }
        }
      }
    }

    CHECK(failures == 0);
    throw Finished{};
  }
}

int main() {
  return run_test([] {
    cycle_limit = 50000002;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
