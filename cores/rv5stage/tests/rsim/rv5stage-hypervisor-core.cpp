// Preserves the rv5stage-hypervisor-core cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"

// Tests host/guest translation, supervisor trap values, RV64 user execution, and WB effects.

bool sstc_case = 0;

std::uint8_t ram[196608];
int cycle, delay_left, stores, walks, flushes, traps, scenario;
int cmo_count, last_cmo;
bool cmo_completed;
bool pbmt_check = 0;
int expected_type, expected_pte_type, pbmt_data_reads;
std::uint64_t pbmt_code, pbmt_data;
bool pending;
int supervisor_root_address = 0;
bool supervisor_root_seen;
std::uint64_t reply;
int cursor, literal_cursor;
std::uint64_t expected_pc;
constexpr std::uint64_t SIGNATURE = UINT64_C(0x20000);
constexpr std::uint64_t GVA = UINT64_C(0x40000000);

std::uint64_t read64(std::uint64_t address) {
  std::uint64_t value = 0;
  CHECK(address + 7 < 196608);
  // These helpers are inlined at many stimulus sites; retain their byte loops.

  for (int lane = 0; lane < 8; lane++)
    bit_slice(value, lane * 8, 8) = ram[int(address) + lane];
  return value;
}
void write64(int address, std::uint64_t value) {

  for (int lane = 0; lane < 8; lane++)
    ram[address + lane] = bit_slice(value, lane * 8, 8);
}
void emit(std::uint32_t instruction) {

  for (int lane = 0; lane < 4; lane++)
    ram[cursor + lane] = bit_slice(instruction, lane * 8, 8);
  cursor += 4;
}
std::uint32_t addi(int rd, int rs, int immediate) {
  return (field(((immediate)&low_mask(12)), 12, 20) |
          field(((rs)&low_mask(5)), 5, 15) | field(UINT64_C(0), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(19), 7, 0));
}
std::uint32_t ld(int rd, int rs, int immediate) {
  return (field(((immediate)&low_mask(12)), 12, 20) |
          field(((rs)&low_mask(5)), 5, 15) | field(UINT64_C(3), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(3), 7, 0));
}
std::uint32_t sd(int rs2, int rs1, int immediate) {
  return (field(((immediate >> 5) & low_mask(7)), 7, 25) |
          field(((rs2)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(UINT64_C(3), 3, 12) |
          field(((immediate)&low_mask(5)), 5, 7) | field(UINT64_C(35), 7, 0));
}
std::uint32_t misaligned_atomic(std::uint8_t store, int base = 10) {
  return (field(((store ? 3 : 2) & low_mask(5)), 5, 27) |
          field(UINT64_C(0), 2, 25) | field(UINT64_C(0), 5, 20) |
          field(((base)&low_mask(5)), 5, 15) | field(UINT64_C(3), 3, 12) |
          field(UINT64_C(12), 5, 7) | field(UINT64_C(47), 7, 0)); // SC.D/LR.D
}
std::uint32_t fp(int funct7, int rd, int rs1, int rs2, int rm = 0) {
  return (field(((funct7)&low_mask(7)), 7, 25) |
          field(((rs2)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(((rm)&low_mask(3)), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(83), 7, 0));
}
std::uint32_t fld(int rd, int rs, int immediate,
                  std::uint8_t single_precision = 0) {
  return (field(((immediate)&low_mask(12)), 12, 20) |
          field(((rs)&low_mask(5)), 5, 15) |
          field(single_precision ? UINT64_C(2) : UINT64_C(3), 3, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(7), 7, 0));
}
std::uint32_t fsd(int rs2, int rs1, int immediate,
                  std::uint8_t single_precision = 0) {
  return (field(((immediate >> 5) & low_mask(7)), 7, 25) |
          field(((rs2)&low_mask(5)), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) |
          field(single_precision ? UINT64_C(2) : UINT64_C(3), 3, 12) |
          field(((immediate)&low_mask(5)), 5, 7) | field(UINT64_C(39), 7, 0));
}
std::uint32_t vset(int width, int rs = 11) {
  return (field(UINT64_C(0), 1, 31) |
          field(((width << 3) & low_mask(11)), 11, 20) |
          field(((rs)&low_mask(5)), 5, 15) | field(UINT64_C(7), 3, 12) |
          field(UINT64_C(12), 5, 7) | field(UINT64_C(87), 7, 0));
}
std::uint32_t vmem(std::uint8_t store, int width, int regno, int base,
                   std::uint8_t indexed = 0, std::uint8_t fault_first = 0) {
  return (field(UINT64_C(0), 4, 28) |
          field(indexed ? UINT64_C(3) : UINT64_C(0), 2, 26) |
          field(UINT64_C(1), 1, 25) |
          field(((indexed       ? 4
                  : fault_first ? 16
                                : 0) &
                 low_mask(5)),
                5, 20) |
          field(((base)&low_mask(5)), 5, 15) |
          field(((width == 0 ? 0 : width + 4) & low_mask(3)), 3, 12) |
          field(((regno)&low_mask(5)), 5, 7) |
          field(store ? UINT64_C(39) : UINT64_C(7), 7, 0));
}
std::uint32_t vadd(int vd, int vs2, int vs1, int mode) {
  return (field(UINT64_C(0), 6, 26) | field(UINT64_C(1), 1, 25) |
          field(((vs2)&low_mask(5)), 5, 20) |
          field(((vs1)&low_mask(5)), 5, 15) |
          field(((mode)&low_mask(3)), 3, 12) | field(((vd)&low_mask(5)), 5, 7) |
          field(UINT64_C(87), 7, 0));
}
std::uint32_t guest_load(int size, int extension, int rd = 12) {
  return (field(((UINT64_C(48) + 2 * size) & low_mask(7)), 7, 25) |
          field(((extension)&low_mask(5)), 5, 20) | field(UINT64_C(10), 5, 15) |
          field(UINT64_C(4), 3, 12) | field(((rd)&low_mask(5)), 5, 7) |
          field(UINT64_C(115), 7, 0));
}
std::uint32_t guest_store(int size) {
  return (field(((UINT64_C(49) + 2 * size) & low_mask(7)), 7, 25) |
          field(UINT64_C(11), 5, 20) | field(UINT64_C(10), 5, 15) |
          field(UINT64_C(4), 3, 12) | field(UINT64_C(0), 5, 7) |
          field(UINT64_C(115), 7, 0));
}
void constant(int rd, std::uint64_t value) {
  write64(literal_cursor, value);
  emit(ld(rd, 31, literal_cursor - UINT64_C(16384)));
  literal_cursor += 8;
}
void csrw(int address, std::uint64_t value) {
  constant(1, value);
  emit((field(((address)&low_mask(12)), 12, 20) | field(UINT64_C(1), 5, 15) |
        field(UINT64_C(1), 3, 12) | field(UINT64_C(0), 5, 7) |
        field(UINT64_C(115), 7, 0)));
}
void machine_boot() {
  cursor = UINT64_C(4096);
  literal_cursor = UINT64_C(14336);
  emit(UINT64_C(20407)); // lui x31, 4: constant pool base
  // Firmware explicitly initializes state access for host and guest software.

  for (int i = 0; i < 4; i++)
    csrw(UINT64_C(780) + i, UINT64_MAX);

  for (int i = 0; i < 4; i++)
    csrw(UINT64_C(1548) + i, UINT64_MAX);
}
void save_csr(int address, int offset) {
  emit((field(((address)&low_mask(12)), 12, 20) | field(UINT64_C(0), 5, 15) |
        field(UINT64_C(2), 3, 12) | field(UINT64_C(21), 5, 7) |
        field(UINT64_C(115), 7, 0)));
  emit(sd(21, 20, offset));
}

void prepare(int kind, std::uint8_t timer = 0) {
  reset = 1;
  sstc_case = timer;
  pbmt_check = 0;
  scenario = kind;
  for (int i = 0; i < 196608; i++)
    ram[i] = 0;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  csrw(UINT64_C(640), UINT64_C(0x8000000000000008));
  csrw(UINT64_C(1664), UINT64_C(0x8000000000000004));
  csrw(UINT64_C(833), (kind >= 14 && kind < 36 && !((kind == 26 || kind == 27)))
                          ? UINT64_C(20480)
                          : GVA);
  if (kind >= 14)
    csrw(UINT64_C(1536),
         ((kind == 22 || kind == 24)) ? UINT64_C(512) : UINT64_C(256));
  if (kind == 35)
    csrw(UINT64_C(512), 1 << 18); // VS SUM applies to HLVX's load semantics
  if (kind >= 36) {
    csrw(UINT64_C(517), GVA + UINT64_C(768));
    csrw(UINT64_C(1539), UINT64_C(1092));
    csrw(UINT64_C(1540), UINT64_C(1) << (2 + (kind - 36) * 4));
    if (timer) {
      csrw(UINT64_C(333), UINT64_C(0xffffffffffffffff));
      csrw(UINT64_C(589), 151);
      csrw(UINT64_C(1541), 50);
      csrw(UINT64_C(778), UINT64_C(0x8000000000000000));
      csrw(UINT64_C(1546), UINT64_C(0x8000000000000000));
      csrw(UINT64_C(774), 2);
      csrw(UINT64_C(1542), 2);
    } else
      csrw(UINT64_C(1605), UINT64_C(1) << (2 + (kind - 36) * 4));
  }
  if (kind == 11) {
    csrw(UINT64_C(1538), 1 << 13);
    csrw(UINT64_C(517), GVA + UINT64_C(768));
  }
  if (kind == 10) {
    constant(29, 1 << 17);
    csrw(UINT64_C(768),
         UINT64_C(0x8000020800)); // MPRV + MPV, effective VS data access
    emit(UINT64_C(0x40001537));
    emit(ld(12, 10, 0));
    emit(UINT64_C(0x300eb073)); // clear MPRV
  } else if (kind == 34)
    csrw(UINT64_C(768), UINT64_C(6144)); // M-mode explicit guest access
  else if (kind >= 14 && kind < 36 && !((kind == 26 || kind == 27)))
    csrw(UINT64_C(768), ((kind == 22 || kind == 23))
                            ? 0
                            : UINT64_C(2048)); // HS, or HU-controlled U
  else
    csrw(UINT64_C(768), UINT64_C(0x8000000800)); // MPV=1, MPP=S
  emit(UINT64_C(0x30200073));

  // Three-level VS and G tables. VS PTE reads themselves require G translation.
  write64(UINT64_C(32776), (UINT64_C(9) << 10) | 1);
  write64(UINT64_C(36864), (UINT64_C(10) << 10) | 1);
  write64(UINT64_C(40960),
          (UINT64_C(16) << 10) | UINT64_C(203)); // guest code, supervisor RXAD
  write64(UINT64_C(40968),
          (UINT64_C(17) << 10) | ((kind == 7) ? UINT64_C(195) : UINT64_C(199)));
  write64(UINT64_C(40976), (UINT64_C(18) << 10) | UINT64_C(207));
  write64(UINT64_C(16384), (UINT64_C(12) << 10) | 1);
  write64(UINT64_C(49152), (UINT64_C(13) << 10) | 1);
  write64(UINT64_C(53312), (UINT64_C(8) << 10) | UINT64_C(223));
  write64(UINT64_C(53320), (UINT64_C(9) << 10) | UINT64_C(223));
  write64(UINT64_C(53328), (UINT64_C(10) << 10) | UINT64_C(223));
  write64(UINT64_C(53376), (UINT64_C(24) << 10) | UINT64_C(223));
  write64(UINT64_C(53384),
          (UINT64_C(25) << 10) | ((kind == 6) ? UINT64_C(211) : UINT64_C(223)));
  write64(UINT64_C(32784),
          (UINT64_C(11) << 10) |
              1); // missing G mapping for a VS intermediate table
  write64(UINT64_C(0x19000), UINT64_C(4660));
  write64(UINT64_C(0x19020), UINT64_C(65261));
  write64(UINT64_C(0x1a000), UINT64_C(22136));

  cursor = UINT64_C(0x18000);
  // Guest constants use immediate arithmetic, never the host pool.
  emit(UINT64_C(0x40001537)); // lui x10, 0x40001
  switch (kind) {
  case 1:
  case 11: {
    {
      emit(UINT64_C(0x40003537));
      emit(ld(11, 10, 0));
    }
  } break;
  case 2: {
    {
      emit(UINT64_C(0x40002537));
      emit(ld(11, 10, 0));
    }
  } break;
  case 3: {
    {
      emit(UINT64_C(0x40002537));
      emit(sd(0, 10, 0));
    }
  } break;
  case 4: {
    {
      emit(UINT64_C(0x40002537));
      emit(UINT64_C(0x50067));
    } // jalr x0,x10
  } break;
  case 5: {
    {
      emit(addi(10, 0, 1));
      emit(UINT64_C(0x1f51513)); // slli x10,x10,31
      emit(ld(11, 10, 0));
    }
  } break;
  case 6:
  case 7: {
    {
      emit(ld(11, 10, 0));
      emit(sd(11, 10, 8));
    }
  } break;
  case 9: {
    emit(UINT64_C(0x22000073)); // HFENCE.VVMA in VS raises virtual-instruction
  } break;
  case 26: {
    emit(guest_load(3, 0));
  } break;
  case 27: {
    emit(guest_store(3));
  } break;
  default: {
    {
      emit(ld(11, 10, 0));
      emit(addi(11, 11, 1));
      emit(sd(11, 10, 8));
    }
  } break;
  }
  if ((kind == 1 || kind == 2 || kind == 3 || kind == 5 || kind == 6 ||
       kind == 7 || kind == 9 || kind == 12 || kind == 13)) {
    emit(UINT64_C(0x400017b7));
    emit(sd(0, 15, 32)); // younger mutation must be squashed
  }
  emit(UINT64_C(115));
  if (kind == 8) {
    emit(ld(11, 10, 0));
    emit(sd(11, 10, 16));
    emit(UINT64_C(115));
  }
  emit(UINT64_C(111));

  cursor = UINT64_C(0x18300); // VS trap handler, translated through both stages
  emit(UINT64_C(0x40001537));
  emit(UINT64_C(0x142026f3));
  emit(sd(13, 10, 128));
  emit(UINT64_C(0x143026f3));
  emit(sd(13, 10, 136));
  emit(UINT64_C(115));
  emit(UINT64_C(111));

  cursor = UINT64_C(8192);
  emit(UINT64_C(0x21a37));
  save_csr(UINT64_C(834), 0);
  emit(UINT64_C(111));
  cursor = UINT64_C(12288);
  emit(UINT64_C(0x20a37));
  save_csr(UINT64_C(322), 0);
  save_csr(UINT64_C(323), 8);
  save_csr(UINT64_C(1603), 16);
  save_csr(UINT64_C(1610), 24);
  save_csr(UINT64_C(1536), 32);
  save_csr(UINT64_C(321), 40);
  emit(sd(12, 20, 56));
  emit(addi(21, 0, 85));
  emit(sd(21, 20, 48));
  if (kind == 8) {
    constant(22, UINT64_C(53384));
    constant(23, (UINT64_C(26) << 10) | UINT64_C(223));
    emit(sd(23, 22, 0));
    emit(UINT64_C(0x62000073)); // HFENCE.GVMA after the PTE store
    emit(UINT64_C(0x14102af3));
    emit(addi(21, 21, 4));
    emit(UINT64_C(0x141a9073));
    emit(UINT64_C(0x10200073));
  } else
    emit(UINT64_C(111));
  switch (kind) {
  case 1:
  case 2:
  case 3:
  case 6:
  case 7: {
    expected_pc = GVA + 8;
  } break;
  case 4: {
    expected_pc = GVA + UINT64_C(8192);
  } break;
  case 5: {
    expected_pc = GVA + 12;
  } break;
  case 8: {
    expected_pc = GVA + 28;
  } break;
  case 9:
  case 13: {
    expected_pc = GVA + 4;
  } break;
  case 11: {
    expected_pc = GVA + UINT64_C(788);
  } break;
  case 12: {
    expected_pc = GVA;
  } break;
  default: {
    expected_pc = GVA + 16;
  } break;
  }
  if (kind >= 14) {
    // Explicit accesses run with V=0 but use VS/G translation. A physical
    // signature store after every load also detects leaked guest context.
    write64(UINT64_C(0x19000), UINT64_C(0x8000000080008080));
    if ((kind == 15 || kind == 17 || kind == 18 || kind == 19 || kind == 21 ||
         kind == 31 || kind == 32 || kind == 33))
      write64(UINT64_C(40968), (UINT64_C(17) << 10) | UINT64_C(201));
    if ((kind == 15 || kind == 18 || kind == 19 || kind == 21 || kind == 31))
      write64(UINT64_C(53384), (UINT64_C(25) << 10) | UINT64_C(217));
    if (kind == 17)
      write64(UINT64_C(53384), (UINT64_C(25) << 10) | UINT64_C(211));
    if (kind == 32)
      write64(UINT64_C(53384), (UINT64_C(26) << 10) | UINT64_C(217));
    if (kind == 33)
      write64(UINT64_C(53384), (UINT64_C(27) << 10) | UINT64_C(217));
    if ((kind == 22 || kind == 24 || kind == 25))
      write64(UINT64_C(40968), (UINT64_C(17) << 10) | UINT64_C(215));
    if (kind == 35) {
      write64(UINT64_C(40968), (UINT64_C(17) << 10) | UINT64_C(217));
      write64(UINT64_C(53384), (UINT64_C(25) << 10) | UINT64_C(217));
    }
    if (kind == 18) {
      write64(UINT64_C(53312), (UINT64_C(8) << 10) | UINT64_C(211));
      write64(UINT64_C(53320), (UINT64_C(9) << 10) | UINT64_C(211));
      write64(UINT64_C(53328), (UINT64_C(10) << 10) | UINT64_C(211));
    }
    if (kind == 19)
      write64(UINT64_C(53328), (UINT64_C(10) << 10) | UINT64_C(217));
    if (kind == 30)
      write64(UINT64_C(53384), (UINT64_C(25) << 10) | UINT64_C(211));
    cursor = UINT64_C(20480);
    emit(UINT64_C(0x40001537)); // guest VA in x10
    emit(UINT64_C(0x20a37));    // physical signature in x20
    if (kind == 14) {
      for (int size = 0; size < 4; size++) {
        emit(guest_load(size, 0));
        emit(sd(12, 20, 128 + size * 16));
        if (size < 3) {
          emit(guest_load(size, 1));
          emit(sd(12, 20, 136 + size * 16));
        }
      }
      constant(11, UINT64_C(0x123456789abcdef0));
      for (int size = 0; size < 4; size++) {
        emit(addi(10, 10, (size == 0) ? 8 : ((size < 3) ? 2 : 4)));
        emit(guest_store(size));
      }
    } else if (kind == 34) {
      constant(29, 1 << 17);
      csrw(UINT64_C(768),
           UINT64_C(
               549755944960)); // MPRV=1, MPV=1, MPP=U; explicit SPVP remains S
      emit(guest_load(3, 0));
      emit(UINT64_C(0x300eb073)); // clear MPRV before host constant loads
      csrw(UINT64_C(833), UINT64_C(20736));
      csrw(UINT64_C(768), UINT64_C(2048));
      emit(UINT64_C(0x30200073));
      cursor = UINT64_C(20736);
      expected_pc = ((cursor)&low_mask(64));
    } else {
      if (kind == 20)
        emit(guest_load(1, 0)); // warm R-only VS leaf
      if (kind == 21)
        emit(guest_load(1, 3)); // warm X-only leaves
      if ((kind == 28 || kind == 29)) {
        emit(UINT64_C(0x40002537));
        emit(addi(10, 10, -3)); // Split HLV/HSV into an unmapped G page.
      }
      expected_pc = ((cursor)&low_mask(64));
      if ((kind == 29 || kind == 30))
        emit(guest_store(3));
      else if (kind == 31)
        emit(guest_load(2, 3));
      else
        emit(guest_load(((kind == 15 || kind == 16 || kind == 17 ||
                          kind == 18 || kind == 19 || kind == 20 ||
                          kind == 32 || kind == 33 || kind == 35))
                            ? 1
                            : 3,
                        ((kind == 15 || kind == 16 || kind == 17 ||
                          kind == 18 || kind == 19 || kind == 20 ||
                          kind == 32 || kind == 33 || kind == 35))
                            ? 3
                            : 0));
    }
    if ((kind == 14 || kind == 15 || kind == 18 || kind == 22 || kind == 24 ||
         kind == 31 || kind == 35))
      expected_pc = ((cursor)&low_mask(64));
    emit(UINT64_C(115));
    emit(sd(0, 10, 32));
    emit(UINT64_C(111));
    if ((kind == 26 || kind == 27))
      expected_pc = GVA + 4;
  }
  if (kind >= 36) {
    cursor = UINT64_C(0x18000);
    emit(UINT64_C(0x40001537));
    emit(ld(12, 10, 0));
    // A pending, locally enabled interrupt wakes WFI even with VS SIE=0.
    // The prior load must complete; the CSR's serializing boundary then
    // enables delivery before the younger signature mutation can retire.
    emit(UINT64_C(0x10500073));
    emit(UINT64_C(0x10016073)); // wfi; csrsi sstatus, 2
    emit(addi(13, 0, 1));
    emit(sd(13, 10, 40));
    emit(UINT64_C(115));
    emit(UINT64_C(111));
    expected_pc = GVA + 24;
    cursor = UINT64_C(0x18300);
    emit(UINT64_C(0x14202773));
    emit(sd(14, 10, 64)); // guest scause
    emit(UINT64_C(0x14102773));
    emit(sd(14, 10, 72)); // guest sepc
    emit(UINT64_C(0x10002773));
    emit(sd(14, 10, 80)); // guest sstatus
    emit(UINT64_C(
        339759219)); // csrwi sip, 0: acknowledges only software pending
    if (timer) {
      emit(addi(14, 0, 300));
      emit(UINT64_C(0x14d71073)); // guest stimecmp rearm
    } else
      emit(UINT64_C(
          0x10405073)); // csrwi sie, 0: masks retained injected pending
    emit(
        UINT64_C(0x10200073)); // sret resumes the interrupted guest instruction
  }
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
}
void run_virtual_interrupt(int kind, std::uint8_t timer = 0) {
  falling();
  prepare(kind, timer);
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1 && !virtualized);
  CHECK(read64(UINT64_C(0x19040)) ==
        (UINT64_C(0x8000000000000001) + (((kind)&low_mask(64)) - 36) * 4));
  CHECK(read64(UINT64_C(0x19048)) == GVA + 16 &&
        read64(UINT64_C(0x19050)) == UINT64_C(0x200000120));
  CHECK(read64(SIGNATURE) == 10 && read64(SIGNATURE + 40) == expected_pc &&
        read64(SIGNATURE + 56) == UINT64_C(0x8000000080008080) &&
        read64(UINT64_C(0x19028)) == 1);
}
void run_explicit(int kind, std::uint64_t cause, std::uint64_t value = 0,
                  std::uint64_t gpa = 0, std::uint64_t transformed = 0) {
  falling();
  prepare(kind);
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1);
  CHECK(read64(SIGNATURE) == cause && read64(SIGNATURE + 8) == value &&
        read64(SIGNATURE + 16) == gpa >> 2 &&
        read64(SIGNATURE + 24) == transformed &&
        read64(SIGNATURE + 40) == expected_pc);
  CHECK(!virtualized && read64(UINT64_C(0x19020)) == UINT64_C(65261));
  if ((cause == 4 || cause == 6 || cause == 13 || cause == 15 || cause == 21 ||
       cause == 23))
    CHECK((read64(SIGNATURE + 32) & UINT64_C(64)) != 0);
  if (kind == 14) {
    CHECK(read64(SIGNATURE + 128) == UINT64_C(0xffffffffffffff80) &&
          read64(SIGNATURE + 136) == UINT64_C(128));
    CHECK(read64(SIGNATURE + 144) == UINT64_C(0xffffffffffff8080) &&
          read64(SIGNATURE + 152) == UINT64_C(32896));
    CHECK(read64(SIGNATURE + 160) == UINT64_C(0xffffffff80008080) &&
          read64(SIGNATURE + 168) == UINT64_C(0x80008080));
    CHECK(read64(SIGNATURE + 176) == UINT64_C(0x8000000080008080));
    CHECK(read64(UINT64_C(0x19008)) == UINT64_C(0x9abcdef0def000f0) &&
          read64(UINT64_C(0x19010)) == UINT64_C(0x123456789abcdef0));
  }
  if ((kind == 15 || kind == 18 || kind == 35))
    CHECK(read64(SIGNATURE + 56) == UINT64_C(32896));
  if (kind == 31)
    CHECK(read64(SIGNATURE + 56) == UINT64_C(0x80008080));
  if ((kind == 22 || kind == 24 || kind == 34))
    CHECK(read64(SIGNATURE + 56) == UINT64_C(0x8000000080008080));
  if (kind == 30)
    CHECK(read64(UINT64_C(0x19000)) == UINT64_C(0x8000000080008080));
}

void run_case(int kind, std::uint64_t cause, std::uint64_t value,
              std::uint64_t gpa, std::uint64_t transformed = 0) {
  falling();
  prepare(kind);
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps >= ((kind == 8) ? 2 : 1))
      break;
  }
  CHECK(traps == ((kind == 8) ? 2 : 1));
  CHECK(read64(SIGNATURE) == cause && read64(SIGNATURE + 8) == value &&
        read64(SIGNATURE + 16) == (gpa >> 2) &&
        read64(SIGNATURE + 24) == transformed);
  CHECK(walks > 0 && !virtualized);
  CHECK(read64(SIGNATURE + 40) == expected_pc);
  CHECK(read64(UINT64_C(0x19020)) == UINT64_C(65261));
  if (kind != 0 && kind != 8 && kind != 9 && kind != 10 && kind != 11)
    CHECK((read64(SIGNATURE + 32) & UINT64_C(64)) != 0);
  if (kind == 0 || kind == 8)
    CHECK(read64(UINT64_C(0x19008)) == UINT64_C(4661));
  if (kind == 8)
    CHECK(read64(UINT64_C(0x1a010)) == UINT64_C(22136) && flushes >= 3);
  if (kind == 10)
    CHECK(read64(SIGNATURE + 56) == UINT64_C(4660));
  if (kind == 11)
    CHECK(read64(UINT64_C(0x19080)) == 13 &&
          read64(UINT64_C(0x19088)) == GVA + UINT64_C(12288));
}
void run_fp(int kind) {
  // Reuse the delayed physical memory and three-level VS/G page tables.
  falling();
  prepare(0);
  reset = 1;
  scenario = kind;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  if (kind == 48) {
    csrw(UINT64_C(771), UINT64_C(32));
    csrw(UINT64_C(772), UINT64_C(32));
  }
  csrw(UINT64_C(640), UINT64_C(0x8000000000000008));
  csrw(UINT64_C(1664), UINT64_C(0x8000000000000004));
  csrw(UINT64_C(512), ((kind == 40 || kind == 42)) ? 0 : UINT64_C(8192));
  csrw(UINT64_C(768), UINT64_C(0x8000000800) |
                          (((kind == 41 || kind == 43)) ? 0 : UINT64_C(8192)));
  csrw(UINT64_C(833), GVA);
  emit(UINT64_C(0x30200073));
  write64(UINT64_C(0x19000), UINT64_C(0x3fc00000));         // single 1.5
  write64(UINT64_C(0x19008), UINT64_C(0x3ff0000000000000)); // double 1
  write64(UINT64_C(0x19010), UINT64_C(0x4008000000000000)); // double 3
  cursor = UINT64_C(0x18000);
  emit(UINT64_C(0x40001537));
  expected_pc = GVA + 4;
  switch (kind) {
  case 40:
  case 41: {
    emit(fp(UINT64_C(120), 1, 0, 0)); // fmv.w.x: disabled even without flags
  } break;
  case 42:
  case 43: {
    emit(UINT64_C(0x302673)); // frcsr: illegal, not virtual instruction
  } break;
  case 44: {
    {
      emit(UINT64_C(0x40002537));
      expected_pc = GVA + 8;
      emit(fld(1, 10, 0));
    }
  } break;
  case 45: {
    {
      emit(UINT64_C(0x40002537));
      expected_pc = GVA + 8;
      emit(fsd(1, 10, 0));
    }
  } break;
  default: {
    {
      emit(fld(1, 10, 0, 1));
      emit(fp(0, 2, 1, 1));
      emit(fsd(2, 10, 40, 1)); // F add
      emit(fld(4, 10, 8));
      emit(fld(5, 10, 16));
      emit(UINT64_C(0x21d073)); // csrwi frm,3: dynamic RUP
      emit(fp(UINT64_C(13), 6, 4, 5,
              7)); // fdiv.d f6,f4,f5,dyn, nontrivial latency
      if (kind == 46) {
        // No dependency on f6: synchronous fault must drain the older divide.
        expected_pc = GVA + 32;
        emit(UINT64_C(0xffffffff));
      } else if (kind == 47) {
        // A faulting FP load must drain the older divide and keep its flags.
        emit(UINT64_C(0x400025b7));
        expected_pc = GVA + 36;
        emit(fld(7, 11, 0));
      } else if (kind == 48) {
        emit(sd(0, 10, 120));
        for (int repeat_index = 0; repeat_index < (64); ++repeat_index)
          emit(UINT64_C(19));
        emit(sd(0, 10, 32));
        emit(UINT64_C(111));
      } else {
        emit(fsd(6, 10, 48));
        emit(UINT64_C(0x302673));
        emit(sd(12, 10, 64)); // shared fcsr
        emit(UINT64_C(0x10002673));
        emit(sd(12, 10, 72)); // guest FS/SD
        expected_pc = GVA + 52;
        emit(UINT64_C(115));
        // HS cleans only VS.FS, then returns without changing the FPRs.
        emit(fsd(6, 10, 56));
        emit(UINT64_C(0x10002673));
        emit(sd(12, 10, 80));
        emit(UINT64_C(115));
        emit(UINT64_C(111));
      }
    }
  } break;
  }
  if (kind != 39) {
    emit(sd(0, 10, 32));
    emit(UINT64_C(111));
  }
  cursor = UINT64_C(12288);
  emit(UINT64_C(0x20a37));
  save_csr(UINT64_C(322), 0);
  save_csr(UINT64_C(323), 8);
  save_csr(UINT64_C(1603), 16);
  save_csr(UINT64_C(1610), 24);
  save_csr(UINT64_C(321), 40);
  save_csr(UINT64_C(256), 96);
  save_csr(UINT64_C(512), 104);
  if ((kind == 39 || kind == 46 || kind == 47 || kind == 48)) {
    save_csr(UINT64_C(3), 80);
    emit(fsd(6, 20, 88));
  }
  emit(addi(21, 0, 85));
  emit(sd(21, 20, 48));
  if (kind == 39) {
    csrw(UINT64_C(512),
         UINT64_C(8192)); // cleaning guest FS must not clean HS FS
    emit(UINT64_C(0x14102af3));
    emit(addi(21, 21, 4));
    emit(UINT64_C(0x141a9073));
    emit(UINT64_C(0x10200073));
  } else
    emit(UINT64_C(111));
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps == (kind == 39 ? 2 : 1))
      break;
  }
  if (traps != (kind == 39 ? 2 : 1))
    std::cerr << "FP guest " << kind << " traps=" << traps << " pc=" << std::hex
              << instruction_address << " stores=" << stores << "\n";
  CHECK(traps == (kind == 39 ? 2 : 1));
  CHECK(!virtualized && walks > 0);
  if ((kind == 39 || kind == 46 || kind == 47 || kind == 48)) {
    CHECK(read64(UINT64_C(0x19028)) == UINT64_C(0x40400000) &&
          read64(SIGNATURE + 88) == UINT64_C(0x3fd5555555555556) &&
          read64(SIGNATURE + 80) == UINT64_C(97));
    CHECK((read64(SIGNATURE + 96) & UINT64_C(0x8000000000006000)) ==
          UINT64_C(0x8000000000006000));
  }
  switch (kind) {
  case 39: {
    {
      CHECK(read64(SIGNATURE) == 10 &&
            read64(UINT64_C(0x19030)) == UINT64_C(0x3fd5555555555556) &&
            read64(UINT64_C(0x19038)) == read64(UINT64_C(0x19030)) &&
            read64(UINT64_C(0x19040)) == UINT64_C(97) &&
            read64(UINT64_C(0x19048)) == UINT64_C(0x8000000200006000) &&
            read64(UINT64_C(0x19050)) == UINT64_C(0x200002000));
    }
  } break;
  case 40:
  case 41:
  case 42:
  case 43:
  case 46: {
    {
      CHECK(read64(SIGNATURE) == 2 && read64(SIGNATURE + 40) == expected_pc);
    }
  } break;
  case 44:
  case 45:
  case 47: {
    {
      CHECK(read64(SIGNATURE) == (kind == 45 ? 23 : 21) &&
            read64(SIGNATURE + 8) == GVA + UINT64_C(8192) &&
            read64(SIGNATURE + 16) == UINT64_C(18432) &&
            read64(SIGNATURE + 40) == expected_pc);
    }
  } break;
  case 48: {
    {
      CHECK(read64(SIGNATURE) == UINT64_C(0x8000000000000005) &&
            read64(SIGNATURE + 40) >= GVA + 36 &&
            read64(SIGNATURE + 40) < GVA + 292);
    }
  } break;
  }
  if (kind != 39)
    CHECK(read64(UINT64_C(0x19020)) == UINT64_C(65261));
}

