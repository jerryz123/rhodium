# Builds and runs a MiniSoC simulator after site-specific SRAM macro mapping.

.DELETE_ON_ERROR:

REPO_DIR := $(abspath ../..)
VLSI_DIR := $(abspath ..)
SIMS_DIR := $(REPO_DIR)/sims
SRAM_DIR := $(REPO_DIR)/sram
SOC ?= mini
TECH ?= sky130
BUILD_ROOT ?= $(VLSI_DIR)/build/sim
BUILD_DIR := $(BUILD_ROOT)/$(SOC)/$(TECH)
HARNESS_SOURCE := sims/mini-soc-harness.rhdl

ifeq ($(SOC),mini)
else
$(error mapped simulation currently supports SOC=mini, not SOC=$(SOC))
endif
ifeq ($(TECH),sky130)
else
$(error mapped simulation currently supports TECH=sky130, not TECH=$(TECH))
endif

FESVR_REVISION := e1fa113cfb6d55d878a3c1ea3befa8d9c13ce154
FESVR_PREFIX ?= $(REPO_DIR)/.tools/fesvr-$(FESVR_REVISION)
FESVR_LIBRARY := $(FESVR_PREFIX)/lib/libfesvr.a
FESVR_DIR := $(SIMS_DIR)/fesvr
VERILATOR_DIR := $(SIMS_DIR)/verilator

CIRCT_OPT ?= $(REPO_DIR)/.tools/firtool-1.155.0/bin/circt-opt
CIRCT_ROOT ?= $(abspath $(dir $(CIRCT_OPT))/..)
RACKET ?= racket
VERILATOR ?= verilator
RISCV_CC ?= riscv64-unknown-elf-gcc
PYTHON ?= python3

