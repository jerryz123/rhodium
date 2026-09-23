# Builds a reusable FESVR-backed SoC simulator and runs target binaries through HTIF.
# SPDX-License-Identifier: Apache-2.0
REPO_DIR := $(abspath ..)
FESVR_DIR := $(CURDIR)/fesvr
VERILATOR_DIR := $(CURDIR)/verilator
CHI_DPI_DIR := $(REPO_DIR)/chi/subordinate/dpi
UART_DPI_DIR := $(REPO_DIR)/devices/uart/dpi
SPIKE_DPI_DIR := $(REPO_DIR)/cores/spike/dpi
PYTHON ?= python3
PATCHED_SUBMODULE_TOOL := $(REPO_DIR)/riscv/patched_submodule.py
SPIKE_IDENTITY := $(shell $(PYTHON) $(PATCHED_SUBMODULE_TOOL) identity --repository $(REPO_DIR) --submodule riscv/riscv-isa-sim --series $(REPO_DIR)/riscv/riscv-isa-sim-patches/series)
FESVR_PREFIX ?= $(REPO_DIR)/.tools/fesvr-$(SPIKE_IDENTITY)
SOC ?= single
CORE ?= rv5stage
TRACE ?= 0
TRACE_FILE ?=
NLOHMANN_JSON_INCLUDE_DIR ?=
BUILD_JOBS ?= 4
BUILD_ROOT ?= /tmp/rhodium-sims
CXX ?= c++
RISCV_CC ?= riscv64-unknown-elf-gcc
RACKET ?= racket
RACO ?= raco
VERILATOR ?= verilator
CIRCT_OPT ?= $(REPO_DIR)/.tools/firtool-1.155.0/bin/circt-opt

