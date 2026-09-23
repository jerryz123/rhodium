# Builds OpenSBI FW_JUMP and qualifies its generic-platform handoff on a concrete SoC.
# SPDX-License-Identifier: Apache-2.0
OPENSBI_SOURCE ?= $(REPO_DIR)/riscv/opensbi
OPENSBI_BUILD_ROOT ?= /tmp/rhodium-software
OPENSBI_OUTPUT := $(OPENSBI_BUILD_ROOT)/$(SOC_ID)/opensbi
OPENSBI_LAYOUT := $(OPENSBI_OUTPUT)/layout.json
OPENSBI_FDT := $(OPENSBI_OUTPUT)/platform.dtb
OPENSBI_FIRMWARE := $(OPENSBI_OUTPUT)/fw_jump.elf
OPENSBI_SMOKE := $(OPENSBI_OUTPUT)/opensbi_smoke.elf
OPENSBI_SCRIPT := $(CURDIR)/opensbi/build.py
OPENSBI_FDT_SCRIPT := $(CURDIR)/opensbi/write-device-tree.rhm
OPENSBI_MAX_CYCLES ?= 50000000

.PHONY: opensbi-setup opensbi-layout opensbi-firmware opensbi-smoke-elf opensbi-run opensbi-test opensbi-adapter-test

opensbi-setup:
	git -C $(REPO_DIR) submodule update --init riscv/opensbi

opensbi-layout: $(OPENSBI_LAYOUT)

$(OPENSBI_LAYOUT): $(PROGRAM_TARGET) $(OPENSBI_SCRIPT)
	mkdir -p "$(OPENSBI_OUTPUT)"
	$(PYTHON) $(OPENSBI_SCRIPT) layout --target "$(PROGRAM_TARGET)" --output "$@"

$(OPENSBI_FDT): $(OPENSBI_FDT_SCRIPT) $(SIMULATOR_SOURCES)
	mkdir -p "$(OPENSBI_OUTPUT)"
	cd "$(REPO_DIR)" && tools/run-racket.sh -S "$(REPO_DIR)" \
	  sims/opensbi/write-device-tree.rhm $(PROGRAM_TARGET_SELECTION) "$(abspath $@)"

opensbi-firmware: $(OPENSBI_LAYOUT) $(OPENSBI_FDT) $(OPENSBI_SCRIPT)
	@test -f "$(OPENSBI_SOURCE)/Makefile" || \
	  (echo 'OpenSBI is not initialized; run make -C sims opensbi-setup' >&2; exit 2)
	$(PYTHON) $(OPENSBI_SCRIPT) firmware --layout "$(OPENSBI_LAYOUT)" \
	  --fdt "$(OPENSBI_FDT)" --source "$(OPENSBI_SOURCE)" \
	  --compiler "$(RISCV_CC)" --output "$(OPENSBI_OUTPUT)" $(if $(OPENSBI_LLVM),--llvm "$(OPENSBI_LLVM)",)

opensbi-smoke-elf: $(OPENSBI_SMOKE)

$(OPENSBI_SMOKE): $(OPENSBI_LAYOUT) $(OPENSBI_SCRIPT) \
                   opensbi/tests/smoke.S opensbi/tests/smoke.ld
	$(PYTHON) $(OPENSBI_SCRIPT) smoke --layout "$(OPENSBI_LAYOUT)" \
	  --source opensbi/tests/smoke.S --linker opensbi/tests/smoke.ld \
	  --compiler "$(RISCV_CC)" --output "$@"

opensbi-run: opensbi-firmware simulator-attestation
	@test -n "$(NEXT_STAGE)" || \
	  (echo 'NEXT_STAGE is required and must be linked at the address in $(OPENSBI_LAYOUT)' >&2; exit 2)
	@test -f "$(abspath $(NEXT_STAGE))" || \
	  (echo 'OpenSBI next-stage ELF does not exist: $(abspath $(NEXT_STAGE))' >&2; exit 2)
	$(SIMULATOR_BINARY) +max-cycles=$(OPENSBI_MAX_CYCLES) \
	  +payload="$(abspath $(NEXT_STAGE))" \
	  "$(OPENSBI_FIRMWARE)"

opensbi-test: $(OPENSBI_SMOKE)
	$(MAKE) opensbi-run SOC="$(SOC)" CORE="$(CORE)" TRACE="$(TRACE)" PREBUILT_SIMULATOR="$(PREBUILT_SIMULATOR)" \
	  BUILD_ROOT="$(BUILD_ROOT)" PROGRAM_TARGET="$(PROGRAM_TARGET)" NEXT_STAGE="$(OPENSBI_SMOKE)" \
	  OPENSBI_BUILD_ROOT="$(OPENSBI_BUILD_ROOT)" OPENSBI_MAX_CYCLES="$(OPENSBI_MAX_CYCLES)" \
	  OPENSBI_LLVM="$(OPENSBI_LLVM)"

opensbi-adapter-test:
	$(PYTHON) -m unittest discover -s opensbi/tests -p 'test_build.py'
