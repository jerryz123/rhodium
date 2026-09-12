#!/usr/bin/env bash
# Builds banked RV5Stage RTL, offline native models, and matched simulator binaries.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
stage="${1:?expected rtl, extract, optimize, native, or verilator}"
harts="${2:-8}"
threads="${3:-8}"
root="${RDS_VVADD_BUILD_DIR:-/tmp/rhodium-vvadd}"
out="$root/harts-$harts"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
cd "$repo"
exec 9>/tmp/rhodium-single-worker-perf.lock
flock -s 9
flags='-O3 -march=native -DNDEBUG'
case "$stage" in
extract)
    if [[ -z "${PLTCOMPILEDROOTS:-}" ]]; then
        vvadd_compiled_root="$(mktemp -d /tmp/rhodium-vvadd-compiled.XXXXXX)"
        trap 'rm -rf "$vvadd_compiled_root"' EXIT
        export PLTCOMPILEDROOTS="$vvadd_compiled_root"
    fi
    export PLTCOLLECTS="$repo:" RDS_HARTS="$harts" RDS_EXTRACTED="$out/extracted.json"
    "${RACKET:-$repo/.tools/racket-9.2/bin/racket}" -y sims/native/emit-banked.rhm
    ;;
optimize)
    rhodium/sim/compiler/run.sh --input "$out/extracted.json" --output "$out/model.rsim" \
      --report "$out/optimized.json" --timing "$out/optimizer-timing.json" --release
    rhodium/sim/compiler/run.sh --input "$out/optimized.json" --no-optimize \
      --snapshot-prefix-work 16 --output "$out/model-parallel.rsim" \
      --report "$out/parallel.json" --timing "$out/parallel-timing.json"
    ;;
