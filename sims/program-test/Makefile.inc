# Runs single-core workload suites and capability-filtered ISA smoke on concrete SoCs.
# SPDX-License-Identifier: Apache-2.0
RISCV_TESTS_DIR ?= $(REPO_DIR)/sw/riscv-isa-tests
COREMARK_DIR ?= $(REPO_DIR)/sw/coremark
COREMARK_PORT_DIR ?= $(REPO_DIR)/sw/coremark-riscv-baremetal
EMBENCH_DIR ?= $(REPO_DIR)/sw/embench-iot
EMBENCH_PORT_DIR ?= $(REPO_DIR)/sw/embench-iot-riscv-baremetal
BRINGUP_DIR ?= $(REPO_DIR)/sw/bringup-bench
BRINGUP_PATCH_SERIES ?= $(REPO_DIR)/sw/bringup-bench-patches/series
BRINGUP_PORT_DIR ?= $(REPO_DIR)/sw/bringup-bench-riscv-baremetal
PROGRAM_BUILD_ROOT ?= /tmp/rhodium-program-tests
PROGRAM_JOBS ?= 1
PROGRAM_TIMEOUT ?= 300
PROGRAM_MAX_CYCLES ?= 10000000
BENCHMARK_MAX_CYCLES ?= 100000000
BENCHMARK_MODE ?= target
NATIVE_SOFTWARE_SOCS := single single-core-rv5stage-soc single-core-spike-soc
SOFTWARE_SOC ?= single
SOFTWARE_CORE ?= rv5stage spike
SMOKE_SOCS := mini tiled mini-rv5stage-soc tiled-rv5stage-soc
TILED_MT_SOCS := tiled tiled-rv5stage-soc
COREMARK_ITERATIONS ?= 1
COREMARK_MAX_CYCLES ?= 100000000
EMBENCH_SCALE ?= 1
EMBENCH_LOCAL_SCALE ?= 1
EMBENCH_WARMUP_HEAT ?= 0
EMBENCH_MAX_CYCLES ?= 100000000
BRINGUP_MAX_CYCLES ?= 100000000
BRINGUP_BENCHMARKS ?=
PROGRAM_PRODUCT_ROOT := $(PROGRAM_BUILD_ROOT)/$(SOC_ID)
ISA_SMOKE_DIR := $(PROGRAM_PRODUCT_ROOT)/isa-smoke
TILED_MT_BENCHMARK_DIR := $(PROGRAM_PRODUCT_ROOT)/mt-benchmark
TILED_MT_HART_COUNTS := 2 4 8
PROGRAM_TARGET_SELECTION := $(if $(filter mini single tiled,$(SOC)),$(SOC) $(CORE),$(SOC))
PROGRAM_TARGET ?= $(dir $(SIMULATOR_BINARY))program-target.json

.PHONY: program-test-setup isa-setup benchmark-setup coremark-setup coremark_scalar-setup embench-setup bringup-setup program-test-adapter-test program-target simulator-attestation
.PHONY: isa-elfs benchmark-elfs tiled-mt-benchmark-elfs coremark-elf coremark_scalar-elf embench-elfs bringup-elfs isa-test benchmark-test tiled-mt-benchmark-test coremark-test coremark_scalar-test embench-test bringup-test
.PHONY: single-core-software-test
.PHONY: isa-smoke-config isa-smoke-elfs isa-smoke
program-target: $(PROGRAM_TARGET)

ifeq ($(PREBUILT_SIMULATOR),)
$(PROGRAM_TARGET): $(REPO_DIR)/sims/program-test/write-target.rhm $(SIMULATOR_SOURCES)
	mkdir -p "$(dir $(PROGRAM_TARGET))"
	cd "$(REPO_DIR)" && tools/run-racket.sh -S "$(REPO_DIR)" sims/program-test/write-target.rhm $(PROGRAM_TARGET_SELECTION) "$(PROGRAM_TARGET)"
else
$(PROGRAM_TARGET):
	@echo "Prebuilt simulator target descriptor is missing: $@" >&2; exit 2
endif

isa-smoke-config:
	@test -n "$(filter $(SOC),$(SMOKE_SOCS))" || (echo 'ISA smoke supports SOC=$(SMOKE_SOCS)' >&2; exit 2)
	mkdir -p "$(ISA_SMOKE_DIR)"
	cd "$(REPO_DIR)" && tools/run-racket.sh -S "$(REPO_DIR)" sims/program-test/write-target.rhm $(PROGRAM_TARGET_SELECTION) "$(ISA_SMOKE_DIR)/target.json"

isa-smoke-elfs: isa-smoke-config
	$(PYTHON) $(REPO_DIR)/sw/build/build.py --suite isa --source "$(RISCV_TESTS_DIR)" \
	  --output "$(ISA_SMOKE_DIR)" --compiler "$(RISCV_CC)" --target "$(ISA_SMOKE_DIR)/target.json" \
	  --isa-selection smoke

