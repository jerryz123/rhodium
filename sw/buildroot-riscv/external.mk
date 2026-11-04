# Includes Rhodium's Buildroot packages without changing upstream sources.
# SPDX-License-Identifier: Apache-2.0
# These flags come from the same path policy as bare-metal and native builds.
# Keep upstream optimization, ABI, hardening, and reproducible timestamp flags.
TARGET_CFLAGS += $(RHODIUM_PATH_FLAGS)
HOST_CFLAGS += $(RHODIUM_PATH_FLAGS)
LINUX_CFLAGS += $(RHODIUM_PATH_FLAGS)
LINUX_MAKE_FLAGS += KAFLAGS="$(RHODIUM_PATH_FLAGS)"
include $(sort $(wildcard $(BR2_EXTERNAL_RHODIUM_PATH)/package/*/*.mk))
