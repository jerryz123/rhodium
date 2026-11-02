// Checks Spike coroutine transport, coherent atomics, cache visibility, and CBO physical access.
// SPDX-License-Identifier: Apache-2.0
#include "spike_core.h"

#include <algorithm>
#include <cassert>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

using rhodium::spike::Configuration;
using rhodium::spike::Inputs;
using rhodium::spike::Outputs;
using rhodium::spike::SpikeCoreModel;

// Exercise both phases of a blocking transaction, including independent snoop traffic.
static void stall_transaction(SpikeCoreModel& model, bool Outputs::*request_valid,
                              bool Inputs::*request_ready, bool Outputs::*response_ready) {
  Inputs inputs;
  for (int cycle = 0; cycle < 4; ++cycle) {
    const auto outputs = model.tick(inputs);
    assert(outputs.*request_valid);
    assert(outputs.*response_ready);
  }
  inputs.*request_ready = true;
  auto outputs = model.tick(inputs);
  assert(!(outputs.*request_valid));
  assert(outputs.*response_ready);
  inputs = Inputs{};
  // An absent line must still receive a snoop reply while the hart waits for memory.
  inputs.snoop_valid = true;
  inputs.snoop_address = 0xffff0000;
  inputs.snoop_invalidate = true;
  outputs = model.tick(inputs);
  assert(outputs.snoop_response_valid);
  assert(!outputs.snoop_response_has_data);
  inputs = Inputs{};
  for (int cycle = 0; cycle < 4; ++cycle) {
    outputs = model.tick(inputs);
    assert(!outputs.address_request_valid && !outputs.instruction_request_valid &&
           !outputs.acquire_request_valid && !outputs.release_request_valid &&
           !outputs.uncached_request_valid);
    assert(outputs.*response_ready);
    assert(outputs.snoop_response_valid);
  }
  inputs.snoop_response_ready = true;
  outputs = model.tick(inputs);
  assert(!outputs.snoop_response_valid);
  assert(outputs.*response_ready);
}

static std::unique_ptr<SpikeCoreModel> check_vector_restart(bool rv64) {
  Configuration configuration;
  configuration.reset_vector = 0x1000;
  configuration.xlen_is_64 = rv64;
  configuration.privilege = "msu";
  configuration.isa = rv64 ? "rv64gcv_zvl256b" : "rv32imac_zicsr_zve32x_zvl64b";
  configuration.vector_length = rv64 ? 256 : 64;
  configuration.vector_element_width = rv64 ? 64 : 32;
  auto model = std::make_unique<SpikeCoreModel>(configuration);
  const std::array<std::uint32_t, 11> program = {
      0x60000093,  // li x1, 0x600 (mstatus.VS)
      0x3000a073,  // csrs mstatus, x1
      0x00400093,  // li x1, 4
      0x0000f057,  // vsetvli x0, x1, e8, m1
      0x0080d073,  // csrwi vstart, 1
      0x020000d7,  // vadd.vv v1, v0, v0: must restart rather than trap
      0x00802173,  // csrr x2, vstart: must now be zero
      0x02a10113,  // addi x2, x2, 42
      0x000020b7,  // lui x1, 2
      rv64 ? 0x0020b023u : 0x0020a023u,  // sd/sw x2, 0(x1)
      0x0000006f,  // j .
  };
  Inputs inputs;
  bool stored = false;
  for (unsigned cycle = 0; cycle < 128; ++cycle) {
    const Outputs outputs = model->tick(inputs);
    inputs = Inputs{};
    if (outputs.address_request_valid) {
      inputs.address_request_ready = true;
      inputs.address_response_valid = true;
      inputs.address_response_cacheable = outputs.address_request_execute;
      inputs.address_response_instruction_cacheable = true;
    }
    if (outputs.instruction_request_valid) {
      assert(outputs.instruction_request_address == 0x1000);
      inputs.instruction_request_ready = true;
      inputs.instruction_response_valid = true;
      std::memcpy(inputs.instruction_response_line.data(), program.data(), sizeof(program));
    }
    if (outputs.uncached_request_valid) {
      assert(outputs.uncached_request_address == 0x2000);
      assert(outputs.uncached_request_write);
      assert(outputs.uncached_request_data == 42);
      stored = true;
      inputs.uncached_request_ready = true;
      inputs.uncached_response_valid = true;
    }
  }
  assert(stored && "vector restart did not reach the result store");
  return model;
}

