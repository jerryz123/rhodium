# Builds the bounded Buildroot Linux workload and runs it through the normal OpenSBI/FESVR path.
# SPDX-License-Identifier: Apache-2.0
LINUX_SOURCE ?= $(REPO_DIR)/sw/buildroot
LINUX_PORT ?= $(REPO_DIR)/sw/buildroot-riscv
LINUX_OUTPUT ?= $(PROGRAM_CONFIG_ROOT)/linux
LINUX_DOWNLOADS ?= $(PROGRAM_BUILD_ROOT)/buildroot-downloads
LINUX_JOBS ?= 4
LINUX_TIMEOUT ?= 1800
LINUX_MAX_CYCLES ?= 500000000

.PHONY: linux-setup linux-elfs linux-smoke-test linux-smoke-run
linux-setup:
	git -C $(REPO_DIR) submodule update --init sw/buildroot sw/opensbi

linux-elfs: $(PROGRAM_TARGET)
	$(PYTHON) $(REPO_DIR)/sw/build/linux.py --source "$(LINUX_SOURCE)" --port "$(LINUX_PORT)" \
	  --opensbi-source "$(OPENSBI_SOURCE)" --opensbi-patches "$(OPENSBI_PATCHES)" --target "$(PROGRAM_TARGET)" \
	  --output "$(LINUX_OUTPUT)" --downloads "$(LINUX_DOWNLOADS)" --jobs $(LINUX_JOBS)

linux-smoke-run: simulator-attestation
	$(PYTHON) program-test/run.py --manifest "$(if $(PROGRAM_MANIFEST),$(PROGRAM_MANIFEST),$(LINUX_OUTPUT)/manifest.json)" \
	  --simulator "$(SIMULATOR_BINARY)" --simulator-metadata "$(SIMULATOR_BINARY).json" \
	  --output "$(LINUX_OUTPUT)/results" --timeout $(LINUX_TIMEOUT) --max-cycles $(LINUX_MAX_CYCLES)

linux-smoke-test: linux-elfs
	$(MAKE) linux-smoke-run SOC="$(SOC_ID)" LINUX_OUTPUT="$(LINUX_OUTPUT)"
