// SPDX-License-Identifier: Apache-2.0
#include "processor.h"
#include "mmu.h"
#include "simif.h"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

class ArchMemory final : public simif_t {
 public:
  cfg_t cfg;
  std::map<size_t, processor_t*> harts;
  std::array<char, 0x4000> memory{};
  unsigned instruction_synchronizations = 0;
  char* addr_to_mem(reg_t address) override {
    return address >= 0x1000 && address < 0x5000 ? memory.data() + address - 0x1000 : nullptr;
  }
  bool mmio_load(reg_t, size_t, uint8_t*) override { return false; }
  bool mmio_store(reg_t, size_t, const uint8_t*) override { return false; }
  void sync_instruction_cache() override { ++instruction_synchronizations; }
  void proc_reset(unsigned) override {}
  const cfg_t& get_cfg() const override { return cfg; }
  const std::map<size_t, processor_t*>& get_harts() const override { return harts; }
  const char* get_symbol(uint64_t) override { return nullptr; }
};

static void check_instruction_synchronization(unsigned xlen, bool logged) {
  ArchMemory memory;
  memory.cfg.pmpregions = 0;
  const std::string isa = "rv" + std::to_string(xlen) + "ima_zicsr_zifencei";
  FILE* log = std::tmpfile();
  assert(log);
  processor_t cpu(isa.c_str(), "msu", &memory.cfg, &memory, 0, false, log, std::cerr);
  memory.harts[0] = &cpu;
  cpu.set_max_vaddr_bits(xlen == 64 ? 39 : 32);
  cpu.reset();
  if (logged) cpu.enable_log_commits();
  auto* state = cpu.get_state();
  auto run = [&](uint32_t instruction) {
    std::memcpy(memory.memory.data(), &instruction, sizeof(instruction));
    cpu.get_mmu()->flush_icache();
    state->pc = 0x1000;
    cpu.step(1);
    assert(state->pc == 0x1004);
  };

  run(0x00100093); // ADDI x1, x0, 1 caches the decoded instruction.
  const uint32_t replacement = 0x00200093; // ADDI x1, x0, 2.
  std::memcpy(memory.memory.data(), &replacement, sizeof(replacement));
  cpu.get_mmu()->flush_tlb();
  state->pc = 0x1000;
  cpu.step(1);
  assert(state->XPR[1] == 2); // Translation invalidation must still discard decoded instructions.
  run(0x12000073); // SFENCE.VMA x0, x0.
  for (unsigned csr : {CSR_SATP, CSR_MSTATUS}) {
    state->XPR.write(1, 0);
    run((csr << 20) | (1 << 15) | (1 << 12) | 0x73); // CSRRW x0, csr, x1.
  }
  assert(memory.instruction_synchronizations == 0);
  run(0x00300093); // Cache ADDI x1, x0, 3 before changing its backing bytes.
  std::memcpy(memory.memory.data(), &replacement, sizeof(replacement));
  const uint32_t fence = 0x0000100f;
  std::memcpy(memory.memory.data() + 4, &fence, sizeof(fence));
  state->pc = 0x1004;
  cpu.step(1); // Execute FENCE.I without a test-side decoded-cache flush.
  assert(state->pc == 0x1008);
  assert(memory.instruction_synchronizations == 1);
  state->pc = 0x1000;
  cpu.step(1);
  assert(state->XPR[1] == 2); // FENCE.I must also invalidate the cached decoded instruction.
  cpu.get_mmu()->flush_icache();
  cpu.get_mmu()->flush_tlb();
  assert(memory.instruction_synchronizations == 1);
  std::fclose(log);
}