static std::unique_ptr<SpikeCoreModel> check_store_conditional(bool rv64, bool doubleword,
                                                            bool grants, bool invalidate,
                                                            bool translated = false) {
  Configuration config;
  config.reset_vector = 0x1000;
  config.xlen_is_64 = rv64;
  config.isa = rv64 ? "rv64ima_zicsr" : "rv32ima_zicsr";
  config.privilege = "msu";
  config.max_vaddr_bits = translated ? 39 : 0;
  config.pmp_regions = 0;
  auto model = std::make_unique<SpikeCoreModel>(config);
  std::vector<std::uint32_t> code = {
      0x000020b7,                          // lui x1, 2
      doubleword ? 0x1000b1afu : 0x1000a1afu, // lr.d/w x3, (x1)
      0x02a00113,                          // li x2, 42
      doubleword ? 0x1820b22fu : 0x1820a22fu, // sc.d/w x4, x2, (x1)
      0x000032b7,                          // lui x5, 3
      0x0042a023,                          // sw x4, (x5): report SC status
      0x03700113,                          // li x2, 55
      doubleword ? 0x0020b423u : 0x0020a423u, // sd/sw x2, 8(x1): scope must have ended
      0x0000006f,                          // j .
  };
  if (translated) {
    // A one-entry Sv39 root maps the low GiB; MPRV exercises translated data in M mode.
    code.insert(code.begin(), {0x00400313, 0x00100393, 0x03f39393, 0x00736333,
                              0x18031073, 0x00020337, 0x000013b7, 0x80038393,
                              0x00736333, 0x30031073});
  }
  Inputs inputs;
  bool upgrade_pending = false, snoop_accepted = false;
  unsigned wait = 0, acquires = 0, reports = 0;
  std::uint64_t status = ~std::uint64_t{0};
  for (unsigned cycle = 0; cycle < 128; ++cycle) {
    const auto out = model->tick(inputs);
    snoop_accepted |= out.snoop_response_valid;
    Inputs next;
    next.snoop_response_ready = true;
    if (out.address_request_valid) {
      assert(out.address_request_address != 0);
      next.address_request_ready = next.address_response_valid = true;
      next.address_response_grant_valid = grants;
      next.address_response_grant_base = out.address_request_address & ~std::uint64_t{63};
      next.address_response_grant_limit = next.address_response_grant_base + 63;
      next.address_response_readable = next.address_response_writable = true;
      next.address_response_executable = out.address_request_execute;
      next.address_response_cacheable = out.address_request_address != 0x3000;
      next.address_response_instruction_cacheable = true;
      next.address_response_atomic = true;
    }
    if (out.instruction_request_valid) {
      assert(out.instruction_request_address == 0x1000 ||
             (translated && out.instruction_request_address == 0x1040));
      next.instruction_request_ready = next.instruction_response_valid = true;
      const std::size_t offset = (out.instruction_request_address - 0x1000) / 4;
      const std::size_t count = std::min<std::size_t>(16, code.size() - offset);
      std::memcpy(next.instruction_response_line.data(), code.data() + offset, count * 4);
    }
    if (out.acquire_request_valid) {
      if (out.acquire_request_address == 0x4000) {
        assert(translated && !out.acquire_request_unique);
        next.acquire_request_ready = next.acquire_response_valid = true;
        next.acquire_response_state = 1;
        next.acquire_response_line[0] = 0xcf; // Valid R/W/X leaf with A/D already set.
      } else {
        assert(out.acquire_request_address == 0x2000);
        ++acquires;
        next.acquire_request_ready = true;
        if (out.acquire_request_unique) {
          upgrade_pending = true;
          wait = 3;
        } else {
          next.acquire_response_valid = true;
          next.acquire_response_state = 1;
          next.acquire_response_line[0] = 7;
        }
      }
    }
    if (upgrade_pending) {
      if (wait == 2 && invalidate) {
        next.snoop_valid = next.snoop_invalidate = true;
        next.snoop_address = 0x2000;
      }
      if (wait-- == 0) {
        next.acquire_response_valid = true;
        next.acquire_response_state = 2;
        next.acquire_response_line[0] = invalidate ? 99 : 7;
        upgrade_pending = false;
      }
    }
    if (out.uncached_request_valid) {
      assert(out.uncached_request_write && out.uncached_request_address == 0x3000);
      status = out.uncached_request_data;
      ++reports;
      next.uncached_request_ready = next.uncached_response_valid = true;
    }
    inputs = next;
  }
  assert(reports == 1 && acquires == 2 && snoop_accepted == invalidate);
  assert(invalidate ? status != 0 : status == 0);
  Inputs probe;
  probe.snoop_valid = probe.snoop_invalidate = probe.snoop_return_to_source = true;
  probe.snoop_address = 0x2000;
  const auto final = model->tick(probe);
  assert(final.snoop_response_valid && final.snoop_response_has_data);
  assert(final.snoop_response_line[0] == (invalidate ? 99 : 42));
  assert(final.snoop_response_line[1] == 55);
  return model;
}

