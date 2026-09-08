# Builds a reusable FESVR-backed SoC simulator and runs target binaries through HTIF.
REPO_DIR := $(abspath ..)
FESVR_DIR := $(CURDIR)/fesvr
VERILATOR_DIR := $(CURDIR)/verilator
CHI_DPI_DIR := $(REPO_DIR)/chi/dpi
UART_DPI_DIR := $(REPO_DIR)/devices/dpi
FESVR_REVISION := e1fa113cfb6d55d878a3c1ea3befa8d9c13ce154
FESVR_PREFIX ?= $(REPO_DIR)/.tools/fesvr-$(FESVR_REVISION)
SOC ?= simple
TRACE ?= 0
TRACE_FILE ?=
NLOHMANN_JSON_SOURCE_DIR ?=
BUILD_JOBS ?= 4
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
SOC_DPI_SOURCE_tiled := $(CHI_DPI_DIR)/chi_dpi_memory_dpi.cc
SOC_DPI_SOURCE := $(SOC_DPI_SOURCE_$(SOC)) $(UART_DPI_DIR)/uart_dpi.cc
SOC_DPI_HEADER_simple := $(CHI_DPI_DIR)/chi_dpi_memory_dpi.h
SOC_DPI_HEADER_tiled := $(CHI_DPI_DIR)/chi_dpi_memory_dpi.h
SOC_DPI_HEADER := $(SOC_DPI_HEADER_$(SOC)) $(UART_DPI_DIR)/uart_dpi.h

CPPFLAGS += -I$(FESVR_PREFIX)/include -I$(FESVR_DIR) -I$(VERILATOR_DIR) -I$(CHI_DPI_DIR)
CXXFLAGS += -std=c++20 -Wall -Wextra -Werror
FESVR_LIBRARY := $(FESVR_PREFIX)/lib/libfesvr.a
SOC_BUILD_DIR := $(BUILD_ROOT)/$(SOC)
ifeq ($(TRACE),1)
ifneq ($(SOC),simple)
$(error TRACE=1 currently supports SOC=simple only)
endif
SOC_BUILD_DIR := $(BUILD_ROOT)/$(SOC)-trace
TRACE_HEADER := $(SOC_BUILD_DIR)/soc_events.h
TRACE_BUILD_DIR := $(SOC_BUILD_DIR)/rheg
TRACE_LIBRARY := $(TRACE_BUILD_DIR)/librheg_perfetto.a
TRACE_SOURCES := $(VERILATOR_DIR)/rheg_dpi.cc
TRACE_DEPENDENCIES := $(TRACE_HEADER) $(TRACE_LIBRARY)
TRACE_VFLAGS := +define+RHEG_TRACE
TRACE_CFLAGS := -I$(SOC_BUILD_DIR) -I$(REPO_DIR)/rheg/runtime -I$(REPO_DIR)/rheg/perfetto
TRACE_LDFLAGS := $(TRACE_LIBRARY) $(TRACE_BUILD_DIR)/librheg_runtime.a
TRACE_RUN_ARGS = +rheg-trace=$(abspath $(TRACE_FILE))
else ifneq ($(TRACE),0)
$(error TRACE must be 0 or 1)
endif
DPI_BUILD_DIR := $(BUILD_ROOT)/tools
TILED_LOWERING_DIR := $(BUILD_ROOT)/tiled-lowering
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
SIMULATOR_SOURCES := $(sort $(shell find $(REPO_DIR) -type f \( -name '*.rhdl' -o -name '*.rhm' \)))

# Failed elaboration or lowering must not leave a reusable partial artifact.
.DELETE_ON_ERROR:

.PHONY: all setup simulator run smoke trace-smoke boot-test host-mmio-test uart-pty-test zicboz-test zihintntl-test dpi-compile-check chi-dpi-memory-test transport-test tiled-lowering-test

all: simulator

include arch-test/Makefile.inc
include program-test/Makefile.inc

setup:
	$(FESVR_DIR)/install.sh