void run_vector(int kind, std::uint8_t timer = 0) {
  std::uint64_t fault_pc;
  bool restart;
  restart = kind == 56;
  falling();
  prepare(0);
  reset = 1;
  scenario = kind;
  sstc_case = timer;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  if (kind == 59) {
    csrw(UINT64_C(771), UINT64_C(32));
    csrw(UINT64_C(772), UINT64_C(32));
  }
  if (timer) {
    csrw(UINT64_C(333), 101);
    csrw(UINT64_C(778), UINT64_C(0x8000000000000000));
  }
  csrw(UINT64_C(640), UINT64_C(0x8000000000000008));
  csrw(UINT64_C(1664), UINT64_C(0x8000000000000004));
  csrw(UINT64_C(512), ((kind == 51 || kind == 53))
                          ? UINT64_C(8192)
                          : (kind == 54 ? UINT64_C(512) : UINT64_C(8704)));
  csrw(UINT64_C(768),
       UINT64_C(0x8000000800) | (kind == 52 ? UINT64_C(8192) : UINT64_C(8704)));
  csrw(UINT64_C(833), GVA);
  emit(UINT64_C(0x30200073));
  write64(UINT64_C(0x19000), 11);
  write64(UINT64_C(0x19008), 22);
  write64(UINT64_C(0x19040), 0);
  write64(UINT64_C(0x19048), UINT64_C(4096));
  write64(UINT64_C(0x19ff8), 11);
  write64(UINT64_C(0x1c000), 22);
  if (kind == 55)
    write64(UINT64_C(53392), (UINT64_C(28) << 10) | UINT64_C(223));
  if ((kind == 50 || kind == 54)) {
    write64(UINT64_C(0x19000), UINT64_C(0x400000003f800000)); // 1, 2
    write64(UINT64_C(0x19008), UINT64_C(0x4080000040400000)); // 3, 4
  }
  cursor = UINT64_C(0x18000);
  emit(UINT64_C(0x40001537));
  emit(addi(11, 0, ((kind == 50 || kind == 54)) ? 4 : 2));
  fault_pc = GVA + ((cursor - UINT64_C(0x18000)) & low_mask(64));
  if (kind == 53)
    emit(UINT64_C(0xc2202673)); // vlenb, disabled in VS
  else
    emit(vset(((kind == 50 || kind == 54)) ? 2 : 3));
  if (kind == 60) {
    emit(addi(11, 0, 1));
    emit(
        UINT64_C(0x1f59593)); // guest VA 0x80000000, implicit PTE at GPA 0xb000
    fault_pc = GVA + ((cursor - UINT64_C(0x18000)) & low_mask(64));
    emit(vmem(0, 3, 2, 11));
  } else if (kind == 61) {
    emit(UINT64_C(0x400025b7));
    fault_pc = GVA + ((cursor - UINT64_C(0x18000)) & low_mask(64));
    emit(vmem(0, 3, 2, 11, 0, 1));
  } else if (kind == 56) {
    emit(addi(13, 10, 64));
    emit(vmem(0, 3, 4, 13));
    fault_pc = GVA + ((cursor - UINT64_C(0x18000)) & low_mask(64));
    emit(vmem(0, 3, 2, 10, 1));
  } else if (kind == 57) {
    emit(vmem(0, 3, 2, 10));
    emit(UINT64_C(0x400025b7));
    emit(addi(11, 11, -8));
    fault_pc = GVA + ((cursor - UINT64_C(0x18000)) & low_mask(64));
    emit(vmem(1, 3, 2, 11));
  } else if ((kind == 55 || kind == 58)) {
    emit(UINT64_C(0x400025b7));
    emit(addi(11, 11, -8));
    emit(vmem(0, 3, 2, 11, 0, kind == 58));
  } else {
    emit(vmem(0, ((kind == 50 || kind == 54)) ? 2 : 3, 2, 10));
    if ((kind == 50 || kind == 54)) {
      fault_pc = GVA + ((cursor - UINT64_C(0x18000)) & low_mask(64));
      emit(vadd(2, 2, 2, 1));
    } else if (kind == 49)
      emit(vadd(2, 2, 1, 3)); // vadd.vi, 1
  }
  if (kind == 59) {
    for (int repeat_index = 0; repeat_index < (64); ++repeat_index)
      emit(UINT64_C(19));
    emit(UINT64_C(111));
  } else {
    emit(addi(13, 10, 128));
    emit(vmem(1, ((kind == 50 || kind == 54)) ? 2 : 3, 2, 13));
    emit(UINT64_C(0xc2002673));
    emit(sd(12, 10, 144)); // vl after fault-first
    emit(UINT64_C(0x802673));
    emit(sd(12, 10, 152)); // vstart after completion
    emit(UINT64_C(115));
    emit(UINT64_C(111));
  }
  cursor = UINT64_C(12288);
  emit(UINT64_C(0x20a37));
  save_csr(UINT64_C(322), 0);
  save_csr(UINT64_C(323), 8);
  save_csr(UINT64_C(1603), 16);
  save_csr(UINT64_C(1610), 24);
  save_csr(UINT64_C(1536), 32);
  save_csr(UINT64_C(321), 40);
  save_csr(UINT64_C(256), 96);
  save_csr(UINT64_C(512), 104);
  if (kind != 52)
    save_csr(UINT64_C(8), 112);
  if (kind == 59) {
    emit(addi(13, 20, 256));
    emit(vmem(1, 3, 2, 13)); // observes shared VRF after IRQ drain
    emit(UINT64_C(0xc2002673));
    emit(sd(12, 20, 272));
  }
  emit(addi(21, 0, 85));
  emit(sd(21, 20, 48));
  if (restart) {
    constant(22, UINT64_C(53392));
    constant(23, (UINT64_C(28) << 10) | UINT64_C(223));
    emit(sd(23, 22, 0));
    emit(UINT64_C(0x62000073)); // repair G mapping and invalidate
    constant(22, UINT64_C(0x19000));
    emit(sd(0, 22, 0));         // element 0 must not execute again
    emit(UINT64_C(0x10200073)); // retry exactly sepc, retaining vstart=1
  } else
    emit(UINT64_C(111));
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 50000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1);
  if ((kind == 51 || kind == 52 || kind == 53 || kind == 54)) {
    CHECK(read64(SIGNATURE) == 2 && read64(SIGNATURE + 40) == fault_pc);
  } else if ((kind == 56 || kind == 57 || kind == 60 || kind == 61)) {
    CHECK(read64(SIGNATURE) == (kind == 57 ? 23 : 21) &&
          read64(SIGNATURE + 8) ==
              (kind == 60 ? UINT64_C(0x80000000) : GVA + UINT64_C(8192)) &&
          read64(SIGNATURE + 16) ==
              (kind == 60 ? UINT64_C(11264) : UINT64_C(18432)) &&
          read64(SIGNATURE + 24) == (kind == 60 ? UINT64_C(12288) : 0) &&
          read64(SIGNATURE + 40) == fault_pc &&
          read64(SIGNATURE + 112) == ((kind == 60 || kind == 61) ? 0 : 1) &&
          (read64(SIGNATURE + 32) & UINT64_C(64)) != 0);
    if (kind == 57)
      CHECK(read64(UINT64_C(0x19ff8)) == 11 && read64(UINT64_C(0x19080)) == 0);
  } else if (kind == 59) {
    CHECK(read64(SIGNATURE) == UINT64_C(0x8000000000000005) &&
          read64(SIGNATURE + 256) == 11 && read64(SIGNATURE + 264) == 22 &&
          read64(SIGNATURE + 272) == 2);
  } else {
    CHECK(read64(SIGNATURE) == 10);
    if (kind == 50) {
      CHECK(read64(UINT64_C(0x19080)) == UINT64_C(0x4080000040000000) &&
            read64(UINT64_C(0x19088)) == UINT64_C(0x4100000040c00000));
    } else {
      CHECK(read64(UINT64_C(0x19080)) == (kind == 49 ? 12 : 11) &&
            read64(UINT64_C(0x19088)) == (kind == 58   ? 0
                                          : kind == 49 ? 23
                                                       : 22));
    }
    CHECK(read64(UINT64_C(0x19090)) == (kind == 58   ? 1
                                        : kind == 50 ? 4
                                                     : 2) &&
          read64(UINT64_C(0x19098)) == 0);
    CHECK((read64(SIGNATURE + 96) & UINT64_C(1536)) == UINT64_C(1536) &&
          (read64(SIGNATURE + 104) & UINT64_C(1536)) == UINT64_C(1536));
  }
  if (restart) {
    for (int limit = 0; limit < 50000; limit++) {
      falling();
      if (traps == 2)
        break;
    }
    CHECK(traps == 2 && read64(SIGNATURE) == 10 &&
          read64(UINT64_C(0x19080)) == 11 && read64(UINT64_C(0x19088)) == 22 &&
          read64(UINT64_C(0x19098)) == 0 && flushes >= 3);
  }
}

