# Builds Rhodium smoke designs and mapped MiniSoC artifacts through CIRCT and LibreLane.

.DELETE_ON_ERROR:

RHODIUM_ROOT := $(abspath ..)
VLSI_ROOT := $(CURDIR)
HARNESS_DIR := $(VLSI_ROOT)/double_wide_openframe
BUILD_DIR := $(VLSI_ROOT)/build
CONFIG := $(VLSI_ROOT)/openlane/double_wide_openframe_project_wrapper/config.yaml
WRAPPER := $(VLSI_ROOT)/verilog/rtl/double_wide_openframe_project_wrapper.v
HARNESS_WRAPPER := $(HARNESS_DIR)/verilog/rtl/double_wide_openframe_project_wrapper.v
PIN_TEMPLATE := $(HARNESS_DIR)/librelane/wrapper/pin_template.def
TOP_NAME := double_wide_openframe_project_wrapper
LVS_CONFIG := $(VLSI_ROOT)/openlane/rhodium_lvs_smoke/config.yaml
LVS_WRAPPER := $(VLSI_ROOT)/verilog/rtl/rhodium_lvs_smoke.v
LVS_TOP_NAME := rhodium_lvs_smoke
LVS_RUN_DIR := $(VLSI_ROOT)/openlane/rhodium_lvs_smoke/runs/RHODIUM_LVS
SRAM_ROOT := $(RHODIUM_ROOT)/sram
MEMORY_MAP_CATALOG := $(SRAM_ROOT)/sky130/macros.ini
MEMORY_MAPPER := $(SRAM_ROOT)/map-memories.py
MEMORY_SITE_PASS_DIR := $(SRAM_ROOT)/circt
MEMORY_SITE_PLUGIN := $(SRAM_ROOT)/build/circt/rhodium-memory-sites.so
MINI_SOC_MEMORY_POLICY := $(VLSI_ROOT)/designs/mini-soc/sky130/sram-map.yaml
MINI_SOC_BUILD_DIR := $(BUILD_DIR)/mini-soc
MINI_SOC_RHODIUM_SOURCES := $(addprefix $(RHODIUM_ROOT)/,$(shell cd $(RHODIUM_ROOT) && rg --files -g '*.rhdl'))
MINI_SOC_MLIR := $(MINI_SOC_BUILD_DIR)/mini-soc.mlir
MINI_SOC_FIRRTL_MEM_MLIR := $(MINI_SOC_BUILD_DIR)/mini-soc-firmem.mlir
MINI_SOC_MEMORY_SITE_INVENTORY := $(MINI_SOC_BUILD_DIR)/memory-sites.json
MINI_SOC_MEMORY_SITE_STAMP := $(MINI_SOC_BUILD_DIR)/memory-sites.stamp
MINI_SOC_RTL := $(MINI_SOC_BUILD_DIR)/mini-soc.sv
MINI_SOC_MEMORY_WRAPPERS := $(MINI_SOC_BUILD_DIR)/memory-wrappers.sv
MINI_SOC_MEMORY_MANIFEST := $(MINI_SOC_BUILD_DIR)/memory-manifest.json
MINI_SOC_MEMORY_CHECK := $(VLSI_ROOT)/tools/check-mini-soc-memory-map.py
MINI_SOC_SYNTH_CONFIG := $(VLSI_ROOT)/openlane/mini_soc/config.yaml
MINI_SOC_SLANG_RUN_DIR := $(VLSI_ROOT)/openlane/mini_soc/runs/RHODIUM_SLANG_CHECK
MINI_SOC_SLANG_NETLIST := $(MINI_SOC_SLANG_RUN_DIR)/final/nl/MiniSoC.nl.v
MINI_SOC_SYNTH_RUN_DIR := $(VLSI_ROOT)/openlane/mini_soc/runs/RHODIUM_SLANG_SYNTH
MINI_SOC_RAW_SYNTH_NETLIST := $(MINI_SOC_SYNTH_RUN_DIR)/final/nl/MiniSoC.nl.v
MINI_SOC_SYNTH_STEP_DIR ?= $(MINI_SOC_SYNTH_RUN_DIR)/01-yosys-synthesis
MINI_SOC_POST_SYNTH_DIR ?= $(MINI_SOC_BUILD_DIR)/synthesis
MINI_SOC_SYNTH_NETLIST := $(MINI_SOC_POST_SYNTH_DIR)/MiniSoC.nl.v
SKY130_SRAM_MODEL = $(PDK_ROOT)/$(PDK)/libs.ref/sky130_sram_macros/verilog/sky130_sram_2kbyte_1rw1r_32x512_8.v

