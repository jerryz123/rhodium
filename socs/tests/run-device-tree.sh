#!/usr/bin/env bash
# Cross-checks every concrete SoC's generated DTB against standard device-tree tools.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
for tool in dtc fdtdump fdtget; do
  if ! command -v "$tool" >/dev/null; then
    echo "SoC device-tree interoperability test requires $tool" >&2
    exit 1
  fi
done

fixture_dir="$(mktemp -d /tmp/rhodium-soc-devicetree.XXXXXX)"
cleanup() {
  rm -rf "$fixture_dir"
}
trap cleanup EXIT

env PLTCOLLECTS="$repo_dir": "$repo_dir/tools/run-racket.sh" \
  "$repo_dir/socs/tests/write-device-trees.rhm" "$fixture_dir"

for name in single-core-rv5stage-soc mini-rv5stage-soc tiled-rv5stage-soc; do
  dtc -I dtb -O dts -o "$fixture_dir/$name-roundtrip.dts" "$fixture_dir/$name.dtb"
  dtc -I dts -O dtb -o "$fixture_dir/$name-from-dts.dtb" "$fixture_dir/$name.dts"
  fdtdump "$fixture_dir/$name.dtb" > "$fixture_dir/$name.dump" 2>&1
  cmp "$fixture_dir/$name.dtb" "$fixture_dir/$name-from-dts.dtb"
  # The public ISA advertisement must follow the default hardware profile.
  case " $(fdtget "$fixture_dir/$name.dtb" /cpus/cpu@0 riscv,isa-extensions) " in
    *" zkt "*) ;;
    *) echo "$name DTB does not advertise Zkt" >&2; exit 1 ;;
  esac
  case " $(fdtget "$fixture_dir/$name.dtb" /cpus/cpu@0 riscv,isa-extensions) " in
    *" zihintntl "*) ;;
    *) echo "$name DTB does not advertise Zihintntl" >&2; exit 1 ;;
  esac
  plic_path=/soc/interrupt-controller@c000000
  uart_path=/soc/serial@10000000
  [[ "$(fdtget "$fixture_dir/$name.dtb" "$plic_path" compatible)" == "sifive,plic-1.0.0" ]]
  [[ "$(fdtget -t x "$fixture_dir/$name.dtb" "$plic_path" reg)" == "0 c000000 0 4000000" ]]
  [[ "$(fdtget "$fixture_dir/$name.dtb" "$plic_path" '#address-cells')" == "0" ]]
  [[ "$(fdtget "$fixture_dir/$name.dtb" "$plic_path" '#interrupt-cells')" == "1" ]]
  [[ "$(fdtget "$fixture_dir/$name.dtb" "$plic_path" riscv,ndev)" == "1" ]]
  [[ -z "$(fdtget "$fixture_dir/$name.dtb" "$plic_path" interrupt-controller)" ]]
  [[ "$(fdtget "$fixture_dir/$name.dtb" "$uart_path" status)" == "okay" ]]
  [[ "$(fdtget "$fixture_dir/$name.dtb" "$uart_path" compatible)" == "ns16550a" ]]
  [[ "$(fdtget -t x "$fixture_dir/$name.dtb" "$uart_path" reg)" == "0 10000000 0 8" ]]
  [[ "$(fdtget "$fixture_dir/$name.dtb" "$uart_path" clock-frequency)" == "100000000" ]]
  [[ "$(fdtget "$fixture_dir/$name.dtb" "$uart_path" reg-shift)" == "0" ]]
  [[ "$(fdtget "$fixture_dir/$name.dtb" "$uart_path" reg-io-width)" == "1" ]]
  [[ "$(fdtget "$fixture_dir/$name.dtb" "$uart_path" interrupts)" == "1" ]]
  [[ "$(fdtget "$fixture_dir/$name.dtb" "$uart_path" interrupt-parent)" == "$(fdtget "$fixture_dir/$name.dtb" "$plic_path" phandle)" ]]
  # Resolve CPU phandles rather than assuming their numeric allocation.
  contexts=()
  hart_count=1
  [[ "$name" != tiled-rv5stage-soc ]] || hart_count=8
  for ((hart=0; hart<hart_count; hart++)); do
    for extension in zic64b za64rs ziccif ziccamoa ziccrse; do
      case " $(fdtget "$fixture_dir/$name.dtb" "/cpus/cpu@$hart" riscv,isa-extensions) " in
        *" $extension "*) ;;
        *) echo "$name hart $hart DTB does not advertise $extension" >&2; exit 1 ;;
      esac
    done
    cpu_phandle="$(fdtget -t x "$fixture_dir/$name.dtb" "/cpus/cpu@$hart/interrupt-controller" phandle)"
    contexts+=("$cpu_phandle" b "$cpu_phandle" 9)
  done
  [[ "$(fdtget -t x "$fixture_dir/$name.dtb" "$plic_path" interrupts-extended)" == "${contexts[*]}" ]]
done