static void check_architecture(unsigned xlen, bool zicclsm, bool hypervisor, bool logged) {
  ArchMemory memory;
  memory.cfg.pmpregions = 0;
  const std::string isa = "rv" + std::to_string(xlen) + "ima" + (hypervisor ? "h" : "") +
                          "_zicsr" + (zicclsm ? "_zicclsm" : "");
  FILE* log = std::tmpfile();
  assert(log);
  processor_t cpu(isa.c_str(), "msu", &memory.cfg, &memory, 0, false, log, std::cerr);
  memory.harts[0] = &cpu;
  cpu.set_max_vaddr_bits(0);
  cpu.reset();
  if (logged) cpu.enable_log_commits();
  auto* state = cpu.get_state();
  auto run = [&](uint32_t instruction) {
    std::memcpy(memory.memory.data(), &instruction, sizeof(instruction));
    cpu.get_mmu()->flush_icache();
    state->pc = 0x1000;
    cpu.step(1);
    assert(state->prv == PRV_M);
  };
  auto write_csr = [&](unsigned csr, reg_t value) {
    state->XPR.write(1, value);
    run((csr << 20) | (1 << 15) | (1 << 12) | 0x73); // CSRRW x0, csr, x1.
    assert(state->pc == 0x1004);
  };
  auto read_csr = [&](unsigned csr) {
    run((csr << 20) | (2 << 12) | (3 << 7) | 0x73); // CSRRS x3, csr, x0.
    assert(state->pc == 0x1004);
    return state->XPR[3];
  };

  for (unsigned csr : {CSR_MENVCFG, CSR_SENVCFG}) {
    write_csr(csr, MENVCFG_FIOM);
    assert(read_csr(csr) & MENVCFG_FIOM); // FIOM remains writable on a Bare-only S hart.
    write_csr(csr, 0);
    assert(!(read_csr(csr) & MENVCFG_FIOM));
  }
  if (xlen == 32) {
    write_csr(CSR_MEDELEGH, ~reg_t(0));
    assert(read_csr(CSR_MEDELEGH) == 0); // Upstream implements the RV32 high-half CSR.
  }
  if (hypervisor) {
    // H mandates writable software-check/hardware-error delegation even without CFI/Zicntr.
    const reg_t fault_delegation = (reg_t(1) << CAUSE_SOFTWARE_CHECK_FAULT) |
                                  (reg_t(1) << CAUSE_HARDWARE_ERROR_FAULT);
    write_csr(CSR_HEDELEG, fault_delegation);
    assert(read_csr(CSR_HEDELEG) == fault_delegation);
    write_csr(CSR_HEDELEG, 0);
    assert(read_csr(CSR_HEDELEG) == 0);
    assert(read_csr(CSR_HGEIP) == 0 && read_csr(CSR_HGEIE) == 0);
    write_csr(CSR_MIDELEG, ~reg_t(0));
    assert(!(read_csr(CSR_MIDELEG) & MIP_SGEIP));
    // Exercise full-register writes and each walking-one bit, not just reset.
    write_csr(CSR_MIE, ~reg_t(0));
    const reg_t implemented = MIP_MSIP | MIP_MTIP | MIP_MEIP | MIP_SSIP | MIP_STIP | MIP_SEIP | MIP_VS_MASK;
    assert(read_csr(CSR_MIE) == implemented);
    assert(read_csr(CSR_HIE) == MIP_VS_MASK);
    for (unsigned bit = 0; bit < xlen; ++bit) {
      write_csr(CSR_MIE, reg_t(1) << bit);
      assert(read_csr(CSR_MIE) == ((reg_t(1) << bit) & implemented));
      assert(!(read_csr(CSR_HIE) & MIP_SGEIP));
    }
    write_csr(CSR_MIE, 0);
    write_csr(CSR_HIE, ~reg_t(0));
    assert(read_csr(CSR_HIE) == MIP_VS_MASK);
    assert(read_csr(CSR_MIE) == MIP_VS_MASK);
    write_csr(CSR_HIE, MIP_SGEIP);
    assert(read_csr(CSR_MIE) == 0 && read_csr(CSR_HIE) == 0);
    write_csr(CSR_HIE, MIP_VSSIP | MIP_VSTIP);
    assert(read_csr(CSR_MIE) == (MIP_VSSIP | MIP_VSTIP));
    write_csr(CSR_MIE, 0);
  }

  cpu.put_csr(CSR_MTVEC, 0x3000);
  for (unsigned width : {4u, 8u}) {
    if (width > xlen / 8) continue;
    // AMOADD.W/D x3, x2, (x1): aligned operations must still work.
    const uint32_t instruction = (2 << 20) | (1 << 15) | ((width == 4 ? 2 : 3) << 12) | (3 << 7) | 0x2f;
    reg_t value = 123;
    std::memcpy(memory.memory.data() + 0x1000, &value, sizeof(value));
    state->XPR.write(1, 0x2000);
    state->XPR.write(2, 5);
    run(instruction);
    assert(state->pc == 0x1004 && state->XPR[3] == 123);
    std::memcpy(&value, memory.memory.data() + 0x1000, sizeof(value));
    assert(value == 128);
    const auto before = memory.memory;
    state->XPR.write(1, 0x2001);
    state->XPR.write(3, 99);
    run(instruction);
    assert(state->pc == 0x3000);
    assert(cpu.get_csr(CSR_MCAUSE) == (zicclsm ? CAUSE_STORE_ACCESS : CAUSE_MISALIGNED_STORE));
    assert(cpu.get_csr(CSR_MTVAL) == 0x2001 && cpu.get_csr(CSR_MEPC) == 0x1000);
    assert(state->XPR[3] == 99 && memory.memory == before);
  }
  std::fclose(log);
}

