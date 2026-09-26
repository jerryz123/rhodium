# Builds and runs UDB-selected ACT4 payloads for a selected platform configuration.
# SPDX-License-Identifier: Apache-2.0
ACT_CONFIGURATION ?= $(SOC_ID)
PYTHON ?= python3
ACT_DIR ?= $(REPO_DIR)/sw/riscv-arch-test
ACT_VENV ?= $(REPO_DIR)/.tools/act-venv
ACT_PYTHON := $(ACT_VENV)/bin/python
ACT_TESTGEN ?= $(ACT_VENV)/bin/testgen
ACT_SAIL ?= $(REPO_DIR)/.tools/sail-0.14.1/bin/sail_riscv_sim
ACT_OBJDUMP ?= riscv64-unknown-elf-objdump
ACT_BUILD_ROOT ?= /tmp/rhodium-arch-test
ACT_CONFIG_DIR := $(ACT_BUILD_ROOT)/configs/$(ACT_CONFIGURATION)
ACT_WORKDIR := $(ACT_BUILD_ROOT)/work/$(ACT_CONFIGURATION)
ACT_SOURCE_DIR := $(ACT_BUILD_ROOT)/source/riscv-arch-test
ACT_TEST_DIR := $(ACT_BUILD_ROOT)/tests
ACT_PATCH_SERIES ?= $(REPO_DIR)/sw/riscv-arch-test-patches/series
PATCHED_SUBMODULE_TOOL ?= $(REPO_DIR)/riscv/patched_submodule.py
ACT_BUNDLE_PATH ?= $(REPO_DIR)/.tools/act-bundle
ACT_JOBS ?= 1
ACT_BUILD_JOBS ?= 2
ACT_TESTGEN_JOBS ?= 0
ACT_FAST ?= 0
ACT_SHARD ?= 0
ACT_SHARDS ?= 1
ACT_ELF_DIR = $(ACT_WORKDIR)/$(ACT_CONFIGURATION)/elfs
ACT_RUN_ELF_DIR = $(if $(filter 1,$(ACT_SHARDS)),$(ACT_ELF_DIR),$(ACT_WORKDIR)/$(ACT_CONFIGURATION)/shards/$(ACT_SHARD)/elfs)
ACT_TIMEOUT ?= 300
ACT_MAX_CYCLES ?= 10000000
ACT_SIMULATOR = $(if $(PREBUILT_SIMULATOR),$(abspath $(PREBUILT_SIMULATOR)),$(BUILD_ROOT)/$(SOC_ID)/obj/VTestDriver)

.PHONY: arch-test-setup arch-test-source arch-test-config arch-test-tests arch-test-elfs arch-test arch-test-run arch-test-adapter-test
arch-test-setup:
	PYTHON="$(PYTHON)" ACT_VENV="$(ACT_VENV)" ACT_BUNDLE_PATH="$(ACT_BUNDLE_PATH)" bash arch-test/setup.sh

arch-test-source:
	$(PYTHON) "$(PATCHED_SUBMODULE_TOOL)" materialize --source "$(ACT_DIR)" \
	  --series "$(ACT_PATCH_SERIES)" --output "$(ACT_SOURCE_DIR)"

arch-test-config:
	@test "$(ACT_CONFIGURATION)" = "$(SOC_ID)" || (echo "ACT configuration must match the selected SoC/core/ISA" >&2; exit 2)
	@mkdir -p "$(ACT_CONFIG_DIR)"
	cd "$(REPO_DIR)" && RACKET="$(RACKET)" tools/run-racket.sh -S "$(REPO_DIR)" \
	  sims/arch-test/write-platform.rhm "$(SOC_ID)" "$(ACT_CONFIG_DIR)"
	$(ACT_PYTHON) arch-test/configure.py --udb $(ACT_CONFIG_DIR)/udb.yaml --output $(ACT_CONFIG_DIR) \
	  --name $(ACT_CONFIGURATION) --compiler $(RISCV_CC) --objdump $(ACT_OBJDUMP) --sail $(ACT_SAIL) \
	  --platform $(ACT_CONFIG_DIR)/platform.json

arch-test-tests: arch-test-source
	rm -rf "$(ACT_TEST_DIR)"
	cp -R "$(ACT_SOURCE_DIR)/tests" "$(ACT_TEST_DIR)"
	cd "$(ACT_SOURCE_DIR)" && PYTHONPATH="$(ACT_SOURCE_DIR)/generators/testgen/src" "$(ACT_TESTGEN)" testplans -o "$(ACT_TEST_DIR)" --extensions all --jobs $(ACT_TESTGEN_JOBS)

arch-test-elfs: arch-test-config arch-test-tests
	@if test -d "$(ACT_WORKDIR)/$(ACT_CONFIGURATION)/elfs"; then \
	  find "$(ACT_WORKDIR)/$(ACT_CONFIGURATION)/elfs" -type f -name '*.elf' -delete; \
	fi
	cd "$(ACT_SOURCE_DIR)" && XDG_CACHE_HOME="$(REPO_DIR)/.tools/act-cache" XDG_DATA_HOME="$(REPO_DIR)/.tools/act-data" \
	  BUNDLE_PATH="$(ACT_BUNDLE_PATH)" "$(ACT_PYTHON)" "$(CURDIR)/arch-test/build.py" \
	  "$(ACT_CONFIG_DIR)/test_config.yaml" --workdir "$(ACT_WORKDIR)" --test-dir "$(ACT_TEST_DIR)" --extensions all --keep-going --jobs $(ACT_BUILD_JOBS) $(if $(filter 1,$(ACT_FAST)),--fast)

arch-test: arch-test-elfs
	$(MAKE) arch-test-run

arch-test-run:
	@test "$(ACT_CONFIGURATION)" = "$(SOC_ID)" || (echo "ACT configuration must match the selected SoC/core/ISA" >&2; exit 2)
	@test "$(SOC_SHAPE)" = simple || (echo "ACT currently supports the Simple SoC shape" >&2; exit 2)
	@test -d "$(ACT_WORKDIR)/$(ACT_CONFIGURATION)/elfs" && \
	  test -n "$$(find "$(ACT_WORKDIR)/$(ACT_CONFIGURATION)/elfs" -name '*.elf' -type f -print -quit)" || \
	  (echo "No ACT ELFs; run arch-test-elfs first" >&2; exit 2)
	$(MAKE) simulator SOC=$(SOC_ID)
	$(if $(filter 1,$(ACT_SHARDS)),@true,$(PYTHON) arch-test/shard.py --elf-dir "$(ACT_ELF_DIR)" --output "$(ACT_RUN_ELF_DIR)" --index $(ACT_SHARD) --count $(ACT_SHARDS))
	rm -f "$(ACT_RUN_ELF_DIR)/../summary.log"
	@status=0; $(ACT_PYTHON) $(ACT_DIR)/run_tests.py --jobs $(ACT_JOBS) --timeout $(ACT_TIMEOUT) \
	  '"$(ACT_PYTHON)" "$(CURDIR)/arch-test/run.py" --simulator "$(ACT_SIMULATOR)" --max-cycles $(ACT_MAX_CYCLES)' \
	  $(ACT_RUN_ELF_DIR) || status=$$?; \
	$(PYTHON) arch-test/report.py $(ACT_RUN_ELF_DIR) || status=1; \
	exit $$status

arch-test-adapter-test:
	$(PYTHON) -m unittest discover -s tests -p 'test_arch_test.py'
