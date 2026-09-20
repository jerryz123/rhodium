# Runs full SingleCoreRV5StageSoC workload suites and capability-filtered ISA smoke on concrete SoCs.
# SPDX-License-Identifier: Apache-2.0
RISCV_TESTS_DIR ?= $(REPO_DIR)/riscv/riscv-isa-tests
COREMARK_DIR ?= $(REPO_DIR)/sims/program-test/coremark
COREMARK_PORT_DIR ?= $(REPO_DIR)/sims/program-test/coremark-riscv-baremetal
PROGRAM_BUILD_ROOT ?= /tmp/rhodium-program-tests
PROGRAM_JOBS ?= 1
PROGRAM_TIMEOUT ?= 300
PROGRAM_MAX_CYCLES ?= 10000000
BENCHMARK_MAX_CYCLES ?= 100000000
BENCHMARK_MODE ?= target
COREMARK_ITERATIONS ?= 1
COREMARK_MAX_CYCLES ?= 100000000
ISA_SMOKE_DIR := $(PROGRAM_BUILD_ROOT)/$(SOC)/isa-smoke
PROGRAM_TARGET ?= $(dir $(SIMULATOR_BINARY))program-target.json

.PHONY: program-test-setup isa-setup benchmark-setup coremark-setup program-test-adapter-test program-target simulator-attestation
.PHONY: isa-elfs benchmark-elfs coremark-elf isa-test benchmark-test coremark-test program-test
.PHONY: isa-smoke-config isa-smoke-elfs isa-smoke
program-target: $(PROGRAM_TARGET)

ifeq ($(PREBUILT_SIMULATOR),)
$(PROGRAM_TARGET): $(REPO_DIR)/sims/program-test/write-target.rhm $(SIMULATOR_SOURCES)
	mkdir -p "$(dir $(PROGRAM_TARGET))"
	cd "$(REPO_DIR)" && tools/run-racket.sh -S "$(REPO_DIR)" sims/program-test/write-target.rhm "$(SOC)" "$(PROGRAM_TARGET)"
else
$(PROGRAM_TARGET):
	@echo "Prebuilt simulator target descriptor is missing: $@" >&2; exit 2
endif

isa-smoke-config:
	mkdir -p "$(ISA_SMOKE_DIR)"
	cd "$(REPO_DIR)" && tools/run-racket.sh -S "$(REPO_DIR)" sims/program-test/write-target.rhm "$(SOC)" "$(ISA_SMOKE_DIR)/target.json"

isa-smoke-elfs: isa-smoke-config
	$(PYTHON) program-test/build.py --suite isa --source "$(RISCV_TESTS_DIR)" \
	  --output "$(ISA_SMOKE_DIR)" --compiler "$(RISCV_CC)" --target "$(ISA_SMOKE_DIR)/target.json"

isa-smoke: isa-smoke-elfs simulator-attestation
	$(PYTHON) program-test/run.py --manifest "$(ISA_SMOKE_DIR)/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	  --output "$(ISA_SMOKE_DIR)/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(PROGRAM_MAX_CYCLES)
isa-setup benchmark-setup:
	git -C $(REPO_DIR) submodule update --init riscv/riscv-isa-tests
	git -C $(RISCV_TESTS_DIR) submodule update --init env

coremark-setup:
	git -C $(REPO_DIR) submodule update --init sims/program-test/coremark

program-test-setup: isa-setup coremark-setup

program-test-adapter-test:
	$(PYTHON) -m unittest discover -s tests -p 'test_program_test.py'

isa-elfs:
	@test "$(SOC)" = single-core-rv5stage-soc || (echo 'Upstream program suites currently support SOC=single-core-rv5stage-soc only' >&2; exit 2)
	$(PYTHON) program-test/build.py --suite $(@:-elfs=) --source "$(RISCV_TESTS_DIR)" \
	  --output "$(PROGRAM_BUILD_ROOT)/$(@:-elfs=)" --compiler "$(RISCV_CC)"

benchmark-elfs: program-target
	@test "$(SOC)" = single-core-rv5stage-soc || (echo 'Upstream program suites currently support SOC=single-core-rv5stage-soc only' >&2; exit 2)
	$(PYTHON) program-test/build.py --suite benchmark --source "$(RISCV_TESTS_DIR)" \
	  --output "$(PROGRAM_BUILD_ROOT)/benchmark" --compiler "$(RISCV_CC)" \
	  --target "$(PROGRAM_TARGET)" --benchmark-mode "$(BENCHMARK_MODE)"

coremark-elf: program-target
	@test "$(SOC)" = simple || (echo 'CoreMark currently supports SOC=simple only' >&2; exit 2)
	$(PYTHON) program-test/build-coremark.py --source "$(COREMARK_DIR)" --port "$(COREMARK_PORT_DIR)" \
	  --output "$(PROGRAM_BUILD_ROOT)/coremark" --compiler "$(RISCV_CC)" \
	  --target "$(PROGRAM_TARGET)" --iterations "$(COREMARK_ITERATIONS)"

isa-test: isa-elfs simulator
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_BUILD_ROOT)/isa/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --output "$(PROGRAM_BUILD_ROOT)/isa/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(PROGRAM_MAX_CYCLES)

simulator-attestation: simulator program-target
	$(PYTHON) program-test/artifact.py $(if $(PREBUILT_SIMULATOR),verify,record) \
	  --binary "$(SIMULATOR_BINARY)" --soc "$(SOC)" --target "$(PROGRAM_TARGET)"

benchmark-test: benchmark-elfs simulator-attestation
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_BUILD_ROOT)/benchmark/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	  --output "$(PROGRAM_BUILD_ROOT)/benchmark/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(BENCHMARK_MAX_CYCLES)

coremark-test: coremark-elf simulator-attestation
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_BUILD_ROOT)/coremark/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	  --output "$(PROGRAM_BUILD_ROOT)/coremark/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(COREMARK_MAX_CYCLES)

program-test:
	$(MAKE) isa-test
	$(MAKE) benchmark-test
	$(MAKE) coremark-test
	$(MAKE) arch-test ACT_CONFIGURATION=single-core-rv5stage-soc