RACKET ?= racket
CIRCT_OPT ?= $(RHODIUM_ROOT)/.tools/firtool-1.155.0/bin/circt-opt
CIRCT_ROOT ?= $(abspath $(dir $(CIRCT_OPT))/..)
VERILATOR ?= verilator
PYTHON ?= python3
YOSYS ?= yosys
NIX_SYSTEM := /nix/var/nix/profiles/default/bin/nix
NIX ?= $(if $(wildcard $(NIX_SYSTEM)),$(NIX_SYSTEM),nix)
NIX_FLAGS ?= --extra-experimental-features 'nix-command flakes' --accept-flake-config
PDK ?= sky130A
PDK_ROOT ?= $(HOME)/.ciel
PDK_COMMIT ?= 74c0e6b118a67d94c24172143d3bd597473fa63d
LIBRELANE := librelane --pdk-root $(PDK_ROOT) --pdk $(PDK)
export PDK_ROOT PDK

.PHONY: all setup-circt rtl contract-check rtl-check lvs-smoke-rtl-check memory-site-plugin memory-map-test \
	mini-soc-memory-map mini-soc-macro-rtl mini-soc-macro-rtl-check \
	mini-soc-slang-check mini-soc-slang-check-native \
	mini-soc-synth mini-soc-synth-native mini-soc-post-synth mini-soc-post-synth-native enable-pdk \
	harden harden-native lvs-smoke lvs-smoke-native integrate integrate-native gds clean

all: rtl-check

$(BUILD_DIR):
	mkdir -p $@

setup-circt:
	$(MAKE) -C $(RHODIUM_ROOT) setup-circt

$(BUILD_DIR)/rhodium-top.mlir: src/rhodium-top.rhdl tools/emit-top.rhm | $(BUILD_DIR)
	env PLTCOMPILEDROOTS=$(BUILD_DIR)/compiled $(RACKET) -S $(RHODIUM_ROOT) tools/emit-top.rhm > $@

$(BUILD_DIR)/rhodium-top.sv: $(BUILD_DIR)/rhodium-top.mlir
	@test -x $(CIRCT_OPT) || { \
		echo "circt-opt not found at $(CIRCT_OPT); run 'make setup-circt' or set CIRCT_OPT" >&2; \
		exit 1; \
	}
	$(CIRCT_OPT) \
		--strip-debuginfo-with-pred='drop-suffix=.mlir' \
		--canonicalize \
		--cse \
		--prettify-verilog \
		--lower-sim-to-sv \
		--lower-verif-to-sv \
		--lower-seq-to-sv='disable-mem-randomization=true disable-reg-randomization=true' \
		--sv-mask-non-synthesizable='mode=ifdef macro=SYNTHESIS' \
		--export-verilog \
		$< -o /dev/null > $@

rtl: $(BUILD_DIR)/rhodium-top.sv

contract-check:
	$(PYTHON) tools/check-openframe-contract.py \
		--wrapper $(WRAPPER) \
		--harness-wrapper $(HARNESS_WRAPPER) \
		--pin-template $(PIN_TEMPLATE)

rtl-check: rtl contract-check
	$(VERILATOR) --lint-only --top-module $(TOP_NAME) -DUSE_POWER_PINS \
		-Wno-fatal -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-PINCONNECTEMPTY \
		$(BUILD_DIR)/rhodium-top.sv $(WRAPPER)

lvs-smoke-rtl-check: rtl
	$(VERILATOR) --lint-only --top-module $(LVS_TOP_NAME) -DUSE_POWER_PINS \
		-Wno-fatal -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-PINCONNECTEMPTY \
		$(BUILD_DIR)/rhodium-top.sv $(LVS_WRAPPER)