HARNESS_SELECTOR_single-core-rv5stage-soc := single rv5stage
HARNESS_SELECTOR_single-core-spike-soc := single spike
HARNESS_SELECTOR_mini-rv5stage-soc := mini rv5stage
HARNESS_SELECTOR_tiled-rv5stage-soc := tiled rv5stage
HARNESS_SELECTOR_mini := mini $(CORE)
HARNESS_SELECTOR_single := single $(CORE)
HARNESS_SELECTOR_tiled := tiled $(CORE)
HARNESS_SELECTOR := $(HARNESS_SELECTOR_$(SOC))
HARNESS_EMITTER := $(REPO_DIR)/sims/emit-soc-harness.rhm
SOC_DPI_SOURCE_single-core-rv5stage-soc := $(CHI_DPI_DIR)/chi_dpi_memory_dpi.cc $(CHI_DPI_DIR)/chi_memory.cc
SOC_DPI_SOURCE_single-core-spike-soc := $(SOC_DPI_SOURCE_single-core-rv5stage-soc) $(SPIKE_DPI_DIR)/spike_core.cc $(SPIKE_DPI_DIR)/spike_dpi.cc
SOC_DPI_SOURCE_tiled-rv5stage-soc := $(SOC_DPI_SOURCE_single-core-rv5stage-soc)
SOC_DPI_SOURCE_mini := $(if $(filter spike,$(CORE)),$(SPIKE_DPI_DIR)/spike_core.cc $(SPIKE_DPI_DIR)/spike_dpi.cc)
SOC_DPI_SOURCE_single := $(SOC_DPI_SOURCE_single-core-rv5stage-soc) $(SOC_DPI_SOURCE_mini)
SOC_DPI_SOURCE_tiled := $(SOC_DPI_SOURCE_single-core-rv5stage-soc) $(SOC_DPI_SOURCE_mini)
SOC_DPI_SOURCE := $(SOC_DPI_SOURCE_$(SOC)) $(UART_DPI_DIR)/uart_dpi.cc
SOC_DPI_HEADER_single-core-rv5stage-soc := $(CHI_DPI_DIR)/chi_dpi_memory_dpi.h $(CHI_DPI_DIR)/chi_memory.h $(VERILATOR_DIR)/chi_image_memory.h
SOC_DPI_HEADER_single-core-spike-soc := $(SOC_DPI_HEADER_single-core-rv5stage-soc) $(SPIKE_DPI_DIR)/spike_core.h $(SPIKE_DPI_DIR)/spike_dpi.h
SOC_DPI_HEADER_tiled-rv5stage-soc := $(SOC_DPI_HEADER_single-core-rv5stage-soc)
SOC_DPI_HEADER_mini := $(if $(filter spike,$(CORE)),$(SPIKE_DPI_DIR)/spike_core.h $(SPIKE_DPI_DIR)/spike_dpi.h)
SOC_DPI_HEADER_single := $(SOC_DPI_HEADER_single-core-rv5stage-soc) $(SOC_DPI_HEADER_mini)
SOC_DPI_HEADER_tiled := $(SOC_DPI_HEADER_single-core-rv5stage-soc) $(SOC_DPI_HEADER_mini)
SOC_DPI_HEADER := $(SOC_DPI_HEADER_$(SOC)) $(UART_DPI_DIR)/uart_dpi.h
SPIKE_DPI_CPPFLAGS := -I$(SPIKE_DPI_DIR) -I$(FESVR_PREFIX)/include/riscv -iquote $(FESVR_PREFIX)/include/fesvr
SOC_DPI_CFLAGS_single-core-rv5stage-soc := -DRHODIUM_CHI_MEMORY
SOC_DPI_CFLAGS_single-core-spike-soc := -DRHODIUM_CHI_MEMORY $(SPIKE_DPI_CPPFLAGS)
SOC_DPI_CFLAGS_tiled-rv5stage-soc := -DRHODIUM_CHI_MEMORY
SOC_DPI_CFLAGS_mini := $(if $(filter spike,$(CORE)),$(SPIKE_DPI_CPPFLAGS))
SOC_DPI_CFLAGS_single := -DRHODIUM_CHI_MEMORY $(SOC_DPI_CFLAGS_mini)
SOC_DPI_CFLAGS_tiled := -DRHODIUM_CHI_MEMORY $(SOC_DPI_CFLAGS_mini)
SOC_DPI_CFLAGS := $(SOC_DPI_CFLAGS_$(SOC))
SOC_DPI_DEPENDENCY_single-core-spike-soc = $(SPIKE_LIBRARY)
SOC_DPI_DEPENDENCY_mini = $(if $(filter spike,$(CORE)),$(SPIKE_LIBRARY))
SOC_DPI_DEPENDENCY_single = $(SOC_DPI_DEPENDENCY_mini)
SOC_DPI_DEPENDENCY_tiled = $(SOC_DPI_DEPENDENCY_mini)
SOC_DPI_DEPENDENCY = $(SOC_DPI_DEPENDENCY_$(SOC))
SOC_DPI_LDFLAGS_single-core-spike-soc := -L$(FESVR_PREFIX)/lib -Wl,-rpath,$(FESVR_PREFIX)/lib -lriscv -ldisasm -lsoftfloat
SOC_DPI_LDFLAGS_mini := $(if $(filter spike,$(CORE)),$(SOC_DPI_LDFLAGS_single-core-spike-soc))
SOC_DPI_LDFLAGS_single := $(SOC_DPI_LDFLAGS_mini)
SOC_DPI_LDFLAGS_tiled := $(SOC_DPI_LDFLAGS_mini)
SOC_DPI_LDFLAGS := $(SOC_DPI_LDFLAGS_$(SOC))

