#!/usr/bin/env bash
# Enforces ownership, dependency, and generated-file boundaries for processor cores.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

search_sources() {
  local pattern="$1"
  shift
  if command -v rg >/dev/null 2>&1; then
    rg -n "$pattern" "$@" --glob '*.rhm' --glob '*.rhdl'
  else
    find "$@" -type f \( -name '*.rhm' -o -name '*.rhdl' \) \
      -exec grep -nHE "$pattern" {} +
  fi
}

search_production_sources() {
  local pattern="$1"
  if command -v rg >/dev/null 2>&1; then
    rg -n "$pattern" cores --glob '*.rhm' --glob '*.rhdl' \
      --glob '!**/tests/**'
  else
    find cores -type f \( -name '*.rhm' -o -name '*.rhdl' \) \
      ! -path '*/tests/*' -exec grep -nHE "$pattern" {} +
  fi
}

forbidden_imports="$(search_production_sources \
  '^[[:space:]]+"[^"]*(rhodium/backend|tests/|examples/)' || true)"
if [[ -n "$forbidden_imports" ]]; then
  echo "processor sources must not import backends, tests, or examples" >&2
  echo "$forbidden_imports" >&2
  exit 1
fi

component_domain_imports="$(
  search_sources '^[[:space:]]+"[^"]*(riscv/|rv5stage/)' \
    cores/alu.rhdl cores/branch-resolver.rhdl cores/cache-prefetch.rhdl \
    cores/cache-replacement.rhdl \
    cores/load-store.rhdl \
    cores/multiplier.rhdl cores/divider.rhdl cores/simd-alu.rhdl \
    | grep -Ev 'riscv/isa/xlen\.rhm' \
    || true
)"
if [[ -n "$component_domain_imports" ]]; then
  echo "reusable processor components may import XLen but not instruction catalogs or named cores" >&2
  echo "$component_domain_imports" >&2
  exit 1
fi

