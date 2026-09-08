#!/usr/bin/env bash
# Cross-checks every concrete SoC's generated DTB against standard device-tree tools.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
for tool in dtc fdtdump fdtget; do
  if ! command -v "$tool" >/dev/null; then
    echo "SoC device-tree interoperability test requires $tool" >&2
    exit 1
  fi
done

fixture_dir="$(mktemp -d /tmp/rhodium-soc-devicetree.XXXXXX)"
compiled_root="${PLTCOMPILEDROOTS:-}"
owns_compiled_root=false
if [[ -z "$compiled_root" ]]; then
  compiled_root="$(mktemp -d /tmp/rhodium-soc-devicetree-compiled.XXXXXX)"
  owns_compiled_root=true
fi
cleanup() {
  rm -rf "$fixture_dir"
  if [[ "$owns_compiled_root" == true ]]; then
    rm -rf "$compiled_root"
  fi
}
trap cleanup EXIT

env PLTCOMPILEDROOTS="$compiled_root" PLTCOLLECTS="$repo_dir": \
  "$repo_dir/tools/run-racket.sh" "$repo_dir/socs/tests/write-device-trees.rhm" "$fixture_dir"

for name in simple mini tiled; do
  dtc -I dtb -O dts -o "$fixture_dir/$name-roundtrip.dts" "$fixture_dir/$name.dtb"
  dtc -I dts -O dtb -o "$fixture_dir/$name-from-dts.dtb" "$fixture_dir/$name.dts"
  fdtdump "$fixture_dir/$name.dtb" > "$fixture_dir/$name.dump" 2>&1
  cmp "$fixture_dir/$name.dtb" "$fixture_dir/$name-from-dts.dtb"
  # The public ISA advertisement must follow the default hardware profile.
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
  [[ "$name" != tiled ]] || hart_count=8
  for ((hart=0; hart<hart_count; hart++)); do
    cpu_phandle="$(fdtget -t x "$fixture_dir/$name.dtb" "/cpus/cpu@$hart/interrupt-controller" phandle)"
    contexts+=("$cpu_phandle" b "$cpu_phandle" 9)
  done
  [[ "$(fdtget -t x "$fixture_dir/$name.dtb" "$plic_path" interrupts-extended)" == "${contexts[*]}" ]]
done

for name in simple tiled; do
  case " $(fdtget "$fixture_dir/$name.dtb" /cpus/cpu@0 riscv,isa-extensions) " in
    *" zcmop "*) ;;
    *) echo "$name DTB does not advertise Zcmop" >&2; exit 1 ;;
  esac
done

[[ "$(fdtget "$fixture_dir/simple.dtb" / model)" == "Rhodium SimpleSoC" ]]
[[ "$(fdtget "$fixture_dir/mini.dtb" / model)" == "Rhodium MiniSoC" ]]
[[ "$(fdtget "$fixture_dir/tiled.dtb" / model)" == "Rhodium TiledSoC" ]]
[[ "$(fdtget -t x "$fixture_dir/simple.dtb" /memory@80000000 reg)" == "0 80000000 0 40000000" ]]
[[ "$(fdtget -t x "$fixture_dir/mini.dtb" /memory@80000000 reg)" == "0 80000000 0 10000" ]]
[[ "$(fdtget -t x "$fixture_dir/tiled.dtb" /memory@80000000 reg)" == "0 80000000 0 8000" ]]
[[ "$(fdtget "$fixture_dir/simple.dtb" /cpus timebase-frequency)" == "100000000" ]]
[[ "$(fdtget "$fixture_dir/tiled.dtb" /cpus timebase-frequency)" == "1000000" ]]
[[ "$(fdtget "$fixture_dir/simple.dtb" /cpus/cpu@0 riscv,isa-base)" == "rv64i" ]]
[[ "$(fdtget "$fixture_dir/simple.dtb" /cpus/cpu@0 mmu-type)" == "riscv,sv39" ]]
[[ "$(fdtget -t x "$fixture_dir/simple.dtb" /soc/clint@2000000 interrupts-extended)" == "1 3 1 7" ]]
[[ "$(fdtget -t x "$fixture_dir/tiled.dtb" /soc/clint@2000000 interrupts-extended)" == "1 3 1 7 2 3 2 7 3 3 3 7 4 3 4 7 5 3 5 7 6 3 6 7 7 3 7 7 8 3 8 7" ]]
[[ "$(fdtget -l "$fixture_dir/tiled.dtb" / | grep -c '^memory@')" == "1" ]]
grep -Fq 'Rhodium TiledSoC' "$fixture_dir/tiled.dump"