static void check_stateen_p1p13(unsigned xlen, bool hypervisor, bool logged) {
  ArchMemory memory;
  memory.cfg.pmpregions = 0;
  const std::string isa = "rv" + std::to_string(xlen) + "ima" + (hypervisor ? "h" : "") + "_zicsr_smstateen";
  FILE* log = std::tmpfile();
  assert(log);
  processor_t cpu(isa.c_str(), "msu", &memory.cfg, &memory, 0, false, log, std::cerr);
  memory.harts[0] = &cpu;
  cpu.set_max_vaddr_bits(0);
  cpu.reset();
  if (logged) cpu.enable_log_commits();
  auto* state = cpu.get_state();
  const unsigned csr = xlen == 32 ? CSR_MSTATEEN0H : CSR_MSTATEEN0;
  const reg_t bit = MSTATEEN0_PRIV113 >> (xlen == 32 ? 32 : 0);
  cpu.put_csr(csr, bit);
  assert(cpu.get_csr(csr) == (xlen == 32 && hypervisor ? bit : 0));
  cpu.put_csr(csr, 0);
  assert(cpu.get_csr(csr) == 0);

  if (xlen == 32 && hypervisor) {
    const uint32_t read_hedelegh = (CSR_HEDELEGH << 20) | (2 << 12) | (3 << 7) | 0x73;
    std::memcpy(memory.memory.data(), &read_hedelegh, sizeof(read_hedelegh));
    cpu.put_csr(CSR_MTVEC, 0x3000);
    for (bool enabled : {false, true}) {
      cpu.put_csr(csr, enabled ? bit : 0);
      cpu.set_privilege(PRV_S, false);
      state->pc = 0x1000;
      cpu.step(1);
      assert(state->pc == (enabled ? 0x1004 : 0x3000));
      assert(state->prv == (enabled ? PRV_S : PRV_M));
      if (!enabled) assert(cpu.get_csr(CSR_MCAUSE) == CAUSE_ILLEGAL_INSTRUCTION);
      else assert(state->XPR[3] == 0);
      cpu.set_privilege(PRV_M, false);
    }
  }
  std::fclose(log);
}

int main() {
  for (unsigned xlen : {32u, 64u})
    for (bool hypervisor : {false, true})
      for (bool logged : {false, true})
        check_stateen_p1p13(xlen, hypervisor, logged);
  for (unsigned xlen : {32u, 64u})
    for (bool logged : {false, true})
      check_instruction_synchronization(xlen, logged);
  for (unsigned xlen : {32u, 64u})
    for (bool zicclsm : {false, true})
      for (bool hypervisor : {false, true})
        for (bool logged : {false, true})
          check_architecture(xlen, zicclsm, hypervisor, logged);
  std::cout << "Spike instruction synchronization, AMO fault policy and zero-GEILEN CSR tests passed\n";
}