isa-smoke: isa-smoke-elfs simulator-attestation
	$(PYTHON) program-test/run.py --manifest "$(ISA_SMOKE_DIR)/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	  --output "$(ISA_SMOKE_DIR)/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(PROGRAM_MAX_CYCLES)
isa-setup benchmark-setup:
	git -C $(REPO_DIR) submodule update --init sw/riscv-isa-tests
	git -C $(RISCV_TESTS_DIR) submodule update --init env

coremark-setup:
	git -C $(REPO_DIR) submodule update --init sw/coremark

coremark_scalar-setup: coremark-setup

embench-setup:
	git -C $(REPO_DIR) submodule update --init sw/embench-iot

bringup-setup:
	git -C $(REPO_DIR) submodule update --init sw/bringup-bench

program-test-setup: isa-setup coremark-setup embench-setup bringup-setup

program-test-adapter-test:
	$(PYTHON) -m unittest discover -s tests -p 'test_program_test.py'
	$(PYTHON) -m unittest discover -s $(REPO_DIR)/sw/tests -p 'test_program_build.py'
	$(PYTHON) -m unittest discover -s $(REPO_DIR)/sw/tests -p 'test_bringup_bench.py'

isa-elfs: program-target
	@test -n "$(filter $(SOC),$(NATIVE_SOFTWARE_SOCS))" || (echo 'The full ISA suite supports SOC=$(NATIVE_SOFTWARE_SOCS)' >&2; exit 2)
	$(PYTHON) $(REPO_DIR)/sw/build/build.py --suite $(@:-elfs=) --source "$(RISCV_TESTS_DIR)" \
	  --output "$(PROGRAM_PRODUCT_ROOT)/$(@:-elfs=)" --compiler "$(RISCV_CC)" \
	  --target "$(PROGRAM_TARGET)" --isa-selection full

benchmark-elfs: program-target
	@test -n "$(filter $(SOC),$(NATIVE_SOFTWARE_SOCS))" || (echo 'The full benchmark suite supports SOC=$(NATIVE_SOFTWARE_SOCS)' >&2; exit 2)
	$(PYTHON) $(REPO_DIR)/sw/build/build.py --suite benchmark --source "$(RISCV_TESTS_DIR)" \
	  --output "$(PROGRAM_PRODUCT_ROOT)/benchmark" --compiler "$(RISCV_CC)" \
	  --target "$(PROGRAM_TARGET)" --benchmark-mode "$(BENCHMARK_MODE)"

tiled-mt-benchmark-elfs: program-target
	@test "$(CORE)" = rv5stage && test -n "$(filter $(SOC),$(TILED_MT_SOCS))" || \
	  (echo 'Multihart benchmarks require the tiled RV5Stage SoC' >&2; exit 2)
	@set -e; for hart_count in $(TILED_MT_HART_COUNTS); do \
	  output="$(TILED_MT_BENCHMARK_DIR)/$$hart_count-harts"; \
	  mkdir -p "$$output"; \
	  $(PYTHON) $(REPO_DIR)/sw/build/build.py --suite benchmark --source "$(RISCV_TESTS_DIR)" \
	    --output "$$output" --compiler "$(RISCV_CC)" --target "$(PROGRAM_TARGET)" \
	    --benchmark-mode target --benchmark-selection multihart \
	    --benchmark-hart-count "$$hart_count"; \
	done

coremark-elf: program-target
	@test -n "$(filter $(SOC),$(NATIVE_SOFTWARE_SOCS))" || (echo 'CoreMark supports SOC=$(NATIVE_SOFTWARE_SOCS)' >&2; exit 2)
	$(PYTHON) $(REPO_DIR)/sw/build/build-coremark.py --source "$(COREMARK_DIR)" --port "$(COREMARK_PORT_DIR)" \
	  --output "$(PROGRAM_PRODUCT_ROOT)/coremark" --compiler "$(RISCV_CC)" \
	  --target "$(PROGRAM_TARGET)" --iterations "$(COREMARK_ITERATIONS)"

coremark_scalar-elf: program-target
	@test -n "$(filter $(SOC),$(NATIVE_SOFTWARE_SOCS))" || (echo 'CoreMark scalar supports SOC=$(NATIVE_SOFTWARE_SOCS)' >&2; exit 2)
	$(PYTHON) $(REPO_DIR)/sw/build/build-coremark.py --source "$(COREMARK_DIR)" --port "$(COREMARK_PORT_DIR)" \
	  --output "$(PROGRAM_PRODUCT_ROOT)/coremark_scalar" --compiler "$(RISCV_CC)" \
	  --target "$(PROGRAM_TARGET)" --iterations "$(COREMARK_ITERATIONS)" --variant coremark_scalar