void run_cmo(int op, int mode, int policy) {
  bool guest, user_mode, illegal, virt, force_flush;
  std::uint32_t instruction;
  std::uint64_t m, h, s, operation_pc;
  guest = (mode == 1 || mode == 2);
  user_mode = (mode == 2 || mode == 3);
  m = (policy == 1 || policy == 7) ? 1
      : policy == 6                ? UINT64_C(209)
                                   : UINT64_C(241);
  h = (policy == 2 || policy == 7 || policy == 8) ? 1
      : policy == 4                               ? UINT64_C(209)
                                                  : UINT64_C(241);
  s = policy == 3 ? 1 : policy == 5 ? UINT64_C(209) : UINT64_C(241);
  illegal = (policy == 1 || policy == 7) || (mode == 3 && policy == 3);
  virt = guest && ((policy == 2 || policy == 8) || (mode == 2 && policy == 3));
  force_flush = op == 0 && (policy == 6 || (guest && policy == 4) ||
                            (user_mode && policy == 5));
  instruction = (field(((op == 3 ? 4 : op) & low_mask(12)), 12, 20) |
                 field(UINT64_C(10), 5, 15) | field(UINT64_C(2), 3, 12) |
                 field(UINT64_C(0), 5, 7) | field(UINT64_C(15), 7, 0));
  falling();
  prepare(0);
  reset = 1;
  scenario = 100 + op * 40 + mode * 10 + policy;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  csrw(UINT64_C(778), m);
  csrw(UINT64_C(1546), h);
  csrw(UINT64_C(266), s);
  csrw(UINT64_C(640), UINT64_C(0x8000000000000008));
  csrw(UINT64_C(1664), UINT64_C(0x8000000000000004));
  csrw(UINT64_C(768),
       (guest ? UINT64_C(0x8000000000) : 0) | (user_mode ? 0 : UINT64_C(2048)));
  csrw(UINT64_C(833), guest ? GVA : UINT64_C(20480));
  emit(UINT64_C(0x30200073));
  if (guest && user_mode) {
    write64(UINT64_C(40960), (UINT64_C(16) << 10) | UINT64_C(219));
    write64(UINT64_C(40968), (UINT64_C(17) << 10) | UINT64_C(215));
  }
  write64(UINT64_C(0x19000), UINT64_C(0x12345678));
  write64(UINT64_C(0x19038), UINT64_C(0xabcdef));
  cursor = guest ? UINT64_C(0x18000) : UINT64_C(20480);
  emit(guest ? UINT64_C(0x40001537) : UINT64_C(0x19537)); // data base
  emit(addi(11, 0, UINT64_C(102)));
  emit(sd(11, 10, 128));
  emit(addi(10, 10, 7));
  if (policy == 8)
    emit(UINT64_C(0x40002537)); // denial precedes missing guest translation
  operation_pc =
      (guest ? GVA : UINT64_C(20480)) +
      ((cursor - (guest ? UINT64_C(0x18000) : UINT64_C(20480))) & low_mask(64));
  emit(instruction);
  emit(UINT64_C(0x840000f)); // fence i,o: FIOM requires memory as well
  emit(guest ? UINT64_C(0x40001537) : UINT64_C(0x19537));
  emit(sd(11, 10, 136)); // younger observable effect
  emit(UINT64_C(115));
  emit(UINT64_C(111));
  cursor = UINT64_C(12288);
  emit(UINT64_C(0x20a37));
  save_csr(UINT64_C(322), 0);
  save_csr(UINT64_C(323), 8);
  save_csr(UINT64_C(321), 40);
  emit(addi(21, 0, 85));
  emit(sd(21, 20, 48));
  emit(UINT64_C(111));
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1);
  if (illegal || virt) {
    CHECK(read64(SIGNATURE) == (illegal ? 2 : 22) &&
          read64(SIGNATURE + 8) == ((instruction)&low_mask(64)) &&
          read64(SIGNATURE + 40) == operation_pc && cmo_count == 0 &&
          read64(UINT64_C(0x19088)) == 0 &&
          read64(UINT64_C(0x19000)) == UINT64_C(0x12345678) &&
          read64(UINT64_C(0x19038)) == UINT64_C(0xabcdef));
  } else {
    CHECK(read64(SIGNATURE) == (user_mode ? 8
                                : guest   ? 10
                                          : 9) &&
          cmo_count == 1 &&
          last_cmo == (op == 3       ? 6
                       : force_flush ? 9
                                     : 7 + op) &&
          read64(UINT64_C(0x19088)) == UINT64_C(102));
    CHECK(read64(UINT64_C(0x19000)) == (op == 3 ? 0 : UINT64_C(0x12345678)) &&
          read64(UINT64_C(0x19038)) == (op == 3 ? 0 : UINT64_C(0xabcdef)));
  }
}

