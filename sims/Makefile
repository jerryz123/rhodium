# Builds a reusable FESVR-backed SoC simulator and runs target binaries through HTIF.
REPO_DIR := $(abspath ..)
FESVR_DIR := $(CURDIR)/fesvr
VERILATOR_DIR := $(CURDIR)/verilator
CHI_DPI_DIR := $(REPO_DIR)/chi/dpi
FESVR_REVISION := e1fa113cfb6d55d878a3c1ea3befa8d9c13ce154
FESVR_PREFIX ?= $(REPO_DIR)/.tools/fesvr-$(FESVR_REVISION)
SOC ?= simple
BUILD_ROOT ?= /tmp/rhodium-sims
CXX ?= c++
RISCV_CC ?= riscv64-unknown-elf-gcc
RACKET ?= racket
RACO ?= raco
VERILATOR ?= verilator
CIRCT_OPT ?= $(REPO_DIR)/.tools/firtool-1.155.0/bin/circt-opt

HARNESS_SOURCE_simple := sims/simple-soc-harness.rhdl
HARNESS_SOURCE_mini := sims/mini-soc-harness.rhdl
HARNESS_SOURCE_tiled := sims/tiled-soc-harness.rhdl
HARNESS_SOURCE := $(HARNESS_SOURCE_$(SOC))
HARNESS_EMITTER := $(REPO_DIR)/sims/emit-soc-harness.rhm
SOC_DPI_SOURCE_simple := $(CHI_DPI_DIR)/chi_dpi_memory_dpi.cc
SOC_DPI_SOURCE := $(SOC_DPI_SOURCE_$(SOC))
SOC_DPI_HEADER_simple := $(CHI_DPI_DIR)/chi_dpi_memory_dpi.h
SOC_DPI_HEADER := $(SOC_DPI_HEADER_$(SOC))

CPPFLAGS += -I$(FESVR_PREFIX)/include -I$(FESVR_DIR) -I$(VERILATOR_DIR) -I$(CHI_DPI_DIR)
CXXFLAGS += -std=c++20 -Wall -Wextra -Werror
FESVR_LIBRARY := $(FESVR_PREFIX)/lib/libfesvr.a
SOC_BUILD_DIR := $(BUILD_ROOT)/$(SOC)
DPI_BUILD_DIR := $(BUILD_ROOT)/tools
TILED_LOWERING_DIR := $(BUILD_ROOT)/tiled-lowering
SOC_MLIR := $(SOC_BUILD_DIR)/soc_harness.mlir
SOC_VERILOG := $(SOC_BUILD_DIR)/SoCHarness.sv
SOC_OBJECT_DIR := $(SOC_BUILD_DIR)/obj
SIMULATOR_BINARY := $(SOC_OBJECT_DIR)/VTestDriver
SMOKE_BINARY := $(SOC_BUILD_DIR)/rv5stage_smoke.elf
SIMULATOR_SOURCES := $(sort $(shell find $(REPO_DIR) -type f \( -name '*.rhdl' -o -name '*.rhm' \)))

.PHONY: all setup simulator run smoke dpi-compile-check chi-dpi-memory-test transport-test tiled-lowering-test

all: simulator

setup:
	$(FESVR_DIR)/install.sh

simulator: $(SIMULATOR_BINARY)
	@echo "Built $(SOC) simulator: $(SIMULATOR_BINARY)"

run: simulator
	@test -n "$(BINARY)" || \
	  (echo "BINARY is required; use 'make -C sims run BINARY=/path/to/program.elf'" >&2; exit 2)
	@test -f "$(abspath $(BINARY))" || \
	  (echo "target binary does not exist: $(abspath $(BINARY))" >&2; exit 2)
	$(SIMULATOR_BINARY) $(HTIF_ARGS) "$(abspath $(BINARY))" $(TARGET_ARGS)

smoke: $(SMOKE_BINARY)
	$(MAKE) run SOC=$(SOC) BINARY=$(SMOKE_BINARY)

dpi-compile-check: $(FESVR_LIBRARY) | $(DPI_BUILD_DIR)
	@test -n "$(VERILATOR_ROOT)" || \
	  (echo "set VERILATOR_ROOT to the Verilator share directory" >&2; exit 1)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I$(VERILATOR_ROOT)/include/vltstd \
	  -c $(VERILATOR_DIR)/direct_mem_htif_dpi.cc -o $(DPI_BUILD_DIR)/direct_mem_htif_dpi.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I$(VERILATOR_ROOT)/include/vltstd \
	  -c $(CHI_DPI_DIR)/chi_dpi_memory_dpi.cc -o $(DPI_BUILD_DIR)/chi_dpi_memory_dpi.o

