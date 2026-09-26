# Resolves explicit SoC/core/ISA selectors into one artifact identity without an ISA fallback.
# SPDX-License-Identifier: Apache-2.0
ifeq ($(origin SOC),undefined)
SOC := $(if $(ACT_CONFIGURATION),$(ACT_CONFIGURATION),simple)
endif
CORE ?= rv5stage
ISA ?=

product_empty :=
product_space := $(product_empty) $(product_empty)
product_axes := $(subst -,$(product_space),$(SOC))
ifneq ($(words $(product_axes)),1)
ifneq ($(words $(product_axes)),3)
$(error SOC must be a shape or an explicit shape-core-isa key)
endif
ifneq ($(SOC),$(subst $(product_space),-,$(strip $(product_axes))))
$(error SOC must be a canonical shape-core-isa key)
endif
SOC_SHAPE := $(word 1,$(product_axes))
SOC_CORE := $(word 2,$(product_axes))
SOC_ISA := $(word 3,$(product_axes))
ifneq ($(strip $(ISA)),)
ifneq ($(ISA),$(SOC_ISA))
$(error ISA conflicts with the explicit SOC product key)
endif
endif
ifeq ($(origin CORE),command line)
ifneq ($(CORE),$(SOC_CORE))
$(error CORE conflicts with the explicit SOC product key)
endif
endif
else
SOC_SHAPE := $(SOC)
SOC_CORE := $(CORE)
SOC_ISA := $(strip $(ISA))
endif
# The existing shape spelling is an alias, never an architectural default.
ifeq ($(SOC_SHAPE),single)
SOC_SHAPE := simple
endif
ifeq ($(filter $(SOC_SHAPE),mini simple tiled),)
$(error Unsupported SoC shape '$(SOC_SHAPE)')
endif
ifeq ($(filter $(SOC_CORE),rv5stage spike),)
$(error Unsupported core '$(SOC_CORE)')
endif
ifneq ($(SOC_ISA),)
ifeq ($(filter $(SOC_ISA),rv32int rv32max rva23),)
$(error Unsupported ISA '$(SOC_ISA)'; expected rv32int, rv32max, or rva23)
endif
endif

# Dependency setup and core-independent host checks do not select a product.
PRODUCT_INDEPENDENT_GOALS := %setup %adapter-test arch-test-source arch-test-tests \
  dpi-compile-check spike-core-compile-check spike-dpi-compile-check spike-dpi-abi-check \
  spike-core-test spike-lowering-test chi-dpi-memory-test transport-test
ifneq ($(filter-out $(PRODUCT_INDEPENDENT_GOALS),$(or $(MAKECMDGOALS),all)),)
ifeq ($(SOC_ISA),)
$(error ISA is required; use ISA=rva23, ISA=rv32int, ISA=rv32max, or SOC=shape-core-isa)
endif
endif

SOC_ID := $(SOC_SHAPE)-$(SOC_CORE)-$(SOC_ISA)
HARNESS_SELECTOR := $(SOC_SHAPE) $(SOC_CORE) $(SOC_ISA)
