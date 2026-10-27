################################################################################
#
# hciattach-opi
#
################################################################################

HCIATTACH_OPI_VERSION = 5.10
HCIATTACH_OPI_SITE = $(BR2_EXTERNAL_OPIZ3_PATH)/package/hciattach-opi/src
HCIATTACH_OPI_SITE_METHOD = local
HCIATTACH_OPI_LICENSE = GPL-2.0
HCIATTACH_OPI_LICENSE_FILES = NOTICE

define HCIATTACH_OPI_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) CC="$(TARGET_CC)" LDFLAGS="$(TARGET_LDFLAGS)" -C $(@D) hciattach_opi
endef

define HCIATTACH_OPI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/output/hciattach_opi $(TARGET_DIR)/usr/bin/hciattach_opi
endef

$(eval $(generic-package))
