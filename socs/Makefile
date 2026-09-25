# Runs focused host configuration and topology-compilation checks for repository SoCs.
# SPDX-License-Identifier: Apache-2.0
REPO_DIR := $(abspath ..)
RACO ?= raco

.PHONY: all config-test

all: config-test

config-test:
	@set -e; \
	cd $(REPO_DIR); \
	env RACO="$(RACO)" tools/run-racket-tests.sh \
	  socs/tests/boot-test.rhm \
	  socs/tests/description-test.rhm \
	  socs/tests/single-core-system-params-test.rhm \
	  socs/tests/single-core-rv5stage-soc-test.rhm \
	  socs/tests/single-core-spike-soc-test.rhm \
	  socs/tests/soc-selection-test.rhm \
	  socs/tests/product-test.rhm \
	  socs/tests/mini-rv5stage-soc-test.rhm \
	  socs/tests/main-memory-test.rhm \
	  socs/tests/tiled-rv5stage-soc-layout-test.rhm \
	  socs/tests/tiled-rv5stage-soc-compile-test.rhm; \
	bash socs/tests/run-device-tree.sh
