# Includes Rhodium's Buildroot packages without changing upstream sources.
# SPDX-License-Identifier: Apache-2.0
include $(sort $(wildcard $(BR2_EXTERNAL_RHODIUM_PATH)/package/*/*.mk))
