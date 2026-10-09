// Preserves the rv5stage-vector-memory cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using response_bits_t =
    std::remove_cvref_t<decltype(uncached_in.presponse.pbits)>;
// Checks vector memory against real MMU/cache execution, including tagged responses and precise restart.

std::uint32_t program_words[4096];
std::uint8_t memory[32768];
std::uint64_t expected[512];
int expected_pc[512];
int pc = 0, expected_count = 0, signatures = 0, cycles = 0;
int hits = 0, warm_run = 0, longest_warm_run = 0, rejections = 0;
int overlapping_hits = 0, scalar_overlap = 0, redirected_tail = 0;
int certified_lookups = 0;
int strided_certificates = 0, strided_store_certificates = 0,
    nonpow2_certificates = 0;
int nonpow2_start = 0, nonpow2_end = 0;
std::uint8_t nonpow2_element_seen = {};
bool vector_sequencing, vector_certifying;
int refills = 0, copybacks = 0, fault_signature = 0, fault_reset_signature = 0,
    whole_fault_signature = 0, whole_fault_reset_signature = 0,
    mask_fault_signature = 0, mask_fault_reset_signature = 0,
    device_elements = 0;
int zero_stride_start = 0, zero_stride_end = 0, masked_splat_start = 0,
    masked_splat_end = 0, wide_splat_start = 0, wide_splat_end = 0;
int zero_stride_reads = 0, register_zero_stride_reads = 0,
    zero_stride_row_writes = 0;
int masked_splat_reads = 0, masked_splat_row_writes = 0, wide_splat_reads = 0,
    wide_splat_row_writes = 0;
bool instruction_valid = 0, uncached_pending = 0, returning = 0,
     writing_back = 0;
std::uint32_t instruction_word;
response_bits_t uncached_response;
std::uint64_t line_address;
std::uint16_t txn;
int beat = 0, delay_cycles = 0, response_phase = 0, uncached_delay = 0;
bool resumed = 0, vector_load_pending = 0;