chi-dpi-memory-test: | $(DPI_BUILD_DIR)
	@test -n "$(VERILATOR_ROOT)" || \
	  (echo "set VERILATOR_ROOT to the Verilator share directory" >&2; exit 1)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I$(VERILATOR_ROOT)/include/vltstd \
	  $(CHI_DPI_DIR)/chi_dpi_memory_dpi.cc \
	  $(REPO_DIR)/chi/tests/chi_dpi_memory_dpi_test.cc \
	  -o $(DPI_BUILD_DIR)/chi_dpi_memory_dpi_test
	$(DPI_BUILD_DIR)/chi_dpi_memory_dpi_test

transport-test:
	cd $(REPO_DIR) && RACO="$(RACO)" tools/run-racket-tests.sh \
	  sims/tests/direct-memory-htif-test.rhm

tiled-lowering-test: | $(TILED_LOWERING_DIR)
	@test -x "$(CIRCT_OPT)" || \
	  (echo "circt-opt not found; run 'make setup-circt' at the repository root or set CIRCT_OPT" >&2; exit 1)
	cd $(REPO_DIR) && RACKET="$(RACKET)" tools/run-racket.sh -S $(REPO_DIR) \
	  $(HARNESS_EMITTER) $(HARNESS_SOURCE_tiled) > $(TILED_LOWERING_DIR)/soc_harness.mlir
	$(CIRCT_OPT) --canonicalize --cse $(TILED_LOWERING_DIR)/soc_harness.mlir -o /dev/null

$(FESVR_LIBRARY):
	@echo "FESVR is not installed; run 'make -C sims setup' first" >&2
	@exit 1

$(SOC_BUILD_DIR) $(DPI_BUILD_DIR) $(TILED_LOWERING_DIR):
	mkdir -p $@

$(SOC_MLIR): $(SIMULATOR_SOURCES) $(HARNESS_EMITTER) | $(SOC_BUILD_DIR)
	@test -n "$(HARNESS_SOURCE)" || \
	  (echo "unsupported SOC '$(SOC)'; expected simple, mini, or tiled" >&2; exit 2)
	cd $(REPO_DIR) && RACKET="$(RACKET)" tools/run-racket.sh -S $(REPO_DIR) \
	  $(HARNESS_EMITTER) $(HARNESS_SOURCE) > $(SOC_MLIR)

$(SOC_VERILOG): $(SOC_MLIR)
	@test -x "$(CIRCT_OPT)" || \
	  (echo "circt-opt not found; run 'make setup-circt' at the repository root or set CIRCT_OPT" >&2; exit 1)
	$(CIRCT_OPT) --canonicalize --cse --prettify-verilog \
	  --lower-seq-hlmem --lower-sim-to-sv --lower-verif-to-sv \
	  --lower-seq-to-sv='disable-mem-randomization=true disable-reg-randomization=true' \
	  --lower-seq-firmem \
	  --hw-memory-sim='disable-mem-randomization=true disable-reg-randomization=true read-enable-mode=undefined' \
	  --sv-mask-non-synthesizable='mode=ifdef macro=SYNTHESIS' \
	  --export-verilog $(SOC_MLIR) -o /dev/null > $(SOC_VERILOG)

$(SIMULATOR_BINARY): $(SOC_VERILOG) $(CURDIR)/TestDriver.v \
                     $(FESVR_DIR)/direct_mem_htif.cc \
                     $(FESVR_DIR)/direct_mem_htif.h \
                     $(VERILATOR_DIR)/direct_mem_htif_dpi.cc \
                     $(VERILATOR_DIR)/direct_mem_htif_dpi.h \
                     $(SOC_DPI_SOURCE) \
                     $(SOC_DPI_HEADER) \
                     $(FESVR_LIBRARY)
	$(VERILATOR) --binary --timing --vpi --assert --Wno-UNOPTFLAT --Wno-SYMRSVDWORD --build-jobs 0 \
	  --top-module TestDriver --Mdir $(SOC_OBJECT_DIR) \
	  $(SOC_VERILOG) $(CURDIR)/TestDriver.v \
	  $(FESVR_DIR)/direct_mem_htif.cc $(VERILATOR_DIR)/direct_mem_htif_dpi.cc \
	  $(SOC_DPI_SOURCE) \
	  -CFLAGS "-I$(FESVR_PREFIX)/include -I$(FESVR_DIR) -I$(VERILATOR_DIR) -std=c++20" \
	  -LDFLAGS "$(FESVR_LIBRARY) -pthread"

$(SMOKE_BINARY): tests/programs/rv5stage_smoke.S \
                 tests/programs/rv5stage_smoke.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64i -mabi=lp64 -nostdlib -nostartfiles \
	  -Wl,--build-id=none -Wl,-T,tests/programs/rv5stage_smoke.ld $< -o $@