void run_pbmt(int vs_type, int g_type, int pte_type, std::uint8_t vs_paged = 1,
              std::uint8_t g_paged = 1, std::uint8_t vectors = 0,
              std::uint8_t enable_m = 1, std::uint8_t enable_h = 1) {
  std::uint64_t cause, entry, data_va;
  bool bad_vs, bad_g, bad_pte;
  falling();
  prepare(0);
  reset = 1;
  scenario = 80;
  expected_type = vs_paged && vs_type != 0 ? vs_type : g_paged ? g_type : 0;
  expected_pte_type = g_paged ? pte_type : 0;
  pbmt_code = g_paged ? UINT64_C(0x18000) : UINT64_C(0x10000);
  pbmt_data = g_paged ? UINT64_C(0x19000) : UINT64_C(0x11000);
  entry = vs_paged ? GVA : UINT64_C(0x10000);
  data_va = vs_paged ? GVA + UINT64_C(4096) : UINT64_C(0x11000);
  bad_vs =
      vs_paged && (vs_type == 3 || (vs_type != 0 && !(enable_m && enable_h)));
  bad_g = g_paged && (g_type == 3 || (g_type != 0 && !enable_m));
  bad_pte =
      vs_paged && g_paged && (pte_type == 3 || (pte_type != 0 && !enable_m));
  cause = bad_pte ? 20 : bad_vs ? 12 : bad_g ? 20 : 10;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  csrw(UINT64_C(778), enable_m ? UINT64_C(0x4000000000000000) : 0);
  csrw(UINT64_C(1546), enable_h ? UINT64_C(0x4000000000000000) : 0);
  csrw(UINT64_C(640), vs_paged ? UINT64_C(0x8000000000000008) : 0);
  csrw(UINT64_C(1664), g_paged ? UINT64_C(0x8000000000000004) : 0);
  csrw(UINT64_C(512), UINT64_C(1536));
  csrw(UINT64_C(768), UINT64_C(0x8000000e00));
  csrw(UINT64_C(833), entry);
  emit(UINT64_C(0x30200073));
  write64(UINT64_C(40960),
          read64(UINT64_C(40960)) | (((vs_type)&low_mask(64)) << 61));
  write64(UINT64_C(40968),
          read64(UINT64_C(40968)) | (((vs_type)&low_mask(64)) << 61));
  write64(UINT64_C(53376),
          read64(UINT64_C(53376)) | (((g_type)&low_mask(64)) << 61));
  write64(UINT64_C(53384),
          read64(UINT64_C(53384)) | (((g_type)&low_mask(64)) << 61));
  for (int p = UINT64_C(53312); p <= UINT64_C(53328); p += 8)
    write64(p, read64(((p)&low_mask(64))) | (((pte_type)&low_mask(64)) << 61));
  write64(int(pbmt_data), UINT64_C(4660));
  write64(int(pbmt_data + 8), UINT64_C(22136));
  cursor = int(pbmt_code);
  emit(vs_paged ? UINT64_C(0x40001537) : UINT64_C(0x11537));
  if (vectors) {
    emit(addi(11, 0, 2));
    emit(vset(3));
    emit(vmem(0, 3, 2, 10));
    emit(addi(10, 10, 32));
    emit(vmem(1, 3, 2, 10));
  } else {
    emit(ld(11, 10, 0));
    emit(ld(12, 10, 8));
    emit(sd(11, 10, 32));
    emit(sd(12, 10, 40));
  }
  emit(UINT64_C(115));
  emit(UINT64_C(111));
  pbmt_check = 1;
  pbmt_data_reads = 0;
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1 && read64(SIGNATURE) == cause);
  if (cause == 10)
    CHECK(read64(pbmt_data + 32) == UINT64_C(4660) &&
          read64(pbmt_data + 40) == UINT64_C(22136) && pbmt_data_reads >= 2);
  else
    CHECK(read64(SIGNATURE + 40) == entry && pbmt_data_reads == 0);
  pbmt_check = 0;
}