simulator: $(SIMULATOR_BINARY)
	$(if $(PREBUILT_SIMULATOR),$(PYTHON) program-test/artifact.py verify --binary "$(SIMULATOR_BINARY)" --soc "$(SOC)",@true)
	@echo "Ready $(SOC) simulator: $(SIMULATOR_BINARY)"

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
	$(MAKE) run SOC=$(SOC) BINARY=$(SMOKE_BINARY)

uart-pty-test: $(UART_PTY_BINARY) simulator
	$(PYTHON) tests/uart_pty_test.py "$(SIMULATOR_BINARY)" "$(UART_PTY_BINARY)"

.PHONY: tiled-memory-test
tiled-memory-test:
	$(MAKE) "$(BUILD_ROOT)/tiled-memory-test/tiled/rv5stage_tiled_memory.elf" run SOC=tiled TRACE=0 PREBUILT_SIMULATOR= \
	  BUILD_ROOT="$(BUILD_ROOT)/tiled-memory-test" \
	  HARNESS_SOURCE=sims/tests/tiled-memory-harness.rhdl \
	  BINARY="$(BUILD_ROOT)/tiled-memory-test/tiled/rv5stage_tiled_memory.elf" \
	  HTIF_ARGS=+max-cycles=50000000

trace-smoke:
	@test -n "$(TRACE_PROCESSOR)" || (echo "TRACE_PROCESSOR must name the native Perfetto importer" >&2; exit 2)
	@test -n "$(TRACE_FILE)" || (echo "TRACE_FILE is required" >&2; exit 2)
	$(MAKE) smoke SOC=simple TRACE=1
	TRACE_PROCESSOR="$(TRACE_PROCESSOR)" bash tests/check-event-trace.sh "$(TRACE_FILE)"
	TRACE_PROCESSOR="$(TRACE_PROCESSOR)" bash tests/check-event-driver.sh "$(BUILD_ROOT)/simple-trace/obj/VTestDriver" "$(BUILD_ROOT)/simple-trace/rv5stage_smoke.elf"

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

zihintntl-test: $(ZIHINTNTL_BINARY) simulator
	$(SIMULATOR_BINARY) +signature=$(ZIHINTNTL_SIGNATURE) +signature-granularity=8 $(ZIHINTNTL_BINARY)
	awk -v variants="$(if $(filter mini,$(SOC)),0000000000000004,0000000000000008)" -v fp="$(if $(filter simple,$(SOC)),0000000000000001,0000000000000000)" \
	  'NR == 1 && $$0 != "123456789abcdef0" { exit 1 } NR == 2 && $$0 != variants { exit 1 } NR == 8 && $$0 != fp { exit 1 } END { if (NR != 8) exit 1 }' $(ZIHINTNTL_SIGNATURE)
	@echo "Zihintntl translated non-allocation, resident preservation, and coherent signature checks passed on $(SOC)"

dpi-compile-check: $(FESVR_LIBRARY) | $(DPI_BUILD_DIR)
	@test -n "$(VERILATOR_ROOT)" || \
	  (echo "set VERILATOR_ROOT to the Verilator share directory" >&2; exit 1)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I$(VERILATOR_ROOT)/include/vltstd \
	  -c $(VERILATOR_DIR)/direct_mem_htif_dpi.cc -o $(DPI_BUILD_DIR)/direct_mem_htif_dpi.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I$(VERILATOR_ROOT)/include/vltstd \
	  -c $(CHI_DPI_DIR)/chi_dpi_memory_dpi.cc -o $(DPI_BUILD_DIR)/chi_dpi_memory_dpi.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(UART_DPI_DIR)/uart_dpi.cc -o $(DPI_BUILD_DIR)/uart_dpi.o

chi-dpi-memory-test: | $(DPI_BUILD_DIR)
	@test -n "$(VERILATOR_ROOT)" || \
	  (echo "set VERILATOR_ROOT to the Verilator share directory" >&2; exit 1)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I$(VERILATOR_ROOT)/include/vltstd \
	  $(CHI_DPI_DIR)/chi_dpi_memory_dpi.cc \
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
ifeq ($(TRACE),1)
	cd $(REPO_DIR) && RACKET="$(RACKET)" tools/run-racket.sh -S $(REPO_DIR) \
	  sims/materialize-event-harness.rkt $(TRACE_HEADER) > $(SOC_MLIR)
