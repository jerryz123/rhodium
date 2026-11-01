# Builds the sole smoke executable with Buildroot's target toolchain and static libc.
# SPDX-License-Identifier: Apache-2.0
RHODIUM_INIT_VERSION = 1
RHODIUM_INIT_SITE = $(BR2_EXTERNAL_RHODIUM_PATH)/package/rhodium-init
RHODIUM_INIT_SITE_METHOD = local
RHODIUM_INIT_LICENSE = Apache-2.0

define RHODIUM_INIT_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -static -Wall -Wextra -Werror \
		-Wl,--build-id=none $(@D)/init.c -o $(@D)/init
endef

define RHODIUM_INIT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/init $(TARGET_DIR)/init
endef

$(eval $(generic-package))