// Execute real tagged accesses through nested translation. Different source
// controls deliberately disagree, so selecting the host's mask cannot pass.
void run_pointer_mask(int kind, int mode, std::uint8_t vs_paged = 1,
                      std::uint8_t g_paged = 1, int fault = 0,
                      std::uint8_t vectors = 0) {
  bool normal_guest, target_user, caller_user, mprv, machine_caller;
  std::uint64_t data_va, data_pa, code_pa, code_va, tagged_address, cause,
      fault_pc, mask_bits;
  normal_guest = kind < 2;
  target_user = (kind == 1 || kind == 3 || kind == 5 || kind == 7);
  caller_user = kind == 3;
  mprv = (kind == 4 || kind == 5);
  machine_caller = mprv || kind == 8;
  data_va = vs_paged ? GVA + UINT64_C(4096) : UINT64_C(0x11000);
  data_pa = g_paged ? UINT64_C(0x19000) : UINT64_C(0x11000);
  code_pa = normal_guest ? (g_paged ? UINT64_C(0x18000) : UINT64_C(0x10000))
                         : UINT64_C(20480);
  code_va =
      normal_guest ? (vs_paged ? GVA : UINT64_C(0x10000)) : UINT64_C(20480);
  mask_bits =
      mode == 2 ? UINT64_C(0xfe00000000000000) : UINT64_C(0xabcd000000000000);
  tagged_address = data_va | mask_bits | (fault == 1 ? 1 : 0);
  cause = fault == 1                ? 4
          : fault != 0 || kind == 6 ? (vs_paged  ? 13
                                       : g_paged ? 21
                                                 : 5)
          : normal_guest            ? (target_user ? 8 : 10)
          : caller_user             ? 8
                                    : 9;
  falling();
  prepare(0);
  reset = 1;
  scenario = 90;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  csrw(UINT64_C(640), vs_paged ? UINT64_C(0x8000000000000008) : 0);
  csrw(UINT64_C(1664), g_paged ? UINT64_C(0x8000000000000004) : 0);
  csrw(UINT64_C(1546), target_user ? 0 : ((mode)&low_mask(64)) << 32);
  csrw(UINT64_C(266),
       target_user && !caller_user ? ((mode)&low_mask(64)) << 32 : 0);
  csrw(UINT64_C(1536), (caller_user ? ((mode)&low_mask(64)) << 48 : 0) |
                           (target_user ? 0 : UINT64_C(256)) | UINT64_C(512));
  csrw(UINT64_C(512), UINT64_C(1536) | (fault == 3 ? UINT64_C(0x80000) : 0));
  constant(10, tagged_address);
  constant(11, 2);
  constant(29, UINT64_C(0x20000));
  csrw(UINT64_C(768),
       (normal_guest || mprv ? UINT64_C(0x8000000000) : 0) |
           (machine_caller                                 ? UINT64_C(6144)
            : caller_user || (normal_guest && target_user) ? 0
                                                           : UINT64_C(2048)) |
           UINT64_C(1536) | (fault == 2 ? UINT64_C(0x80000) : 0));
  csrw(UINT64_C(833), code_va);
  emit(UINT64_C(0x30200073));
  if (normal_guest && target_user)
    write64(UINT64_C(40960), (UINT64_C(16) << 10) | UINT64_C(219));
  write64(UINT64_C(40968),
          (UINT64_C(17) << 10) |
              (target_user ? UINT64_C(223)
                           : UINT64_C(207))); // HLVX uses X, but never masks
  write64(int(data_pa), UINT64_C(4660));
  write64(int(data_pa + 8), UINT64_C(22136));
  write64(int(data_pa + 32), 0);
  write64(int(data_pa + 40), 0);
  cursor = int(code_pa);
  if (mprv) {
    // Set effective VS/VU only after entering M, with no host data access
    // until the explicitly cleared MPRV after the tested operations.
    constant(1, UINT64_C(0x8000020000) | (target_user ? 0 : UINT64_C(2048)) |
                    UINT64_C(1536) | (fault == 2 ? UINT64_C(0x80000) : 0));
    emit(UINT64_C(0x30009073));
  }
  fault_pc = code_va + ((cursor - int(code_pa)) & low_mask(64));
  if (vectors) {
    emit(vset(3));
    fault_pc += 4;
    emit(vmem(0, 3, 2, 10));
    emit(addi(10, 10, 32));
    emit(vmem(1, 3, 2, 10));
  } else if (normal_guest || mprv) {
    emit(fault == 1 ? misaligned_atomic(0) : ld(12, 10, 0));
    emit(ld(13, 10, 8));
    emit(sd(12, 10, 32));
    emit(sd(13, 10, 40));
  } else {
    emit(guest_load(kind == 6 ? 2 : 3, kind == 6 ? 3 : 0));
    emit(addi(11, 12, 0));
    if (kind == 2 && fault == 0) {
      // Change HS-owned policy between guest transactions; the younger HSV
      // must capture the new mode after the serializing CSR restart.
      csrw(UINT64_C(1546), ((mode == 2 ? 3 : 2) & low_mask(64)) << 32);
      constant(10, (data_va + 32) | (mode == 2 ? UINT64_C(0xabcd000000000000)
                                               : UINT64_C(0xfe00000000000000)));
    } else
      emit(addi(10, 10, 32));
    emit(guest_store(3));
  }
  if (machine_caller) {
    if (mprv)
      emit(UINT64_C(0x300eb073)); // clear MPRV without a host memory operand
    csrw(UINT64_C(768), UINT64_C(2048));
    csrw(UINT64_C(833), UINT64_C(24576));
    emit(UINT64_C(0x30200073));
    cursor = UINT64_C(24576);
  }
  emit(UINT64_C(115));
  emit(UINT64_C(111));
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1 && read64(SIGNATURE) == cause);
  if (fault != 0 || kind == 6) {
    CHECK(read64(SIGNATURE + 8) ==
              (fault == 1 ? data_va + 1 : tagged_address) &&
          read64(SIGNATURE + 40) == fault_pc && read64(data_pa + 32) == 0);
  } else {
    CHECK(read64(data_pa + 32) == UINT64_C(4660) &&
          (!(normal_guest || mprv) || read64(data_pa + 40) == UINT64_C(22136)));
  }
}

void run_state_enable(int target, std::uint8_t machine_allow,
                      std::uint8_t guest_allow, std::uint8_t user_mode,
                      std::uint8_t write_access) {
  int address, index;
  std::uint32_t instruction;
  std::uint64_t gate, expected_cause;
  bool allowed;
  address = target == 0   ? UINT64_C(266)
            : target == 1 ? UINT64_C(270)
                          : UINT64_C(1546);
  index = target == 1 ? 2 : 0;
  gate =
      target == 1 ? UINT64_C(0x8000000000000000) : UINT64_C(0x4000000000000000);
  allowed = machine_allow && guest_allow && !user_mode && target != 2;
  expected_cause = allowed ? 10 : machine_allow ? 22 : 2;
  falling();
  prepare(0);
  reset = 1;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  csrw(UINT64_C(640), UINT64_C(0x8000000000000008));
  csrw(UINT64_C(1664), UINT64_C(0x8000000000000004));
  csrw(UINT64_C(266), 0);
  csrw(UINT64_C(1548) + index, guest_allow ? gate : 0);
  csrw(UINT64_C(780) + index, machine_allow ? gate : 0);
  csrw(UINT64_C(768),
       user_mode ? UINT64_C(0x8000000000) : UINT64_C(0x8000000800));
  csrw(UINT64_C(833), GVA);
  emit(UINT64_C(0x30200073));
  if (user_mode) {
    write64(UINT64_C(40960), (UINT64_C(16) << 10) | UINT64_C(219));
    write64(UINT64_C(40968), (UINT64_C(17) << 10) | UINT64_C(215));
  }
  instruction = (field(((address)&low_mask(12)), 12, 20) |
                 field((write_access ? UINT64_C(14) : UINT64_C(0)), 5, 15) |
                 field((write_access ? UINT64_C(1) : UINT64_C(2)), 3, 12) |
                 field(UINT64_C(12), 5, 7) | field(UINT64_C(115), 7, 0));
  cursor = UINT64_C(0x18000);
  emit(UINT64_C(0x40001537));
  emit(addi(14, 0, 1));
  emit(instruction);
  emit(addi(13, 0, 1));
  emit(sd(13, 10, 32));
  emit(UINT64_C(115));
  emit(UINT64_C(111));
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1 && !virtualized && read64(SIGNATURE) == expected_cause &&
        read64(SIGNATURE + 40) == GVA + (allowed ? 20 : 8) &&
        read64(SIGNATURE + 8) == (allowed ? 0 : ((instruction)&low_mask(64))) &&
        read64(UINT64_C(0x19020)) == (allowed ? 1 : UINT64_C(65261)));
}

void run_invalidation(int kind, std::uint8_t batch) {
  std::uint32_t invalidate;
  falling();
  prepare(0);
  reset = 1;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  csrw(UINT64_C(640), UINT64_C(0x8000000000000008));
  csrw(UINT64_C(1664), UINT64_C(0x8000000000000004));
  csrw(UINT64_C(768), kind == 0 ? UINT64_C(0x8000000800) : UINT64_C(2048));
  csrw(UINT64_C(1536),
       UINT64_C(256)); // HS explicit loads use VS permissions (SPVP=S)
  csrw(UINT64_C(833), kind == 0 ? GVA : UINT64_C(20480));
  emit(UINT64_C(0x30200073));
  // New guest data page and a guest-writable alias of its own leaf PTE page.
  write64(UINT64_C(53392), (UINT64_C(28) << 10) | UINT64_C(223));
  write64(UINT64_C(53400), (UINT64_C(10) << 10) | UINT64_C(223));
  write64(UINT64_C(40976), (UINT64_C(19) << 10) | UINT64_C(199));
  write64(UINT64_C(0x1c000), UINT64_C(22136));
  invalidate = kind == 0   ? UINT64_C(0x16000073)
               : kind == 1 ? UINT64_C(0x26000073)
                           : UINT64_C(0x66000073);
  cursor = kind == 0 ? UINT64_C(0x18000) : UINT64_C(20480);
  emit(UINT64_C(0x40001537)); // data VA in x10
  if (kind == 0) {
    emit(ld(12, 10, 0));
    emit(sd(12, 10, 8));        // warm old composed translation
    emit(UINT64_C(0x40002737)); // guest alias of leaf PTE page in x14
    emit(UINT64_C(22455));
    emit(addi(15, 15,
              ((UINT64_C(18) << 10) | UINT64_C(199)) -
                  UINT64_C(20480))); // PTE -> GPA 0x12000
    emit(sd(15, 14, 8));
  } else {
    emit(UINT64_C(0x20a37)); // signature in x20
    emit(guest_load(3, 0));
    emit(sd(12, 20, 128));
    constant(22, kind == 1 ? UINT64_C(40968) : UINT64_C(53384));
    constant(23, kind == 1 ? (UINT64_C(18) << 10) | UINT64_C(199)
                           : (UINT64_C(28) << 10) | UINT64_C(223));
    emit(sd(23, 22, 0));
  }
  emit(UINT64_C(0x18000073)); // SFENCE.W.INVAL after delayed PTE store
  emit(invalidate);
  if (batch)
    emit(invalidate | (5 << 15) | (3 << 20)); // conservative all-entry scope
  emit(UINT64_C(0x18100073)); // SFENCE.INVAL.IR before new implicit reads
  if (kind == 0) {
    emit(ld(12, 10, 0));
    emit(sd(12, 10, 16));
  } else {
    emit(guest_load(3, 0));
    emit(sd(12, 20, 136));
  }
  emit(UINT64_C(115));
  emit(UINT64_C(111));
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1 && read64(SIGNATURE) == (kind == 0 ? 10 : 9) &&
        flushes >= (batch ? 4 : 3));
  CHECK(kind == 0 ? (read64(UINT64_C(0x19008)) == UINT64_C(4660) &&
                     read64(UINT64_C(0x1c010)) == UINT64_C(22136))
                  : (read64(SIGNATURE + 128) == UINT64_C(4660) &&
                     read64(SIGNATURE + 136) == UINT64_C(22136)));
}

void run_invalidation_denial(int op, std::uint8_t user_mode) {
  std::uint32_t instruction;
  bool allowed;
  switch (op) {
  case 0: {
    instruction = UINT64_C(0x16000073);
  } break;
  case 1: {
    instruction = UINT64_C(0x18000073);
  } break;
  case 2: {
    instruction = UINT64_C(0x18100073);
  } break;
  case 3: {
    instruction = UINT64_C(0x26000073);
  } break;
  case 4: {
    instruction = UINT64_C(0x66000073);
  } break;
  }
  allowed = !user_mode && (op == 1 || op == 2);
  falling();
  prepare(0);
  reset = 1;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  csrw(UINT64_C(640), UINT64_C(0x8000000000000008));
  csrw(UINT64_C(1664), UINT64_C(0x8000000000000004));
  csrw(UINT64_C(1536),
       UINT64_C(0x100000)); // VTVM must not trap the two ordering instructions
  csrw(UINT64_C(768), user_mode ? UINT64_C(0x8000100000)
                                : UINT64_C(0x8000100800)); // TVM also set
  csrw(UINT64_C(833), GVA);
  emit(UINT64_C(0x30200073));
  if (user_mode) {
    write64(UINT64_C(40960), (UINT64_C(16) << 10) | UINT64_C(219));
    write64(UINT64_C(40968), (UINT64_C(17) << 10) | UINT64_C(215));
  }
  cursor = UINT64_C(0x18000);
  emit(UINT64_C(0x40001537));
  emit(instruction);
  emit(sd(0, 10, 32));
  emit(UINT64_C(115));
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1 && read64(SIGNATURE) == (allowed ? 10 : 22) &&
        read64(SIGNATURE + 40) == GVA + (allowed ? 12 : 4) &&
        read64(SIGNATURE + 8) == (allowed ? 0 : ((instruction)&low_mask(64))) &&
        read64(UINT64_C(0x19020)) == (allowed ? 0 : UINT64_C(65261)));
}

