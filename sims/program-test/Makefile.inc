# Runs full SimpleSoC workload suites and capability-filtered ISA smoke on concrete SoCs.
RISCV_TESTS_DIR ?= $(REPO_DIR)/riscv/riscv-isa-tests
PROGRAM_BUILD_ROOT ?= /tmp/rhodium-program-tests
PROGRAM_JOBS ?= 1
PROGRAM_TIMEOUT ?= 300
PROGRAM_MAX_CYCLES ?= 10000000
BENCHMARK_MAX_CYCLES ?= 100000000
BENCHMARK_MODE ?= target
ISA_SMOKE_DIR := $(PROGRAM_BUILD_ROOT)/$(SOC)/isa-smoke
PROGRAM_TARGET ?= $(dir $(SIMULATOR_BINARY))program-target.json

.PHONY: program-test-setup program-test-adapter-test program-target benchmark-simulator-attestation isa-elfs benchmark-elfs isa-test benchmark-test program-test
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

isa-smoke: isa-smoke-elfs simulator
	$(PYTHON) program-test/run.py --manifest "$(ISA_SMOKE_DIR)/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --output "$(ISA_SMOKE_DIR)/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(PROGRAM_MAX_CYCLES)
program-test-setup:
	git -C $(REPO_DIR) submodule update --init riscv/riscv-isa-tests
	git -C $(RISCV_TESTS_DIR) submodule update --init env

program-test-adapter-test:
	$(PYTHON) -m unittest discover -s tests -p 'test_program_test.py'

isa-elfs:
	@test "$(SOC)" = simple || (echo 'Upstream program suites currently support SOC=simple only' >&2; exit 2)
	$(PYTHON) program-test/build.py --suite $(@:-elfs=) --source "$(RISCV_TESTS_DIR)" \
	  --output "$(PROGRAM_BUILD_ROOT)/$(@:-elfs=)" --compiler "$(RISCV_CC)"

benchmark-elfs: program-target
	@test "$(SOC)" = simple || (echo 'Upstream program suites currently support SOC=simple only' >&2; exit 2)
	$(PYTHON) program-test/build.py --suite benchmark --source "$(RISCV_TESTS_DIR)" \
	  --output "$(PROGRAM_BUILD_ROOT)/benchmark" --compiler "$(RISCV_CC)" \
	  --target "$(PROGRAM_TARGET)" --benchmark-mode "$(BENCHMARK_MODE)"

isa-test: isa-elfs simulator
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_BUILD_ROOT)/isa/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --output "$(PROGRAM_BUILD_ROOT)/isa/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(PROGRAM_MAX_CYCLES)

benchmark-simulator-attestation: simulator program-target
	$(PYTHON) program-test/artifact.py $(if $(PREBUILT_SIMULATOR),verify,record) \
	  --binary "$(SIMULATOR_BINARY)" --soc "$(SOC)" --target "$(PROGRAM_TARGET)"

benchmark-test: benchmark-elfs benchmark-simulator-attestation
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_BUILD_ROOT)/benchmark/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	  --output "$(PROGRAM_BUILD_ROOT)/benchmark/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(BENCHMARK_MAX_CYCLES)

program-test:
	$(MAKE) isa-test
	$(MAKE) benchmark-test
	$(MAKE) arch-test ACT_CONFIGURATION=simple-soc
