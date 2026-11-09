################################################################################
#
# statusd
#
################################################################################

STATUSD_VERSION = 1.0
STATUSD_SITE = $(BR2_EXTERNAL_OPIZ3_PATH)/package/statusd/src
STATUSD_SITE_METHOD = local
STATUSD_LICENSE = GPL-2.0
STATUSD_DEPENDENCIES = libdrm host-pkgconf

define STATUSD_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -Wno-format-truncation $(TARGET_LDFLAGS) -o $(@D)/statusd $(@D)/*.c -lm $$($(PKG_CONFIG_HOST_BINARY) --cflags --libs libdrm)
endef

define STATUSD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/statusd $(TARGET_DIR)/usr/sbin/statusd
endef

$(eval $(generic-package))