void run_sha_fetch(int kind, std::uint8_t delegate_vs) {
  std::uint64_t start_pc, cause, trap_value, fault_gpa, fault_inst;
  bool delegated;
  falling();
  prepare(0);
  reset = 1;
  start_pc = GVA + (kind == 2 ? UINT64_C(0x1ffffe) : UINT64_C(4094));
  cause = kind == 0                  ? 12
          : (kind == 1 || kind == 2) ? 20
          : kind == 3                ? 1
          : (kind == 4 || kind == 5) ? 2
                                     : 10;
  trap_value = kind < 4    ? start_pc + 2
               : kind == 4 ? UINT64_C(32768)
               : kind == 5 ? UINT64_C(11)
                           : 0;
  fault_gpa = kind == 1 ? UINT64_C(0x11000) : kind == 2 ? UINT64_C(45056) : 0;
  fault_inst = kind == 2 ? UINT64_C(12288) : 0;
  delegated = delegate_vs && (cause == 1 || cause == 2 || cause == 12);
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(517), GVA + UINT64_C(768));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  csrw(UINT64_C(1538), delegate_vs ? UINT64_C(0xffffff) : 0);
  csrw(UINT64_C(640), UINT64_C(0x8000000000000008));
  csrw(UINT64_C(1664), UINT64_C(0x8000000000000004));
  csrw(UINT64_C(768), UINT64_C(0x8000000800));
  csrw(UINT64_C(833), start_pc);
  constant(10, GVA + UINT64_C(8192));
  emit(UINT64_C(0x30200073));
  // The second executable page and the handler's independent data page.
  write64(UINT64_C(40968), (UINT64_C(17) << 10) | UINT64_C(203));
  write64(UINT64_C(40976), (UINT64_C(18) << 10) | UINT64_C(199));
  write64(UINT64_C(53392), (UINT64_C(28) << 10) | UINT64_C(223));
  write64(UINT64_C(0x1c020), UINT64_C(65261));
  if (kind == 0)
    write64(UINT64_C(40968), 0);
  if (kind == 1)
    write64(UINT64_C(53384), 0);
  if (kind == 2) {
    // Crossing a VS leaf-table boundary makes the second half's G-stage
    // fault belong to an implicit VS PTE read, not the instruction GPA.
    write64(UINT64_C(45048), (UINT64_C(16) << 10) | UINT64_C(203));
    write64(UINT64_C(36872), (UINT64_C(11) << 10) | 1);
  }
  if (kind == 3)
    write64(UINT64_C(53384),
            (UINT64_C(26) << 10) | UINT64_C(223)); // non-executable PMA
  cursor = UINT64_C(0x18ffe);
  if (kind == 4) {
    ram[cursor] = 0;
    ram[cursor + 1] = UINT64_C(128);
    cursor += 2;
  } else
    emit(kind == 5 ? UINT64_C(11) : addi(12, 0, 85));
  if (kind == 6)
    emit(UINT64_C(115)); // successfully assembled cross-page ADDI
  emit(sd(0, 10, 32));
  emit(UINT64_C(111));
  cursor = UINT64_C(0x18300);
  emit(UINT64_C(0x40002537));
  emit(UINT64_C(0x142026f3));
  emit(sd(13, 10, 128));
  emit(UINT64_C(0x143026f3));
  emit(sd(13, 10, 136));
  emit(UINT64_C(0x141026f3));
  emit(sd(13, 10, 144));
  emit(UINT64_C(115));
  emit(UINT64_C(111));
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1 && read64(SIGNATURE) == (delegated ? 10 : cause));
  if (delegated) {
    CHECK(read64(UINT64_C(0x1c080)) == cause &&
          read64(UINT64_C(0x1c088)) == trap_value &&
          read64(UINT64_C(0x1c090)) == start_pc);
  } else {
    CHECK(read64(SIGNATURE + 8) == trap_value &&
          read64(SIGNATURE + 16) == (fault_gpa >> 2) &&
          read64(SIGNATURE + 24) == fault_inst &&
          read64(SIGNATURE + 40) == start_pc + (kind == 6 ? 4 : 0));
  }
  CHECK(read64(UINT64_C(0x1c020)) == UINT64_C(65261) &&
        (kind != 6 || read64(SIGNATURE + 56) == 85));
}

void run_sha_data(int kind, std::uint8_t delegate_vs) {
  std::uint64_t cause, address;
  falling();
  prepare(0);
  reset = 1;
  cause = kind == 0   ? 13
          : kind == 1 ? 15
          : kind == 2 ? 5
          : kind == 3 ? 7
          : kind == 4 ? 4
                      : 6;
  address = GVA + UINT64_C(4096) + (kind >= 4 ? 1 : 0);
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(517), GVA + UINT64_C(768));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  csrw(UINT64_C(1538), delegate_vs ? UINT64_C(0xffffff) : 0);
  csrw(UINT64_C(640), UINT64_C(0x8000000000000008));
  csrw(UINT64_C(1664), UINT64_C(0x8000000000000004));
  csrw(UINT64_C(768), UINT64_C(0x8000000800));
  csrw(UINT64_C(833), GVA);
  emit(UINT64_C(0x30200073));
  write64(UINT64_C(40976), (UINT64_C(18) << 10) | UINT64_C(199));
  write64(UINT64_C(53392), (UINT64_C(28) << 10) | UINT64_C(223));
  if (kind == 0)
    write64(UINT64_C(40968), 0);
  if (kind == 1)
    write64(UINT64_C(40968), (UINT64_C(17) << 10) | UINT64_C(195));
  if ((kind == 2 || kind == 3))
    scenario = 13; // explicit physical service denial
  cursor = UINT64_C(0x18000);
  emit(UINT64_C(0x40001537));
  if (kind >= 4)
    emit(addi(10, 10, 1));
  emit(kind >= 4               ? misaligned_atomic(bit_slice(kind, 0, 1))
       : bit_slice(kind, 0, 1) ? sd(0, 10, 0)
                               : ld(12, 10, 0));
  emit(sd(0, 10, 32));
  emit(UINT64_C(111));
  cursor = UINT64_C(0x18300);
  emit(UINT64_C(0x40002537));
  emit(UINT64_C(0x142026f3));
  emit(sd(13, 10, 128));
  emit(UINT64_C(0x143026f3));
  emit(sd(13, 10, 136));
  emit(UINT64_C(0x141026f3));
  emit(sd(13, 10, 144));
  emit(UINT64_C(115));
  emit(UINT64_C(111));
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1 && read64(SIGNATURE) == (delegate_vs ? 10 : cause));
  if (delegate_vs) {
    CHECK(read64(UINT64_C(0x1c080)) == cause &&
          read64(UINT64_C(0x1c088)) == address &&
          read64(UINT64_C(0x1c090)) == GVA + (kind >= 4 ? 8 : 4));
  } else {
    CHECK(read64(SIGNATURE + 8) == address && read64(SIGNATURE + 16) == 0 &&
          read64(SIGNATURE + 40) == GVA + (kind >= 4 ? 8 : 4));
  }
  CHECK(read64(UINT64_C(0x19000)) == UINT64_C(4660) &&
        read64(UINT64_C(0x19020)) == UINT64_C(65261));
}

// Sstvala/Ssu64xl and the MMU side of Ssccptr. Real instructions generate
// faults through host translation, not injected CSR exception payloads.
// Move the root across readable PMA regions, including non-executable RAM.
// The SoC main-memory audit separately proves complete coherent RAM coverage.
void run_supervisor(int kind, int root = UINT64_C(32768),
                    std::uint64_t virtual_base = GVA) {
  std::uint64_t cause, value, pc;
  bool user_mode;
  falling();
  prepare(0);
  reset = 1;
  supervisor_root_address = root + int(bit_slice(virtual_base, 30, 9)) * 8;
  user_mode = kind == 10;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  csrw(UINT64_C(384),
       UINT64_C(0x8000000000000000) | ((root >> 12) & low_mask(64)));
  csrw(UINT64_C(768), user_mode ? 0 : UINT64_C(2048));
  csrw(UINT64_C(833), virtual_base);
  constant(10,
           virtual_base + UINT64_C(4096) + ((kind == 4 || kind == 5) ? 1 : 0));
  constant(11, virtual_base + UINT64_C(8192));
  emit(UINT64_C(0x30200073));
  // Identity-map firmware/handler data; map S/U code and data separately.
  write64(root, UINT64_C(207));
  write64(root + int(bit_slice(virtual_base, 30, 9)) * 8,
          (UINT64_C(9) << 10) | 1);
  write64(UINT64_C(36864), (UINT64_C(10) << 10) | 1);
  write64(UINT64_C(40960),
          (UINT64_C(5) << 10) | (user_mode ? UINT64_C(219) : UINT64_C(203)));
  write64(UINT64_C(40968),
          (UINT64_C(25) << 10) | (user_mode ? UINT64_C(215) : UINT64_C(199)));
  write64(UINT64_C(40976), 0);
  if (kind == 0)
    write64(UINT64_C(40968), 0);
  if (kind == 1)
    write64(UINT64_C(40968), (UINT64_C(25) << 10) | UINT64_C(195));
  if ((kind == 2 || kind == 3))
    scenario = 13; // Physical data response denial.
  if (kind == 7)
    write64(UINT64_C(40976),
            (UINT64_C(26) << 10) | UINT64_C(203)); // PMA denies fetch.
  cursor = UINT64_C(20480);
  pc = virtual_base;
  switch (kind) {
  case 0:
  case 2:
  case 4: {
    {
      cause = kind == 0 ? 13 : kind == 2 ? 5 : 4;
      value = virtual_base + UINT64_C(4096) + (kind == 4 ? 1 : 0);
      emit(kind == 4 ? misaligned_atomic(0) : ld(12, 10, 0));
    }
  } break;
  case 1:
  case 3:
  case 5: {
    {
      cause = kind == 1 ? 15 : kind == 3 ? 7 : 6;
      value = virtual_base + UINT64_C(4096) + (kind == 5 ? 1 : 0);
      emit(kind == 5 ? misaligned_atomic(1) : sd(0, 10, 0));
    }
  } break;
  case 6:
  case 7: {
    {
      cause = kind == 6 ? 12 : 1;
      value = virtual_base + UINT64_C(8192);
      pc = value;
      emit(UINT64_C(0x58067)); // jalr x0,x11: jump to a faulting page.
    }
  } break;
  case 8: {
    {
      cause = 2;
      value = UINT64_C(32768);
      ram[cursor] = 0;
      ram[cursor + 1] = UINT64_C(128);
      cursor += 2;
    }
  } break;
  case 9: {
    {
      cause = 2;
      value = UINT64_C(11);
      emit(UINT64_C(11));
    }
  } break;
  case 10: {
    {
      // RV64-only shift and LD/SD in U mode must preserve upper data bits.
      emit(addi(12, 0, 1));
      emit(UINT64_C(0x2861613)); // slli x12,x12,40
      emit(addi(12, 12, UINT64_C(291)));
      emit(sd(12, 10, 8));
      emit(ld(13, 10, 8));
      emit(sd(13, 10, 16));
      cause = 8;
      value = 0;
      pc = virtual_base + 24;
      emit(UINT64_C(115));
    }
  } break;
  default: {
    fail(1, "unknown supervisor test case");
  } break;
  }
  emit(sd(0, 10, 32));
  emit(UINT64_C(111));
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 30000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1 && read64(SIGNATURE) == cause &&
        read64(SIGNATURE + 8) == value && read64(SIGNATURE + 40) == pc);
  CHECK(walks >= 3 && supervisor_root_seen && !virtualized &&
        read64(UINT64_C(0x19020)) == UINT64_C(65261));
  if (user_mode)
    CHECK(read64(UINT64_C(0x19008)) == UINT64_C(0x10000000123) &&
          read64(UINT64_C(0x19010)) == UINT64_C(0x10000000123));
}

