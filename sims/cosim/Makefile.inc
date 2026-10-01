# Builds the product-independent embedded Sail reference qualification.
# SPDX-License-Identifier: Apache-2.0
COSIM_SAIL_PREFIX ?= $(REPO_DIR)/.tools/sail-riscv-0.14.1-sail-0.20.2-$(SAIL_IDENTITY)
COSIM_BUILD_DIR ?= $(REPO_DIR)/.rhodium-cache/sail-cosim/$(SAIL_IDENTITY)
.PHONY: sail-cosim-test
sail-cosim-test: $(FESVR_LIBRARY)
	cmake -S cosim -B "$(COSIM_BUILD_DIR)" -DCMAKE_BUILD_TYPE=Debug \
	  -DSailModel_DIR="$(COSIM_SAIL_PREFIX)/lib/cmake/SailModel" -DFESVR_PREFIX="$(FESVR_PREFIX)"
	cmake --build "$(COSIM_BUILD_DIR)" --parallel $(BUILD_JOBS)
	ctest --test-dir "$(COSIM_BUILD_DIR)" --output-on-failure
