# Builds the reusable Perfetto archives, converter, and contract tests.
# SPDX-License-Identifier: Apache-2.0
.DEFAULT_GOAL := all

REPO_DIR := $(abspath ../..)
BUILD_DIR ?= /tmp/rhodium-perfetto-build
PYTHON ?= python3
CXX ?= c++
AR ?= ar
PKG_CONFIG ?= pkg-config
CXXFLAGS ?= -O2
override CXXFLAGS += -std=c++17 -MMD -MP

RUNTIME_DIR := $(REPO_DIR)/rheg/runtime
TEST_DIR := $(REPO_DIR)/rheg/tests
RISCV_DIR := $(REPO_DIR)/riscv
SPIKE_SOURCE_DIR := $(RISCV_DIR)/riscv-isa-sim
SPIKE_PATCH_DIR := $(RISCV_DIR)/riscv-isa-sim-patches
SPIKE_PATCHES := $(wildcard $(SPIKE_PATCH_DIR)/*.patch)
PATCHED_SUBMODULE_TOOL := $(RISCV_DIR)/patched_submodule.py
SPIKE_IDENTITY := $(shell $(PYTHON) $(PATCHED_SUBMODULE_TOOL) identity --repository $(REPO_DIR) \
  --submodule riscv/riscv-isa-sim --series $(SPIKE_PATCH_DIR)/series)
SPIKE_PATCHED_DIR := $(BUILD_DIR)/spike-patched
SPIKE_PATCH_STAMP := $(SPIKE_PATCHED_DIR)/.rhodium-patched-$(SPIKE_IDENTITY)
SPIKE_CONFIG_DIR := $(BUILD_DIR)/spike-config
SPIKE_CONFIG := $(SPIKE_CONFIG_DIR)/config.h
OBJECT_DIR := $(BUILD_DIR)/objects

ifeq ($(strip $(NLOHMANN_JSON_INCLUDE_DIR)),)
NLOHMANN_JSON_INCLUDE_DIR := $(shell $(PKG_CONFIG) --variable=includedir nlohmann_json 2>/dev/null)
endif
ifneq ($(strip $(NLOHMANN_JSON_INCLUDE_DIR)),)
NLOHMANN_JSON_HEADER := $(NLOHMANN_JSON_INCLUDE_DIR)/nlohmann/json.hpp
JSON_INCLUDES := -isystem $(NLOHMANN_JSON_INCLUDE_DIR)
endif

COMMON_INCLUDES := -I$(RUNTIME_DIR) -I$(REPO_DIR)/rheg/perfetto
SPIKE_INCLUDES := -isystem $(SPIKE_PATCHED_DIR)/riscv -isystem $(SPIKE_PATCHED_DIR)/softfloat -I$(SPIKE_CONFIG_DIR)

RUNTIME_OBJECT := $(OBJECT_DIR)/runtime/rheg.o
PERFETTO_OBJECT := $(OBJECT_DIR)/perfetto/rheg_perfetto.o
MAIN_OBJECT := $(OBJECT_DIR)/perfetto/main.o
DISASM_OBJECTS := $(OBJECT_DIR)/spike/disasm.o $(OBJECT_DIR)/spike/regnames.o $(OBJECT_DIR)/spike/isa_parser.o
EVENT_PERFETTO_OBJECT := $(OBJECT_DIR)/tests/event-perfetto-test.o
EVENT_STREAM_OBJECT := $(OBJECT_DIR)/tests/event-stream-test.o
ALL_OBJECTS := $(RUNTIME_OBJECT) $(PERFETTO_OBJECT) $(MAIN_OBJECT) $(DISASM_OBJECTS) $(EVENT_PERFETTO_OBJECT) $(EVENT_STREAM_OBJECT)

RUNTIME_LIBRARY := $(BUILD_DIR)/librheg_runtime.a
PERFETTO_LIBRARY := $(BUILD_DIR)/librheg_perfetto.a
CONVERTER := $(BUILD_DIR)/rheg-perfetto
EVENT_PERFETTO_TEST := $(BUILD_DIR)/event-perfetto-test
EVENT_STREAM_TEST := $(BUILD_DIR)/event-stream-test

.PHONY: all libraries tests test
all: $(CONVERTER)

libraries: $(RUNTIME_LIBRARY) $(PERFETTO_LIBRARY)

tests: $(CONVERTER) $(EVENT_PERFETTO_TEST) $(EVENT_STREAM_TEST)

test: tests
	$(EVENT_PERFETTO_TEST) $(BUILD_DIR)

$(SPIKE_PATCH_STAMP): $(PATCHED_SUBMODULE_TOOL) $(SPIKE_PATCH_DIR)/series $(SPIKE_PATCHES) \
    $(SPIKE_SOURCE_DIR)/disasm/disasm.cc $(SPIKE_SOURCE_DIR)/disasm/regnames.cc \
    $(SPIKE_SOURCE_DIR)/disasm/isa_parser.cc
	@mkdir -p $(BUILD_DIR)
	$(PYTHON) $(PATCHED_SUBMODULE_TOOL) materialize --source $(SPIKE_SOURCE_DIR) \
	  --series $(SPIKE_PATCH_DIR)/series --output $(SPIKE_PATCHED_DIR)
	@touch $@

$(SPIKE_CONFIG):
	@mkdir -p $(@D)
	@printf '%s\n' '// Standalone disassembly requires no simulator configuration defines.' > $@

$(OBJECT_DIR)/runtime/%.o: $(RUNTIME_DIR)/%.cc
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(COMMON_INCLUDES) -c $< -o $@

$(OBJECT_DIR)/perfetto/rheg_perfetto.o: $(REPO_DIR)/rheg/perfetto/rheg_perfetto.cc $(NLOHMANN_JSON_HEADER) $(SPIKE_PATCH_STAMP) $(SPIKE_CONFIG)
	@mkdir -p $(@D)
	@printf '%s\n' '#include <nlohmann/json.hpp>' | $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(JSON_INCLUDES) -MF /dev/null -x c++ -fsyntax-only - >/dev/null 2>&1 || { \
	  echo 'Cannot compile with nlohmann/json.hpp; install nlohmann-json3-dev or nlohmann-json, or set NLOHMANN_JSON_INCLUDE_DIR=/path/to/include' >&2; \
	  exit 1; \
	}
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(COMMON_INCLUDES) $(JSON_INCLUDES) $(SPIKE_INCLUDES) -c $< -o $@

$(OBJECT_DIR)/perfetto/%.o: $(REPO_DIR)/rheg/perfetto/%.cc
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(COMMON_INCLUDES) -c $< -o $@

$(OBJECT_DIR)/spike/%.o: $(SPIKE_PATCH_STAMP) $(SPIKE_CONFIG)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SPIKE_INCLUDES) -c $(SPIKE_PATCHED_DIR)/disasm/$*.cc -o $@

$(OBJECT_DIR)/tests/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(COMMON_INCLUDES) -c $< -o $@

$(RUNTIME_LIBRARY): $(RUNTIME_OBJECT)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $^

$(PERFETTO_LIBRARY): $(PERFETTO_OBJECT) $(DISASM_OBJECTS)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $^

$(CONVERTER): $(MAIN_OBJECT) $(PERFETTO_LIBRARY) $(RUNTIME_LIBRARY)
	$(CXX) $(LDFLAGS) -o $@ $(MAIN_OBJECT) $(PERFETTO_LIBRARY) $(RUNTIME_LIBRARY) -lz $(LDLIBS)

$(EVENT_PERFETTO_TEST): $(EVENT_PERFETTO_OBJECT) $(PERFETTO_LIBRARY) $(RUNTIME_LIBRARY)
	$(CXX) $(LDFLAGS) -o $@ $(EVENT_PERFETTO_OBJECT) $(PERFETTO_LIBRARY) $(RUNTIME_LIBRARY) -lz $(LDLIBS)

$(EVENT_STREAM_TEST): $(EVENT_STREAM_OBJECT) $(PERFETTO_LIBRARY) $(RUNTIME_LIBRARY)
	$(CXX) $(LDFLAGS) -o $@ $(EVENT_STREAM_OBJECT) $(PERFETTO_LIBRARY) $(RUNTIME_LIBRARY) -lz $(LDLIBS)

$(ALL_OBJECTS:.o=.d): ;
-include $(wildcard $(ALL_OBJECTS:.o=.d))