riscv_mapping_named_core_imports="$(search_sources '^[[:space:]]+"[^" ]*rv5stage/' cores/riscv || true)"
if [[ -n "$riscv_mapping_named_core_imports" ]]; then
  echo "reusable RISC-V component mappings must not import named cores" >&2
  echo "$riscv_mapping_named_core_imports" >&2
  exit 1
fi

component_control_imports="$(search_sources '^[[:space:]]+"(alu|operand|branch|mem|writeback|system)-ctrl\.rhdl"' \
  cores/rv5stage/decode/alu-ctrl.rhdl \
  cores/rv5stage/decode/operand-ctrl.rhdl \
  cores/rv5stage/decode/branch-ctrl.rhdl \
  cores/rv5stage/decode/mem-ctrl.rhdl \
  cores/rv5stage/decode/multiply-ctrl.rhdl \
  cores/rv5stage/decode/divide-ctrl.rhdl \
  cores/rv5stage/decode/writeback-ctrl.rhdl \
  cores/rv5stage/decode/system-ctrl.rhdl || true)"
if [[ -n "$component_control_imports" ]]; then
  echo "RV5Stage component control decoders must not import sibling control decoders" >&2
  echo "$component_control_imports" >&2
  exit 1
fi

pipeline_transport_imports="$(search_sources 'simple-memory' \
  cores/rv5stage/core.rhdl || true)"
if [[ -n "$pipeline_transport_imports" ]]; then
  echo "RV5Stage pipeline must depend on its cache protocol, not SimpleMemory" >&2
  echo "$pipeline_transport_imports" >&2
  exit 1
fi

alternate_core_sources="$(find cores/rv5stage -maxdepth 1 -type f -name 'core-*.rhdl' -print)"
if [[ -n "$alternate_core_sources" ]]; then
  echo "RV5Stage must keep one authoritative core implementation" >&2
  echo "$alternate_core_sources" >&2
  exit 1
fi

legacy_rv5stage_fetch_sources="$(find cores/rv5stage -maxdepth 1 -type f \
  \( -name 'frontend.rhdl' -o -name 'frontend-control.rhdl' \
     -o -name 'fetch-source.rhdl' -o -name 'fetch-packet.rhdl' \
     -o -name 'fetch-scan.rhdl' -o -name 'instruction-buffer.rhdl' \
     -o -name 'btb.rhdl' -o -name 'ras.rhdl' \) -print)"
if [[ -n "$legacy_rv5stage_fetch_sources" ]]; then
  echo "RV5Stage fetch and predictor sources must live under cores/rv5stage/fetch" >&2
  echo "$legacy_rv5stage_fetch_sources" >&2
  exit 1
fi

bpd_consumer_imports="$(search_sources \
  '^[[:space:]]+"\.\./(frontend|source|packet|scan|instruction-buffer|protocol)\.rhdl"' \
  cores/rv5stage/fetch/bpd || true)"
if [[ -n "$bpd_consumer_imports" ]]; then
  echo "RV5Stage branch predictors must not import their fetch consumers" >&2
  echo "$bpd_consumer_imports" >&2
  exit 1
fi

fetch_protocol_implementation_imports="$(search_sources \
  '^[[:space:]]+"bpd/(btb|ras)\.rhdl"' \
  cores/rv5stage/fetch/protocol.rhdl cores/rv5stage/fetch/packet.rhdl || true)"
if [[ -n "$fetch_protocol_implementation_imports" ]]; then
  echo "RV5Stage fetch protocols must not depend on predictor implementations" >&2
  echo "$fetch_protocol_implementation_imports" >&2
  exit 1
fi

cache_cross_imports="$(search_sources '^[[:space:]]+"[^" ]*(icache|dcache)/' \
  cores/rv5stage/icache cores/rv5stage/dcache \
  || true)"
if [[ -n "$cache_cross_imports" ]]; then
  echo "RV5Stage instruction and data cache packages must not import each other" >&2
  echo "$cache_cross_imports" >&2
  exit 1
fi

chi_cache_implementation_imports="$(search_sources \
  '^[[:space:]]+"\.\./(icache|dcache)/cache\.rhdl"' \
  cores/rv5stage/chi || true)"
if [[ -n "$chi_cache_implementation_imports" ]]; then
  echo "RV5Stage CHI engines may import cache protocols but not cache implementations" >&2
  echo "$chi_cache_implementation_imports" >&2
  exit 1
fi

legacy_rv5stage_chi_sources="$(find cores/rv5stage -maxdepth 1 -type f \
  \( -name 'chi.rhdl' -o -name 'refill.rhdl' -o -name 'write-unique.rhdl' \
     -o -name 'writeback.rhdl' -o -name 'snoop.rhdl' -o -name 'uncached.rhdl' \) \
  -print)"
if [[ -n "$legacy_rv5stage_chi_sources" ]]; then
  echo "RV5Stage CHI configuration and transaction engines must live under cores/rv5stage/chi" >&2
  echo "$legacy_rv5stage_chi_sources" >&2
  exit 1
fi

unexpected_root_sources="$(find cores -maxdepth 1 -type f \( -name '*.rhm' -o -name '*.rhdl' \) \
  ! -name 'alu.rhdl' ! -name 'branch-resolver.rhdl' \
  ! -name 'cache-prefetch.rhdl' ! -name 'cache-replacement.rhdl' \
  ! -name 'load-store.rhdl' ! -name 'multiplier.rhdl' \
  ! -name 'divider.rhdl' ! -name 'simd-alu.rhdl' -print)"
if [[ -n "$unexpected_root_sources" ]]; then
  echo "only reusable processor components may live directly under cores/" >&2
  echo "$unexpected_root_sources" >&2
  exit 1
fi

compiled_directories="$(find cores -type d -name compiled -print)"
if [[ -n "$compiled_directories" ]]; then
  echo "generated Racket bytecode must not live under cores/" >&2
  echo "$compiled_directories" >&2
  exit 1
fi