embench-elfs: program-target
	@test -n "$(filter $(SOC),$(NATIVE_SOFTWARE_SOCS))" || (echo 'Embench-IoT supports SOC=$(NATIVE_SOFTWARE_SOCS)' >&2; exit 2)
	$(PYTHON) $(REPO_DIR)/sw/build/build-embench.py --source "$(EMBENCH_DIR)" --port "$(EMBENCH_PORT_DIR)" \
	  --output "$(PROGRAM_PRODUCT_ROOT)/embench" --compiler "$(RISCV_CC)" \
	  --target "$(PROGRAM_TARGET)" --scale "$(EMBENCH_SCALE)" \
	  --local-scale "$(EMBENCH_LOCAL_SCALE)" --warmup-heat "$(EMBENCH_WARMUP_HEAT)"

bringup-elfs: program-target
	@test -n "$(filter $(SOC),$(NATIVE_SOFTWARE_SOCS))" || (echo 'Bringup-Bench supports SOC=$(NATIVE_SOFTWARE_SOCS)' >&2; exit 2)
	$(PYTHON) $(REPO_DIR)/sw/build/build-bringup-bench.py --source "$(BRINGUP_DIR)" \
	  --patch-series "$(BRINGUP_PATCH_SERIES)" --port "$(BRINGUP_PORT_DIR)" \
	  --output "$(PROGRAM_PRODUCT_ROOT)/bringup" --compiler "$(RISCV_CC)" \
	  --target "$(PROGRAM_TARGET)" $(if $(BRINGUP_BENCHMARKS),--benchmarks "$(BRINGUP_BENCHMARKS)",)

isa-test: isa-elfs simulator-attestation
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_PRODUCT_ROOT)/isa/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	  --output "$(PROGRAM_PRODUCT_ROOT)/isa/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(PROGRAM_MAX_CYCLES)

simulator-attestation: simulator program-target
	$(PYTHON) program-test/artifact.py $(if $(PREBUILT_SIMULATOR),verify,record) \
	  --binary "$(SIMULATOR_BINARY)" --soc "$(SOC_ID)" --target "$(PROGRAM_TARGET)"

benchmark-test: benchmark-elfs simulator-attestation
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_PRODUCT_ROOT)/benchmark/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	  --output "$(PROGRAM_PRODUCT_ROOT)/benchmark/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(BENCHMARK_MAX_CYCLES)

tiled-mt-benchmark-test: tiled-mt-benchmark-elfs simulator-attestation
	@status=0; for hart_count in $(TILED_MT_HART_COUNTS); do \
	  output="$(TILED_MT_BENCHMARK_DIR)/$$hart_count-harts"; \
	  $(PYTHON) program-test/run.py --manifest "$$output/manifest.json" \
	    --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	    --output "$$output/results" --jobs $(PROGRAM_JOBS) \
	    --timeout $(PROGRAM_TIMEOUT) --max-cycles $(BENCHMARK_MAX_CYCLES) || status=1; \
	done; exit $$status

coremark-test: coremark-elf simulator-attestation
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_PRODUCT_ROOT)/coremark/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	  --output "$(PROGRAM_PRODUCT_ROOT)/coremark/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(COREMARK_MAX_CYCLES)

coremark_scalar-test: coremark_scalar-elf simulator-attestation
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_PRODUCT_ROOT)/coremark_scalar/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	  --output "$(PROGRAM_PRODUCT_ROOT)/coremark_scalar/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(COREMARK_MAX_CYCLES)

embench-test: embench-elfs simulator-attestation
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_PRODUCT_ROOT)/embench/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	  --output "$(PROGRAM_PRODUCT_ROOT)/embench/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(EMBENCH_MAX_CYCLES)

bringup-test: bringup-elfs simulator-attestation
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_PRODUCT_ROOT)/bringup/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	  --output "$(PROGRAM_PRODUCT_ROOT)/bringup/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(BRINGUP_MAX_CYCLES)

single-core-software-test:
	@status=0; for core in $(SOFTWARE_CORE); do \
	  for suite in isa benchmark coremark coremark_scalar embench; do \
	    $(MAKE) "$$suite-test" SOC=$(SOFTWARE_SOC) CORE="$$core" || status=1; \
	  done; \
	  $(MAKE) bringup-test SOC=$(SOFTWARE_SOC) CORE="$$core" \
	    BRINGUP_BENCHMARKS=anagram,blake2b,bubble-sort,checkers,fft-int,lz-compress,pi-calc,rho-factor || status=1; \
	  $(MAKE) arch-test SOC=$(SOFTWARE_SOC) CORE="$$core" ACT_CONFIGURATION="single-core-$$core-soc" || status=1; \
	  $(MAKE) opensbi-test SOC=$(SOFTWARE_SOC) CORE="$$core" || status=1; \
	done; exit "$$status"
