// Exercises patched Spike HPM counting, privilege filtering, CSR writes, and overflow traps.
// SPDX-License-Identifier: Apache-2.0
#include "processor.h"
#include "mmu.h"
#include "simif.h"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

class HpmMemory final : public simif_t {
 public:
  cfg_t cfg;
  std::map<size_t, processor_t*> harts;
  std::array<char, 0x4000> memory{};
  char* addr_to_mem(reg_t address) override {
    return address >= 0x1000 && address < 0x5000 ? memory.data() + address - 0x1000 : nullptr;
  }
  bool mmio_load(reg_t, size_t, uint8_t*) override { return false; }
  bool mmio_store(reg_t, size_t, const uint8_t*) override { return false; }
  void proc_reset(unsigned) override {}
  const cfg_t& get_cfg() const override { return cfg; }
  const std::map<size_t, processor_t*>& get_harts() const override { return harts; }
  const char* get_symbol(uint64_t) override { return nullptr; }
};

static void check_hpm(unsigned xlen, bool hypervisor, bool logged) {
  HpmMemory memory;
  memory.cfg.pmpregions = 0;
  const std::string isa = "rv" + std::to_string(xlen) + "ima" + (hypervisor ? "h" : "") + "_zicsr_zicntr_zihpm_sscofpmf";
  FILE* log = std::tmpfile();
  assert(log);
  processor_t cpu(isa.c_str(), "msu", &memory.cfg, &memory, 0, false, log, std::cerr);
  memory.harts[0] = &cpu;
  cpu.set_max_vaddr_bits(0);
  cpu.reset();
  if (logged) cpu.enable_log_commits();
  auto* state = cpu.get_state();
  auto read = [&](unsigned csr) { return cpu.get_csr(csr); };
  auto write = [&](unsigned csr, reg_t value) { cpu.put_csr(csr, value); };
  auto counter = [&]() {
    return read(CSR_MHPMCOUNTER3) | (xlen == 32 ? read(CSR_MHPMCOUNTER3H) << 32 : 0);
  };
  auto set_counter = [&](reg_t value) {
    write(CSR_MHPMCOUNTER3, value);
    if (xlen == 32) write(CSR_MHPMCOUNTER3H, value >> 32);
  };
  auto selector = [&](reg_t value) {
    write(CSR_MHPMEVENT3, value);
    if (xlen == 32) write(CSR_MHPMEVENT3H, value >> 32);
  };
  auto run = [&](uint32_t instruction = 0x00000013, size_t count = 1) {
    for (size_t i = 0; i < memory.memory.size(); i += 4)
      std::memcpy(memory.memory.data() + i, &instruction, 4);
    cpu.get_mmu()->flush_icache();
    state->pc = 0x1000;
    cpu.step(count);
  };
  auto csr_write = [&](unsigned csr, reg_t value) {
    state->XPR.write(1, value);
    run((csr << 20) | (1 << 15) | (1 << 12) | 0x73);
  };
  auto csr_read = [&](unsigned csr) { run((csr << 20) | (2 << 12) | (2 << 7) | 0x73); };

  selector(2);
  run(0x00000013, 7);
  assert(counter() == 7); // Covers fast cached execution and the logging path.
  csr_write(CSR_MHPMCOUNTER3, 40);
  assert(counter() == 40);
  csr_write(CSR_MHPMEVENT3, 2);
  assert(counter() == 40);
  if (xlen == 32) {
    csr_write(CSR_MHPMCOUNTER3H, 1);
    assert(counter() == (reg_t(1) << 32) + 40);
    csr_write(CSR_MHPMEVENT3H, 0);
    assert(counter() == (reg_t(1) << 32) + 40);
  }
  set_counter(0);
  write(CSR_MCOUNTINHIBIT, 8);
  run();
  assert(counter() == 0);
  // The inhibit value sampled before the CSR write governs that instruction.
  csr_write(CSR_MCOUNTINHIBIT, 0);
  assert(counter() == 0);
  run();
  assert(counter() == 1);
  selector(99);
  assert(read(CSR_MHPMEVENT3) == 0);
  run();
  assert(counter() == 1);
  write(CSR_MCOUNTEREN, ~reg_t(0));
  write(CSR_SCOUNTEREN, ~reg_t(0));
  assert(read(CSR_MCOUNTEREN) == 15 && read(CSR_SCOUNTEREN) == 15);
  write(CSR_MCOUNTINHIBIT, ~reg_t(0));
  assert(read(CSR_MCOUNTINHIBIT) == 13);
  write(CSR_MCOUNTINHIBIT, 0);
  write(CSR_MHPMCOUNTER4, ~reg_t(0));
  write(CSR_MHPMEVENT4, ~reg_t(0));
  assert(read(CSR_MHPMCOUNTER4) == 0 && read(CSR_MHPMEVENT4) == 0);
  selector(0);
  set_counter(123);
  cpu.set_privilege(PRV_U, false);
  csr_read(CSR_HPMCOUNTER3);
  assert(state->prv == PRV_U && state->XPR[2] == 123);
  cpu.set_privilege(PRV_M, false);
  write(CSR_MCOUNTEREN, 0);
  cpu.set_privilege(PRV_S, false);
  csr_read(CSR_HPMCOUNTER3);
  assert(state->prv == PRV_M && read(CSR_MCAUSE) == 2);
  if (hypervisor) {
    write(CSR_MCOUNTEREN, 8);
    write(CSR_HCOUNTEREN, 0);
    cpu.set_privilege(PRV_S, true);
    csr_read(CSR_HPMCOUNTER3);
    assert(read(CSR_MCAUSE) == 22);
    write(CSR_MCOUNTEREN, 0);
    cpu.set_privilege(PRV_S, true);
    csr_read(CSR_HPMCOUNTER3);
    assert(read(CSR_MCAUSE) == 2); // Machine denial outranks guest denial.
  }

  struct Mode { reg_t privilege; bool guest; reg_t filter; };
  for (auto mode : {Mode{PRV_M, false, MHPMEVENT_MINH}, Mode{PRV_S, false, MHPMEVENT_SINH},
                    Mode{PRV_U, false, MHPMEVENT_UINH}, Mode{PRV_S, true, MHPMEVENT_VSINH},
                    Mode{PRV_U, true, MHPMEVENT_VUINH}}) {
    if (mode.guest && !hypervisor) continue;
    cpu.set_privilege(PRV_M, false);
    set_counter(0);
    selector(2 | mode.filter);
    cpu.set_privilege(mode.privilege, mode.guest);
    run();
    assert(counter() == 0);
    cpu.set_privilege(PRV_M, false);
    selector(2);
    cpu.set_privilege(mode.privilege, mode.guest);
    run();
    assert(counter() == 1);
  }
  cpu.set_privilege(PRV_M, false);
  // MRET must be charged to M, not its destination privilege.
  selector(2 | MHPMEVENT_MINH);
  set_counter(0);
  write(CSR_MEPC, 0x2000);
  write(CSR_MSTATUS, reg_t(PRV_S) << 11);
  run(0x30200073);
  assert(state->prv == PRV_S && counter() == 0);
  run();
  assert(counter() == 1);
  // Trapping attempts count only for the functional-cycle event.
  cpu.set_privilege(PRV_M, false);
  write(CSR_MTVEC, 0x2000);
  selector(2);
  set_counter(0);
  run(0x00000073); // ECALL.
  assert(counter() == 0 && read(CSR_MCAUSE) == 11);
  selector(1);
  run(0x00000073);
  assert(counter() == 1);

  selector(2);
  set_counter(~reg_t(0));
  write(CSR_MIP, 0);
  run();
  assert(counter() == 0 && read(CSR_SCOUNTOVF) == 8 && (read(CSR_MIP) & MIP_LCOFIP));
  write(CSR_MIP, 0);
  set_counter(~reg_t(0));
  run();
  assert(counter() == 0 && !(read(CSR_MIP) & MIP_LCOFIP)); // Sticky OF disables repeat IRQ.
  selector(2);
  assert(!(read(CSR_MIP) & MIP_LCOFIP)); // CSR writes cannot generate overflow.
  set_counter(~reg_t(0));
  csr_write(CSR_MIP, 0);
  assert(read(CSR_MIP) & MIP_LCOFIP); // Fresh overflow wins simultaneous clear.

  cpu.set_privilege(PRV_S, false);
  write(CSR_MCOUNTEREN, 0);
  assert(read(CSR_SCOUNTOVF) == 0);
  write(CSR_MCOUNTEREN, 8);
  assert(read(CSR_SCOUNTOVF) == 8);
  if (hypervisor) {
    cpu.set_privilege(PRV_S, true);
    write(CSR_HCOUNTEREN, 0);
    assert(read(CSR_SCOUNTOVF) == 0);
    write(CSR_HCOUNTEREN, 8);
    assert(read(CSR_SCOUNTOVF) == 8);
    write(CSR_HIDELEG, MIP_LCOFIP);
    assert(!(read(CSR_HIDELEG) & MIP_LCOFIP));
  }
  // Both M and delegated S delivery stop a bulk step at the exact boundary.
  for (bool delegated : {false, true}) {
    cpu.set_privilege(PRV_M, false);
    selector(2);
    set_counter(~reg_t(0));
    write(CSR_MIP, 0);
    write(CSR_MIE, MIP_LCOFIP);
    write(CSR_MIDELEG, delegated ? MIP_LCOFIP : 0);
    write(CSR_MTVEC, 0x2000);
    write(CSR_STVEC, 0x2000);
    write(CSR_MSTATUS, MSTATUS_MIE | MSTATUS_SIE);
    if (delegated) cpu.set_privilege(PRV_S, false);
    run(0x00000013, 5);
    assert(counter() == 0 && state->pc == 0x2000);
    assert(read(delegated ? CSR_SEPC : CSR_MEPC) == 0x1004);
    assert(read(delegated ? CSR_SCAUSE : CSR_MCAUSE) == ((reg_t(1) << (xlen - 1)) | 13));
    if (delegated) {
      csr_write(CSR_SIP, 0);
      assert(!(read(CSR_MIP) & MIP_LCOFIP));
    }
  }
  if (hypervisor) {
    for (unsigned cause : {10, 2, 6}) {
      cpu.set_privilege(PRV_M, false);
      const reg_t pending = reg_t(1) << cause;
      write(CSR_MIP, MIP_LCOFIP);
      write(CSR_HVIP, pending);
      write(CSR_MIE, pending | MIP_LCOFIP);
      write(CSR_MIDELEG, MIP_LCOFIP);
      write(CSR_HIDELEG, 0);
      write(CSR_MSTATUS, MSTATUS_SIE);
      cpu.set_privilege(PRV_S, false);
      run();
      assert(read(CSR_SCAUSE) == ((reg_t(1) << 63) | cause));
    }
    cpu.set_privilege(PRV_M, false);
    write(CSR_HVIP, MIP_VSSIP);
    write(CSR_MIP, MIP_LCOFIP);
    write(CSR_MIE, MIP_VSSIP | MIP_LCOFIP);
    write(CSR_HIDELEG, MIP_VSSIP);
    write(CSR_VSSTATUS, MSTATUS_SIE);
    cpu.set_privilege(PRV_S, true);
    run();
    assert(!state->v && read(CSR_SCAUSE) == ((reg_t(1) << 63) | 13)); // HS before VS.
    write(CSR_HVIP, 0);
  }
  cpu.set_privilege(PRV_M, false);
  write(CSR_MIE, 0);
  selector(1);
  set_counter(0);
  run(0x10500073); // WFI retires once; repeated idle steps are not instructions.
  assert(counter() == 1);
  cpu.step(5);
  assert(counter() == 1);
  cpu.reset();
  assert(counter() == 0 && read(CSR_MHPMEVENT3) == 0 && read(CSR_SCOUNTOVF) == 0);
  std::fclose(log);
}

int main() {
  for (bool logged : {false, true}) {
    check_hpm(32, false, logged);
    check_hpm(64, false, logged);
    check_hpm(64, true, logged);
  }
  std::cout << "Spike HPM RV32/RV64/RV64H checks passed (fast and logged execution)\n";
}