MEMORY_SITE_PLUGIN := $(SRAM_DIR)/build/circt/rhodium-memory-sites.so
MEMORY_MAPPER := $(SRAM_DIR)/map-memories.py
MEMORY_CATALOG := $(SRAM_DIR)/$(TECH)/macros.ini
MEMORY_POLICY := $(VLSI_DIR)/designs/mini-soc/$(TECH)/sram-map.yaml
FUNCTIONAL_MODELS := $(wildcard $(SRAM_DIR)/$(TECH)/models/*.functional.sv)

SOC_MLIR := $(BUILD_DIR)/soc-harness.mlir
MAPPED_MLIR := $(BUILD_DIR)/soc-harness-mapped.mlir
MAPPING_INVENTORY := $(BUILD_DIR)/memory-sites.json
MAPPING_SELECTION_STAMP := $(BUILD_DIR)/memory-sites.stamp
MEMORY_WRAPPERS := $(BUILD_DIR)/memory-wrappers.sv
MEMORY_MANIFEST := $(BUILD_DIR)/memory-manifest.json
MEMORY_MANIFEST_CHECK := $(VLSI_DIR)/tools/check-mini-soc-memory-map.py
MAPPING_STAMP := $(BUILD_DIR)/memory-map.stamp
SOC_VERILOG := $(BUILD_DIR)/SoCHarness.sv
SOC_OBJECT_DIR := $(BUILD_DIR)/obj
SIMULATOR_BINARY := $(SOC_OBJECT_DIR)/VTestDriver
SMOKE_BINARY := $(BUILD_DIR)/rv5stage_smoke.elf
SIMULATOR_SOURCES := $(sort $(shell find $(REPO_DIR) -type f \( -name '*.rhdl' -o -name '*.rhm' \)))

.PHONY: all setup mapping mapped-rtl simulator run smoke

all: simulator

setup:
	$(MAKE) -C $(SIMS_DIR) setup FESVR_PREFIX=$(FESVR_PREFIX)

mapping: $(MAPPING_STAMP)

mapped-rtl: $(SOC_VERILOG) $(MAPPING_STAMP)

simulator: $(SIMULATOR_BINARY)
	@echo "Built mapped $(SOC)/$(TECH) simulator: $(SIMULATOR_BINARY)"

run: simulator
	@test -n "$(BINARY)" || \
		(echo "BINARY is required; use 'make -C vlsi/sim run BINARY=/path/to/program.elf'" >&2; exit 2)
	@test -f "$(abspath $(BINARY))" || \
		(echo "target binary does not exist: $(abspath $(BINARY))" >&2; exit 2)
	$(SIMULATOR_BINARY) $(HTIF_ARGS) "$(abspath $(BINARY))" $(TARGET_ARGS)

smoke: $(SMOKE_BINARY)
	$(MAKE) run SOC=$(SOC) TECH=$(TECH) BUILD_ROOT=$(BUILD_ROOT) \
		FESVR_PREFIX=$(FESVR_PREFIX) CIRCT_OPT=$(CIRCT_OPT) \
		RACKET=$(RACKET) VERILATOR=$(VERILATOR) RISCV_CC=$(RISCV_CC) \
		PYTHON=$(PYTHON) BINARY=$(SMOKE_BINARY)

$(BUILD_DIR):
	mkdir -p $@

$(MEMORY_SITE_PLUGIN): $(SRAM_DIR)/circt/CMakeLists.txt $(SRAM_DIR)/circt/MemorySitePass.cpp
	$(MAKE) -C $(SRAM_DIR) plugin CIRCT_OPT=$(CIRCT_OPT) CIRCT_ROOT=$(CIRCT_ROOT)
	@test -s $@

$(SOC_MLIR): $(SIMULATOR_SOURCES) | $(BUILD_DIR)
	cd $(REPO_DIR) && RACKET="$(RACKET)" tools/run-racket.sh -S $(REPO_DIR) \
		sims/emit-soc-harness.rhm $(HARNESS_SOURCE) > $@

$(MAPPING_SELECTION_STAMP): $(SOC_MLIR) $(MEMORY_SITE_PLUGIN) $(MEMORY_POLICY)
	@test -x $(CIRCT_OPT) || { \
		echo "circt-opt not found at $(CIRCT_OPT); run 'make setup-circt' at the repository root or set CIRCT_OPT" >&2; \
		exit 1; \
	}
	$(CIRCT_OPT) \
		--load-pass-plugin=$(MEMORY_SITE_PLUGIN) \
		--pass-pipeline="builtin.module(strip-debuginfo-with-pred{drop-suffix=.mlir},canonicalize,cse,rhodium-select-hw-top{top=SoCHarness},hw-flatten-modules{hw-inline-all=true hw-inline-with-state=true},symbol-dce,lower-seq-firmem,rhodium-map-memory-sites{policy=$(MEMORY_POLICY) inventory=$(MAPPING_INVENTORY) top=SoCHarness scope-prefix=soc},symbol-dce)" \
		$(SOC_MLIR) -o $(MAPPED_MLIR)
	@test -s $(MAPPED_MLIR)
	@test -s $(MAPPING_INVENTORY)
	touch $@

$(MAPPING_STAMP): $(MAPPING_SELECTION_STAMP) $(MEMORY_MAPPER) $(MEMORY_CATALOG) $(FUNCTIONAL_MODELS) $(MEMORY_MANIFEST_CHECK)
	@test -n "$(FUNCTIONAL_MODELS)" || { \
		echo "no functional SRAM model found under $(SRAM_DIR)/$(TECH)/models" >&2; \
		exit 1; \
	}
	$(PYTHON) $(MEMORY_MAPPER) $(MAPPED_MLIR) \
		--catalog $(MEMORY_CATALOG) \
		--site-inventory $(MAPPING_INVENTORY) \
		--output-verilog $(MEMORY_WRAPPERS) \
		--output-manifest $(MEMORY_MANIFEST)
	$(PYTHON) $(MEMORY_MANIFEST_CHECK) $(MEMORY_MANIFEST) \
		--top SoCHarness --scope-prefix soc
	@test -s $(MEMORY_WRAPPERS)
	touch $@

$(SOC_VERILOG): $(MAPPING_SELECTION_STAMP)
	$(CIRCT_OPT) \
		--lower-seq-hlmem \
		--prettify-verilog \
		--lower-sim-to-sv \
		--lower-verif-to-sv \
		--lower-seq-to-sv='disable-mem-randomization=true disable-reg-randomization=true' \
		--hw-memory-sim='disable-mem-randomization=true disable-reg-randomization=true read-enable-mode=undefined' \
		--symbol-dce \
		--sv-mask-non-synthesizable='mode=ifdef macro=SYNTHESIS' \
		--export-verilog \
		$(MAPPED_MLIR) -o /dev/null > $@

$(FESVR_LIBRARY):
	@echo "FESVR is not installed; run 'make -C vlsi/sim setup' first" >&2
	@exit 1

$(SIMULATOR_BINARY): $(SOC_VERILOG) $(MAPPING_STAMP) $(SIMS_DIR)/TestDriver.v \
		$(FESVR_DIR)/direct_mem_htif.cc $(FESVR_DIR)/direct_mem_htif.h \
		$(VERILATOR_DIR)/direct_mem_htif_dpi.cc \
		$(VERILATOR_DIR)/direct_mem_htif_dpi.h $(FESVR_LIBRARY)
	$(VERILATOR) --binary --timing --vpi --assert --Wno-UNOPTFLAT --Wno-SYMRSVDWORD --build-jobs 0 \
		--top-module TestDriver --Mdir $(SOC_OBJECT_DIR) \
		$(SOC_VERILOG) $(MEMORY_WRAPPERS) $(FUNCTIONAL_MODELS) \
		$(SIMS_DIR)/TestDriver.v $(FESVR_DIR)/direct_mem_htif.cc \
		$(VERILATOR_DIR)/direct_mem_htif_dpi.cc \
		-CFLAGS "-I$(FESVR_PREFIX)/include -I$(FESVR_DIR) -I$(VERILATOR_DIR) -std=c++20" \
		-LDFLAGS "$(FESVR_LIBRARY) -pthread"

$(SMOKE_BINARY): $(SIMS_DIR)/tests/programs/rv5stage_smoke.S \
		$(SIMS_DIR)/tests/programs/rv5stage_smoke.ld | $(BUILD_DIR)
	$(RISCV_CC) -march=rv64i -mabi=lp64 -nostdlib -nostartfiles \
		-Wl,--build-id=none -Wl,-T,$(SIMS_DIR)/tests/programs/rv5stage_smoke.ld $< -o $@
