# Selects profile-supported RV32 and RV64 tests from upstream inventories.
# SPDX-License-Identifier: Apache-2.0
include $(src_dir)/Makefile

# Privileged platform groups remain outside this instruction-environment adapter.
# ACT has its own independent UDB selection.
ifndef program_groups
$(error program_groups must be selected from the concrete target)
endif
program_virtual_groups ?=
$(foreach group,$(program_groups),$(if $($(group)_p_tests),,$(error Missing upstream physical test inventory: $(group))))
# The single-core products trap misaligned data accesses; these tests require completing them.
program_excluded := rv$(XLEN)ui-p-ma_data rv$(XLEN)ui-v-ma_data
program_selected := $(filter-out $(program_excluded),$(foreach group,$(program_groups),$($(group)_p_tests)) $(foreach group,$(program_virtual_groups),$($(group)_v_tests)))
.PHONY: program-manifest
program-manifest:
	@printf '%s\n' $(sort $(program_selected))