$(MEMORY_SITE_PLUGIN): $(MEMORY_SITE_PASS_DIR)/CMakeLists.txt $(MEMORY_SITE_PASS_DIR)/MemorySitePass.cpp
	$(MAKE) -C $(SRAM_ROOT) plugin CIRCT_OPT=$(CIRCT_OPT) CIRCT_ROOT=$(CIRCT_ROOT)
	@test -s $@

memory-site-plugin: $(MEMORY_SITE_PLUGIN)

memory-map-test:
	$(MAKE) -C $(SRAM_ROOT) test CIRCT_OPT=$(CIRCT_OPT) CIRCT_ROOT=$(CIRCT_ROOT) \
		PYTHON=$(PYTHON) VERILATOR=$(VERILATOR)

$(MINI_SOC_BUILD_DIR):
	mkdir -p $@

$(MINI_SOC_MLIR): tools/emit-mini-soc.rhm $(MINI_SOC_RHODIUM_SOURCES) | $(MINI_SOC_BUILD_DIR)
	env PLTCOMPILEDROOTS=$(BUILD_DIR)/compiled PLTCOLLECTS=$(RHODIUM_ROOT): \
		$(RACKET) -S $(RHODIUM_ROOT) tools/emit-mini-soc.rhm > $@

$(MINI_SOC_MEMORY_SITE_STAMP): $(MINI_SOC_MLIR) $(MEMORY_SITE_PLUGIN) $(MINI_SOC_MEMORY_POLICY)
	@test -x $(CIRCT_OPT) || { \
		echo "circt-opt not found at $(CIRCT_OPT); run 'make setup-circt' or set CIRCT_OPT" >&2; \
		exit 1; \
	}
	$(CIRCT_OPT) \
		--load-pass-plugin=$(MEMORY_SITE_PLUGIN) \
		--pass-pipeline="builtin.module(strip-debuginfo-with-pred{drop-suffix=.mlir},canonicalize,cse,rhodium-select-hw-top{top=MiniSoC},hw-flatten-modules{hw-inline-all=true hw-inline-with-state=true},symbol-dce,lower-seq-firmem,rhodium-map-memory-sites{policy=$(MINI_SOC_MEMORY_POLICY) inventory=$(MINI_SOC_MEMORY_SITE_INVENTORY) top=MiniSoC},symbol-dce)" \
		$(MINI_SOC_MLIR) -o $(MINI_SOC_FIRRTL_MEM_MLIR)
	@test -s $(MINI_SOC_FIRRTL_MEM_MLIR)
	@test -s $(MINI_SOC_MEMORY_SITE_INVENTORY)
	touch $@

mini-soc-memory-map: \
		$(MINI_SOC_MEMORY_SITE_STAMP) $(MEMORY_MAPPER) $(MEMORY_MAP_CATALOG) $(MINI_SOC_MEMORY_CHECK)
	$(PYTHON) $(MEMORY_MAPPER) $(MINI_SOC_FIRRTL_MEM_MLIR) \
		--catalog $(MEMORY_MAP_CATALOG) \
		--site-inventory $(MINI_SOC_MEMORY_SITE_INVENTORY) \
		--pdk-root $(PDK_ROOT)/$(PDK) \
		--output-verilog $(MINI_SOC_MEMORY_WRAPPERS) \
		--output-manifest $(MINI_SOC_MEMORY_MANIFEST)
	@test -s $(MINI_SOC_MEMORY_WRAPPERS)
	@test -s $(MINI_SOC_MEMORY_MANIFEST)
	$(PYTHON) $(MINI_SOC_MEMORY_CHECK) $(MINI_SOC_MEMORY_MANIFEST) --top MiniSoC

$(MINI_SOC_RTL): $(MINI_SOC_MEMORY_SITE_STAMP)
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
		$(MINI_SOC_FIRRTL_MEM_MLIR) -o /dev/null > $@

mini-soc-macro-rtl: $(MINI_SOC_RTL) mini-soc-memory-map