CPPFLAGS += -I$(FESVR_PREFIX)/include -I$(FESVR_DIR) -I$(VERILATOR_DIR) -I$(CHI_DPI_DIR)
CXXFLAGS += -std=c++20 -Wall -Wextra -Werror
FESVR_LIBRARY := $(FESVR_PREFIX)/lib/libfesvr.a
SPIKE_LIBRARY := $(FESVR_PREFIX)/lib/libriscv.so
SOC_BUILD_DIR := $(BUILD_ROOT)/$(SOC)$(if $(filter mini single tiled,$(SOC)),-$(CORE))
SOC_ID := $(SOC)$(if $(filter mini single tiled,$(SOC)),-$(CORE))
ifeq ($(TRACE),1)
ifneq ($(SOC)-$(CORE),single-rv5stage)
ifneq ($(SOC),single-core-rv5stage-soc)
$(error TRACE=1 currently supports SOC=single CORE=rv5stage only)
endif
endif
SOC_BUILD_DIR := $(SOC_BUILD_DIR)-trace
TRACE_HEADER := $(SOC_BUILD_DIR)/soc_events.h
TRACE_BUILD_DIR := $(SOC_BUILD_DIR)/rheg
TRACE_LIBRARY := $(TRACE_BUILD_DIR)/librheg_perfetto.a
TRACE_SOURCES := $(VERILATOR_DIR)/rheg_dpi.cc
TRACE_DEPENDENCIES := $(TRACE_HEADER) $(TRACE_LIBRARY)
TRACE_VFLAGS := +define+RHEG_TRACE
TRACE_CFLAGS := -I$(SOC_BUILD_DIR) -I$(REPO_DIR)/rheg/runtime -I$(REPO_DIR)/rheg/perfetto
TRACE_LDFLAGS := $(TRACE_LIBRARY) $(TRACE_BUILD_DIR)/librheg_runtime.a -lz
TRACE_RUN_ARGS = +rheg-trace=$(abspath $(TRACE_FILE))
else ifneq ($(TRACE),0)
$(error TRACE must be 0 or 1)
endif
DPI_BUILD_DIR := $(BUILD_ROOT)/tools
TILED_LOWERING_DIR := $(BUILD_ROOT)/tiled-lowering
SPIKE_LOWERING_DIR := $(BUILD_ROOT)/spike-lowering
SPIKE_MLIR := $(SPIKE_LOWERING_DIR)/spike.mlir
SPIKE_VERILOG := $(SPIKE_LOWERING_DIR)/SpikeCore.sv
SPIKE_VERILATOR_DIR := $(SPIKE_LOWERING_DIR)/obj
SOC_MLIR := $(SOC_BUILD_DIR)/soc_harness.mlir
SOC_VERILOG := $(SOC_BUILD_DIR)/SoCHarness.sv
SOC_OBJECT_DIR := $(SOC_BUILD_DIR)/obj
SIMULATOR_BINARY := $(if $(PREBUILT_SIMULATOR),$(abspath $(PREBUILT_SIMULATOR)),$(SOC_OBJECT_DIR)/VTestDriver)
SMOKE_BINARY := $(SOC_BUILD_DIR)/rv5stage_smoke.elf
UART_PTY_BINARY := $(SOC_BUILD_DIR)/rv5stage_uart_pty.elf
TILED_MEMORY_BINARY := $(SOC_BUILD_DIR)/rv5stage_tiled_memory.elf
BOOT_BINARIES := $(SOC_BUILD_DIR)/rv5stage_boot_2000.elf $(SOC_BUILD_DIR)/rv5stage_boot_3000.elf
HOST_MMIO_BINARY := $(SOC_BUILD_DIR)/rv5stage_host_mmio.elf
HOST_MMIO_SIGNATURE := $(SOC_BUILD_DIR)/rv5stage_host_mmio.signature
ZICBOZ_BINARY := $(SOC_BUILD_DIR)/rv5stage_zicboz.elf
ZICBOZ_SIGNATURE := $(SOC_BUILD_DIR)/rv5stage_zicboz.signature
ZIHINTNTL_BINARY := $(SOC_BUILD_DIR)/rv5stage_zihintntl.elf
ZIHINTNTL_SIGNATURE := $(SOC_BUILD_DIR)/rv5stage_zihintntl.signature
LRSC_BINARIES := $(SOC_BUILD_DIR)/rv5stage_lrsc_0.elf $(SOC_BUILD_DIR)/rv5stage_lrsc_1.elf
LRSC_HARTS := $(if $(filter tiled tiled-rv5stage-soc,$(SOC)),8,1)
LRSC_IALIGN := $(if $(filter mini mini-rv5stage-soc,$(SOC)),4,2)
LRSC_PAGE_TABLE_BASE := $(if $(filter mini mini-rv5stage-soc,$(SOC)),0x8000b000,0x80010000)
LRSC_MEMORY_LIMIT := $(if $(filter mini mini-rv5stage-soc,$(SOC)),0x80010000,0xc0000000)
SIMULATOR_SOURCES := $(sort $(shell find $(REPO_DIR) -type f \( -name '*.rhdl' -o -name '*.rhm' \)))

# Failed elaboration or lowering must not leave a reusable partial artifact.
.DELETE_ON_ERROR:

