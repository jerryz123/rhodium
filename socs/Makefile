# Runs focused host configuration and topology-compilation checks for repository SoCs.
REPO_DIR := $(abspath ..)
RACO ?= raco

.PHONY: all config-test

all: config-test

config-test:
	cd $(REPO_DIR) && RACO="$(RACO)" tools/run-racket-tests.sh \
	  socs/tests/boot-test.rhm \
	  socs/tests/simple-soc-test.rhm \
	  socs/tests/mini-soc-test.rhm \
	  socs/tests/tiled-soc-layout-test.rhm \
	  socs/tests/tiled-soc-compile-test.rhm
