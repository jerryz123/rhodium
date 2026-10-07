#!/usr/bin/env bash
# Enforces ownership, dependency, and generated-file boundaries for processor cores.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

# Search Rhombus/RHDL sources in the supplied paths using ripgrep or a portable fallback.
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

# Search all core production sources while excluding test fixtures from ownership checks.
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

# Search reusable components regardless of whether they are root modules or component families.
search_shared_sources() {
  local pattern="$1"
  if command -v rg >/dev/null 2>&1; then
    rg -n "$pattern" cores --glob '*.rhm' --glob '*.rhdl' \
      --glob '!**/tests/**' --glob '!cores/rv5stage/**' --glob '!cores/rv2wide/**' --glob '!cores/spike/**'
  else
    find cores -type f \( -name '*.rhm' -o -name '*.rhdl' \) \
      ! -path '*/tests/*' ! -path 'cores/rv5stage/*' \
      ! -path 'cores/rv2wide/*' ! -path 'cores/spike/*' \
      -exec grep -nHE "$pattern" {} +
  fi
}

forbidden_imports="$(search_production_sources \
  '^[[:space:]]+(lib\()?"[^"]*(rhodium/backend|sims/|tests/|examples/)' || true)"
if [[ -n "$forbidden_imports" ]]; then
  echo "processor sources must not import simulators, backends, tests, or examples" >&2
  echo "$forbidden_imports" >&2
  exit 1
fi

component_domain_imports="$(
  search_sources '^[[:space:]]+(lib\()?"[^"]*(riscv/|rv5stage/|rv2wide/|spike/)' \
    cores/alu.rhdl cores/branch-resolver.rhdl cores/cache-prefetch.rhdl \
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

shared_named_core_imports="$(search_shared_sources \
  '^[[:space:]]+(lib\()?"[^" ]*(rv5stage|rv2wide|spike)/' || true)"
if [[ -n "$shared_named_core_imports" ]]; then
  echo "shared processor components must not import named-core implementation or pipeline policy" >&2
  echo "$shared_named_core_imports" >&2
  exit 1
fi

rv2wide_named_core_imports="$(search_sources \
  '^[[:space:]]+(lib\()?"[^" ]*(rv5stage|spike)/' cores/rv2wide || true)"
if [[ -n "$rv2wide_named_core_imports" ]]; then
  echo "RV2Wide must reuse shared components, not import another named core" >&2
  echo "$rv2wide_named_core_imports" >&2
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
  echo "RV5Stage fetch belongs under cores/rv5stage/fetch; shared predictors belong under cores/bpred" >&2
  echo "$legacy_rv5stage_fetch_sources" >&2
  exit 1
fi

bpred_protocol_implementation_imports="$(search_sources \
  '^[[:space:]]+(lib\()?"([^" ]*/)?(btb|ras)\.rhdl"' \
  cores/bpred/protocol.rhdl || true)"
if [[ -n "$bpred_protocol_implementation_imports" ]]; then
  echo "branch prediction protocols must not import predictor implementations" >&2
  echo "$bpred_protocol_implementation_imports" >&2
  exit 1
fi

fetch_protocol_implementation_imports="$(search_sources \
  '^[[:space:]]+(lib\()?"[^" ]*bpred/(btb|ras)\.rhdl"' \
  cores/rv5stage/fetch/protocol.rhdl cores/rv5stage/fetch/packet.rhdl || true)"
if [[ -n "$fetch_protocol_implementation_imports" ]]; then
  echo "RV5Stage fetch protocols must not depend on predictor implementations" >&2
  echo "$fetch_protocol_implementation_imports" >&2
  exit 1
fi

chi_cache_implementation_imports="$(search_sources \
  '^[[:space:]]+(lib\()?"[^" ]*(icache|dcache|l1i|l1d)/cache\.rhdl"' \
  cores/rv5stage/chi cores/cache/chi || true)"
if [[ -n "$chi_cache_implementation_imports" ]]; then
  echo "CHI engines may import cache protocols but not cache implementations" >&2
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

compiled_directories="$(find cores -type d -name compiled -print)"
if [[ -n "$compiled_directories" ]]; then
  echo "generated Racket bytecode must not live under cores/" >&2
  echo "$compiled_directories" >&2
  exit 1
fi