.PHONY: all setup simulator run smoke trace-smoke boot-test host-mmio-test uart-pty-test zicboz-test zihintntl-test dpi-compile-check spike-core-compile-check spike-dpi-compile-check spike-dpi-abi-check spike-core-test spike-lowering-test chi-dpi-memory-test transport-test tiled-lowering-test

all: simulator

include arch-test/Makefile.inc
include program-test/Makefile.inc
include opensbi/Makefile.inc

setup:
	PYTHON="$(PYTHON)" FESVR_PREFIX="$(FESVR_PREFIX)" $(FESVR_DIR)/install.sh

simulator: $(SIMULATOR_BINARY)
	$(if $(PREBUILT_SIMULATOR),$(PYTHON) program-test/artifact.py verify --binary "$(SIMULATOR_BINARY)" --soc "$(SOC_ID)",@true)
	@echo "Ready $(SOC_ID) simulator: $(SIMULATOR_BINARY)"

run: simulator
ifeq ($(TRACE),1)
	@test -n "$(TRACE_FILE)" || (echo "TRACE_FILE is required for TRACE=1" >&2; exit 2)
endif
	@test -n "$(BINARY)" || \
	  (echo "BINARY is required; use 'make -C sims run BINARY=/path/to/program.elf'" >&2; exit 2)
	@test -f "$(abspath $(BINARY))" || \
	  (echo "target binary does not exist: $(abspath $(BINARY))" >&2; exit 2)
	$(SIMULATOR_BINARY) $(TRACE_RUN_ARGS) $(HTIF_ARGS) "$(abspath $(BINARY))" $(TARGET_ARGS)

smoke: $(SMOKE_BINARY)
	$(MAKE) run SOC=$(SOC) CORE=$(CORE) BINARY=$(SMOKE_BINARY)

uart-pty-test: $(UART_PTY_BINARY) simulator
	$(PYTHON) tests/uart_pty_test.py "$(SIMULATOR_BINARY)" "$(UART_PTY_BINARY)"

.PHONY: lrsc-test lrsc-run
lrsc-test:
	@test "$(CORE)" = rv5stage && test -n "$(filter $(SOC),mini single tiled mini-rv5stage-soc single-core-rv5stage-soc tiled-rv5stage-soc)"
	$(MAKE) lrsc-run SOC=$(SOC) TRACE=0 PREBUILT_SIMULATOR= \
	  BUILD_ROOT="$(BUILD_ROOT)/lrsc-test" \
	  HARNESS_SELECTOR=$(if $(filter tiled-rv5stage-soc,$(SOC)),sims/tests/lrsc-tiled-soc-harness.rhdl,$(HARNESS_SELECTOR_$(SOC)))

lrsc-run: $(LRSC_BINARIES) simulator
	@status=0; for mode in 0 1; do \
	  echo "LR/SC qualification: SOC=$(SOC) Sv39=$$mode harts=$(LRSC_HARTS)"; \
	  signature="$(SOC_BUILD_DIR)/lrsc_$$mode.signature"; \
	  if $(SIMULATOR_BINARY) +max-cycles=20000000 +signature="$$signature" +signature-granularity=8 "$(SOC_BUILD_DIR)/rv5stage_lrsc_$$mode.elf" && \
	    awk -v expected="$(if $(filter tiled-rv5stage-soc,$(SOC)),0000000000000040,0000000000000008)" '{ if ($$0 != expected) exit 1 } END { if (NR != 6) exit 1 }' "$$signature"; then \
	    echo "LR/SC qualification passed: SOC=$(SOC) Sv39=$$mode (6 placements)"; \
	  else echo "LR/SC qualification FAILED: SOC=$(SOC) Sv39=$$mode"; status=1; fi; \
	done; exit $$status

.PHONY: tiled-memory-test
tiled-memory-test:
	$(MAKE) "$(BUILD_ROOT)/tiled-memory-test/tiled-rv5stage-soc/rv5stage_tiled_memory.elf" run SOC=tiled-rv5stage-soc TRACE=0 PREBUILT_SIMULATOR= \
	  BUILD_ROOT="$(BUILD_ROOT)/tiled-memory-test" \
	  HARNESS_SELECTOR=sims/tests/tiled-rv5stage-soc-memory-harness.rhdl \
	  BINARY="$(BUILD_ROOT)/tiled-memory-test/tiled-rv5stage-soc/rv5stage_tiled_memory.elf" \
	  HTIF_ARGS=+max-cycles=50000000

