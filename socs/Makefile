# Runs focused compilation and elaboration checks for repository SoCs.
REPO_DIR := $(abspath ..)
RACO ?= raco

.PHONY: all rtl-elaboration-test tiled-compile-test tiled-elaboration-test

all: rtl-elaboration-test tiled-compile-test tiled-elaboration-test

rtl-elaboration-test:
	cd $(REPO_DIR) && RACO="$(RACO)" tools/run-racket-tests.sh \
	  socs/tests/boot-test.rhm \
	  socs/tests/simple-soc-test.rhm \
	  socs/tests/mini-soc-test.rhm

tiled-compile-test:
	cd $(REPO_DIR) && RACO="$(RACO)" tools/run-racket-tests.sh \
	  socs/tests/tiled-soc-layout-test.rhm \
	  socs/tests/tiled-soc-compile-test.rhm

tiled-elaboration-test:
	cd $(REPO_DIR) && RACO="$(RACO)" tools/run-racket-tests.sh \
	  socs/tests/tiled-time-test.rhm \
	  socs/tests/tiled-tiles-test.rhm \
	  socs/tests/tiled-soc-test.rhm
