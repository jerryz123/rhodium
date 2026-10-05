# Builds embedded Sail validation and exact-product reference configurations.
# SPDX-License-Identifier: Apache-2.0
COSIM_SAIL_PREFIX ?= $(REPO_DIR)/.tools/sail-riscv-0.14.1-sail-0.20.3-$(SAIL_IDENTITY)
COSIM_BUILD_DIR ?= $(REPO_DIR)/.rhodium-cache/sail-cosim/sail-0.20.3-$(SAIL_IDENTITY)
COSIM_CONFIG_DIR ?= $(SOC_BUILD_DIR)/cosim
COSIM_PYTHON ?= $(ACT_PYTHON)
COSIM_HOOK_BUILD_DIR ?= $(REPO_DIR)/.rhodium-cache/cosim-hooks
.PHONY: cosim-hooks-test
cosim-hooks-test:
	cmake -S cosim -B "$(COSIM_HOOK_BUILD_DIR)" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCOSIM_WITH_SAIL=OFF
	cmake --build "$(COSIM_HOOK_BUILD_DIR)" --parallel $(BUILD_JOBS)
	ctest --test-dir "$(COSIM_HOOK_BUILD_DIR)" --output-on-failure

.PHONY: sail-cosim-build sail-cosim-test sail-cosim-config sail-cosim-config-test sail-config-adapter-test
sail-cosim-build: $(FESVR_LIBRARY)
	cmake -S cosim -B "$(COSIM_BUILD_DIR)" -DCMAKE_BUILD_TYPE=Debug -DCOSIM_WITH_SAIL=ON \
	  -DSailModel_DIR="$(COSIM_SAIL_PREFIX)/lib/cmake/SailModel" -DFESVR_PREFIX="$(FESVR_PREFIX)"
	cmake --build "$(COSIM_BUILD_DIR)" --parallel $(BUILD_JOBS)
sail-cosim-test: sail-cosim-build
	ctest --test-dir "$(COSIM_BUILD_DIR)" --output-on-failure

sail-cosim-config:
	mkdir -p "$(COSIM_CONFIG_DIR)"
	cd "$(REPO_DIR)" && RACKET="$(RACKET)" tools/run-racket.sh -S "$(REPO_DIR)" \
	  sims/cosim/write-product.rhm "$(SOC_ID)" "$(abspath $(COSIM_CONFIG_DIR))/product.json"
	$(COSIM_PYTHON) cosim/configure.py --product "$(COSIM_CONFIG_DIR)/product.json" \
	  --name "$(SOC_ID)" --sail "$(COSIM_SAIL_PREFIX)/bin/sail_riscv_sim" --output "$(COSIM_CONFIG_DIR)"

ifeq ($(COSIM),1)
$(SOC_BUILD_DIR)/soc_cosim.json: $(SOC_EMITTED)
	@test -f "$@" || (echo "cosim descriptor missing; rebuild $(SOC_EMITTED)" >&2; exit 1)

$(COSIM_CONFIG_DIR)/runtime-config.h: $(SOC_BUILD_DIR)/soc_cosim.json $(SIMULATOR_SOURCES) $(CURDIR)/cosim/configure.py $(CURDIR)/sail/configuration.py $(CURDIR)/sail/product.py $(COSIM_SAIL_PREFIX)/bin/sail_riscv_sim
	$(MAKE) sail-cosim-config SOC=$(SOC_ID) COSIM=1
	$(COSIM_PYTHON) cosim/configure.py --product "$(COSIM_CONFIG_DIR)/product.json" \
	  --name "$(SOC_ID)" --sail "$(COSIM_SAIL_PREFIX)/bin/sail_riscv_sim" --output "$(COSIM_CONFIG_DIR)" \
	  --observations "$(SOC_BUILD_DIR)/soc_cosim.json"

$(COSIM_BUILD_DIR)/runtime.stamp: $(wildcard $(CURDIR)/cosim/*.cc $(CURDIR)/cosim/*.h) $(CURDIR)/cosim/CMakeLists.txt
	$(MAKE) sail-cosim-build SOC=$(SOC_ID)
	touch "$@"
endif

.PHONY: cosim-smoke
ifeq ($(COSIM),1)
cosim-smoke: $(SOC_BUILD_DIR)/cosim-smoke.elf
	$(MAKE) run SOC=$(SOC_ID) COSIM=1 BINARY=$(SOC_BUILD_DIR)/cosim-smoke.elf
	$(PYTHON) cosim/tests/check-corruption.py "$(SIMULATOR_BINARY)" "$(SOC_BUILD_DIR)/cosim-smoke.elf" $(if $(filter 1,$(TRACE)),+rheg-trace=$(abspath $(TRACE_FILE)).corruption)
else
cosim-smoke:
	$(MAKE) cosim-smoke SOC=$(SOC_ID) COSIM=1
endif

$(SOC_BUILD_DIR)/cosim-smoke.elf: $(CURDIR)/cosim/tests/scalar.S $(CURDIR)/tests/programs/smoke.ld | $(SOC_BUILD_DIR)
	$(RISCV_CC) -march=rv64imac_zicsr -mabi=lp64 -nostdlib -nostartfiles \
	  -Wl,--build-id=none -Wl,-T,tests/programs/smoke.ld $< -o $@

sail-cosim-config-test: sail-cosim-config sail-cosim-build
	"$(COSIM_BUILD_DIR)/sail_product_config_test" "$(COSIM_CONFIG_DIR)/sail.json" "$(COSIM_CONFIG_DIR)/manifest.json"

sail-config-adapter-test:
	$(PYTHON) -m unittest discover -s tests -p 'test_sail_config.py'
