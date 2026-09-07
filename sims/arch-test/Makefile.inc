# Builds and runs UDB-selected ACT4 payloads for a selected platform configuration.
ACT_CONFIGURATION ?= simple-soc
PYTHON ?= python3
ACT_DIR ?= $(REPO_DIR)/riscv/riscv-arch-test
ACT_VENV ?= $(REPO_DIR)/.tools/act-venv
ACT_PYTHON := $(ACT_VENV)/bin/python
ACT_SAIL ?= $(REPO_DIR)/.tools/sail-0.13.1/bin/sail_riscv_sim
ACT_OBJDUMP ?= riscv64-unknown-elf-objdump
ACT_BUILD_ROOT ?= /tmp/rhodium-arch-test
ACT_CONFIG_DIR := $(ACT_BUILD_ROOT)/configs/$(ACT_CONFIGURATION)
ACT_WORKDIR := $(ACT_BUILD_ROOT)/work/$(ACT_CONFIGURATION)
ACT_BUNDLE_PATH ?= $(REPO_DIR)/.tools/act-bundle
ACT_JOBS ?= 1
ACT_TIMEOUT ?= 300
ACT_MAX_CYCLES ?= 10000000
ACT_PLATFORM := $(CURDIR)/arch-test/configs/$(ACT_CONFIGURATION).mk
-include $(ACT_PLATFORM)

.PHONY: arch-test-setup arch-test-config arch-test-elfs arch-test arch-test-run arch-test-adapter-test
arch-test-setup:
	PYTHON="$(PYTHON)" ACT_VENV="$(ACT_VENV)" ACT_BUNDLE_PATH="$(ACT_BUNDLE_PATH)" bash arch-test/setup.sh

arch-test-config:
	@test -f "$(ACT_PLATFORM)" || (echo "Unknown ACT configuration: $(ACT_CONFIGURATION)" >&2; exit 2)
	$(MAKE) -C $(REPO_DIR) riscv-udb-config RISCV_UDB_CONFIGURATION=$(ACT_UDB_CONFIGURATION) RISCV_UDB_OUTPUT=$(ACT_CONFIG_DIR)/udb.yaml
	$(ACT_PYTHON) arch-test/configure.py --udb $(ACT_CONFIG_DIR)/udb.yaml --output $(ACT_CONFIG_DIR) \
	  --name $(ACT_CONFIGURATION) --compiler $(RISCV_CC) --objdump $(ACT_OBJDUMP) --sail $(ACT_SAIL) \
	  --ram-origin $(ACT_RAM_ORIGIN) --ram-bytes $(ACT_RAM_BYTES) --test-base $(ACT_TEST_BASE)

arch-test-elfs: arch-test-config
	@if test -d "$(ACT_WORKDIR)/$(ACT_CONFIGURATION)/elfs"; then \
	  find "$(ACT_WORKDIR)/$(ACT_CONFIGURATION)/elfs" -type f -name '*.elf' -delete; \
	fi
	cd "$(ACT_DIR)" && XDG_CACHE_HOME="$(REPO_DIR)/.tools/act-cache" XDG_DATA_HOME="$(REPO_DIR)/.tools/act-data" \
	  BUNDLE_PATH="$(ACT_BUNDLE_PATH)" "$(ACT_VENV)/bin/act" \
	  "$(ACT_CONFIG_DIR)/test_config.yaml" --workdir "$(ACT_WORKDIR)" --test-dir tests --extensions all

arch-test: arch-test-elfs
	$(MAKE) arch-test-run

arch-test-run:
	@test -f "$(ACT_PLATFORM)" || (echo "Unknown ACT configuration: $(ACT_CONFIGURATION)" >&2; exit 2)
	@test -d "$(ACT_WORKDIR)/$(ACT_CONFIGURATION)/elfs" && \
	  test -n "$$(find "$(ACT_WORKDIR)/$(ACT_CONFIGURATION)/elfs" -name '*.elf' -type f -print -quit)" || \
	  (echo "No ACT ELFs; run arch-test-elfs first" >&2; exit 2)
	$(MAKE) simulator SOC=$(ACT_SOC)
	$(ACT_PYTHON) $(ACT_DIR)/run_tests.py --jobs $(ACT_JOBS) --timeout $(ACT_TIMEOUT) \
	  '$(ACT_PYTHON) $(CURDIR)/arch-test/run.py --simulator $(BUILD_ROOT)/$(ACT_SOC)/obj/VTestDriver --max-cycles $(ACT_MAX_CYCLES)' \
	  $(ACT_WORKDIR)/$(ACT_CONFIGURATION)/elfs

arch-test-adapter-test:
	$(PYTHON) -m unittest discover -s tests -p 'test_arch_test.py'