for name in single-core-spike-soc mini-spike-soc tiled-spike-soc; do
  dtc -I dtb -O dts -o "$fixture_dir/$name-roundtrip.dts" "$fixture_dir/$name.dtb"
  dtc -I dts -O dtb -o "$fixture_dir/$name-from-dts.dtb" "$fixture_dir/$name.dts"
  fdtdump "$fixture_dir/$name.dtb" > "$fixture_dir/$name.dump" 2>&1
  cmp "$fixture_dir/$name.dtb" "$fixture_dir/$name-from-dts.dtb"
  case "$name" in
    single-core-spike-soc) model='Rhodium Single-Core Spike SoC'; memory_bytes=40000000; hart_count=1 ;;
    mini-spike-soc) model='Rhodium Mini Spike SoC'; memory_bytes=10000; hart_count=1 ;;
    tiled-spike-soc) model='Rhodium Tiled Spike SoC'; memory_bytes=40000000; hart_count=8 ;;
  esac
  [[ "$(fdtget "$fixture_dir/$name.dtb" / model)" == "$model" ]]
  [[ "$(fdtget -t x "$fixture_dir/$name.dtb" /memory@80000000 reg)" == "0 80000000 0 $memory_bytes" ]]
  for ((hart=0; hart<hart_count; hart++)); do
    [[ "$(fdtget "$fixture_dir/$name.dtb" "/cpus/cpu@$hart" riscv,isa-base)" == "rv64i" ]]
    [[ "$(fdtget "$fixture_dir/$name.dtb" "/cpus/cpu@$hart" mmu-type)" == "riscv,sv39" ]]
    for extension in i m a f d c zicsr zifencei zicntr; do
      case " $(fdtget "$fixture_dir/$name.dtb" "/cpus/cpu@$hart" riscv,isa-extensions) " in
        *" $extension "*) ;;
        *) echo "$name hart $hart DTB does not advertise $extension" >&2; exit 1 ;;
      esac
    done
  done
  for cache in i d; do
    [[ "$(fdtget "$fixture_dir/$name.dtb" /cpus/cpu@0 "$cache-cache-size")" == "16384" ]]
    [[ "$(fdtget "$fixture_dir/$name.dtb" /cpus/cpu@0 "$cache-cache-sets")" == "64" ]]
    [[ "$(fdtget "$fixture_dir/$name.dtb" /cpus/cpu@0 "$cache-cache-block-size")" == "64" ]]
  done
done

for name in single-core-rv5stage-soc tiled-rv5stage-soc; do
  case " $(fdtget "$fixture_dir/$name.dtb" /cpus/cpu@0 riscv,isa-extensions) " in
    *" zcmop "*) ;;
    *) echo "$name DTB does not advertise Zcmop" >&2; exit 1 ;;
  esac
done

for name in single-core-rv5stage-soc tiled-rv5stage-soc; do
  for extension in zfh zvfh zvkt zvkb zvl32b zvl64b zvl128b zcb zfa zicbom ssnpm supm; do
    case " $(fdtget "$fixture_dir/$name.dtb" /cpus/cpu@0 riscv,isa-extensions) " in
      *" $extension "*) ;;
      *) echo "$name DTB does not advertise $extension" >&2; exit 1 ;;
    esac
  done
done
for name in mini-rv5stage-soc; do
  for extension in zcb zfa zicbom ssnpm supm zvfh zvkt; do
    case " $(fdtget "$fixture_dir/$name.dtb" /cpus/cpu@0 riscv,isa-extensions) " in
      *" $extension "*) echo "$name DTB unexpectedly advertises $extension" >&2; exit 1 ;;
      *) ;;
    esac
  done
done

[[ "$(fdtget "$fixture_dir/single-core-rv5stage-soc.dtb" / model)" == "Rhodium Single-Core RV5Stage SoC" ]]
[[ "$(fdtget "$fixture_dir/mini-rv5stage-soc.dtb" / model)" == "Rhodium Mini RV5Stage SoC" ]]
[[ "$(fdtget "$fixture_dir/tiled-rv5stage-soc.dtb" / model)" == "Rhodium Tiled RV5Stage SoC" ]]
[[ "$(fdtget -t x "$fixture_dir/single-core-rv5stage-soc.dtb" /memory@80000000 reg)" == "0 80000000 0 40000000" ]]
[[ "$(fdtget -t x "$fixture_dir/mini-rv5stage-soc.dtb" /memory@80000000 reg)" == "0 80000000 0 10000" ]]
[[ "$(fdtget -t x "$fixture_dir/tiled-rv5stage-soc.dtb" /memory@80000000 reg)" == "0 80000000 0 40000000" ]]
[[ "$(fdtget "$fixture_dir/single-core-rv5stage-soc.dtb" /cpus timebase-frequency)" == "100000000" ]]
[[ "$(fdtget "$fixture_dir/tiled-rv5stage-soc.dtb" /cpus timebase-frequency)" == "1000000" ]]
[[ "$(fdtget "$fixture_dir/single-core-rv5stage-soc.dtb" /cpus/cpu@0 riscv,isa-base)" == "rv64i" ]]
[[ "$(fdtget "$fixture_dir/single-core-rv5stage-soc.dtb" /cpus/cpu@0 mmu-type)" == "riscv,sv39" ]]
for cache in i d; do
  [[ "$(fdtget "$fixture_dir/single-core-rv5stage-soc.dtb" /cpus/cpu@0 "$cache-cache-size")" == "16384" ]]
  [[ "$(fdtget "$fixture_dir/single-core-rv5stage-soc.dtb" /cpus/cpu@0 "$cache-cache-sets")" == "64" ]]
  [[ "$(fdtget "$fixture_dir/single-core-rv5stage-soc.dtb" /cpus/cpu@0 "$cache-cache-block-size")" == "64" ]]
done
[[ "$(fdtget -t x "$fixture_dir/single-core-rv5stage-soc.dtb" /soc/clint@2000000 interrupts-extended)" == "1 3 1 7" ]]
[[ "$(fdtget -t x "$fixture_dir/tiled-rv5stage-soc.dtb" /soc/clint@2000000 interrupts-extended)" == "1 3 1 7 2 3 2 7 3 3 3 7 4 3 4 7 5 3 5 7 6 3 6 7 7 3 7 7 8 3 8 7" ]]
[[ "$(fdtget -l "$fixture_dir/tiled-rv5stage-soc.dtb" / | grep -c '^memory@')" == "1" ]]
grep -Fq 'Rhodium Tiled RV5Stage SoC' "$fixture_dir/tiled-rv5stage-soc.dump"
