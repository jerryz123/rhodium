# Runs focused host configuration and topology-compilation checks for repository SoCs.
REPO_DIR := $(abspath ..)
RACO ?= raco

.PHONY: all config-test

all: config-test

config-test:
	@set -e; \
	cd $(REPO_DIR); \
	soc_compiled_root="$${PLTCOMPILEDROOTS:-}"; \
	owns_compiled_root=false; \
	if [ -z "$$soc_compiled_root" ]; then \
	  soc_compiled_root="$$(mktemp -d /tmp/rhodium-soc-compiled.XXXXXX)"; \
	  owns_compiled_root=true; \
	fi; \
	trap 'if [ "$$owns_compiled_root" = true ]; then rm -rf "$$soc_compiled_root"; fi' EXIT; \
	env PLTCOMPILEDROOTS="$$soc_compiled_root" RACO="$(RACO)" tools/run-racket-tests.sh \
	  socs/tests/boot-test.rhm \
	  socs/tests/description-test.rhm \
	  socs/tests/simple-soc-test.rhm \
	  socs/tests/mini-soc-test.rhm \
	  socs/tests/tiled-soc-layout-test.rhm \
	  socs/tests/tiled-soc-compile-test.rhm; \
	env PLTCOMPILEDROOTS="$$soc_compiled_root" bash socs/tests/run-device-tree.sh
