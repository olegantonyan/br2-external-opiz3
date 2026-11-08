################################################################################
#
# tone
#
################################################################################

TONE_VERSION = 1.0
TONE_SITE = $(BR2_EXTERNAL_OPIZ3_PATH)/package/tone/src
TONE_SITE_METHOD = local
TONE_LICENSE = MIT
TONE_DEPENDENCIES = alsa-lib

define TONE_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -o $(@D)/tone $(@D)/tone.c -lasound -lm
endef

define TONE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tone $(TARGET_DIR)/usr/bin/tone
endef

$(eval $(generic-package))
