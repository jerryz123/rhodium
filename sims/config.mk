# Adapts Make arguments to the shared public selection contract without parsing config names.
# SPDX-License-Identifier: Apache-2.0
ifeq ($(origin SOC),undefined)
SOC := $(if $(ACT_CONFIGURATION),$(ACT_CONFIGURATION),simple)
endif
CORE ?= rv5stage
ISA ?=

CONFIG_INDEPENDENT_GOALS := %setup %adapter-test arch-test-source arch-test-tests \
  dpi-compile-check spike-core-compile-check spike-dpi-compile-check spike-dpi-abi-check \
  spike-core-test spike-lowering-test chi-dpi-memory-test transport-test sail-cosim-test sail-cosim-build cosim-hooks-test simulation-runtime-test
config_required := $(filter-out $(CONFIG_INDEPENDENT_GOALS),$(or $(MAKECMDGOALS),all))
# Quote every argument, including embedded apostrophes, before crossing the shell boundary.
config_quote = '$(subst ','"'"',$(1))'
config_axes := $(shell $(PYTHON) $(REPO_DIR)/socs/configs/selection.py \
  --soc $(call config_quote,$(SOC)) --isa $(call config_quote,$(ISA)) \
  $(if $(filter command line environment override,$(origin CORE)),--core $(call config_quote,$(CORE))) \
  $(if $(config_required),,--optional))
ifeq ($(word 1,$(config_axes)),ERROR:)
$(error $(config_axes))
endif
ifeq ($(strip $(config_axes)),)
$(error Config selection failed)
endif
SOC_SHAPE := $(word 1,$(config_axes))
SOC_CORE := $(word 2,$(config_axes))
SOC_ISA := $(word 3,$(config_axes))
SOC_ID := $(SOC_SHAPE)-$(SOC_CORE)-$(SOC_ISA)
HARNESS_SELECTOR := $(SOC_SHAPE) $(SOC_CORE) $(SOC_ISA)