trace-smoke:
	@test -n "$(TRACE_PROCESSOR)" || (echo "TRACE_PROCESSOR must name the native Perfetto importer" >&2; exit 2)
	@test -n "$(TRACE_FILE)" || (echo "TRACE_FILE is required" >&2; exit 2)
	$(MAKE) smoke SOC=single CORE=rv5stage TRACE=1
	TRACE_PROCESSOR="$(TRACE_PROCESSOR)" bash tests/check-event-trace.sh "$(TRACE_FILE)"
	TRACE_PROCESSOR="$(TRACE_PROCESSOR)" bash tests/check-event-driver.sh "$(BUILD_ROOT)/single-rv5stage-trace/obj/VTestDriver" "$(BUILD_ROOT)/single-rv5stage-trace/rv5stage_smoke.elf"

boot-test: $(BOOT_BINARIES) simulator
	@set -e; for binary in $(BOOT_BINARIES); do $(SIMULATOR_BINARY) "$$binary"; done
	@echo "Two ELF entries booted through the same $(SOC) harness and ROM"

host-mmio-test: $(HOST_MMIO_BINARY) simulator
	$(SIMULATOR_BINARY) +signature=$(HOST_MMIO_SIGNATURE) +signature-granularity=8 $(HOST_MMIO_BINARY)
	awk '{ if ($$0 != "123456789abcdef0") exit 1 } END { if (NR != 1) exit 1 }' $(HOST_MMIO_SIGNATURE)
	@echo "Host boot-register and UART MMIO checks passed"

zicboz-test: $(ZICBOZ_BINARY) simulator
	$(SIMULATOR_BINARY) +signature=$(ZICBOZ_SIGNATURE) +signature-granularity=16 $(ZICBOZ_BINARY)
	awk '{ expected = (NR > 4 && NR <= 8) ? "00000000000000000000000000000000" : "ffffffffffffffffffffffffffffffff"; if ($$0 != expected) exit 1 } END { if (NR != 12) exit 1 }' $(ZICBOZ_SIGNATURE)
	@echo "Zicboz offset, neighbor, and coherent signature checks passed"

ifneq ($(filter $(SOC_ID),single-rv5stage single-core-rv5stage-soc),)
zihintntl-test: $(ZIHINTNTL_BINARY) simulator
	$(SIMULATOR_BINARY) +signature=$(ZIHINTNTL_SIGNATURE) +signature-granularity=8 $(ZIHINTNTL_BINARY)
	awk 'NR == 1 && $$0 != "123456789abcdef0" { exit 1 } NR == 2 && $$0 != "0000000000000008" { exit 1 } NR == 8 && $$0 != "0000000000000001" { exit 1 } END { if (NR != 8) exit 1 }' $(ZIHINTNTL_SIGNATURE)
	@echo "Zihintntl translated non-allocation, dirty-data preservation, and coherent signature checks passed on $(SOC)"
else
zihintntl-test:
	@echo "zihintntl-test supports only SOC=single CORE=rv5stage" >&2
	@exit 2
endif

dpi-compile-check: $(FESVR_LIBRARY) | $(DPI_BUILD_DIR)
	@test -n "$(VERILATOR_ROOT)" || \
	  (echo "set VERILATOR_ROOT to the Verilator share directory" >&2; exit 1)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I$(VERILATOR_ROOT)/include/vltstd \
	  -c $(VERILATOR_DIR)/direct_mem_htif_dpi.cc -o $(DPI_BUILD_DIR)/direct_mem_htif_dpi.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I$(VERILATOR_ROOT)/include/vltstd \
	  -c $(CHI_DPI_DIR)/chi_dpi_memory_dpi.cc -o $(DPI_BUILD_DIR)/chi_dpi_memory_dpi.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(UART_DPI_DIR)/uart_dpi.cc -o $(DPI_BUILD_DIR)/uart_dpi.o

spike-core-compile-check: $(SPIKE_LIBRARY) | $(DPI_BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SPIKE_DPI_CPPFLAGS) \
	  -c $(SPIKE_DPI_DIR)/spike_core.cc -o $(DPI_BUILD_DIR)/spike_core.o

