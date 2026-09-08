# Runs full SimpleSoC workload suites and capability-filtered ISA smoke on concrete SoCs.
RISCV_TESTS_DIR ?= $(REPO_DIR)/riscv/riscv-isa-tests
PROGRAM_BUILD_ROOT ?= /tmp/rhodium-program-tests
PROGRAM_JOBS ?= 1
PROGRAM_TIMEOUT ?= 300
PROGRAM_MAX_CYCLES ?= 10000000
BENCHMARK_MAX_CYCLES ?= 100000000
ISA_SMOKE_DIR := $(PROGRAM_BUILD_ROOT)/$(SOC)/isa-smoke

.PHONY: program-test-setup program-test-adapter-test isa-elfs benchmark-elfs isa-test benchmark-test program-test
.PHONY: isa-smoke-config isa-smoke-elfs isa-smoke
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

isa-elfs benchmark-elfs:
	@test "$(SOC)" = simple || (echo 'Upstream program suites currently support SOC=simple only' >&2; exit 2)
	$(PYTHON) program-test/build.py --suite $(@:-elfs=) --source "$(RISCV_TESTS_DIR)" \
	  --output "$(PROGRAM_BUILD_ROOT)/$(@:-elfs=)" --compiler "$(RISCV_CC)"

isa-test: isa-elfs simulator
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_BUILD_ROOT)/isa/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --output "$(PROGRAM_BUILD_ROOT)/isa/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(PROGRAM_MAX_CYCLES)

benchmark-test: benchmark-elfs simulator
	$(PYTHON) program-test/run.py --manifest "$(PROGRAM_BUILD_ROOT)/benchmark/manifest.json" \
	  --simulator "$(SIMULATOR_BINARY)" --output "$(PROGRAM_BUILD_ROOT)/benchmark/results" \
	  --jobs $(PROGRAM_JOBS) --timeout $(PROGRAM_TIMEOUT) --max-cycles $(BENCHMARK_MAX_CYCLES)

program-test:
	$(MAKE) isa-test
	$(MAKE) benchmark-test
	$(MAKE) arch-test ACT_CONFIGURATION=simple-soc