std::uint32_t addi(int rd, int rs, int imm) {
  return (((uint128(((imm)&low_mask(12))) & mask128(12)) << 20) |
          ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(19)) & mask128(7)) << 0));
}
std::uint32_t csr(int address, int rd, int rs, int op = 1) {
  return (((uint128(((address)&low_mask(12))) & mask128(12)) << 20) |
          ((uint128(((rs)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((op)&low_mask(3))) & mask128(3)) << 12) |
          ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(115)) & mask128(7)) << 0));
}
std::uint32_t vmem(std::uint8_t store, int width, int regno, int base,
                   std::uint8_t masked = 0, std::uint8_t strided = 0,
                   int stride = 0) {
  return (
      ((uint128(UINT64_C(0)) & mask128(4)) << 28) |
      ((uint128(strided ? UINT64_C(2) : UINT64_C(0)) & mask128(2)) << 26) |
      ((uint128(!masked) & mask128(1)) << 25) |
      ((uint128(((strided ? stride : 0) & low_mask(5))) & mask128(5)) << 20) |
      ((uint128(((base)&low_mask(5))) & mask128(5)) << 15) |
      ((uint128(((width == 0 ? 0 : width + 4) & low_mask(3))) & mask128(3))
       << 12) |
      ((uint128(((regno)&low_mask(5))) & mask128(5)) << 7) |
      ((uint128(store ? UINT64_C(39) : UINT64_C(7)) & mask128(7)) << 0));
}
std::uint32_t indexed_vmem(std::uint8_t store, std::uint8_t ordered,
                           int index_width, int regno, int base, int index_reg,
                           std::uint8_t masked = 0) {
  return (((uint128(UINT64_C(0)) & mask128(4)) << 28) |
          ((uint128(ordered ? UINT64_C(3) : UINT64_C(1)) & mask128(2)) << 26) |
          ((uint128(!masked) & mask128(1)) << 25) |
          ((uint128(((index_reg)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(((base)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((index_width == 0 ? 0 : index_width + 4) & low_mask(3))) &
            mask128(3))
           << 12) |
          ((uint128(((regno)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(store ? UINT64_C(39) : UINT64_C(7)) & mask128(7)) << 0));
}
std::uint32_t segment_vmem(std::uint8_t store, int width, int fields, int regno,
                           int base, std::uint8_t masked = 0,
                           std::uint8_t strided = 0, int stride = 0) {
  return (
      ((uint128(((fields - 1) & low_mask(3))) & mask128(3)) << 29) |
      ((uint128(UINT64_C(0)) & mask128(1)) << 28) |
      ((uint128(strided ? UINT64_C(2) : UINT64_C(0)) & mask128(2)) << 26) |
      ((uint128(!masked) & mask128(1)) << 25) |
      ((uint128(((strided ? stride : 0) & low_mask(5))) & mask128(5)) << 20) |
      ((uint128(((base)&low_mask(5))) & mask128(5)) << 15) |
      ((uint128(((width == 0 ? 0 : width + 4) & low_mask(3))) & mask128(3))
       << 12) |
      ((uint128(((regno)&low_mask(5))) & mask128(5)) << 7) |
      ((uint128(store ? UINT64_C(39) : UINT64_C(7)) & mask128(7)) << 0));
}
std::uint32_t indexed_segment_vmem(std::uint8_t store, std::uint8_t ordered,
                                   int index_width, int fields, int regno,
                                   int base, int index_reg,
                                   std::uint8_t masked = 0) {
  return (((uint128(((fields - 1) & low_mask(3))) & mask128(3)) << 29) |
          ((uint128(UINT64_C(0)) & mask128(1)) << 28) |
          ((uint128(ordered ? UINT64_C(3) : UINT64_C(1)) & mask128(2)) << 26) |
          ((uint128(!masked) & mask128(1)) << 25) |
          ((uint128(((index_reg)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(((base)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((index_width == 0 ? 0 : index_width + 4) & low_mask(3))) &
            mask128(3))
           << 12) |
          ((uint128(((regno)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(store ? UINT64_C(39) : UINT64_C(7)) & mask128(7)) << 0));
}
std::uint32_t fault_only_first_vmem(int width, int fields, int regno, int base,
                                    std::uint8_t masked = 0) {
  return (((uint128(((fields - 1) & low_mask(3))) & mask128(3)) << 29) |
          ((uint128(UINT64_C(0)) & mask128(1)) << 28) |
          ((uint128(UINT64_C(0)) & mask128(2)) << 26) |
          ((uint128(!masked) & mask128(1)) << 25) |
          ((uint128(UINT64_C(16)) & mask128(5)) << 20) |
          ((uint128(((base)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((width == 0 ? 0 : width + 4) & low_mask(3))) & mask128(3))
           << 12) |
          ((uint128(((regno)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(7)) & mask128(7)) << 0));
}
std::uint32_t whole_register_vmem(std::uint8_t store, int width, int registers,
                                  int regno, int base) {
  return (((uint128(((registers - 1) & low_mask(3))) & mask128(3)) << 29) |
          ((uint128(UINT64_C(0)) & mask128(1)) << 28) |
          ((uint128(UINT64_C(0)) & mask128(2)) << 26) |
          ((uint128(UINT64_C(1)) & mask128(1)) << 25) |
          ((uint128(UINT64_C(8)) & mask128(5)) << 20) |
          ((uint128(((base)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((store || width == 0 ? 0 : width + 4) & low_mask(3))) &
            mask128(3))
           << 12) |
          ((uint128(((regno)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(store ? UINT64_C(39) : UINT64_C(7)) & mask128(7)) << 0));
}
std::uint32_t mask_vmem(std::uint8_t store, int regno, int base) {
  return (((uint128(UINT64_C(0)) & mask128(3)) << 29) |
          ((uint128(UINT64_C(0)) & mask128(1)) << 28) |
          ((uint128(UINT64_C(0)) & mask128(2)) << 26) |
          ((uint128(UINT64_C(1)) & mask128(1)) << 25) |
          ((uint128(UINT64_C(11)) & mask128(5)) << 20) |
          ((uint128(((base)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(((regno)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(store ? UINT64_C(39) : UINT64_C(7)) & mask128(7)) << 0));
}
std::uint32_t vint(int op, int vd, int vs2, int vs1, int mode = 0) {
  return (((uint128(((op)&low_mask(6))) & mask128(6)) << 26) |
          ((uint128(UINT64_C(1)) & mask128(1)) << 25) |
          ((uint128(((vs2)&low_mask(5))) & mask128(5)) << 20) |
          ((uint128(((vs1)&low_mask(5))) & mask128(5)) << 15) |
          ((uint128(((mode)&low_mask(3))) & mask128(3)) << 12) |
          ((uint128(((vd)&low_mask(5))) & mask128(5)) << 7) |
          ((uint128(UINT64_C(87)) & mask128(7)) << 0));
}
std::uint64_t read_word(int address) {
  std::uint64_t value = 0;
  for (int b = 0; b < 8; b++)
    bit_slice(value, b * 8, 8) = memory[address + b];
  return value;
}
void write_word(int address, std::uint64_t value) {
  for (int b = 0; b < 8; b++)
    memory[address + b] = bit_slice(value, b * 8, 8);
}
void emit(std::uint32_t instruction) { program_words[pc++] = instruction; }
void li(int rd, int value) {
  emit((
      ((uint128((((value + 2048) >> 12) & low_mask(20))) & mask128(20)) << 12) |
      ((uint128(((rd)&low_mask(5))) & mask128(5)) << 7) |
      ((uint128(UINT64_C(55)) & mask128(7)) << 0)));
  emit(addi(rd, rd, value));
}
void configure(int sew, int lmul, int vl) {
  emit(addi(6, 0, vl));
  emit((((uint128(UINT64_C(0)) & mask128(1)) << 31) |
        ((uint128((((sew << 3) | lmul) & low_mask(11))) & mask128(11)) << 20) |
        ((uint128(UINT64_C(6)) & mask128(5)) << 15) |
        ((uint128(UINT64_C(7)) & mask128(3)) << 12) |
        ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
        ((uint128(UINT64_C(87)) & mask128(7)) << 0)));
}
void signature(int regno, std::uint64_t value) {
  int offset = expected_count * 8;
  expected_pc[expected_count] = pc * 4;
  expected[expected_count++] = value;
  emit((((uint128(((offset >> 5) & low_mask(7))) & mask128(7)) << 25) |
        ((uint128(((regno)&low_mask(5))) & mask128(5)) << 20) |
        ((uint128(UINT64_C(20)) & mask128(5)) << 15) |
        ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
        ((uint128(((offset)&low_mask(5))) & mask128(5)) << 7) |
        ((uint128(UINT64_C(35)) & mask128(7)) << 0)));
  emit(UINT64_C(267386895));
}
void check_memory(int base, int bytes,
                  const std::vector<std::uint64_t> &values) {
  li(9, base);
  for (int wordno = 0; wordno < bytes / 8; wordno++) {
    emit((((uint128(((wordno * 8) & low_mask(12))) & mask128(12)) << 20) |
          ((uint128(UINT64_C(9)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
    signature(7, values[wordno]);
  }
}
void check_strided_memory(int base, int stride, int count,
                          const std::vector<std::uint64_t> &values) {
  li(9, base);
  for (int wordno = 0; wordno < count; wordno++) {
    emit((((uint128(((wordno * stride) & low_mask(12))) & mask128(12)) << 20) |
          ((uint128(UINT64_C(9)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
    signature(7, values[wordno]);
  }
}

void drive() {
  {
    instruction_in = {};
    instruction_in.prequest.pready =
        instruction_out.pflush || !instruction_valid;
    instruction_in.presponse.pvalid = instruction_valid;
    instruction_in.presponse.pbits.pword = instruction_word;
    uncached_in = {};
    uncached_in.prequest.pready = !uncached_pending;
    uncached_in.presponse.pvalid = uncached_pending && uncached_delay == 0;
    uncached_in.presponse.pbits = uncached_response;
    uncached_in.pdrained = !uncached_pending;
    chi_in = {};
    chi_in.prequests.pready =
        cycles % 4 != 0 && !returning && !writing_back && response_phase == 0;
    chi_in.presponses.pvalid = response_phase != 0;
    chi_in.presponses.pbits.popcode = response_phase == 1   ? UINT64_C(3)
                                      : response_phase == 2 ? UINT64_C(7)
                                                            : UINT64_C(5);
    chi_in.presponses.pbits.psrc_uid = UINT64_C(1);
    chi_in.presponses.pbits.ptgt_uid = UINT64_C(3);
    chi_in.presponses.pbits.ptxn_uid = txn;
    chi_in.presponses.pbits.pdbid_uor_ugroup_uid = UINT64_C(85);
    chi_in.presponses.pbits.ppcrd_utype = UINT64_C(2);
    chi_in.prequester_uresponses.pready = cycles % 3 != 0;
    chi_in.prequest_udata.pready = cycles % 3 != 0;
    chi_in.presponse_udata.pvalid =
        returning && delay_cycles == 0 && cycles % 3 != 0;
    chi_in.presponse_udata.pbits.popcode = UINT64_C(4);
    chi_in.presponse_udata.pbits.presp = UINT64_C(2);
    chi_in.presponse_udata.pbits.pbyte_uenable = UINT64_C(65535);
    chi_in.presponse_udata.pbits.pdata_uid = ((beat)&low_mask(2));
    chi_in.presponse_udata.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
        UINT64_C(1);
    chi_in.presponse_udata.pbits.pdbid_uor_umecid = UINT64_C(85);
    chi_in.presponse_udata.pbits.ptxn_uid = txn;
    chi_in.presponse_udata.pbits.psrc_uid = UINT64_C(1);
    chi_in.presponse_udata.pbits.ptgt_uid = UINT64_C(3);
    write_bits(
        chi_in.presponse_udata.pbits.pdata, 0, 128,
        (((uint128(read_word(int(line_address) + 16 * beat + 8)) & mask128(64))
          << 64) |
         ((uint128(read_word(int(line_address) + 16 * beat)) & mask128(64))
          << 0)));
  }
}

void observe() {
  {
    defer(cycles, cycles + 1);
    if (reset) {
      defer(instruction_valid, 0);
      defer(permit_demand, 0);
    } else {
      // One rejection per transaction, followed by persistent readiness: no
      // artificial periodic readiness/replay phase lock.
      if (demand_attempt) {
        if (!permit_demand) {
          defer(permit_demand, 1);
          defer(rejections, rejections + 1);
        } else if (demand_fire)
          defer(permit_demand, 0);
      }
      if (instruction_out.pflush)
        defer(instruction_valid, 0);
      else if (instruction_in.presponse.pvalid &&
               instruction_out.presponse.pready)
        defer(instruction_valid, 0);
      if (instruction_out.prequest.pvalid && instruction_in.prequest.pready) {
        defer(instruction_valid, 1);
        defer(instruction_word,
              program_words[int(instruction_out.prequest.pbits.paddress / 4) %
                            4096]);
      }
      if (load_hit) {
        if (returning && line_address == UINT64_C(5440))
          defer(overlapping_hits, overlapping_hits + 1);
        defer(hits, hits + 1);
        defer(warm_run, warm_run + 1);
        if (warm_run + 1 > longest_warm_run)
          defer(longest_warm_run, warm_run + 1);
      } else
        defer(warm_run, 0);
      if (resumed && load_issue)
        CHECK(load_address != UINT64_C(20464) &&
              load_address != UINT64_C(20472));
      if (load_issue && load_address == UINT64_C(4864) && vector_load_pending)
        defer(scalar_overlap, scalar_overlap + 1);
      if (signatures >= zero_stride_start && signatures < zero_stride_end &&
          load_issue) {
        if (load_address == UINT64_C(10240))
          defer(zero_stride_reads, zero_stride_reads + 1);
        if (load_address == UINT64_C(10288))
          defer(register_zero_stride_reads, register_zero_stride_reads + 1);
      }
      if (signatures >= nonpow2_start && signatures < nonpow2_end &&
          load_issue && load_address >= UINT64_C(11008) &&
          load_address <= UINT64_C(11111)) {
        CHECK((load_address == UINT64_C(11008) ||
               load_address == UINT64_C(11032) ||
               load_address == UINT64_C(11056) ||
               load_address == UINT64_C(11080)));
        switch (load_address) {
        case UINT64_C(11008): {
          defer(bit_slice(nonpow2_element_seen, 0, 1), UINT64_C(1));
        } break;
        case UINT64_C(11032): {
          defer(bit_slice(nonpow2_element_seen, 1, 1), UINT64_C(1));
        } break;
        case UINT64_C(11056): {
          defer(bit_slice(nonpow2_element_seen, 2, 1), UINT64_C(1));
        } break;
        case UINT64_C(11080): {
          defer(bit_slice(nonpow2_element_seen, 3, 1), UINT64_C(1));
        } break;
        default: {
          ;
        } break;
        }
      }
      if (signatures >= masked_splat_start && signatures < masked_splat_end &&
          load_issue && load_address == UINT64_C(10466))
        defer(masked_splat_reads, masked_splat_reads + 1);
      if (signatures >= wide_splat_start && signatures < wide_splat_end &&
          load_issue && load_address == UINT64_C(10240))
        defer(wide_splat_reads, wide_splat_reads + 1);
      if (instruction_out.pflush && vector_load_pending)
        defer(redirected_tail, redirected_tail + 1);
      if (device_elements != 0 && load_issue && load_address == UINT64_C(4872))
        CHECK(device_elements == 4 && !uncached_pending);
      if (transaction_fire && transaction.paccess == 2 &&
          transaction.paddress == UINT64_C(9984))
        CHECK(!vector_load_pending);
      if (chi_out.prequests.pvalid && chi_in.prequests.pready) {
        CHECK((chi_out.prequests.pbits.popcode == UINT64_C(2) ||
               chi_out.prequests.pbits.popcode == UINT64_C(7) ||
               chi_out.prequests.pbits.popcode == UINT64_C(27)));
        defer(line_address,
              ((chi_out.prequests.pbits.paddress) & low_mask(64)));
        defer(txn, chi_out.prequests.pbits.ptxn_uid);
        if (chi_out.prequests.pbits.pallow_uretry)
          defer(response_phase, 1);
        else if (chi_out.prequests.pbits.popcode == UINT64_C(27)) {
          defer(writing_back, 1);
          defer(response_phase, 3);
          defer(copybacks, copybacks + 1);
          defer(beat, 0);
        } else {
          defer(returning, 1);
          defer(beat, 0);
          defer(delay_cycles, 19);
          defer(refills, refills + 1);
        }
      }
      if (chi_in.presponses.pvalid && chi_out.presponses.pready)
        defer(response_phase, response_phase == 1 ? 2 : 0);
      if (delay_cycles > 0)
        defer(delay_cycles, delay_cycles - 1);
      if (chi_in.presponse_udata.pvalid && chi_out.presponse_udata.pready) {
        if (beat == 3)
          defer(returning, 0);
        else
          defer(beat, beat + 1);
      }
      if (chi_out.prequest_udata.pvalid && chi_in.prequest_udata.pready) {
        CHECK(writing_back);
        for (int b = 0; b < 16; b++)
          if (bit_slice(chi_out.prequest_udata.pbits.pbyte_uenable, b, 1))
            defer(memory[int(line_address) +
                         16 * int(chi_out.prequest_udata.pbits.pdata_uid) + b],
                  bit_slice(chi_out.prequest_udata.pbits.pdata, b * 8, 8));
        if (beat == 3)
          defer(writing_back, 0);
        else
          defer(beat, beat + 1);
      }
      if (uncached_delay > 0)
        defer(uncached_delay, uncached_delay - 1);
      if (uncached_in.presponse.pvalid) {
        defer(uncached_pending, 0);
        defer(vector_load_pending, 0);
      }
      if (uncached_out.prequest.pvalid && uncached_in.prequest.pready) {
        defer(uncached_pending, 1);
        defer(uncached_delay, 11);
        defer(uncached_response,
              std::remove_cvref_t<decltype(uncached_response)>{
                  .paccess_ufault = 0,
                  .pdata = UINT64_C(49),
                  .pcontext = {.pwriteback = uncached_out.prequest.pbits
                                                 .prequest.pcontext.pwriteback,
                               .porigin = uncached_out.prequest.pbits.prequest
                                              .pcontext.porigin}});
        if (uncached_out.prequest.pbits.prequest.paddress == UINT64_C(40960)) {
          CHECK(uncached_out.prequest.pbits.prequest.paccess == 1 &&
                bit_slice(
                    uncached_out.prequest.pbits.prequest.pcontext.pwriteback, 7,
                    (8) - (7) + 1) == 3);
          defer(vector_load_pending, 1);
          defer(uncached_delay, 50);
        } else if (uncached_out.prequest.pbits.prequest.paddress >=
                   UINT64_C(36864)) {
          CHECK(uncached_out.prequest.pbits.prequest.paccess == 2 &&
                uncached_out.prequest.pbits.prequest.paddress ==
                    UINT64_C(36864) + ((device_elements * 8) & low_mask(64)) &&
                uncached_out.prequest.pbits.prequest.pdata ==
                    ((device_elements + 1) & low_mask(64)));
          defer(device_elements, device_elements + 1);
        } else {
          if (uncached_out.prequest.pbits.prequest.pdata !=
              expected[signatures])
            std::cerr << "signature " << signatures << " pc " << std::hex
                      << expected_pc[signatures] << " got "
                      << uncached_out.prequest.pbits.prequest.pdata
                      << " expected " << expected[signatures] << std::dec
                      << '\n';
          CHECK(uncached_out.prequest.pbits.prequest.paccess == 2 &&
                uncached_out.prequest.pbits.prequest.paddress ==
                    UINT64_C(32768) + ((signatures * 8) & low_mask(64)) &&
                uncached_out.prequest.pbits.prequest.pdata ==
                    expected[signatures]);
          defer(signatures, signatures + 1);
          if (signatures == fault_reset_signature ||
              signatures == whole_fault_reset_signature ||
              signatures == mask_fault_reset_signature)
            defer(resumed, 0);
          if (signatures == fault_signature + 3 ||
              signatures == whole_fault_signature + 3 ||
              signatures == mask_fault_signature + 3)
            defer(resumed, 1);
          if (signatures + 1 == expected_count) {
            CHECK(zero_stride_reads == 1 && register_zero_stride_reads == 4 &&
                  zero_stride_row_writes == 1);
            CHECK(masked_splat_reads == 1 && masked_splat_row_writes == 1);
            CHECK(wide_splat_reads == 1 && wide_splat_row_writes == 4);
            CHECK(nonpow2_element_seen == UINT64_C(15) &&
                  strided_store_certificates > 0 && nonpow2_certificates > 0);
            CHECK((COMPLETION_SLOTS < 8 ||
                   (longest_warm_run >= 8 && overlapping_hits > 0)) &&
                  certified_lookups > 0 && strided_certificates > 0 &&
                  scalar_overlap > 0 && redirected_tail > 0 && rejections > 8 &&
                  device_elements == 4);
            ;
            throw Finished{};
          }
        }
      }
      CHECK(cycles < 80000);
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  permit_demand = 0;
  {
    std::vector<std::uint64_t> values, field_values;
    int fault_pc, continuation, before_handler, fof_fault_pc, fof_continuation,
        before_fof_handler, whole_fault_pc, whole_continuation,
        before_whole_handler, mask_fault_pc, before_mask_handler, offsets[4];
    std::array<int, 5> mask_lengths;
    for (int i = 0; i < 4096; i++)
      program_words[i] = UINT64_C(111);
    for (int i = 0; i < 32768; i++)
      memory[i] = UINT64_C(85);
    li(20, UINT64_C(32768));
    li(1, UINT64_C(1536));
    emit(csr(UINT64_C(768), 0, 1));
    li(1, UINT64_C(7168));
    emit(csr(UINT64_C(773), 0, 1));
    for (int sew = 0; sew < 4; sew++) {
      values.assign(2 << sew, 0);
      for (int i = 0; i < 16; i++) {
        for (int b = 0; b < (1 << sew); b++)
          memory[UINT64_C(4096) + sew * 256 + (i << sew) + b] =
              ((std::uint64_t(i + 1) >> (8 * b)) & low_mask(8));
        for (int b = 0; b < (1 << sew); b++)
          bit_slice(values[(i << sew) / 8], (((i << sew) + b) % 8) * 8, 8) =
              ((std::uint64_t(i + 2) >> (8 * b)) & low_mask(8));
      }
      configure(sew, 3, 16);
      li(8, UINT64_C(4096) + sew * 256);
      li(9, UINT64_C(8192) + sew * 256);
      emit(vmem(0, sew, 8, 8));
      emit(vint(0, 16, 8, 1, 3)); // packed vadd.vi, between memory macros
      emit(vmem(1, sew, 16, 9));
      if (sew == 3) {
        // The adjacent younger store must win, including for vector readback.
        emit(vmem(1, sew, 8, 9));
        emit(vmem(0, sew, 24, 9));
        emit(vmem(1, sew, 24, 9));
        for (int i = 0; i < 16; i++)
          values[i] = ((i + 1) & low_mask(64));
      }
      check_memory(UINT64_C(8192) + sew * 256, 16 << sew, values);
      // Warm-cache vector loads must sustain one element per cycle.
      if (sew == 3) {
        write_word(UINT64_C(5112), UINT64_C(74565));
        li(10, UINT64_C(5112));
        emit((((uint128(UINT64_C(0)) & mask128(12)) << 20) |
              ((uint128(UINT64_C(10)) & mask128(5)) << 15) |
              ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
              ((uint128(UINT64_C(7)) & mask128(5)) << 7) |
              ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
        emit(UINT64_C(267386895));
      }
      emit(vmem(0, sew, 8, 8));
      if (sew == 3) {
        // Exercise a warm scalar hit after a vector load without depending on
        // its exact overlap cycle in the sequencer.
        emit((((uint128(UINT64_C(0)) & mask128(12)) << 20) |
              ((uint128(UINT64_C(10)) & mask128(5)) << 15) |
              ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
              ((uint128(UINT64_C(7)) & mask128(5)) << 7) |
              ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
      }
    }
    // Cold first element followed by seven resident elements: complete hits
    // ahead of an older delayed miss, then drain the tagged results in order.
    values.assign(8, 0);
    for (int i = 0; i < 8; i++) {
      values[i] = ((101 + i) & low_mask(64));
      write_word(UINT64_C(5496) + i * 8, values[i]);
    }
    li(8, UINT64_C(5504));
    emit((((uint128(UINT64_C(0)) & mask128(12)) << 20) |
          ((uint128(UINT64_C(8)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
    emit(UINT64_C(267386895));
    configure(3, 2, 8);
    li(8, UINT64_C(5496));
    li(9, UINT64_C(9728));
    emit(vmem(0, 3, 8, 8));
    emit(vmem(1, 3, 8, 9));
    check_memory(UINT64_C(9728), 64, values);
    // EEW differs from SEW: EMUL=4, not LMUL=8.
    configure(1, 3, 16);
    li(8, UINT64_C(4096));
    emit(vmem(0, 0, 8, 8));
    configure(0, 0, 16);
    emit(vint(28, 0, 8, 8, 3)); // vmsleu.vi: enable the first eight elements
    emit(vint(11, 16, 16, 16)); // clear v16 with vxor
    li(7, UINT64_C(85));
    emit(vint(0, 16, 16, 7, 4));
    emit(csr(8, 0, 3, 5)); // vstart=3
    emit(vmem(0, 0, 16, 8, 1));
    li(9, UINT64_C(9216));
    emit(vmem(1, 0, 16, 9));
    values.assign(2, 0);
    values[0] = UINT64_C(578437695757702485);
    values[1] = UINT64_C(6148914691236517205);
    check_memory(UINT64_C(9216), 16, values);
    // Strided memory advances a captured full address. Positive, negative,
    // zero, and nonzero-vstart cases exercise the incremental sequencer.
    values.assign(4, 0);
    for (int i = 0; i < 4; i++) {
      values[i] = ((UINT64_C(769) + i) & low_mask(64));
      write_word(UINT64_C(10240) + i * 16, values[i]);
    }
    configure(3, 1, 4);
    li(8, UINT64_C(10240));
    li(9, UINT64_C(10752));
    li(10, 16);
    emit(vmem(0, 3, 8, 8, 0, 1, 10));
    emit(vmem(1, 3, 8, 9));
    check_memory(UINT64_C(10752), 32, values);
    // A runtime 24-byte stride gets a 32-byte certificate envelope but keeps
    // exact elementwise stores and reverse loads at the original stride.
    li(9, UINT64_C(11008));
    li(10, 24);
    emit(vmem(1, 3, 8, 9, 0, 1, 10));
    check_strided_memory(UINT64_C(11008), 24, 4, values);
    for (int i = 0; i < 4; i++)
      values[i] = ((UINT64_C(772) - i) & low_mask(64));
    nonpow2_start = expected_count;
    li(8, UINT64_C(11080));
    li(9, UINT64_C(10816));
    li(10, -24);
    emit(vmem(0, 3, 8, 8, 0, 1, 10));
    emit(vmem(1, 3, 8, 9));
    check_memory(UINT64_C(10816), 32, values);
    nonpow2_end = expected_count;
    zero_stride_start = expected_count;
    // A zero held in x10 must still read each element; encoded x0 may read once.
    li(8, UINT64_C(10288));
    li(10, 0);
    emit(vmem(0, 3, 16, 8, 0, 1, 10));
    configure(1, 0, 4);
    values.assign(1, 0);
    values[0] = UINT64_C(216457559970743041);
    li(8, UINT64_C(10240));
    li(9, UINT64_C(10880));
    emit(vmem(0, 1, 8, 8, 0, 1, 0));
    emit(vmem(1, 1, 8, 9));
    check_memory(UINT64_C(10880), 8, values);
    zero_stride_end = expected_count;
    masked_splat_start = expected_count;
    // Restart at element two with only element three enabled. A cold single
    // read must update one lane; an all-masked successor must perform no read.
    write_word(UINT64_C(10432), UINT64_C(6148914691236517205));
    write_word(UINT64_C(10448), UINT64_C(8));
    write_word(UINT64_C(10456), UINT64_C(0));
    write_word(UINT64_C(10464), UINT64_C(197984256));
    li(8, UINT64_C(10432));
    emit(vmem(0, 1, 8, 8));
    li(8, UINT64_C(10448));
    emit(vmem(0, 0, 0, 8));
    emit(csr(8, 0, 2, 5));
    li(8, UINT64_C(10466));
    emit(vmem(0, 1, 8, 8, 1, 1, 0));
    li(9, UINT64_C(10888));
    emit(vmem(1, 1, 8, 9));
    values.assign(1, 0);
    values[0] = UINT64_C(850429729635128661);
    check_memory(UINT64_C(10888), 8, values);
    li(8, UINT64_C(10456));
    emit(vmem(0, 0, 0, 8));
    li(8, UINT64_C(10466));
    emit(vmem(0, 1, 8, 8, 1, 1, 0));
    li(9, UINT64_C(10896));
    emit(vmem(1, 1, 8, 9));
    check_memory(UINT64_C(10896), 8, values);
    masked_splat_end = expected_count;
    configure(3, 1, 4);
    wide_splat_start = expected_count;
    li(8, UINT64_C(10240));
    li(9, UINT64_C(10912));
    emit(vmem(0, 3, 8, 8, 0, 1, 0));
    emit(vmem(1, 3, 8, 9));
    values.assign(1, 0);
    values[0] = UINT64_C(769);
    check_memory(UINT64_C(10912), 8, values);
    wide_splat_end = expected_count;
    emit(vint(11, 16, 16, 16));
    li(7, UINT64_C(85));
    emit(vint(0, 16, 16, 7, 4));
    emit(csr(8, 0, 2, 5));
    li(8, UINT64_C(10240));
    li(9, UINT64_C(10944));
    li(10, 16);
    emit(vmem(0, 3, 16, 8, 0, 1, 10));
    emit(vmem(1, 3, 16, 9));
    values.assign(4, 0);
    values[0] = UINT64_C(85);
    values[1] = UINT64_C(85);
    values[2] = UINT64_C(771);
    values[3] = UINT64_C(772);
    check_memory(UINT64_C(10944), 32, values);
    // Indexed memory uses encoded index EEW but vtype SEW for transferred data.
    // Nonmonotonic offsets also prove that addresses are base+offset, not scaled.
    values.assign(4, 0);
    for (int i = 0; i < 4; i++)
      write_word(UINT64_C(11520) + i * 8,
                 ((UINT64_C(1025) + i) & low_mask(64)));
    memory[UINT64_C(11264)] = 24;
    memory[UINT64_C(11265)] = 0;
    memory[UINT64_C(11266)] = 0;
    memory[UINT64_C(11267)] = 0;
    memory[UINT64_C(11268)] = 16;
    memory[UINT64_C(11269)] = 0;
    memory[UINT64_C(11270)] = 8;
    memory[UINT64_C(11271)] = 0;
    configure(3, 1, 4);
    li(8, UINT64_C(11264));
    emit(vmem(0, 1, 16, 8));
    li(8, UINT64_C(11520));
    emit(indexed_vmem(0, 0, 1, 8, 8, 16));
    li(9, UINT64_C(11776));
    emit(vmem(1, 3, 8, 9));
    values[0] = UINT64_C(1028);
    values[1] = UINT64_C(1025);
    values[2] = UINT64_C(1027);
    values[3] = UINT64_C(1026);
    check_memory(UINT64_C(11776), 32, values);
    li(9, UINT64_C(11840));
    emit(indexed_vmem(1, 1, 1, 8, 9, 16));
    values[0] = UINT64_C(1025);
    values[1] = UINT64_C(1026);
    values[2] = UINT64_C(1027);
    values[3] = UINT64_C(1028);
    check_memory(UINT64_C(11840), 32, values);
    // Unit-stride segments pipeline distinct field operations through the
    // completion window, map fields to consecutive EMUL groups, and advance
    // vstart in whole-segment units.
    values.assign(12, 0);
    for (int element = 0; element < 4; element++) {
      for (int field = 0; field < 3; field++) {
        values[element * 3 + field] =
            ((UINT64_C(1280) + element * 16 + field) & low_mask(64));
        write_word(UINT64_C(12544) + (element * 3 + field) * 8,
                   values[element * 3 + field]);
      }
    }
    configure(2, 0, 4);
    li(8, UINT64_C(12544));
    emit(segment_vmem(0, 3, 3, 8, 8));
    for (int field = 0; field < 3; field++) {
      field_values.assign(4, 0);
      for (int element = 0; element < 4; element++)
        field_values[element] = values[element * 3 + field];
      li(9, UINT64_C(12800) + field * 64);
      emit(vmem(1, 3, 8 + field * 2, 9));
      check_memory(UINT64_C(12800) + field * 64, 32, field_values);
    }
    li(9, UINT64_C(13056));
    emit(segment_vmem(1, 3, 3, 8, 9));
    check_memory(UINT64_C(13056), 96, values);
    emit(csr(8, 0, 1, 5));
    li(9, UINT64_C(13312));
    emit(segment_vmem(1, 3, 3, 8, 9));
    values[0] = UINT64_C(6148914691236517205);
    values[1] = UINT64_C(6148914691236517205);
    values[2] = UINT64_C(6148914691236517205);
    check_memory(UINT64_C(13312), 96, values);
    emit(csr(8, 7, 0, 2));
    signature(7, 0);
    // Constant-stride segments use one captured signed stride per segment while
    // keeping fields contiguous. Exercise positive, negative, zero, and vstart
    // progression with EEW=64/SEW=32 and EMUL=2 field groups.
    for (int element = 0; element < 4; element++) {
      for (int field = 0; field < 3; field++) {
        values[element * 3 + field] =
            ((UINT64_C(1536) + element * 16 + field) & low_mask(64));
        write_word(UINT64_C(13568) + element * 32 + field * 8,
                   values[element * 3 + field]);
      }
    }
    li(8, UINT64_C(13568));
    li(10, 32);
    emit(segment_vmem(0, 3, 3, 8, 8, 0, 1, 10));
    for (int field = 0; field < 3; field++) {
      field_values.assign(4, 0);
      for (int element = 0; element < 4; element++)
        field_values[element] = values[element * 3 + field];
      li(9, UINT64_C(13824) + field * 64);
      emit(vmem(1, 3, 8 + field * 2, 9));
      check_memory(UINT64_C(13824) + field * 64, 32, field_values);
    }
    li(9, UINT64_C(14080));
    li(10, 32);
    emit(segment_vmem(1, 3, 3, 8, 9, 0, 1, 10));
    for (int field = 0; field < 3; field++) {
      field_values.assign(4, 0);
      for (int element = 0; element < 4; element++)
        field_values[element] = values[element * 3 + field];
      check_strided_memory(UINT64_C(14080) + field * 8, 32, 4, field_values);
    }
    li(9, UINT64_C(14528));
    li(10, -32);
    emit(segment_vmem(1, 3, 3, 8, 9, 0, 1, 10));
    for (int field = 0; field < 3; field++) {
      field_values.assign(4, 0);
      for (int element = 0; element < 4; element++)
        field_values[element] = values[(3 - element) * 3 + field];
      check_strided_memory(UINT64_C(14432) + field * 8, 32, 4, field_values);
    }
    li(9, UINT64_C(14720));
    emit(segment_vmem(1, 3, 3, 8, 9, 0, 1, 0));
    field_values.assign(3, 0);
    for (int field = 0; field < 3; field++)
      field_values[field] = values[9 + field];
    check_memory(UINT64_C(14720), 24, field_values);
    emit(csr(8, 0, 1, 5));
    li(9, UINT64_C(14848));
    li(10, 32);
    emit(segment_vmem(1, 3, 3, 8, 9, 0, 1, 10));
    for (int field = 0; field < 3; field++) {
      field_values.assign(4, 0);
      field_values[0] = UINT64_C(6148914691236517205);
      for (int element = 1; element < 4; element++)
        field_values[element] = values[element * 3 + field];
      check_strided_memory(UINT64_C(14848) + field * 8, 32, 4, field_values);
    }
    emit(csr(8, 7, 0, 2));
    signature(7, 0);
    // Indexed segments combine one vector byte offset per segment with
    // contiguous SEW-sized fields. Exercise mixed index/data EEW, both
    // ordering encodings, repeated ordered offsets, and segment-granular vstart.
    offsets[0] = 72;
    offsets[1] = 0;
    offsets[2] = 48;
    offsets[3] = 24;
    values.assign(12, 0);
    for (int element = 0; element < 4; element++) {
      memory[UINT64_C(15104) + element * 2] =
          ((offsets[element]) & low_mask(8));
      memory[UINT64_C(15105) + element * 2] =
          ((offsets[element] >> 8) & low_mask(8));
      for (int field = 0; field < 3; field++) {
        values[element * 3 + field] =
            ((UINT64_C(1792) + element * 16 + field) & low_mask(64));
        write_word(UINT64_C(15360) + offsets[element] + field * 8,
                   values[element * 3 + field]);
      }
    }
    configure(3, 1, 4);
    li(8, UINT64_C(15104));
    emit(vmem(0, 1, 16, 8));
    li(8, UINT64_C(15360));
    emit(indexed_segment_vmem(0, 0, 1, 3, 8, 8, 16));
    for (int field = 0; field < 3; field++) {
      field_values.assign(4, 0);
      for (int element = 0; element < 4; element++)
        field_values[element] = values[element * 3 + field];
      li(9, UINT64_C(15616) + field * 64);
      emit(vmem(1, 3, 8 + field * 2, 9));
      check_memory(UINT64_C(15616) + field * 64, 32, field_values);
    }
    li(9, UINT64_C(15872));
    emit(indexed_segment_vmem(1, 1, 1, 3, 8, 9, 16));
    for (int element = 0; element < 4; element++) {
      field_values.assign(3, 0);
      for (int field = 0; field < 3; field++)
        field_values[field] = values[element * 3 + field];
      check_memory(UINT64_C(15872) + offsets[element], 24, field_values);
    }
    emit(vint(11, 16, 16, 16));
    li(9, UINT64_C(16128));
    emit(indexed_segment_vmem(1, 1, 1, 3, 8, 9, 16));
    field_values.assign(3, 0);
    for (int field = 0; field < 3; field++)
      field_values[field] = values[9 + field];
    check_memory(UINT64_C(16128), 24, field_values);
    li(8, UINT64_C(15104));
    emit(vmem(0, 1, 16, 8));
    emit(csr(8, 0, 1, 5));
    li(9, UINT64_C(16256));
    emit(indexed_segment_vmem(1, 1, 1, 3, 8, 9, 16));
    for (int element = 0; element < 4; element++) {
      field_values.assign(3, 0);
      for (int field = 0; field < 3; field++)
        field_values[field] = element == 0 ? UINT64_C(6148914691236517205)
                                           : values[element * 3 + field];
      check_memory(UINT64_C(16256) + offsets[element], 24, field_values);
    }
    emit(csr(8, 7, 0, 2));
    signature(7, 0);
    // Whole-register transfers ignore vl/vtype geometry, stream encoded-EEW
    // elements across consecutive registers, and preserve the current vl.
    values.assign(4, 0);
    for (int word = 0; word < 4; word++) {
      values[word] = ((UINT64_C(33024) + word) & low_mask(64));
      write_word(UINT64_C(16640) + word * 8, values[word]);
    }
    configure(0, 0, 1);
    li(8, UINT64_C(16640));
    emit(whole_register_vmem(0, 1, 2, 8, 8));
    li(9, UINT64_C(16896));
    emit(whole_register_vmem(1, 0, 2, 8, 9));
    check_memory(UINT64_C(16896), 32, values);
    emit(csr(UINT64_C(3104), 7, 0, 2));
    signature(7, 1);
    // Mask loads/stores transfer ceil(vl/8) bytes in one register regardless
    // of SEW/LMUL. Exercise both sides of each byte boundary and full VLEN.
    write_word(UINT64_C(17408), UINT64_C(18364758544493064720));
    write_word(UINT64_C(17416), UINT64_C(81985529216486895));
    mask_lengths = {1, 7, 8, 9, 128};
    for (int test = 0; test < 5; test++) {
      int transferred = (mask_lengths[test] + 7) / 8;
      int checked_words = (transferred + 7) / 8;
      configure(0, 3, mask_lengths[test]);
      li(8, UINT64_C(17408));
      emit(mask_vmem(0, 8, 8));
      li(9, UINT64_C(17920) + test * 32);
      emit(mask_vmem(1, 8, 9));
      values.assign(checked_words, 0);
      for (int word = 0; word < checked_words; word++)
        values[word] = UINT64_C(6148914691236517205);
      for (int byte_index = 0; byte_index < transferred; byte_index++)
        bit_slice(values[byte_index / 8], (byte_index % 8) * 8, 8) =
            memory[UINT64_C(17408) + byte_index];
      check_memory(UINT64_C(17920) + test * 32, checked_words * 8, values);
    }
    // A load into v0 must update the dedicated predicate shadow used by the
    // following ordinary masked store, while vsm reads the same packed bytes.
    memory[UINT64_C(17600)] = UINT64_C(165);
    memory[UINT64_C(17601)] = UINT64_C(1);
    for (int element = 0; element < 9; element++)
      memory[UINT64_C(17632) + element] =
          ((UINT64_C(128) + element) & low_mask(8));
    configure(0, 0, 9);
    li(8, UINT64_C(17600));
    emit(mask_vmem(0, 0, 8));
    li(8, UINT64_C(17632));
    emit(vmem(0, 0, 16, 8));
    li(9, UINT64_C(17664));
    emit(vmem(1, 0, 16, 9, 1));
    values.assign(2, 0);
    values[0] = UINT64_C(6148914691236517205);
    values[1] = UINT64_C(6148914691236517205);
    for (int element = 0; element < 9; element++)
      if ((((element < 8 ? UINT64_C(165) : UINT64_C(1)) >> (element % 8)) &
           1) != 0)
        bit_slice(values[element / 8], (element % 8) * 8, 8) =
            ((UINT64_C(128) + element) & low_mask(8));
    check_memory(UINT64_C(17664), 16, values);
    li(9, UINT64_C(17696));
    emit(mask_vmem(1, 0, 9));
    values.assign(1, 0);
    values[0] = UINT64_C(6148914691236495781);
    check_memory(UINT64_C(17696), 8, values);
    configure(3, 1, 4);
    // Empty and fully masked bodies must not touch an unmapped address.
    li(8, UINT64_C(65536));
    emit(csr(8, 0, 20, 5));
    emit(vmem(0, 0, 16, 8));
    emit(vint(25, 0, 8, 8)); // vmsne.vv v0,v8,v8
    emit(vmem(0, 0, 16, 8, 1));
    emit(vmem(1, 0, 16, 8, 1));
    li(10, 32);
    emit(segment_vmem(0, 3, 3, 16, 8, 1, 1, 10));
    emit(segment_vmem(1, 3, 3, 16, 8, 1, 1, 10));
    emit(indexed_segment_vmem(0, 1, 1, 3, 8, 8, 16, 1));
    emit(indexed_segment_vmem(1, 1, 1, 3, 8, 8, 16, 1));
    emit(fault_only_first_vmem(3, 1, 8, 8, 1));
    emit(fault_only_first_vmem(3, 3, 8, 8, 1));
    emit(csr(8, 7, 0, 2));
    signature(7, 0);
    configure(0, 3, 0);
    li(8, UINT64_C(65536));
    emit(mask_vmem(0, 0, 8));
    emit(mask_vmem(1, 0, 8));
    emit(csr(8, 7, 0, 2));
    signature(7, 0);
    // A scalar load may pass an older vector load's delayed completion, but
    // the following scalar store must wait for that vector load to drain.
    // A younger taken branch must preserve the accepted vector response owner.
    configure(3, 0, 1);
    li(8, UINT64_C(40960));
    li(9, UINT64_C(4864));
    li(10, UINT64_C(9984));
    emit(vmem(0, 3, 8, 8));
    emit(UINT64_C(8388719));
    emit(addi(7, 0, -1));
    emit((((uint128(UINT64_C(0)) & mask128(12)) << 20) |
          ((uint128(UINT64_C(9)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
    emit((((uint128(UINT64_C(0)) & mask128(7)) << 25) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 20) |
          ((uint128(UINT64_C(10)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(35)) & mask128(7)) << 0)));
    emit(vint(16, 11, 8, 0, 2));
    emit((((uint128(UINT64_C(0)) & mask128(7)) << 25) |
          ((uint128(UINT64_C(11)) & mask128(5)) << 20) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(51)) & mask128(7)) << 0)));
    signature(7, UINT64_C(50));
    // Exactly-once vector stores through the uncached/device LSU path.
    configure(3, 1, 4);
    li(8, UINT64_C(4864));
    emit(vmem(0, 3, 8, 8));
    li(9, UINT64_C(36864));
    emit(vmem(1, 3, 8, 9));
    emit((((uint128(UINT64_C(8)) & mask128(12)) << 20) |
          ((uint128(UINT64_C(8)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(3)) & mask128(7)) << 0)));
    signature(7, 2);
    // Fault-only-first loads serialize unresolved elements. A later page fault
    // truncates VL without trapping; a fault in segment zero remains precise.
    write_word(UINT64_C(12288), UINT64_C(4097));
    write_word(UINT64_C(16384), UINT64_C(5121));
    write_word(UINT64_C(20512), UINT64_C(1223));
    write_word(UINT64_C(20520), 0);
    write_word(UINT64_C(8152), UINT64_C(49));
    write_word(UINT64_C(8160), UINT64_C(50));
    write_word(UINT64_C(8168), UINT64_C(51));
    write_word(UINT64_C(8176), UINT64_C(33));
    write_word(UINT64_C(8184), UINT64_C(34));
    li(7, 1);
    emit((((uint128(UINT64_C(0)) & mask128(6)) << 26) |
          ((uint128(UINT64_C(63)) & mask128(6)) << 20) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(19)) & mask128(7)) << 0)));
    emit(addi(7, 7, 3));
    emit(csr(UINT64_C(384), 0, 7));
    configure(3, 1, 4);
    li(8, UINT64_C(20472));
    li(1, UINT64_C(134656));
    emit(csr(UINT64_C(768), 0, 1));
    emit(fault_only_first_vmem(3, 1, 8, 8));
    li(1, UINT64_C(1536));
    emit(csr(UINT64_C(768), 0, 1));
    emit(csr(UINT64_C(3104), 7, 0, 2));
    signature(7, 1);
    emit(csr(8, 7, 0, 2));
    signature(7, 0);
    li(9, UINT64_C(10240));
    emit(vmem(1, 3, 8, 9));
    values.assign(1, 0);
    values[0] = UINT64_C(34);
    check_memory(UINT64_C(10240), 8, values);
    configure(3, 1, 4);
    li(8, UINT64_C(20440));
    li(1, UINT64_C(134656));
    emit(csr(UINT64_C(768), 0, 1));
    emit(fault_only_first_vmem(3, 2, 8, 8));
    li(1, UINT64_C(1536));
    emit(csr(UINT64_C(768), 0, 1));
    emit(csr(UINT64_C(3104), 7, 0, 2));
    signature(7, 2);
    emit(csr(8, 7, 0, 2));
    signature(7, 0);
    li(9, UINT64_C(10272));
    emit(vmem(1, 3, 8, 9));
    values.assign(2, 0);
    values[0] = UINT64_C(49);
    values[1] = UINT64_C(51);
    check_memory(UINT64_C(10272), 16, values);
    li(9, UINT64_C(10304));
    emit(vmem(1, 3, 10, 9));
    values[0] = UINT64_C(50);
    values[1] = UINT64_C(33);
    check_memory(UINT64_C(10304), 16, values);
    configure(3, 1, 4);
    li(1, UINT64_C(7680));
    emit(csr(UINT64_C(773), 0, 1));
    li(8, UINT64_C(20480));
    li(1, UINT64_C(134656));
    emit(csr(UINT64_C(768), 0, 1));
    fof_fault_pc = pc * 4;
    emit(fault_only_first_vmem(3, 1, 8, 8));
    fof_continuation = pc;
    before_fof_handler = pc;
    pc = 1920;
    li(1, UINT64_C(1536));
    emit(csr(UINT64_C(768), 0, 1));
    emit(csr(UINT64_C(833), 7, 0, 2));
    signature(7, ((fof_fault_pc)&low_mask(64)));
    emit(csr(UINT64_C(834), 7, 0, 2));
    signature(7, 13);
    emit(csr(UINT64_C(835), 7, 0, 2));
    signature(7, UINT64_C(20480));
    emit(csr(UINT64_C(3104), 7, 0, 2));
    signature(7, 4);
    emit(csr(8, 7, 0, 2));
    signature(7, 0);
    li(10, fof_continuation * 4);
    emit((((uint128(UINT64_C(0)) & mask128(12)) << 20) |
          ((uint128(UINT64_C(10)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(103)) & mask128(7)) << 0)));
    pc = before_fof_handler;
    li(1, UINT64_C(7168));
    emit(csr(UINT64_C(773), 0, 1));
    // An indexed segmented load crosses an Sv39 leaf boundary after one whole segment.
    for (int element = 0; element < 4; element++) {
      memory[UINT64_C(12032) + element * 2] = ((element * 16) & low_mask(8));
      memory[UINT64_C(12033) + element * 2] = 0;
    }
    li(8, UINT64_C(12032));
    emit(vmem(0, 1, 16, 8));
    write_word(UINT64_C(8176), UINT64_C(33));
    write_word(UINT64_C(8184), UINT64_C(34));
    for (int i = 0; i < 6; i++)
      write_word(UINT64_C(24576) + i * 8, ((UINT64_C(35) + i) & low_mask(64)));
    li(7, 1);
    emit((((uint128(UINT64_C(0)) & mask128(6)) << 26) |
          ((uint128(UINT64_C(63)) & mask128(6)) << 20) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(1)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(19)) & mask128(7)) << 0)));
    emit(addi(7, 7, 3));
    emit(csr(UINT64_C(384), 0, 7));
    li(8, UINT64_C(20464));
    li(1, UINT64_C(134656));
    emit(csr(UINT64_C(768), 0, 1));
    fault_pc = pc * 4;
    emit(indexed_segment_vmem(0, 1, 1, 2, 8, 8, 16));
    continuation = pc;
    li(1, UINT64_C(1536));
    emit(csr(UINT64_C(768), 0, 1));
    li(9, UINT64_C(9472));
    emit(vmem(1, 3, 8, 9));
    li(9, UINT64_C(9536));
    emit(vmem(1, 3, 10, 9));
    // Handler signatures precede these continuation signatures.
    before_handler = pc;
    pc = 1792;
    li(1, UINT64_C(1536));
    emit(csr(UINT64_C(768), 0, 1));
    fault_signature = expected_count;
    emit(csr(UINT64_C(834), 7, 0, 2));
    signature(7, 13);
    emit(csr(UINT64_C(835), 7, 0, 2));
    signature(7, UINT64_C(20480));
    emit(csr(8, 7, 0, 2));
    signature(7, 1);
    emit(csr(UINT64_C(833), 10, 0, 2));
    signature(10, ((fault_pc)&low_mask(64)));
    li(9, UINT64_C(20520));
    li(7, UINT64_C(6343));
    emit((((uint128(UINT64_C(0)) & mask128(7)) << 25) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 20) |
          ((uint128(UINT64_C(9)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(35)) & mask128(7)) << 0)));
    emit(UINT64_C(267386895));
    emit(UINT64_C(301990003));
    li(8, UINT64_C(20464));
    li(1, UINT64_C(134656));
    emit(csr(UINT64_C(768), 0, 1));
    emit((((uint128(UINT64_C(0)) & mask128(12)) << 20) |
          ((uint128(UINT64_C(10)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(103)) & mask128(7)) << 0)));
    pc = before_handler;
    values.assign(4, 0);
    values[0] = UINT64_C(33);
    values[1] = UINT64_C(35);
    values[2] = UINT64_C(37);
    values[3] = UINT64_C(39);
    check_memory(UINT64_C(9472), 32, values);
    values[0] = UINT64_C(34);
    values[1] = UINT64_C(36);
    values[2] = UINT64_C(38);
    values[3] = UINT64_C(40);
    check_memory(UINT64_C(9536), 32, values);
    emit(csr(8, 7, 0, 2));
    signature(7, 0);
    fault_reset_signature = expected_count - 1;
    // A whole-register load faults at the first element of its second
    // register, reports that encoded-EEW position, and resumes from it after
    // the handler repairs the leaf mapping.
    li(9, UINT64_C(20520));
    emit((((uint128(UINT64_C(0)) & mask128(7)) << 25) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 20) |
          ((uint128(UINT64_C(9)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(35)) & mask128(7)) << 0)));
    emit(UINT64_C(267386895));
    emit(UINT64_C(301990003));
    li(1, UINT64_C(7424));
    emit(csr(UINT64_C(773), 0, 1));
    configure(0, 0, 1);
    li(8, UINT64_C(20464));
    li(1, UINT64_C(134656));
    emit(csr(UINT64_C(768), 0, 1));
    whole_fault_pc = pc * 4;
    emit(whole_register_vmem(0, 3, 2, 8, 8));
    whole_continuation = pc;
    li(1, UINT64_C(1536));
    emit(csr(UINT64_C(768), 0, 1));
    li(9, UINT64_C(17152));
    emit(whole_register_vmem(1, 0, 2, 8, 9));
    before_whole_handler = pc;
    pc = 1856;
    li(1, UINT64_C(1536));
    emit(csr(UINT64_C(768), 0, 1));
    whole_fault_signature = expected_count;
    emit(csr(UINT64_C(834), 7, 0, 2));
    signature(7, 13);
    emit(csr(UINT64_C(835), 7, 0, 2));
    signature(7, UINT64_C(20480));
    emit(csr(8, 7, 0, 2));
    signature(7, 2);
    emit(csr(UINT64_C(833), 10, 0, 2));
    signature(10, ((whole_fault_pc)&low_mask(64)));
    li(9, UINT64_C(20520));
    li(7, UINT64_C(6343));
    emit((((uint128(UINT64_C(0)) & mask128(7)) << 25) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 20) |
          ((uint128(UINT64_C(9)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(35)) & mask128(7)) << 0)));
    emit(UINT64_C(267386895));
    emit(UINT64_C(301990003));
    li(8, UINT64_C(20464));
    li(1, UINT64_C(134656));
    emit(csr(UINT64_C(768), 0, 1));
    emit((((uint128(UINT64_C(0)) & mask128(12)) << 20) |
          ((uint128(UINT64_C(10)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(103)) & mask128(7)) << 0)));
    pc = before_whole_handler;
    values.assign(4, 0);
    values[0] = UINT64_C(33);
    values[1] = UINT64_C(34);
    values[2] = UINT64_C(35);
    values[3] = UINT64_C(36);
    check_memory(UINT64_C(17152), 32, values);
    emit(csr(8, 7, 0, 2));
    signature(7, 0);
    whole_fault_reset_signature = expected_count - 1;
    // A mask load uses byte-granular vstart. Fault after eight accepted bytes,
    // repair the leaf, then resume at byte eight without repeating the prefix.
    li(9, UINT64_C(20520));
    emit((((uint128(UINT64_C(0)) & mask128(7)) << 25) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 20) |
          ((uint128(UINT64_C(9)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(35)) & mask128(7)) << 0)));
    emit(UINT64_C(267386895));
    emit(UINT64_C(301990003));
    li(1, UINT64_C(8448));
    emit(csr(UINT64_C(773), 0, 1));
    configure(0, 3, 128);
    li(8, UINT64_C(20472));
    li(1, UINT64_C(134656));
    emit(csr(UINT64_C(768), 0, 1));
    mask_fault_pc = pc * 4;
    emit(mask_vmem(0, 0, 8));
    before_mask_handler = pc;
    pc = 2112;
    li(1, UINT64_C(1536));
    emit(csr(UINT64_C(768), 0, 1));
    mask_fault_signature = expected_count;
    emit(csr(UINT64_C(834), 7, 0, 2));
    signature(7, 13);
    emit(csr(UINT64_C(835), 7, 0, 2));
    signature(7, UINT64_C(20480));
    emit(csr(8, 7, 0, 2));
    signature(7, 8);
    emit(csr(UINT64_C(833), 10, 0, 2));
    signature(10, ((mask_fault_pc)&low_mask(64)));
    li(9, UINT64_C(20520));
    li(7, UINT64_C(6343));
    emit((((uint128(UINT64_C(0)) & mask128(7)) << 25) |
          ((uint128(UINT64_C(7)) & mask128(5)) << 20) |
          ((uint128(UINT64_C(9)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(3)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(35)) & mask128(7)) << 0)));
    emit(UINT64_C(267386895));
    emit(UINT64_C(301990003));
    li(8, UINT64_C(20472));
    li(1, UINT64_C(134656));
    emit(csr(UINT64_C(768), 0, 1));
    emit((((uint128(UINT64_C(0)) & mask128(12)) << 20) |
          ((uint128(UINT64_C(10)) & mask128(5)) << 15) |
          ((uint128(UINT64_C(0)) & mask128(3)) << 12) |
          ((uint128(UINT64_C(0)) & mask128(5)) << 7) |
          ((uint128(UINT64_C(103)) & mask128(7)) << 0)));
    pc = before_mask_handler;
    li(1, UINT64_C(1536));
    emit(csr(UINT64_C(768), 0, 1));
    emit(csr(8, 7, 0, 2));
    mask_fault_reset_signature = expected_count;
    signature(7, 0);
    emit(UINT64_C(111));
    CHECK(before_handler < 1792 && before_whole_handler < 1856 &&
          before_fof_handler < 1920 && before_mask_handler < 2112);
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
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
// Observe pre-edge public component ports; host responder updates are deferred.
extern "C" void test_vector_write(std::uint64_t rst, std::uint64_t valid,
                                  std::uint64_t address, std::uint64_t data,
                                  std::uint64_t mask) {
  if (rst || !valid)
    return;
  if (signatures >= zero_stride_start && signatures < zero_stride_end &&
      address == 16) {
    CHECK(data == UINT64_C(0x0301030103010301) && mask == UINT64_MAX);
    ++zero_stride_row_writes;
  }
  if (signatures >= masked_splat_start && signatures < masked_splat_end &&
      address == 16 && mask == UINT64_C(0xffff000000000000)) {
    CHECK((data & mask) == UINT64_C(0x0bcd000000000000));
    ++masked_splat_row_writes;
  }
  if (signatures >= wide_splat_start && signatures < wide_splat_end &&
      address >= 16 && address <= 19) {
    CHECK(address == unsigned(16 + wide_splat_row_writes) && data == 0x301 &&
          mask == UINT64_MAX);
    ++wide_splat_row_writes;
  }
}
extern "C" void test_vector_lifetime(std::uint64_t, std::uint64_t sequencing,
                                     std::uint64_t certifying) {
  vector_sequencing = sequencing;
  vector_certifying = certifying;
}
extern "C" void test_certificates(std::uint64_t rst, std::uint64_t physical,
                                  std::uint64_t lookup_valid,
                                  std::uint64_t is_vector, std::uint64_t valid,
                                  std::uint64_t store, std::uint64_t first,
                                  std::uint64_t last) {
  if (rst)
    return;
  if (physical && lookup_valid && is_vector)
    ++certified_lookups;
  if (valid && !store && first == 0x2800 && last == 0x2837)
    ++strided_certificates;
  else if (valid && !store && first == 0x2ae8 && last == 0x2b4f &&
           signatures >= nonpow2_start && signatures < nonpow2_end)
    ++nonpow2_certificates;
  else if (valid && store && first == 0x2b00 && last == 0x2b67)
    ++strided_store_certificates;
}