// Ordinary unaligned accesses succeed, including noncontiguous physical
// pages. Check load/store round-trips and untouched adjacent bytes in S/VS.
void run_misaligned_access(std::uint8_t guest, int engine,
                           std::uint8_t cross_page) {
  int code, length, offset, source, destination;
  std::uint64_t pc;
  falling();
  prepare(0);
  reset = 1;
  code = guest ? UINT64_C(0x18000) : UINT64_C(20480);
  length = engine == 2 ? 16 : 8;
  offset = cross_page ? UINT64_C(4096) - length + 5 : 1;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  csrw(guest ? UINT64_C(640) : UINT64_C(384), UINT64_C(0x8000000000000008));
  csrw(UINT64_C(1664), guest ? UINT64_C(0x8000000000000004) : 0);
  csrw(UINT64_C(512), UINT64_C(8704));
  csrw(UINT64_C(768), (guest ? UINT64_C(0x8000000000) : 0) | UINT64_C(10752));
  csrw(UINT64_C(833), GVA);
  constant(10, GVA + UINT64_C(4096) + ((offset)&low_mask(64)));
  constant(13, GVA + UINT64_C(8192) + ((offset)&low_mask(64)));
  constant(11, 2);
  emit(UINT64_C(0x30200073));
  if (!guest) {
    write64(UINT64_C(32768), UINT64_C(207));
    write64(UINT64_C(40960), (UINT64_C(5) << 10) | UINT64_C(203));
    write64(UINT64_C(40968), (UINT64_C(25) << 10) | UINT64_C(199));
  }
  write64(UINT64_C(40976),
          ((guest ? UINT64_C(18) : UINT64_C(28)) << 10) | UINT64_C(199));
  write64(UINT64_C(40984),
          ((guest ? UINT64_C(19) : UINT64_C(36)) << 10) | UINT64_C(199));
  write64(UINT64_C(53392), (UINT64_C(28) << 10) | UINT64_C(223));
  write64(UINT64_C(53400), (UINT64_C(36) << 10) | UINT64_C(223));
  for (int index = -1; index <= length; index++) {
    source = offset + index < UINT64_C(4096)
                 ? UINT64_C(0x19000) + offset + index
                 : UINT64_C(0x1c000) + offset + index - UINT64_C(4096);
    destination = offset + index < UINT64_C(4096)
                      ? UINT64_C(0x1c000) + offset + index
                      : UINT64_C(0x24000) + offset + index - UINT64_C(4096);
    ram[source] = ((UINT64_C(49) + index) & low_mask(8));
    ram[destination] = UINT64_C(165);
  }
  cursor = code;
  switch (engine) {
  case 0: {
    {
      emit(ld(12, 10, 0));
      emit(sd(12, 13, 0));
    }
  } break;
  case 1: {
    {
      emit(fld(0, 10, 0));
      emit(fsd(0, 13, 0));
    }
  } break;
  case 2: {
    {
      emit(vset(3));
      emit(vmem(0, 3, 2, 10));
      emit(vmem(1, 3, 2, 13));
    }
  } break;
  default: {
    fail(1, "unknown unaligned access engine");
  } break;
  }
  pc = GVA + ((cursor - code) & low_mask(64));
  emit(UINT64_C(115));
  emit(UINT64_C(111));
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 50000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  CHECK(traps == 1 && read64(SIGNATURE) == (guest ? 10 : 9) &&
        read64(SIGNATURE + 40) == pc);
  for (int index = -1; index <= length; index++) {
    destination = offset + index < UINT64_C(4096)
                      ? UINT64_C(0x1c000) + offset + index
                      : UINT64_C(0x24000) + offset + index - UINT64_C(4096);
    CHECK(ram[destination] == ((index < 0 || index == length)
                                   ? UINT64_C(165)
                                   : ((UINT64_C(49) + index) & low_mask(8))));
  }
}

// Cross-page scalar/FP/vector faults must identify the failing portion,
// while retaining the instruction PC, vector element, and guest GPA.
// Modes: host/VS/G page faults, VS delegation, host/guest PMA rejection,
// and warm read-only second-page translations rejected by stores.
void run_split_fault(int mode, int engine, std::uint8_t store,
                     std::uint8_t first_fault = 0) {
  bool guest, delegated, warm;
  int code;
  std::uint64_t cause, address, pc, sig;
  guest = (mode == 1 || mode == 2 || mode == 3 || mode == 5 || mode == 7);
  delegated = mode == 3;
  warm = mode >= 6;
  cause = (mode == 4 || mode == 5) ? (store ? 7 : 5)
          : mode == 2              ? (store ? 23 : 21)
                                   : (store ? 15 : 13);
  address = GVA + (first_fault ? (engine == 2 ? UINT64_C(8181) : UINT64_C(8189))
                               : UINT64_C(8192));
  code = guest ? UINT64_C(0x18000) : UINT64_C(20480);
  falling();
  prepare(0);
  reset = 1;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), UINT64_C(0xffffff));
  csrw(UINT64_C(1538), delegated ? UINT64_C(0xffffff) : 0);
  csrw(UINT64_C(517), GVA + UINT64_C(768));
  csrw(guest ? UINT64_C(640) : UINT64_C(384), UINT64_C(0x8000000000000008));
  csrw(UINT64_C(1664), guest ? UINT64_C(0x8000000000000004) : 0);
  csrw(UINT64_C(512), UINT64_C(8704));
  csrw(UINT64_C(768), (guest ? UINT64_C(0x8000000000) : 0) | UINT64_C(10752));
  csrw(UINT64_C(833), GVA);
  constant(10, GVA + (engine == 2 ? UINT64_C(8181) : UINT64_C(8189)));
  constant(11, engine == 2 ? 2 : 0);
  constant(14, GVA + UINT64_C(8192));
  constant(15, GVA + UINT64_C(13296));
  emit(UINT64_C(0x30200073));
  if (!guest) {
    write64(UINT64_C(32768), UINT64_C(207));
    write64(UINT64_C(40960), (UINT64_C(5) << 10) | UINT64_C(203));
    write64(UINT64_C(40968), (UINT64_C(25) << 10) | UINT64_C(199));
  }
  write64(UINT64_C(40976), ((guest ? UINT64_C(18) : UINT64_C(28)) << 10) |
                               (warm ? UINT64_C(195) : UINT64_C(199)));
  write64(UINT64_C(53392), (UINT64_C(28) << 10) | UINT64_C(223));
  if ((mode == 0 || mode == 1 || mode == 3))
    write64(first_fault ? UINT64_C(40968) : UINT64_C(40976), 0);
  if (mode == 2)
    write64(first_fault ? UINT64_C(53384) : UINT64_C(53392), 0);
  if (mode == 4)
    write64(UINT64_C(40976),
            (UINT64_C(48) << 10) | UINT64_C(199)); // Unmapped physical RAM.
  if (mode == 5)
    write64(UINT64_C(53392), (UINT64_C(48) << 10) | UINT64_C(223));
  // A separate mapped page lets the VS handler record the fault independently.
  write64(UINT64_C(40984), (UINT64_C(19) << 10) | UINT64_C(199));
  write64(UINT64_C(53400), (UINT64_C(28) << 10) | UINT64_C(223));
  write64(UINT64_C(0x19ff8), UINT64_C(0xdecafbad01234567));
  write64(guest ? UINT64_C(0x1c3f0) : UINT64_C(0x133f0), UINT64_C(0xfeedface));
  cursor = code;
  if (warm)
    emit(ld(12, 14, 0));
  if (engine == 2)
    emit(vset(3));
  if (store && engine == 1)
    emit(fp(UINT64_C(121), 0, 0, 0)); // fmv.d.x f0,x0
  if (store && engine == 2)
    emit(UINT64_C(0x5e003157)); // vmv.v.i v2,0
  pc = GVA + ((cursor - code) & low_mask(64));
  switch (engine) {
  case 0: {
    emit(store ? sd(0, 10, 0) : ld(12, 10, 0));
  } break;
  case 1: {
    emit(store ? fsd(0, 10, 0) : fld(0, 10, 0));
  } break;
  case 2: {
    emit(vmem(store, 3, 2, 10));
  } break;
  default: {
    fail(1, "unknown split engine");
  } break;
  }
  emit(sd(0, 15, 0));
  emit(UINT64_C(111)); // A distinct younger mutation must not escape.
  cursor = UINT64_C(0x18300);
  emit(UINT64_C(0x40003a37));
  save_csr(UINT64_C(322), 128);
  save_csr(UINT64_C(323), 136);
  save_csr(UINT64_C(321), 144);
  emit(UINT64_C(115));
  emit(UINT64_C(111));
  cursor = UINT64_C(12288);
  emit(UINT64_C(0x20a37));
  save_csr(UINT64_C(322), 0);
  save_csr(UINT64_C(323), 8);
  save_csr(UINT64_C(1603), 16);
  save_csr(UINT64_C(1610), 24);
  save_csr(UINT64_C(1536), 32);
  save_csr(UINT64_C(321), 40);
  save_csr(UINT64_C(8), 112);
  emit(addi(21, 0, 85));
  emit(sd(21, 20, 48));
  emit(UINT64_C(111));
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  for (int limit = 0; limit < 50000; limit++) {
    falling();
    if (traps == 1)
      break;
  }
  sig = delegated ? UINT64_C(0x1c080) : SIGNATURE;
  CHECK(traps == 1 && read64(sig) == cause && read64(sig + 8) == address &&
        read64(delegated ? sig + 16 : sig + 40) == pc);
  if (!delegated) {
    CHECK(read64(SIGNATURE + 16) ==
              (mode == 2 ? (first_fault ? (engine == 2 ? UINT64_C(18429)
                                                       : UINT64_C(18431))
                                        : UINT64_C(18432))
                         : 0) &&
          read64(SIGNATURE + 24) == 0);
    if (guest)
      CHECK((read64(SIGNATURE + 32) & UINT64_C(64)) != 0);
  }
  if (engine == 2)
    CHECK(read64(SIGNATURE + 112) == (first_fault ? 0 : 1));
  CHECK(read64(guest ? UINT64_C(0x1c3f0) : UINT64_C(0x133f0)) ==
        UINT64_C(0xfeedface));
  if (store && !first_fault)
    CHECK(ram[UINT64_C(0x19ffd)] == 0 && ram[UINT64_C(0x19ffe)] == 0 &&
          ram[UINT64_C(0x19fff)] == 0);
  else
    CHECK(ram[UINT64_C(0x19ffd)] == UINT64_C(251) &&
          ram[UINT64_C(0x19ffe)] == UINT64_C(202) &&
          ram[UINT64_C(0x19fff)] == UINT64_C(222));
}

// Exercise the retained WB owner with a cache-supplied live reservation.
// wake: 0 = timeout, 1 = early reservation loss, 2 = late loss, 3 = MTIP.
void run_wrs(std::uint8_t guest, std::uint8_t user_mode, std::uint8_t tw,
             std::uint8_t vtw, std::uint8_t short_wait, int wake) {
  std::uint32_t instruction;
  std::uint64_t cause;
  int started, elapsed;
  bool fault_expected;
  reset = 1;
  scenario = 62;
  sstc_case = 0;
  pbmt_check = 0;
  reservation_valid = 1;
  instruction = short_wait ? UINT64_C(0x1d00073) : UINT64_C(0xd00073);
  fault_expected = !short_wait && wake == 0 && (tw || (guest && vtw));
  cause = wake == 3 ? UINT64_C(0x8000000000000007)
          : tw      ? UINT64_C(2)
                    : UINT64_C(22);
  for (int i = 0; i < 196608; i++)
    ram[i] = 0;
  machine_boot();
  csrw(UINT64_C(773), UINT64_C(8192));
  csrw(UINT64_C(261), UINT64_C(12288));
  csrw(UINT64_C(770), (1 << 2) | (1 << 22));
  csrw(UINT64_C(774), 4);
  csrw(UINT64_C(1542), 4);
  csrw(UINT64_C(262), 4);
  csrw(UINT64_C(772),
       128); // MTIP is locally enabled, including while WRS waits.
  csrw(UINT64_C(1536), vtw ? UINT64_C(1) << 21 : 0);
  csrw(UINT64_C(768), (guest ? UINT64_C(1) << 39 : 0) |
                          (user_mode ? 0 : UINT64_C(2048)) |
                          (tw ? UINT64_C(1) << 21 : 0));
  csrw(UINT64_C(833), UINT64_C(20480));
  constant(20, SIGNATURE);
  emit(UINT64_C(0x30200073));
  cursor = UINT64_C(20480);
  emit(UINT64_C(0xc0202b73)); // x22 = instret before WRS
  emit(instruction);
  emit(UINT64_C(0xc0202bf3)); // x23 = instret after WRS
  emit(UINT64_C(0x416b8bb3)); // sub x23, x23, x22
  emit(sd(23, 20, 32));
  emit(addi(21, 0, 1));
  emit(sd(21, 20, 40)); // younger side effect
  emit(sd(21, 20, 48));
  emit(UINT64_C(111));
  for (int machine = 0; machine < 2; machine++) {
    cursor = machine != 0 ? UINT64_C(8192) : UINT64_C(12288);
    emit(machine != 0 ? UINT64_C(0xb0202bf3) : UINT64_C(0xc0202bf3));
    emit(UINT64_C(0x416b8bb3));
    emit(sd(23, 20, 32));
    save_csr(machine != 0 ? UINT64_C(834) : UINT64_C(322), 0);
    save_csr(machine != 0 ? UINT64_C(833) : UINT64_C(321), 8);
    save_csr(machine != 0 ? UINT64_C(835) : UINT64_C(323), 16);
    emit(addi(21, 0, 1));
    emit(sd(21, 20, 48));
    emit(UINT64_C(111));
  }
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    falling();
  reset = 0;
  started = -1;
  for (int step = 0; step < 6500 && read64(SIGNATURE + 48) == 0; step++) {
    falling();
    if (started < 0 && instruction_valid &&
        instruction_address == UINT64_C(20484))
      started = cycle;
    if (started >= 0) {
      elapsed = cycle - started;
      if (wake == 1 && elapsed == 80)
        reservation_valid = 0;
      if (wake == 2 && elapsed == 4300) {
        CHECK(read64(SIGNATURE + 48) == 0 && read64(SIGNATURE + 40) == 0);
        reservation_valid = 0;
      }
      if (wake == 3 && elapsed == 80)
        interrupts.pmachine_utimer = 1;
    }
  }
  CHECK(started >= 0 && read64(SIGNATURE + 48) == 1);
  if (fault_expected || wake == 3) {
    CHECK(read64(SIGNATURE) == cause &&
          read64(SIGNATURE + 8) ==
              (wake == 3 ? UINT64_C(20488) : UINT64_C(20484)) &&
          read64(SIGNATURE + 16) ==
              (wake == 3 ? 0 : ((instruction)&low_mask(64))));
    CHECK(read64(SIGNATURE + 32) == (wake == 3 ? 2 : 1) &&
          read64(SIGNATURE + 40) == 0);
  } else {
    CHECK(read64(SIGNATURE + 32) == 2 && read64(SIGNATURE + 40) == 1);
  }
  if (wake == 0)
    CHECK(cycle - started >= 4096 && cycle - started < 4400);
  reservation_valid = 0;
}