spike-dpi-compile-check: spike-core-compile-check
	@test -n "$(VERILATOR_ROOT)" || \
	  (echo "set VERILATOR_ROOT to the Verilator share directory" >&2; exit 1)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SPIKE_DPI_CPPFLAGS) -I$(VERILATOR_ROOT)/include/vltstd \
	  -c $(SPIKE_DPI_DIR)/spike_dpi.cc -o $(DPI_BUILD_DIR)/spike_dpi.o

spike-core-test: $(SPIKE_LIBRARY) | $(DPI_BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SPIKE_DPI_CPPFLAGS) \
	  $(SPIKE_DPI_DIR)/spike_core.cc $(REPO_DIR)/cores/spike/tests/spike_core_test.cc \
	  -L$(FESVR_PREFIX)/lib -Wl,-rpath,$(FESVR_PREFIX)/lib \
	  -lriscv -lfesvr -ldisasm -lsoftfloat -pthread \
	  -o $(DPI_BUILD_DIR)/spike_core_test
	$(DPI_BUILD_DIR)/spike_core_test

chi-dpi-memory-test: | $(DPI_BUILD_DIR)
	@test -n "$(VERILATOR_ROOT)" || \
	  (echo "set VERILATOR_ROOT to the Verilator share directory" >&2; exit 1)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I$(VERILATOR_ROOT)/include/vltstd \
	  $(CHI_DPI_DIR)/chi_dpi_memory_dpi.cc $(CHI_DPI_DIR)/chi_memory.cc \
	  $(REPO_DIR)/chi/tests/chi_dpi_memory_dpi_test.cc \
	  -o $(DPI_BUILD_DIR)/chi_dpi_memory_dpi_test
	$(DPI_BUILD_DIR)/chi_dpi_memory_dpi_test

transport-test: $(FESVR_LIBRARY) | $(DPI_BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FESVR_DIR)/direct_mem_htif.cc tests/direct_mem_htif_test.cc $(FESVR_LIBRARY) -pthread -o $(DPI_BUILD_DIR)/direct_mem_htif_test
	$(DPI_BUILD_DIR)/direct_mem_htif_test
	cd $(REPO_DIR) && RACO="$(RACO)" tools/run-racket-tests.sh \
	  sims/tests/direct-memory-htif-test.rhm

tiled-lowering-test: | $(TILED_LOWERING_DIR)
	@test -x "$(CIRCT_OPT)" || \
	  (echo "circt-opt not found; run 'make setup-circt' at the repository root or set CIRCT_OPT" >&2; exit 1)
	cd $(REPO_DIR) && RACKET="$(RACKET)" tools/run-racket.sh -S $(REPO_DIR) \
	  $(HARNESS_EMITTER) $(HARNESS_SELECTOR_tiled-rv5stage-soc) > $(TILED_LOWERING_DIR)/soc_harness.mlir
	$(CIRCT_OPT) --canonicalize --cse $(TILED_LOWERING_DIR)/soc_harness.mlir -o /dev/null

spike-lowering-test: | $(SPIKE_LOWERING_DIR)
	@test -x "$(CIRCT_OPT)" || \
	  (echo "circt-opt not found; run 'make setup-circt' at the repository root or set CIRCT_OPT" >&2; exit 1)
	cd $(REPO_DIR) && RACKET="$(RACKET)" tools/run-racket.sh -S $(REPO_DIR) \
	  cores/spike/tests/emit.rhm > $(SPIKE_MLIR)
	$(CIRCT_OPT) --canonicalize --cse $(SPIKE_MLIR) -o /dev/null

spike-dpi-abi-check: spike-lowering-test
	@test -n "$(VERILATOR_ROOT)" || \
	  (echo "set VERILATOR_ROOT to the Verilator share directory" >&2; exit 1)
	$(CIRCT_OPT) --canonicalize --cse --prettify-verilog \
	  --lower-seq-hlmem --lower-sim-to-sv --lower-verif-to-sv \
	  --lower-seq-to-sv='disable-mem-randomization=true disable-reg-randomization=true' \
	  --export-verilog $(SPIKE_MLIR) -o /dev/null > $(SPIKE_VERILOG)
	$(VERILATOR) --cc -Wno-fatal --top-module SpikeCore --Mdir $(SPIKE_VERILATOR_DIR) $(SPIKE_VERILOG)
	$(CXX) -std=c++20 -I$(VERILATOR_ROOT)/include/vltstd \
	  -include $(SPIKE_DPI_DIR)/spike_dpi.h \
	  -include $(SPIKE_VERILATOR_DIR)/VSpikeCore__Dpi.h \
	  -x c++ -fsyntax-only /dev/null