native)
    default_specialization=none
    if ((threads==1)); then default_specialization=empty; fi
    specialization="${RDS_VVADD_SPECIALIZE:-$default_specialization}"
    case "$specialization" in
      none) ;;
      empty)
        if ((threads!=1)); then printf 'empty specialization requires one worker\n' >&2; exit 2; fi
        if [[ ! -s "$out/optimized.json" ]]; then
            printf 'empty specialization requires optimized.json; run optimize or set RDS_VVADD_SPECIALIZE=none\n' >&2
            exit 2
        fi ;;
      *) printf 'unsupported vvadd specialization: %s\n' "$specialization" >&2; exit 2 ;;
    esac
    local_guards="${RDS_VVADD_LOCAL_GUARDS:-0}"
    if [[ ! "$local_guards" =~ ^(0|[1-9][0-9]{0,2})$ ]] || ((local_guards!=0&&(local_guards<8||local_guards>256))); then
        printf 'RDS_VVADD_LOCAL_GUARDS must be 0 or 8..256\n' >&2; exit 2
    fi
    if ((local_guards!=0)) && [[ "$specialization" != empty ]]; then
        printf 'local guards require empty specialization\n' >&2; exit 2
    fi
    native="$out/native-$threads"
    mkdir -p "$native/runtime"
    model="$out/model.rsim"
    source_ir="$out/optimized.json"
    if ((threads>1)) && [[ -s "$out/model-parallel.rsim" ]]; then
        model="$out/model-parallel.rsim"
        source_ir="$out/parallel.json"
    fi
    cp "$model" "$native/model.rsim"
    matcher_masks="${RDS_VVADD_MATCHER_MASKS:-0}"
    if [[ -z "${RDS_VVADD_MATCHER_MASKS+x}" && "${RDS_VVADD_LIFT_CONTRACTS:-0}" == 1 && "$threads" == 1 ]]; then matcher_masks=1; fi
    case "$matcher_masks" in 0|1) ;; *) printf 'RDS_VVADD_MATCHER_MASKS must be 0 or 1\n' >&2; exit 2 ;; esac
    offline_options=()
    specialize_decoders="${RDS_VVADD_SPECIALIZE_DECODERS:-0}"
    case "$specialize_decoders" in
      0) ;; 1) offline_options+=(--specialize-decoders) ;;
      *) printf 'RDS_VVADD_SPECIALIZE_DECODERS must be 0 or 1\n' >&2; exit 2 ;;
    esac
    stationary_matchers="${RDS_VVADD_FOLD_STATIONARY_MATCHERS:-0}"
    case "$stationary_matchers" in
      0) ;; 1) offline_options+=(--fold-stationary-matchers) ;;
      *) printf 'RDS_VVADD_FOLD_STATIONARY_MATCHERS must be 0 or 1\n' >&2; exit 2 ;;
    esac
    idle_fifos="${RDS_VVADD_FOLD_IDLE_FIFOS:-0}"
    case "$idle_fifos" in
      0) ;; 1) offline_options+=(--fold-idle-fifos) ;;
      *) printf 'RDS_VVADD_FOLD_IDLE_FIFOS must be 0 or 1\n' >&2; exit 2 ;;
    esac
    if [[ "$matcher_masks" == 1 ]]; then offline_options+=(--simplify-matcher-masks); fi
    indexed_datapaths="${RDS_VVADD_INDEXED_DATAPATHS:-0}"
    case "$indexed_datapaths" in
      0) ;; 1) offline_options+=(--lift-indexed-datapaths) ;;
      *) printf 'RDS_VVADD_INDEXED_DATAPATHS must be 0 or 1\n' >&2; exit 2 ;;
    esac
    reuse_grants="${RDS_VVADD_REUSE_GRANTS:-0}"
    case "$reuse_grants" in
      0) ;; 1) offline_options+=(--reuse-matcher-grants) ;;
      *) printf 'RDS_VVADD_REUSE_GRANTS must be 0 or 1\n' >&2; exit 2 ;;
    esac
    case "${RDS_VVADD_LIFT_CONTRACTS:-0}" in
      0) ;;
      1) offline_options+=(--lift-contracts) ;;
      *) printf 'RDS_VVADD_LIFT_CONTRACTS must be 0 or 1\n' >&2; exit 2 ;;
    esac
    handshake_rows="${RDS_VVADD_SHARE_HANDSHAKES:-0}"
    case "$handshake_rows" in
      0) ;; 1) offline_options+=(--share-handshake-rows) ;;
      *) printf 'RDS_VVADD_SHARE_HANDSHAKES must be 0 or 1\n' >&2; exit 2 ;;
    esac
    if ((${#offline_options[@]})); then
        if [[ ! -s "$source_ir" ]]; then
            printf 'contract lifting requires the selected model JSON snapshot\n' >&2; exit 2
        fi
        # Establish exact image correspondence before replacing the model with
        # its lifted form. A stale JSON report must not silently choose a design.
        rhodium/sim/compiler/run.sh --input "$source_ir" --no-optimize --output "$native/source.rsim"
        if ! cmp -s "$model" "$native/source.rsim"; then
            printf 'contract lifting JSON does not match the selected model image\n' >&2; exit 2
        fi
        rhodium/sim/compiler/run.sh --input "$source_ir" --no-optimize "${offline_options[@]}" \
          --output "$native/model.rsim" --report "$native/contract-ir.json" --timing "$native/contract-timing.json"
        source_ir="$native/contract-ir.json"
    fi
    for source in rhodium/sim/runtime/*.c; do
        "${CC:-clang}" -O3 -march=native -DNDEBUG -std=c17 -pthread -c "$source" -o "$native/runtime/$(basename "${source%.c}").o"
    done
    ar rcs "$native/runtime/librds.a" "$native/runtime/"*.o
    "${CXX:-clang++}" -O3 -march=native -DNDEBUG sims/native/native-vvadd.cpp "$native/runtime/librds.a" -pthread -ldl -o "$native/native-vvadd"
    "${CC:-clang}" -O3 -march=native -DNDEBUG sims/native/compile-model.c "$native/runtime/librds.a" -pthread -ldl -o "$native/compile-model"
    default_flags=4290056208
    if ((threads>1)); then default_flags=4088747088; fi
    export RDS_WORKERS="$threads" RDS_FLAGS="${RDS_FLAGS:-$default_flags}" RDS_PLAN_REPORT="$native/plan.json"
    "$native/compile-model" "$native/model.rsim" "$native/model.c"
    if [[ "$specialization" == empty ]]; then
        mv "$native/model.c" "$native/model-unspecialized.c"
        specializer_options=(--partial --matcher-ancestors)
        if ((local_guards!=0)); then specializer_options+=(--local-guards "$local_guards"); fi
        bash sims/native/specialize-empty.sh "$source_ir" "$native/plan.json" \
          "$native/model-unspecialized.c" "$native/model.c" "$native/specialization.json" "${specializer_options[@]}"
    else
        rm -f "$native/model-unspecialized.c" "$native/specialization.json"
    fi
    "${CC:-clang}" -O3 -march=native -DNDEBUG -fPIC -shared "$native/model.c" -o "$native/model.so"
    printf '%s\n' "$RDS_FLAGS" > "$native/flags"
    printf '%s\n' "$specialization" > "$native/specialization"
    printf '%s\n' "$local_guards" > "$native/local-guards"
    printf '%s\n' "$idle_fifos" > "$native/idle-fifos"
    printf '%s\n' "$handshake_rows" > "$native/handshake-rows"
    printf '%s\n' "$specialize_decoders" > "$native/specialize-decoders"
    printf '%s\n' "$stationary_matchers" > "$native/stationary-matchers"
    printf '%s\n' "${RDS_VVADD_LIFT_CONTRACTS:-0}" > "$native/contract-lifting"
    printf '%s\n' "$matcher_masks" > "$native/matcher-masks"
    printf '%s\n' "$indexed_datapaths" > "$native/indexed-datapaths"
    printf '%s\n' "$reuse_grants" > "$native/matcher-grants"
    sha256sum "$native/model.rsim" "$native/native-vvadd" "$native/model.c" "$native/model.so" > "$native/artifacts.sha256"
    ;;
rtl)
    if [[ -z "${PLTCOMPILEDROOTS:-}" ]]; then
        vvadd_compiled_root="$(mktemp -d /tmp/rhodium-vvadd-compiled.XXXXXX)"
        trap 'rm -rf "$vvadd_compiled_root"' EXIT
        export PLTCOMPILEDROOTS="$vvadd_compiled_root"
    fi
    export PLTCOLLECTS="$repo:" RDS_HARTS="$harts"
    "${RACKET:-$repo/.tools/racket-9.2/bin/racket}" -y sims/emit-soc-harness.rhm sims/banked-soc-harness.rhdl > "$out/model.mlir"
    "${CIRCT_OPT:-$repo/.tools/firtool-1.155.0/bin/circt-opt}" --canonicalize --cse --prettify-verilog \
      --lower-seq-hlmem --lower-sim-to-sv --lower-verif-to-sv \
      --lower-seq-to-sv='disable-mem-randomization=true disable-reg-randomization=true' --lower-seq-firmem \
      --hw-memory-sim='disable-mem-randomization=true disable-reg-randomization=true read-enable-mode=undefined' \
      --sv-mask-non-synthesizable='mode=ifdef macro=SYNTHESIS' \
      --export-verilog "$out/model.mlir" -o /dev/null > "$out/model.sv"
    ;;
verilator)
    "${RISCV_CLANG:-clang}" --target=riscv64-unknown-elf -march=rv64i_zicsr -mabi=lp64 -nostdlib \
      "-fuse-ld=${RISCV_LD:-lld}" -Wl,--build-id=none -Wl,-T,sims/tests/programs/vvadd.ld \
      sims/tests/programs/vvadd.S -o "$out/vvadd.elf"
    "${LLVM_OBJCOPY:-llvm-objcopy}" -O binary "$out/vvadd.elf" "$out/vvadd.bin"
    verilator --cc -O3 --threads "$threads" --no-assert --Wno-UNOPTFLAT --Wno-SYMRSVDWORD \
      --top-module SoCHarness --Mdir "$out/verilated-$threads" \
      --exe "$repo/sims/native/verilator-vvadd.cpp" \
      -CFLAGS "$flags -DRDS_VERILATOR_THREADS=$threads -DRDS_VERILATOR_HARTS=$harts" "$out/model.sv"
    make -C "$out/verilated-$threads" -f VSoCHarness.mk -j"${BUILD_JOBS:-4}" CXX="${CXX:-clang++}" LINK="${CXX:-clang++}" \
      "OPT_FAST=$flags" "OPT_SLOW=$flags" "OPT_GLOBAL=$flags" \
      "VM_USER_CFLAGS=$flags -DRDS_VERILATOR_THREADS=$threads -DRDS_VERILATOR_HARTS=$harts" \
      CXXFLAGS= OPT= USER_CPPFLAGS= USER_LDFLAGS= OBJCACHE=
    sha256sum "$out/model.sv" "$out/vvadd.bin" "$out/verilated-$threads/VSoCHarness" > "$out/verilated-$threads.sha256"
    ;;
*) printf 'unsupported stage: %s\n' "$stage" >&2; exit 2 ;;
esac
