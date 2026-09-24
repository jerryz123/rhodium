# Selects profile-supported physical and virtual RV64 tests from upstream inventories.
# SPDX-License-Identifier: Apache-2.0
include $(src_dir)/Makefile

# Privileged platform groups remain outside this instruction-environment adapter.
# ACT has its own independent UDB selection.
program_groups ?= rv64ui rv64uc rv64um rv64ua rv64uf rv64ud rv64uzba rv64uzbb rv64uzbs rv64uzicond rv64mzicbo
program_virtual_groups ?=
$(foreach group,$(program_groups),$(if $($(group)_p_tests),,$(error Missing upstream physical test inventory: $(group))))
# The single-core products trap misaligned data accesses; these tests require completing them.
program_excluded := rv64ui-p-ma_data rv64ui-v-ma_data
program_selected := $(filter-out $(program_excluded),$(foreach group,$(program_groups),$($(group)_p_tests)) $(foreach group,$(program_virtual_groups),$($(group)_v_tests)))
.PHONY: program-manifest
program-manifest:
	@printf '%s\n' $(sort $(program_selected))
