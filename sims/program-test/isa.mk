# Selects physical-environment RV64 instruction tests from upstream's own inventory.
include $(src_dir)/Makefile

# The initial suite covers instruction behavior, not upstream's virtual or
# privileged platform environments. ACT has its own independent UDB selection.
program_groups := rv64ui rv64uc rv64um rv64ua rv64uf rv64ud rv64uzba rv64uzbb rv64uzbs rv64uzicond rv64mzicbo
$(foreach group,$(program_groups),$(if $($(group)_p_tests),,$(error Missing upstream physical test inventory: $(group))))
# SimpleSoC traps misaligned data accesses; this test requires completing them.
program_excluded := rv64ui-p-ma_data
program_selected := $(filter-out $(program_excluded),$(foreach group,$(program_groups),$($(group)_p_tests)))
.PHONY: program-manifest
program-manifest:
	@printf '%s\n' $(sort $(program_selected))
