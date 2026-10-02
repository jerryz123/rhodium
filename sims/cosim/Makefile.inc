# Builds embedded Sail qualification and exact-product reference configurations.
# SPDX-License-Identifier: Apache-2.0
COSIM_SAIL_PREFIX ?= $(REPO_DIR)/.tools/sail-riscv-0.14.1-sail-0.20.2-$(SAIL_IDENTITY)
COSIM_BUILD_DIR ?= $(REPO_DIR)/.rhodium-cache/sail-cosim/$(SAIL_IDENTITY)
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

sail-cosim-config-test: sail-cosim-config sail-cosim-build
	"$(COSIM_BUILD_DIR)/sail_product_config_test" "$(COSIM_CONFIG_DIR)/sail.json" "$(COSIM_CONFIG_DIR)/manifest.json"

sail-config-adapter-test:
	$(PYTHON) -m unittest discover -s tests -p 'test_sail_config.py'
