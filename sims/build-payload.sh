#!/usr/bin/env bash
# Compiles one selected harness ELF using the platform's concrete ISA and memory layout.
# SPDX-License-Identifier: Apache-2.0
source sims/build-env.sh
name="${RHODIUM_PAYLOAD:?Moon must select one payload}"
out="$PWD/.rhodium-cache/moon/payloads/$soc/$variant/$name.elf"
source=sims/tests/programs/${name}.S
linker=sims/tests/programs/${name}.ld
march=rv64i_zicsr
abi=lp64
if [[ $isa == rv32* ]]; then march=rv32i_zicsr; abi=ilp32; fi
flags=()
case "$name" in
  smoke)
    if [[ ${TRACE:-0} == 1 ]]; then march=rv64ic_zicsr; flags+=(-DRHODIUM_SMOKE_COMPRESSED_SENTINEL=1); fi
    case "$isa" in
      rva23) march=rv64gcv_zvbb_zvfh; flags+=(-DRHODIUM_SMOKE_VECTOR=1);;
      rv32max) march=rv32imafcb_zicsr_zve32f_zvl64b_zvbb; flags+=(-DRHODIUM_SMOKE_VECTOR=1);;
      rv32int) march=rv32imacb_zicsr_zve32x_zvl64b_zvbb; flags+=(-DRHODIUM_SMOKE_VECTOR=1);;
    esac ;;
  boot_2000|boot_3000)
    source=sims/tests/programs/smoke.S; linker=sims/tests/programs/smoke.ld
    flags+=(-Wl,--defsym,payload_entry=0x8000${name#boot_}) ;;
  uart_pty) linker=sims/tests/programs/smoke.ld ;;
  host_mmio) ;;
  lrsc_0|lrsc_1)
    source=sims/tests/programs/lrsc.S; linker=sims/tests/programs/lrsc.ld; march=rv64ia_zicsr_zifencei
    harts=1; case "$shape" in tiled8core) harts=8;; tiled4core) harts=4;; esac
    page=0x80010000; limit=0xc0000000
    if [[ $shape == mini ]]; then page=0x8000b000; limit=0x80010000; fi
    flags+=(-DLRSC_HARTS=$harts -DLRSC_TRANSLATED=${name#lrsc_} -DLRSC_IALIGN=2 -DLRSC_PAGE_TABLE_BASE=$page -Wl,--defsym,lrsc_page_tables=$page,--defsym,lrsc_memory_limit=$limit) ;;
  zicboz) march=rv64im_zicsr ;;
  zihintntl|zihintntl-policy)
    source=sims/tests/programs/zihintntl.S; linker=sims/tests/programs/zihintntl.ld; march=rv64im_zicsr_zicntr
    read -r -a flags <<< "${NTL_CPPFLAGS:-}"
    if [[ $name == zihintntl-policy ]]; then flags+=(-DRHODIUM_NTL_CACHE_POLICY=1); fi ;;
  cosim-smoke) source=sims/cosim/tests/runtime/scalar.S; linker=sims/tests/programs/smoke.ld; march=${march/i_zicsr/imac_zicsr} ;;
  *) echo "Unknown payload: $name" >&2; exit 2 ;;
esac
mkdir -p "$(dirname "$out")"
source tools/build/paths.sh
compilation_path_flags --build "$(dirname "$out")"
"${RISCV_CC:-riscv64-unknown-elf-gcc}" -march="$march" -mabi="$abi" -nostdlib -nostartfiles \
  "${flags[@]}" "${path_flags[@]}" -Wl,--build-id=none -Wl,-T,"$linker" "$source" -o "$out"
