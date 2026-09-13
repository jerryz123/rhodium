# Builds and tests the technology-independent SRAM mapping package.
# SPDX-License-Identifier: Apache-2.0

.DELETE_ON_ERROR:

REPO_DIR := $(abspath ..)
BUILD_DIR ?= $(CURDIR)/build
PLUGIN_BUILD_DIR := $(BUILD_DIR)/circt
PLUGIN := $(PLUGIN_BUILD_DIR)/rhodium-memory-sites.so
CIRCT_PASS_DIR := $(CURDIR)/circt

CIRCT_OPT ?= $(REPO_DIR)/.tools/firtool-1.155.0/bin/circt-opt
CIRCT_ROOT ?= $(abspath $(dir $(CIRCT_OPT))/..)
CIRCT_CMAKE_DIR ?= $(CIRCT_ROOT)/lib/cmake/circt
CMAKE ?= cmake
PYTHON ?= python3
VERILATOR ?= verilator

.PHONY: all plugin test clean

all: test

$(PLUGIN): $(CIRCT_PASS_DIR)/CMakeLists.txt $(CIRCT_PASS_DIR)/MemorySitePass.cpp
	@test -x $(CIRCT_OPT) || { \
		echo "circt-opt not found at $(CIRCT_OPT); run 'make setup-circt' at the repository root or set CIRCT_OPT" >&2; \
		exit 1; \
	}
	@test -f $(CIRCT_CMAKE_DIR)/CIRCTConfig.cmake || { \
		echo "CIRCT CMake package not found at $(CIRCT_CMAKE_DIR); set CIRCT_ROOT to the matching installation" >&2; \
		exit 1; \
	}
	$(CMAKE) -S $(CIRCT_PASS_DIR) -B $(PLUGIN_BUILD_DIR) \
		-DCIRCT_DIR=$(CIRCT_CMAKE_DIR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(PLUGIN_BUILD_DIR) --parallel
	@test -s $@

plugin: $(PLUGIN)

test: $(PLUGIN)
	env PYTHONDONTWRITEBYTECODE=1 PYTHON=$(PYTHON) VERILATOR=$(VERILATOR) CIRCT_OPT=$(CIRCT_OPT) \
		MEMORY_SITE_PLUGIN=$(PLUGIN) \
		$(PYTHON) -m unittest discover -s tests -p 'test_*.py'

clean:
	rm -rf $(BUILD_DIR)