$(FESVR_LIBRARY):
	@echo "FESVR is not installed; run 'make -C sims setup' first" >&2
	@exit 1

$(SPIKE_LIBRARY):
	@echo "Spike is not installed; run 'make -C sims setup' first" >&2
	@exit 1

$(SOC_BUILD_DIR) $(DPI_BUILD_DIR) $(TILED_LOWERING_DIR) $(SPIKE_LOWERING_DIR):
	mkdir -p $@

$(SOC_MLIR): $(SIMULATOR_SOURCES) $(HARNESS_EMITTER) $(CURDIR)/Makefile | $(SOC_BUILD_DIR)
	@test -n "$(HARNESS_SELECTOR)" || \
	  (echo "unsupported SOC '$(SOC)'; expected mini, single, or tiled" >&2; exit 2)
ifeq ($(TRACE),1)
	cd $(REPO_DIR) && RACKET="$(RACKET)" tools/run-racket.sh -S $(REPO_DIR) \
	  sims/materialize-event-harness.rkt $(TRACE_HEADER) > $(SOC_MLIR)
else
	cd $(REPO_DIR) && RACKET="$(RACKET)" tools/run-racket.sh -S $(REPO_DIR) \
	  $(HARNESS_EMITTER) $(HARNESS_SELECTOR) > $(SOC_MLIR)
endif

ifeq ($(TRACE),1)
$(SOC_MLIR): $(CURDIR)/materialize-event-harness.rkt
$(TRACE_HEADER): $(SOC_MLIR)
	@test -f $@ || (cd $(REPO_DIR) && RACKET="$(RACKET)" tools/run-racket.sh -S $(REPO_DIR) \
	  sims/materialize-event-harness.rkt $@ > $(SOC_MLIR))