else
	cd $(REPO_DIR) && RACKET="$(RACKET)" tools/run-racket.sh -S $(REPO_DIR) \
	  $(HARNESS_EMITTER) $(HARNESS_SOURCE) > $(SOC_MLIR)
endif

ifeq ($(TRACE),1)
$(SOC_MLIR): $(CURDIR)/materialize-event-harness.rkt
$(TRACE_HEADER): $(SOC_MLIR)
	@test -f $@ || (cd $(REPO_DIR) && RACKET="$(RACKET)" tools/run-racket.sh -S $(REPO_DIR) \
	  sims/materialize-event-harness.rkt $@ > $(SOC_MLIR))

$(TRACE_LIBRARY): $(wildcard $(REPO_DIR)/rheg/runtime/* $(REPO_DIR)/rheg/perfetto/*)
	cmake -S $(REPO_DIR)/rheg/perfetto -B $(TRACE_BUILD_DIR) \
	  $(if $(NLOHMANN_JSON_SOURCE_DIR),-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$(NLOHMANN_JSON_SOURCE_DIR)) \
	  $(if $(RHEG_SPIKE_SOURCE_DIR),-DFETCHCONTENT_SOURCE_DIR_RHEG_SPIKE=$(RHEG_SPIKE_SOURCE_DIR))
	cmake --build $(TRACE_BUILD_DIR) -j $(BUILD_JOBS)
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
                     $(VERILATOR_DIR)/relink.mk \
                     $(FESVR_DIR)/direct_mem_htif.cc \
                     $(FESVR_DIR)/direct_mem_htif.h \
                     $(VERILATOR_DIR)/direct_mem_htif_dpi.cc \
                     $(VERILATOR_DIR)/direct_mem_htif_dpi.h \
                     $(SOC_DPI_SOURCE) \
                     $(SOC_DPI_HEADER) \
                     $(TRACE_SOURCES) $(TRACE_DEPENDENCIES) \
                     $(FESVR_LIBRARY)
	# The outer prerequisites include libraries omitted from Verilator's link rule.
	$(VERILATOR) --binary --timing --vpi --assert --Wno-UNOPTFLAT --Wno-SYMRSVDWORD --build-jobs $(BUILD_JOBS) $(TRACE_VFLAGS) \
	  -MAKEFLAGS "-f $(VERILATOR_DIR)/relink.mk" \
	  --top-module TestDriver --Mdir $(SOC_OBJECT_DIR) \
	  $(SOC_VERILOG) $(CURDIR)/TestDriver.v \
	  $(FESVR_DIR)/direct_mem_htif.cc $(VERILATOR_DIR)/direct_mem_htif_dpi.cc \
	  $(SOC_DPI_SOURCE) $(TRACE_SOURCES) \
	  -CFLAGS "-I$(FESVR_PREFIX)/include -I$(FESVR_DIR) -I$(VERILATOR_DIR) -std=c++20 $(TRACE_CFLAGS)" \
	  -LDFLAGS "$(FESVR_LIBRARY) -pthread $(TRACE_LDFLAGS)"
else
$(SIMULATOR_BINARY):
	@echo "Prebuilt simulator is missing: $@" >&2; exit 2
endif

$(SMOKE_BINARY): tests/programs/rv5stage_smoke.S \
                 tests/programs/rv5stage_smoke.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64i_zicsr -mabi=lp64 -nostdlib -nostartfiles \
	  -Wl,--build-id=none -Wl,-T,tests/programs/rv5stage_smoke.ld $< -o $@

$(UART_PTY_BINARY): tests/programs/rv5stage_uart_pty.S tests/programs/rv5stage_smoke.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64i -mabi=lp64 -nostdlib -nostartfiles \
	  -Wl,--build-id=none -Wl,-T,tests/programs/rv5stage_smoke.ld $< -o $@

$(TILED_MEMORY_BINARY): tests/programs/rv5stage_tiled_memory.S tests/programs/rv5stage_smoke.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64i -mabi=lp64 -nostdlib -nostartfiles \
	  -Wl,--build-id=none -Wl,-T,tests/programs/rv5stage_smoke.ld $< -o $@

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