mini-soc-macro-rtl-check: mini-soc-macro-rtl
	@test -f $(SKY130_SRAM_MODEL) || { \
		echo "Sky130 SRAM model not found at $(SKY130_SRAM_MODEL); run 'make enable-pdk'" >&2; \
		exit 1; \
	}
	$(VERILATOR) --lint-only --timing --top-module MiniSoC \
		-Wno-fatal -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-PINCONNECTEMPTY -Wno-UNOPTFLAT \
		$(MINI_SOC_RTL) $(MINI_SOC_MEMORY_WRAPPERS) $(SKY130_SRAM_MODEL)

mini-soc-slang-check: mini-soc-macro-rtl
	@if command -v librelane >/dev/null 2>&1; then \
		$(MAKE) mini-soc-slang-check-native; \
	elif command -v $(NIX) >/dev/null 2>&1; then \
		$(NIX) $(NIX_FLAGS) develop $(HARNESS_DIR) --command $(MAKE) mini-soc-slang-check-native; \
	else \
		echo "mini-soc-slang-check requires either LibreLane or Nix; see vlsi/README.md" >&2; \
		exit 1; \
	fi

mini-soc-slang-check-native: mini-soc-macro-rtl
	command -v librelane >/dev/null
	$(LIBRELANE) $(MINI_SOC_SYNTH_CONFIG) --run-tag RHODIUM_SLANG_CHECK --overwrite \
		--override-config SYNTH_ELABORATE_ONLY=true --only Yosys.Synthesis \
		--condensed --hide-progress-bar
	test -s $(MINI_SOC_SLANG_NETLIST)
	grep -q "use_slang" $(MINI_SOC_SLANG_RUN_DIR)/*-yosys-synthesis/yosys-synthesis.log
	test "$$(grep -c 'sky130_sram_2kbyte_1rw1r_32x512_8 ' $(MINI_SOC_SLANG_NETLIST))" -eq 36
	@if grep -Eq 'struct[[:space:]]+packed|typedef[[:space:]]+struct' $(MINI_SOC_SLANG_NETLIST); then \
		echo "Slang elaboration left packed structs in $(MINI_SOC_SLANG_NETLIST)" >&2; \
		exit 1; \
	fi

mini-soc-synth: mini-soc-macro-rtl
	@if command -v librelane >/dev/null 2>&1; then \
		$(MAKE) mini-soc-synth-native; \
	elif command -v $(NIX) >/dev/null 2>&1; then \
		$(NIX) $(NIX_FLAGS) develop $(HARNESS_DIR) --command $(MAKE) mini-soc-synth-native; \
	else \
		echo "mini-soc-synth requires either LibreLane or Nix; see vlsi/README.md" >&2; \
		exit 1; \
	fi

mini-soc-synth-native: mini-soc-macro-rtl
	command -v librelane >/dev/null
	$(LIBRELANE) $(MINI_SOC_SYNTH_CONFIG) --run-tag RHODIUM_SLANG_SYNTH --overwrite \
		--only Yosys.Synthesis --condensed --hide-progress-bar
	test -s $(MINI_SOC_RAW_SYNTH_NETLIST)
	grep -q "use_slang" $(MINI_SOC_SYNTH_RUN_DIR)/*-yosys-synthesis/yosys-synthesis.log
	$(MAKE) mini-soc-post-synth-native

mini-soc-post-synth:
	test -s $(MINI_SOC_SYNTH_STEP_DIR)/MiniSoC.nl.v.json
	@if command -v $(YOSYS) >/dev/null 2>&1 && command -v jq >/dev/null 2>&1; then \
		$(MAKE) mini-soc-post-synth-native; \
	else \
		$(NIX) $(NIX_FLAGS) develop $(HARNESS_DIR) --command $(MAKE) mini-soc-post-synth-native; \
	fi

mini-soc-post-synth-native:
	mkdir -p $(MINI_SOC_POST_SYNTH_DIR)
	env MINI_SOC_SYNTH_STEP_DIR="$(MINI_SOC_SYNTH_STEP_DIR)" MINI_SOC_POST_SYNTH_DIR="$(MINI_SOC_POST_SYNTH_DIR)" \
		$(YOSYS) -Q -T -l $(MINI_SOC_POST_SYNTH_DIR)/yosys.log -c $(VLSI_ROOT)/openlane/mini_soc/post-synth.tcl
	test -s $(MINI_SOC_SYNTH_NETLIST)
	test "$$(grep -c 'sky130_sram_2kbyte_1rw1r_32x512_8 ' $(MINI_SOC_SYNTH_NETLIST))" -eq 36

enable-pdk:
	@if command -v ciel >/dev/null 2>&1; then \
		ciel enable $(PDK_COMMIT) --pdk-family $(PDK); \
	elif command -v $(NIX) >/dev/null 2>&1; then \
		$(NIX) $(NIX_FLAGS) develop $(HARNESS_DIR) --command ciel enable $(PDK_COMMIT) --pdk-family $(PDK); \
	else \
		echo "enable-pdk requires either ciel or Nix; see vlsi/README.md" >&2; \
		exit 1; \
	fi

harden: rtl-check
	@if command -v librelane >/dev/null 2>&1; then \
		$(MAKE) harden-native; \
	elif command -v $(NIX) >/dev/null 2>&1; then \
		$(NIX) $(NIX_FLAGS) develop $(HARNESS_DIR) --command $(MAKE) harden-native; \
	else \
		echo "harden requires either LibreLane or Nix; see vlsi/README.md" >&2; \
		exit 1; \
	fi

harden-native: rtl
	command -v librelane >/dev/null
	$(LIBRELANE) $(CONFIG) --save-views-to $(BUILD_DIR)/views
	test -s $(BUILD_DIR)/views/gds/$(TOP_NAME).gds

lvs-smoke: rtl-check lvs-smoke-rtl-check
	@if command -v librelane >/dev/null 2>&1; then \
		$(MAKE) lvs-smoke-native; \
	elif command -v $(NIX) >/dev/null 2>&1; then \
		$(NIX) $(NIX_FLAGS) develop $(HARNESS_DIR) --command $(MAKE) lvs-smoke-native; \
	else \
		echo "lvs-smoke requires either LibreLane or Nix; see vlsi/README.md" >&2; \
		exit 1; \
	fi

lvs-smoke-native: rtl lvs-smoke-rtl-check
	command -v librelane >/dev/null
	$(LIBRELANE) $(LVS_CONFIG) --run-tag RHODIUM_LVS --overwrite --condensed \
		--hide-progress-bar --save-views-to $(BUILD_DIR)/lvs-views
	test -s $(BUILD_DIR)/lvs-views/gds/$(LVS_TOP_NAME).gds
	grep -Eq "^Final result: (Circuits|Netlists) match" \
		$(LVS_RUN_DIR)/*-netgen-lvs/reports/lvs.netgen.rpt

integrate: harden
	@if command -v magic >/dev/null 2>&1; then \
		$(MAKE) integrate-native; \
	elif command -v $(NIX) >/dev/null 2>&1; then \
		$(NIX) $(NIX_FLAGS) develop $(HARNESS_DIR) --command $(MAKE) integrate-native; \
	else \
		echo "integrate requires either Magic or Nix; see vlsi/README.md" >&2; \
		exit 1; \
	fi

integrate-native:
	command -v magic >/dev/null
	mkdir -p $(BUILD_DIR)/integrated
	PADFRAME_GDS=$(HARNESS_DIR)/gds/double_wide_chip_io.gds \
	USER_WRAPPER_GDS=$(BUILD_DIR)/views/gds/$(TOP_NAME).gds \
	OUT_GDS=$(BUILD_DIR)/integrated/double_wide_openframe.gds \
	magic -dnull -noconsole \
		-rcfile $(PDK_ROOT)/$(PDK)/libs.tech/magic/$(PDK).magicrc \
		$(HARNESS_DIR)/scripts/integrate_openframe.tcl
	test -s $(BUILD_DIR)/integrated/double_wide_openframe.gds

gds: integrate

clean:
	rm -rf $(BUILD_DIR) openlane/double_wide_openframe_project_wrapper/runs \
		openlane/rhodium_lvs_smoke/runs