void drive() {
  {
    instruction_word = UINT64_C(19);
    if (instruction_valid && instruction_address + 3 < 196608)
      for (int lane = 0; lane < 4; lane++)
        bit_slice(instruction_word, lane * 8, 8) =
            ram[int(instruction_address) + lane];
    memory_ready = !pending && (cycle % 7 != 2);
    response_valid = pending && delay_left == 0;
    response_data = reply;
    memory_fault = (scenario == 12 && memory_walker &&
                    memory_address == UINT64_C(53328)) ||
                   (scenario == 13 && !memory_walker &&
                    memory_address == UINT64_C(0x19000));
  }
}

void observe() {
  {
    if (reset) {
      defer(cycle, 0);
      defer(delay_left, 0);
      defer(pending, 0);
      defer(reply, 0);
      defer(interrupts, std::remove_cvref_t<decltype(interrupts)>{});
      defer(time_counter, 100);
      stores = 0;
      walks = 0;
      flushes = 0;
      traps = 0;
      supervisor_root_seen = 0;
      cmo_count = 0;
      last_cmo = 0;
      cmo_completed = 0;
    } else {
      defer(cycle, cycle + 1);
      if (pbmt_check && instruction_valid && instruction_address >= pbmt_code &&
          instruction_address < pbmt_code + 128)
        CHECK(instruction_cacheable == (expected_type == 0) &&
              instruction_device == (expected_type == 2));
      if (translation_flush)
        flushes++;
      if (pending && delay_left != 0)
        defer(delay_left, delay_left - 1);
      else if (response_valid && response_ready) {
        defer(pending, 0);
        if (cmo_count != 0)
          cmo_completed = 1;
      }
      if (memory_valid && memory_ready && !memory_fault) {
        if (pbmt_check) {
          if (!memory_walker && memory_address >= pbmt_data &&
              memory_address < pbmt_data + 128) {
            CHECK(memory_pbmt == ((expected_type)&low_mask(2)));
            if (!memory_write)
              pbmt_data_reads++;
          }
          if (memory_walker && memory_address >= UINT64_C(32768) &&
              memory_address < UINT64_C(45056))
            CHECK(memory_pbmt == ((expected_pte_type)&low_mask(2)));
        }
        CHECK(!pending);
        CHECK(memory_address < 196608);
        if (memory_walker) {
          walks++;
          CHECK(!memory_write && bit_slice(memory_address, 0, 3) == 0 &&
                memory_mask == UINT64_C(255));
        }
        if (memory_walker &&
            memory_address == ((supervisor_root_address)&low_mask(64)))
          supervisor_root_seen = 1;
        if (memory_operation >= 6) {
          cmo_count++;
          last_cmo = int(memory_operation);
          CHECK((memory_address & ~UINT64_C(63)) == UINT64_C(0x19000) &&
                read64(UINT64_C(0x19080)) == UINT64_C(102));
          // Model only the external cache-service contract here; production
          // line mutation and CHI effects have their own cache regressions.
          if (memory_operation == 6)
            for (int lane = 0; lane < 64; lane++)
              ram[UINT64_C(0x19000) + lane] = 0;
        }
        if (memory_write) {
          for (int lane = 0; lane < 8; lane++)
            if (bit_slice(memory_mask, lane, 1))
              ram[(int(memory_address) & ~7) + lane] =
                  bit_slice(memory_data, lane * 8, 8);
          stores++;
          if (scenario >= 100 && memory_address == UINT64_C(0x19088))
            CHECK(cmo_completed);
          if (memory_address == SIGNATURE + 48)
            traps++;
          // The marker is younger than the divide, without a result dependency.
          // Raise HS timer delivery while the older FP operation may still run.
          if (scenario == 48 && memory_address == UINT64_C(0x19078))
            defer(interrupts.psupervisor_utimer, 1);
          CHECK(memory_address != UINT64_C(0x21000));
        }
        defer(reply, read64(memory_address & ~UINT64_C(7)));
        defer(delay_left, (((scenario == 59 || (sstc_case && scenario == 37)) &&
                            memory_address == UINT64_C(0x19000)) ||
                           memory_operation >= 6)
                              ? 40
                              : 2 + cycle % 3);
        if (memory_address == UINT64_C(0x19000) && !memory_write &&
            !memory_walker) {
          if (sstc_case)
            defer(time_counter, 101);
          else if (scenario == 59)
            defer(interrupts.psupervisor_utimer, 1);
        }
        defer(pending, 1);
      }
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  reservation_valid = 0;
  interrupts = {};
  time_counter = 0;
  {
    for (int guest = 0; guest < 2; guest++)
      for (int engine = 0; engine < 3; engine++) {
        run_misaligned_access(((guest)&low_mask(1)), engine, 0);
        run_misaligned_access(((guest)&low_mask(1)), engine, 1);
      }
    for (int mode = 0; mode < 8; mode++)
      for (int engine = 0; engine < 3; engine++) {
        if (mode < 6)
          run_split_fault(mode, engine, 0);
        run_split_fault(mode, engine, 1);
        if (mode < 3) {
          run_split_fault(mode, engine, 0, 1);
          run_split_fault(mode, engine, 1, 1);
        }
      }
    for (int kind = 0; kind <= 10; kind++) {
      run_supervisor(kind);
      run_supervisor(kind, UINT64_C(32768), UINT64_C(0xffffffc000000000));
    }
    run_supervisor(10, UINT64_C(0x11000));
    run_supervisor(10, UINT64_C(0x18000));
    run_supervisor(10, UINT64_C(0x1a000));
    run_supervisor(10, UINT64_C(0x1c000));
    run_supervisor(10, UINT64_C(0x2f000));

    for (int user_mode = 0; user_mode < 2; user_mode++) {
      for (int controls = 0; controls < 4; controls++) {
        run_wrs(1, ((user_mode)&low_mask(1)), bit_slice(controls, 0, 1),
                bit_slice(controls, 1, 1), 0, controls == 0 ? 2 : 0);
        run_wrs(1, ((user_mode)&low_mask(1)), bit_slice(controls, 0, 1),
                bit_slice(controls, 1, 1), 1, 0);
      }
      for (int tw = 0; tw < 2; tw++) {
        run_wrs(1, ((user_mode)&low_mask(1)), ((tw)&low_mask(1)), 1, 0, 1);
        run_wrs(1, ((user_mode)&low_mask(1)), ((tw)&low_mask(1)), 1, 0, 3);
      }
    }
    run_wrs(0, 0, 0, 1, 0, 2); // VTW must not constrain HS execution.

    for (int kind = 0; kind < 7; kind++) {
      run_sha_fetch(kind, 0);
      run_sha_fetch(kind, 1);
    }

    for (int kind = 0; kind < 6; kind++) {
      run_sha_data(kind, 0);
      run_sha_data(kind, 1);
    }

    for (int kind = 0; kind < 3; kind++) {
      run_invalidation(kind, 0);
      run_invalidation(kind, 1);
    }
    for (int op = 0; op < 5; op++) {
      run_invalidation_denial(op, 0);
      run_invalidation_denial(op, 1);
    }

    for (int target = 0; target < 3; target++)
      for (int controls = 0; controls < 16; controls++)
        run_state_enable(target, bit_slice(controls, 0, 1),
                         bit_slice(controls, 1, 1), bit_slice(controls, 2, 1),
                         bit_slice(controls, 3, 1));

    run_case(0, 10, 0, 0);
    run_case(1, 13, GVA + UINT64_C(12288), 0);
    run_case(2, 21, GVA + UINT64_C(8192), UINT64_C(0x12000));
    run_case(3, 23, GVA + UINT64_C(8192), UINT64_C(0x12000));
    run_case(4, 20, GVA + UINT64_C(8192), UINT64_C(0x12000));
    run_case(5, 21, UINT64_C(0x80000000), UINT64_C(45056), UINT64_C(12288));
    run_case(6, 23, GVA + UINT64_C(4104), UINT64_C(0x11008));
    run_case(7, 15, GVA + UINT64_C(4104), 0);
    run_case(8, 10, 0, 0);
    run_case(9, 22, UINT64_C(0x22000073), 0);
    run_case(10, 10, 0, 0);
    run_case(11, 10, 0, 0);
    run_case(12, 1, GVA, 0);
    run_case(13, 5, GVA + UINT64_C(4096), 0);
    run_explicit(14, 9);
    run_explicit(15, 9);
    run_explicit(16, 13, GVA + UINT64_C(4096));
    run_explicit(17, 21, GVA + UINT64_C(4096), UINT64_C(0x11000));
    run_explicit(18, 9);
    run_explicit(19, 21, GVA + UINT64_C(4096), UINT64_C(40968),
                 UINT64_C(12288));
    run_explicit(20, 13, GVA + UINT64_C(4096));
    run_explicit(21, 13, GVA + UINT64_C(4096));
    run_explicit(22, 8);
    run_explicit(23, 2, ((guest_load(3, 0)) & low_mask(64)));
    run_explicit(24, 9);
    run_explicit(25, 13, GVA + UINT64_C(4096));
    run_explicit(26, 22, ((guest_load(3, 0)) & low_mask(64)));
    run_explicit(27, 22, ((guest_store(3)) & low_mask(64)));
    run_explicit(28, 21, GVA + UINT64_C(8192), UINT64_C(0x12000));
    run_explicit(29, 23, GVA + UINT64_C(8192), UINT64_C(0x12000));
    run_explicit(30, 23, GVA + UINT64_C(4096), UINT64_C(0x11000));
    run_explicit(31, 9);
    run_explicit(32, 5, GVA + UINT64_C(4096));
    run_explicit(33, 5, GVA + UINT64_C(4096));
    run_explicit(34, 9);
    run_explicit(35, 9);
    run_virtual_interrupt(36);
    run_virtual_interrupt(37);
    run_virtual_interrupt(38);
    run_virtual_interrupt(37, 1);
    run_vector(59, 1);
    for (int kind = 39; kind <= 48; kind++)
      run_fp(kind);
    for (int kind = 49; kind <= 61; kind++)
      run_vector(kind);
    for (int op = 0; op < 4; op++) {
      for (int mode = 0; mode < 4; mode++)
        for (int policy = 0; policy < 8; policy++)
          run_cmo(op, mode, policy);
      run_cmo(op, 1, 8);
    }

    for (int modes = 0; modes < 4; modes++)
      for (int v = 0; v < 3; v++)
        for (int g = 0; g < 3; g++)
          run_pbmt(v, g, 1, ((modes & 1) & low_mask(1)),
                   ((modes >> 1) & low_mask(1)));
    for (int v = 0; v < 3; v++)
      for (int g = 0; g < 3; g++)
        run_pbmt(v, g, 2, 1, 1, 1);
    run_pbmt(1, 0, 0, 1, 1, 0, 1, 0);
    run_pbmt(1, 0, 0, 1, 1, 0, 0, 1);
    run_pbmt(0, 1, 0, 1, 1, 0, 0, 1);
    run_pbmt(0, 0, 1, 1, 1, 0, 0, 1);
    run_pbmt(3, 0, 0);
    run_pbmt(0, 3, 0);
    run_pbmt(0, 0, 3);

    for (int mode = 2; mode <= 3; mode++) {
      for (int kind = 0; kind < 9; kind++)
        run_pointer_mask(kind, mode);
      for (int paging = 0; paging < 3; paging++) {
        run_pointer_mask(0, mode, ((paging & 1) & low_mask(1)),
                         ((paging >> 1) & low_mask(1)));
        run_pointer_mask(1, mode, ((paging & 1) & low_mask(1)),
                         ((paging >> 1) & low_mask(1)), 0, 1);
      }
      run_pointer_mask(0, mode, 1, 1, 0, 1);
      run_pointer_mask(0, mode, 1, 1, 1); // transformed misalignment value
      run_pointer_mask(0, mode, 1, 1, 2); // HS MXR disables guest masking
      run_pointer_mask(1, mode, 1, 1, 3); // VS MXR disables VU masking
      run_pointer_mask(2, mode, 1, 1,
                       3); // VS MXR applies to explicit guest access
    }

    throw Finished{};
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