int main() {
  std::vector<std::unique_ptr<SpikeCoreModel>> sc_models;
  for (bool rv64 : {false, true})
    for (bool doubleword : {false, true})
      if (rv64 || !doubleword)
        for (bool grants : {false, true})
          for (bool invalidate : {false, true})
            sc_models.push_back(check_store_conditional(rv64, doubleword, grants, invalidate));
  for (bool grants : {false, true})
    for (bool invalidate : {false, true})
      sc_models.push_back(check_store_conditional(true, true, grants, invalidate, true));
  const auto restart32 = check_vector_restart(false);
  const auto restart64 = check_vector_restart(true);
  Configuration configuration;
  assert(configuration.max_retired_instructions_per_cycle == 64);
  configuration.reset_vector = 0x1000;
  configuration.isa = "rv64ima_zicsr_ssccptr_sstvecd_sstvala_sscounterenw_ssu64xl";
  configuration.privilege = "msu";
  configuration.max_vaddr_bits = 39;
  configuration.instruction_cache_sets = 2;
  configuration.instruction_cache_ways = 1;
  configuration.data_cache_sets = 2;
  configuration.data_cache_ways = 1;
  // Configuration fields are assertions about the selected ISA, not hints.
  auto mismatched = configuration;
  mismatched.xlen_is_64 = false;
  bool rejected = false;
  try { SpikeCoreModel invalid(mismatched); }
  catch (const std::invalid_argument&) { rejected = true; }
  assert(rejected);
  auto vector_configuration = configuration;
  vector_configuration.isa = "rv64gcv_zvl256b";
  vector_configuration.vector_length = 256;
  vector_configuration.vector_element_width = 64;
  SpikeCoreModel vector_model(vector_configuration);
  vector_configuration.vector_length = 128;
  rejected = false;
  try { SpikeCoreModel invalid(vector_configuration); }
  catch (const std::invalid_argument&) { rejected = true; }
  assert(rejected);
  SpikeCoreModel model(configuration);

  Inputs inputs;
  Outputs outputs = model.tick(inputs);
  assert(outputs.address_request_valid);
  assert(outputs.address_request_address == 0x1000);
  assert(outputs.address_request_execute);

  outputs = model.tick(inputs);
  assert(outputs.address_request_valid);
  assert(outputs.address_request_address == 0x1000);

  // A response alone cannot advance the coroutine while its request is backpressured.
  inputs.address_response_valid = true;
  inputs.address_response_cacheable = true;
  inputs.address_response_instruction_cacheable = true;
  outputs = model.tick(inputs);
  assert(outputs.address_request_valid);
  assert(!outputs.address_response_ready);
  assert(!outputs.instruction_request_valid);
  inputs = Inputs{};
  for (int cycle = 0; cycle < 4; ++cycle) {
    outputs = model.tick(inputs);
    assert(outputs.address_request_valid);
    assert(outputs.address_request_address == 0x1000);
    assert(!outputs.instruction_request_valid);
  }
  inputs.address_request_ready = true;
  outputs = model.tick(inputs);
  assert(outputs.instruction_request_valid);
  assert(outputs.instruction_request_address == 0x1000);

  stall_transaction(model, &Outputs::instruction_request_valid, &Inputs::instruction_request_ready,
                    &Outputs::instruction_response_ready);
  inputs = Inputs{};
  inputs.instruction_request_ready = true;
  inputs.instruction_response_valid = true;
  constexpr std::array<std::uint32_t, 7> program = {
      0x000020b7,  // lui x1, 0x2
      0x02a00113,  // addi x2, x0, 42
      0x0020b023,  // sd x2, 0(x1)
      0x0000b183,  // ld x3, 0(x1)
      0x08008093,  // addi x1, x1, 128
      0x0020b023,  // sd x2, 0(x1)
      0x0000006f,  // jal x0, 0
  };
  std::uint32_t nop = 0x00000013;
  for (std::size_t index = 0; index < 16; ++index) {
    std::memcpy(reinterpret_cast<std::uint8_t*>(inputs.instruction_response_line.data()) + index * 4,
                &nop, sizeof(nop));
  }
  std::memcpy(inputs.instruction_response_line.data(), program.data(), sizeof(program));
  outputs = model.tick(inputs);
  assert(!outputs.instruction_request_valid);

  std::size_t acquire_count = 0;
  std::size_t release_count = 0;
  inputs = Inputs{};
  for (std::size_t cycle = 0; cycle < 96; ++cycle) {
    outputs = model.tick(inputs);
    Inputs next;
    if (outputs.address_request_valid) {
      stall_transaction(model, &Outputs::address_request_valid, &Inputs::address_request_ready,
                        &Outputs::address_response_ready);
      next.address_request_ready = true;
      next.address_response_valid = true;
      next.address_response_cacheable = true;
      next.address_response_instruction_cacheable = true;
    }
    assert(!outputs.instruction_request_valid);
    if (outputs.acquire_request_valid) {
      assert(outputs.acquire_request_address == (acquire_count == 0 ? 0x2000 : 0x2080));
      assert(outputs.acquire_request_unique);
      ++acquire_count;
      stall_transaction(model, &Outputs::acquire_request_valid, &Inputs::acquire_request_ready,
                        &Outputs::acquire_response_ready);
      next.acquire_request_ready = true;
      next.acquire_response_valid = true;
      next.acquire_response_state = 2;
    }
    if (outputs.release_request_valid) {
      assert(outputs.release_request_address == 0x2000);
      assert(outputs.release_request_state == 4);
      assert(outputs.release_request_line[0] == 42);
      ++release_count;
      stall_transaction(model, &Outputs::release_request_valid, &Inputs::release_request_ready,
                        &Outputs::release_response_ready);
      next.release_request_ready = true;
      next.release_response_valid = true;
    }
    assert(!outputs.uncached_request_valid);
    inputs = next;
  }
  assert(acquire_count == 2);
  assert(release_count == 1);

  assert(outputs.snoop_ready);
  inputs = Inputs{};
  inputs.snoop_valid = true;
  inputs.snoop_address = 0x2080;
  inputs.snoop_invalidate = true;
  inputs.snoop_return_to_source = true;
  outputs = model.tick(inputs);
  assert(!outputs.snoop_ready);
  assert(outputs.snoop_response_valid);
  assert(outputs.snoop_response_state == 0);
  assert(outputs.snoop_response_pass_dirty);
  assert(outputs.snoop_response_has_data);
  assert(outputs.snoop_response_line[0] == 42);

  inputs = Inputs{};
  outputs = model.tick(inputs);
  assert(outputs.snoop_response_valid);
  assert(outputs.snoop_response_line[0] == 42);
  inputs.snoop_response_ready = true;
  outputs = model.tick(inputs);
  assert(!outputs.snoop_response_valid);

  // Spike fetches a 32-bit instruction as two halfwords when C is enabled.
  // Uncached responses retain their position within the selected 64-bit lane.
  Configuration uncached_configuration;
  uncached_configuration.reset_vector = 0x1000;
  uncached_configuration.isa = "rv64imac_zicsr";
  uncached_configuration.privilege = "msu";
  uncached_configuration.instruction_cache_sets = 1;
  uncached_configuration.instruction_cache_ways = 1;
  uncached_configuration.data_cache_sets = 1;
  uncached_configuration.data_cache_ways = 1;
  SpikeCoreModel uncached_model(uncached_configuration);
  Inputs uncached_inputs;
  std::size_t low_half_fetches = 0;
  std::size_t high_half_fetches = 0;
  bool trapped_to_zero = false;
  for (std::size_t cycle = 0; cycle < 128; ++cycle) {
    const Outputs uncached_outputs = uncached_model.tick(uncached_inputs);
    Inputs next;
    if (uncached_outputs.address_request_valid) {
      trapped_to_zero |= uncached_outputs.address_request_address == 0;
      next.address_request_ready = true;
      next.address_response_valid = true;
      next.address_response_instruction_cacheable = true;
    }
    if (uncached_outputs.uncached_request_valid) {
      stall_transaction(uncached_model, &Outputs::uncached_request_valid, &Inputs::uncached_request_ready,
                        &Outputs::uncached_response_ready);
      assert(uncached_outputs.uncached_request_size == 1);
      if (uncached_outputs.uncached_request_address == 0x1000) ++low_half_fetches;
      if (uncached_outputs.uncached_request_address == 0x1002) ++high_half_fetches;
      next.uncached_request_ready = true;
      next.uncached_response_valid = true;
      next.uncached_response_data = 0x000000000000006f;
    }
    uncached_inputs = next;
  }
  assert(!trapped_to_zero);
  assert(low_half_fetches >= 1);
  assert(high_half_fetches >= 1);

  // SFENCE.VMA and translation/privilege CSR writes retain the physical line;
  // FENCE.I invalidates it, forcing exactly one refill before execution continues.
  Configuration fence_configuration;
  fence_configuration.reset_vector = 0x1000;
  fence_configuration.isa = "rv64ima_zicsr_zifencei";
  fence_configuration.privilege = "msu";
  fence_configuration.max_vaddr_bits = 39;
  fence_configuration.instruction_cache_sets = 1;
  fence_configuration.instruction_cache_ways = 1;
  fence_configuration.data_cache_sets = 1;
  fence_configuration.data_cache_ways = 1;
  SpikeCoreModel fence_model(fence_configuration);
  Inputs fence_inputs;
  std::size_t instruction_fills = 0;
  bool fence_trapped_to_zero = false;
  for (std::size_t cycle = 0; cycle < 128; ++cycle) {
    const Outputs fence_outputs = fence_model.tick(fence_inputs);
    Inputs next;
    if (fence_outputs.address_request_valid) {
      fence_trapped_to_zero |= fence_outputs.address_request_address == 0;
      next.address_request_ready = true;
      next.address_response_valid = true;
      next.address_response_cacheable = true;
      next.address_response_instruction_cacheable = true;
    }
    if (fence_outputs.instruction_request_valid) {
      ++instruction_fills;
      next.instruction_request_ready = true;
      next.instruction_response_valid = true;
      next.instruction_response_line[0] = 0x1800107312000073ULL; // SFENCE.VMA; CSRRW x0, satp, x0.
      next.instruction_response_line[1] = 0x0000100f30001073ULL; // CSRRW x0, mstatus, x0; FENCE.I.
      next.instruction_response_line[2] = 0x0000006fULL;
    }
    fence_inputs = next;
  }
  assert(instruction_fills == 2);
  assert(!fence_trapped_to_zero);

  // CBO.ZERO must acquire one coherent line and publish a dirty zero line.
  // A region without the cache-block-zero PMA permission must fault instead.
  // Keep all FESVR coroutine contexts alive for this process-long fixture.
  std::array<std::unique_ptr<SpikeCoreModel>, 2> zero_models;
  for (bool allowed : {true, false}) {
    Configuration zero_configuration;
    zero_configuration.reset_vector = 0x1000;
    zero_configuration.isa = "rv64ima_zicsr_zic64b_zicboz";
    zero_configuration.privilege = "msu";
    zero_models[allowed] = std::make_unique<SpikeCoreModel>(zero_configuration);
    auto& zero_model = *zero_models[allowed];
    Inputs zero_inputs;
    std::size_t zero_classifications = 0;
    std::size_t zero_acquires = 0;
    bool zero_trapped = false;
    for (std::size_t cycle = 0; cycle < 96; ++cycle) {
      const Outputs zero_outputs = zero_model.tick(zero_inputs);
      Inputs next;
      if (zero_outputs.address_request_valid) {
        next.address_request_ready = true;
        next.address_response_valid = true;
        next.address_response_cacheable = true;
        next.address_response_instruction_cacheable = true;
        if (zero_outputs.address_request_address == 0x2000) {
          assert(zero_outputs.address_request_size == 6);
          assert(zero_outputs.address_request_write);
          next.address_response_cache_block_zero = allowed;
          ++zero_classifications;
        }
        zero_trapped |= zero_outputs.address_request_address == 0;
      }
      if (zero_outputs.instruction_request_valid) {
        next.instruction_request_ready = true;
        next.instruction_response_valid = true;
        next.instruction_response_line[0] = 0x0040a00f000020b7ULL;
        next.instruction_response_line[1] = 0x0000006fULL;
      }
      if (zero_outputs.acquire_request_valid) {
        assert(allowed);
        assert(zero_outputs.acquire_request_address == 0x2000);
        assert(zero_outputs.acquire_request_unique);
        ++zero_acquires;
        next.acquire_request_ready = true;
        next.acquire_response_valid = true;
        next.acquire_response_state = 2;
        next.acquire_response_line.fill(~std::uint64_t{0});
      }
      zero_inputs = next;
    }
    assert(zero_classifications >= 1);
    assert(zero_acquires == (allowed ? 1U : 0U));
    assert(zero_trapped == !allowed);
    if (allowed) {
      Inputs snoop;
      snoop.snoop_valid = true;
      snoop.snoop_address = 0x2000;
      snoop.snoop_invalidate = true;
      snoop.snoop_return_to_source = true;
      const Outputs response = zero_model.tick(snoop);
      assert(response.snoop_response_valid);
      assert(response.snoop_response_pass_dirty);
      assert(response.snoop_response_has_data);
      for (std::uint64_t word : response.snoop_response_line) assert(word == 0);
    }
  }

  // Management CBOs may use a readable or writable block, but neither kind
  // of physical access is available in the fault hole.
  std::array<std::unique_ptr<SpikeCoreModel>, 3> management_models;
  for (std::size_t permission = 0; permission < management_models.size(); ++permission) {
    Configuration management_configuration;
    management_configuration.reset_vector = 0x1000;
    management_configuration.isa = "rv64ima_zicsr_zic64b_zicbom";
    management_configuration.privilege = "msu";
    management_models[permission] = std::make_unique<SpikeCoreModel>(management_configuration);
    auto& management_model = *management_models[permission];
    Inputs management_inputs;
    std::size_t classifications = 0;
    bool trapped = false;
    for (std::size_t cycle = 0; cycle < 96; ++cycle) {
      const Outputs management_outputs = management_model.tick(management_inputs);
      Inputs next;
      if (management_outputs.address_request_valid) {
        next.address_request_ready = true;
        next.address_response_valid = true;
        next.address_response_cacheable = true;
        next.address_response_instruction_cacheable = true;
        if (management_outputs.address_request_address == 0x2000) {
          assert(management_outputs.address_request_size == 6);
          ++classifications;
          next.address_response_fault = permission == 0 ||
              (permission == 1 && management_outputs.address_request_write) ||
              (permission == 2 && !management_outputs.address_request_write);
        }
        trapped |= management_outputs.address_request_address == 0;
      }
      if (management_outputs.instruction_request_valid) {
        next.instruction_request_ready = true;
        next.instruction_response_valid = true;
        next.instruction_response_line[0] = 0x0010a00f000020b7ULL;
        next.instruction_response_line[1] = 0x0000006fULL;
      }
      management_inputs = next;
    }
    assert(classifications >= 1);
    assert(trapped == (permission == 0));
  }

  // An AMO must request a unique line before reading it, complete its update
  // without another RTL transaction, and reject a region without atomic PMA.
  std::array<std::unique_ptr<SpikeCoreModel>, 2> amo_models;
  for (bool allowed : {true, false}) {
    Configuration amo_configuration;
    amo_configuration.reset_vector = 0x1000;
    amo_configuration.isa = "rv64ima_zicsr";
    amo_configuration.privilege = "msu";
    amo_models[allowed] = std::make_unique<SpikeCoreModel>(amo_configuration);
    auto& amo_model = *amo_models[allowed];
    Inputs amo_inputs;
    std::size_t amo_classifications = 0;
    std::size_t amo_acquires = 0;
    bool amo_trapped = false;
    for (std::size_t cycle = 0; cycle < 96; ++cycle) {
      const Outputs amo_outputs = amo_model.tick(amo_inputs);
      Inputs next;
      if (amo_outputs.address_request_valid) {
        next.address_request_ready = true;
        next.address_response_valid = true;
        next.address_response_cacheable = true;
        next.address_response_instruction_cacheable = true;
        if (amo_outputs.address_request_address == 0x2000) {
          assert(amo_outputs.address_request_size == 2);
          next.address_response_atomic = allowed;
          ++amo_classifications;
        }
        amo_trapped |= amo_outputs.address_request_address == 0;
      }
      if (amo_outputs.instruction_request_valid) {
        next.instruction_request_ready = true;
        next.instruction_response_valid = true;
        next.instruction_response_line[0] = 0x00100113000020b7ULL;
        next.instruction_response_line[1] = 0x0000006f0020a1afULL;
      }
      if (amo_outputs.acquire_request_valid) {
        assert(allowed);
        assert(amo_outputs.acquire_request_address == 0x2000);
        assert(amo_outputs.acquire_request_unique);
        ++amo_acquires;
        next.acquire_request_ready = true;
        next.acquire_response_valid = true;
        next.acquire_response_state = 2;
        next.acquire_response_line[0] = 7;
      }
      amo_inputs = next;
      if (amo_trapped) break;
    }
    assert(amo_classifications == 2);
    assert(amo_acquires == (allowed ? 1U : 0U));
    assert(amo_trapped == !allowed);
    if (allowed) {
      Inputs snoop;
      snoop.snoop_valid = true;
      snoop.snoop_address = 0x2000;
      snoop.snoop_invalidate = true;
      snoop.snoop_return_to_source = true;
      const Outputs response = amo_model.tick(snoop);
      assert(response.snoop_response_valid);
      assert(response.snoop_response_pass_dirty);
      assert(response.snoop_response_line[0] == 8);
    }
  }
}