$(TRACE_LIBRARY): $(wildcard $(REPO_DIR)/rheg/runtime/* $(REPO_DIR)/rheg/perfetto/* $(REPO_DIR)/riscv/riscv-isa-sim-patches/*.patch) $(REPO_DIR)/riscv/riscv-isa-sim-patches/series $(PATCHED_SUBMODULE_TOOL)
	$(MAKE) -C $(REPO_DIR)/rheg/perfetto BUILD_DIR=$(TRACE_BUILD_DIR) PYTHON=$(PYTHON) CXX=$(CXX) \
	  $(if $(NLOHMANN_JSON_INCLUDE_DIR),NLOHMANN_JSON_INCLUDE_DIR=$(NLOHMANN_JSON_INCLUDE_DIR)) \
	  libraries
endif

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

ifeq ($(PREBUILT_SIMULATOR),)
$(SIMULATOR_BINARY): $(CURDIR)/Makefile $(SOC_VERILOG) $(CURDIR)/TestDriver.v \
                     $(VERILATOR_DIR)/image_memory_map.cc $(FESVR_DIR)/image_memory.h \
                     $(VERILATOR_DIR)/relink.mk \
                     $(FESVR_DIR)/direct_mem_htif.cc \
                     $(FESVR_DIR)/direct_mem_htif.h \
                     $(VERILATOR_DIR)/direct_mem_htif_dpi.cc \
                     $(VERILATOR_DIR)/direct_mem_htif_dpi.h \
                     $(SOC_DPI_SOURCE) \
                     $(SOC_DPI_HEADER) \
                     $(TRACE_SOURCES) $(TRACE_DEPENDENCIES) \
                     $(FESVR_LIBRARY) $(SOC_DPI_DEPENDENCY)
	# The outer prerequisites include libraries omitted from Verilator's link rule.
	$(VERILATOR) --binary --timing --vpi --assert --Wno-UNOPTFLAT --Wno-SYMRSVDWORD --build-jobs $(BUILD_JOBS) $(TRACE_VFLAGS) \
	  -MAKEFLAGS "-f $(VERILATOR_DIR)/relink.mk" \
	  --top-module TestDriver --Mdir $(SOC_OBJECT_DIR) \
	  $(SOC_VERILOG) $(CURDIR)/TestDriver.v \
	  $(FESVR_DIR)/direct_mem_htif.cc $(VERILATOR_DIR)/direct_mem_htif_dpi.cc \
	  $(SOC_DPI_SOURCE) $(TRACE_SOURCES) $(VERILATOR_DIR)/image_memory_map.cc \
	  -CFLAGS "-I$(FESVR_PREFIX)/include -I$(FESVR_DIR) -I$(VERILATOR_DIR) -I$(CHI_DPI_DIR) -std=c++20 $(SOC_DPI_CFLAGS) $(TRACE_CFLAGS)" \
	  -LDFLAGS "$(SOC_DPI_LDFLAGS) $(FESVR_LIBRARY) -pthread $(TRACE_LDFLAGS)"
else
$(SIMULATOR_BINARY):
	@echo "Prebuilt simulator is missing: $@" >&2; exit 2
endif

$(SMOKE_BINARY): tests/programs/rv5stage_smoke.S \
                 tests/programs/rv5stage_smoke.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64ic_zicsr -mabi=lp64 -nostdlib -nostartfiles \
	  -DRHODIUM_SMOKE_COMPRESSED_SENTINEL=1 \
	  -Wl,--build-id=none -Wl,-T,tests/programs/rv5stage_smoke.ld $< -o $@

$(UART_PTY_BINARY): tests/programs/rv5stage_uart_pty.S tests/programs/rv5stage_smoke.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64i -mabi=lp64 -nostdlib -nostartfiles \
	  -Wl,--build-id=none -Wl,-T,tests/programs/rv5stage_smoke.ld $< -o $@

$(TILED_MEMORY_BINARY): tests/programs/rv5stage_tiled_memory.S tests/programs/rv5stage_smoke.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64i -mabi=lp64 -nostdlib -nostartfiles \
	  -Wl,--build-id=none -Wl,-T,tests/programs/rv5stage_smoke.ld $< -o $@

$(SOC_BUILD_DIR)/rv5stage_lrsc_%.elf: tests/programs/rv5stage_lrsc.S tests/programs/rv5stage_lrsc.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64ia_zicsr_zifencei -mabi=lp64 -nostdlib -nostartfiles \
	  -DLRSC_HARTS=$(LRSC_HARTS) -DLRSC_TRANSLATED=$* \
	  -DLRSC_IALIGN=$(LRSC_IALIGN) -DLRSC_PAGE_TABLE_BASE=$(LRSC_PAGE_TABLE_BASE) \
	  -Wl,--defsym,lrsc_page_tables=$(LRSC_PAGE_TABLE_BASE),--defsym,lrsc_memory_limit=$(LRSC_MEMORY_LIMIT) \
	  -Wl,--build-id=none -Wl,-T,tests/programs/rv5stage_lrsc.ld $< -o $@

$(HOST_MMIO_BINARY): tests/programs/rv5stage_host_mmio.S tests/programs/rv5stage_host_mmio.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64i -mabi=lp64 -nostdlib -nostartfiles \
	  -Wl,--build-id=none -Wl,-T,tests/programs/rv5stage_host_mmio.ld $< -o $@

$(SOC_BUILD_DIR)/rv5stage_boot_%.elf: tests/programs/rv5stage_smoke.S tests/programs/rv5stage_smoke.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64i_zicsr -mabi=lp64 -nostdlib -nostartfiles \
	  -Wl,--build-id=none -Wl,--defsym,payload_entry=0x8000$* \
	  -Wl,-T,tests/programs/rv5stage_smoke.ld $< -o $@

$(ZICBOZ_BINARY): tests/programs/rv5stage_zicboz.S tests/programs/rv5stage_zicboz.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64im_zicsr -mabi=lp64 -nostdlib -nostartfiles \
	  -Wl,--build-id=none -Wl,-T,tests/programs/rv5stage_zicboz.ld $< -o $@

$(ZIHINTNTL_BINARY): tests/programs/rv5stage_zihintntl.S tests/programs/rv5stage_zihintntl.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64im_zicsr_zicntr -mabi=lp64 -nostdlib -nostartfiles \
	  -Wl,--build-id=none -Wl,-T,tests/programs/rv5stage_zihintntl.ld $< -o $@
