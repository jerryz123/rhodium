# Adapts Make arguments to the shared public selection contract without parsing product names.
# SPDX-License-Identifier: Apache-2.0
ifeq ($(origin SOC),undefined)
SOC := $(if $(ACT_CONFIGURATION),$(ACT_CONFIGURATION),simple)
endif
CORE ?= rv5stage
ISA ?=

PRODUCT_INDEPENDENT_GOALS := %setup %adapter-test arch-test-source arch-test-tests \
  dpi-compile-check spike-core-compile-check spike-dpi-compile-check spike-dpi-abi-check \
  spike-core-test spike-lowering-test chi-dpi-memory-test transport-test sail-cosim-test sail-cosim-build
product_required := $(filter-out $(PRODUCT_INDEPENDENT_GOALS),$(or $(MAKECMDGOALS),all))
# Quote every argument, including embedded apostrophes, before crossing the shell boundary.
product_quote = '$(subst ','"'"',$(1))'
product_axes := $(shell $(PYTHON) $(REPO_DIR)/socs/products/selection.py \
  --soc $(call product_quote,$(SOC)) --isa $(call product_quote,$(ISA)) \
  $(if $(filter command line environment override,$(origin CORE)),--core $(call product_quote,$(CORE))) \
  $(if $(product_required),,--optional))
ifeq ($(word 1,$(product_axes)),ERROR:)
$(error $(product_axes))
endif
ifeq ($(strip $(product_axes)),)
$(error Product selection failed)
endif
SOC_SHAPE := $(word 1,$(product_axes))
SOC_CORE := $(word 2,$(product_axes))
SOC_ISA := $(word 3,$(product_axes))
SOC_ID := $(SOC_SHAPE)-$(SOC_CORE)-$(SOC_ISA)
HARNESS_SELECTOR := $(SOC_SHAPE) $(SOC_CORE) $(SOC_ISA)
